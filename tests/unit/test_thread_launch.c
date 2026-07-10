/*
 * test_thread_launch.c - M5.2b 线程启动路径单元测试（宿主机）
 *
 * M5.2b 让 clone 出来的线程真正跑进 ring3：spawn_uthread_thread 用 6-qword
 * thread-start 帧 + start-trampoline，把 entry(arg) 拉起来并在首次派发时
 * wrmsr IA32_FS_BASE 设置 TLS，退出时按 CLONE_CHILD_CLEARTID 清 tid。
 *
 * 内核真实实现分散在汇编 trampoline / sched64 / syscall_dispatch64，含大量
 * 内核依赖无法在宿主机直接链接。这里复刻其纯算法语义（帧布局、arg->rdi 映射、
 * rflags IF 强制、参数校验、CHILD_CLEARTID 语义），锁定 M5.2b 契约。
 */
#include "unit_test.h"
#include <stdint.h>
#include <string.h>

/* 直接复用真实的 clone 标志定义，保证测试与内核常量同源。 */
#include "../../src/arch/x86_64/include/clone64.h"

/* --- 复刻 6-qword thread-start 帧布局 (sched64.c spawn_uthread_thread) --- */
enum {
    TFRAME_ARG = 0,   /* popped -> %rdi */
    TFRAME_RIP = 1,
    TFRAME_CS  = 2,
    TFRAME_RFLAGS = 3,
    TFRAME_RSP = 4,
    TFRAME_SS  = 5,
    TFRAME_QWORDS = 6
};

typedef struct {
    uint64_t frame[TFRAME_QWORDS];
    uint64_t frame_base;   /* 对齐后的帧基址 */
    uint64_t fs_base;      /* 记录待 wrmsr 的 TLS */
    int      ok;
} model_slot_t;

/* 复刻 spawn 的帧组装 + rflags IF 强制 + 16B 对齐语义 */
static int model_spawn_thread(model_slot_t *s,
                              uint64_t rip, uint64_t rsp, uint64_t arg,
                              uint64_t rflags, uint16_t cs, uint16_t ss,
                              uint64_t fs_base, uint64_t kstack_top) {
    memset(s, 0, sizeof(*s));
    if (rip == 0u || rsp == 0u) { s->ok = 0; return -1; }
    rflags |= 0x200ULL;  /* 强制 IF=1 */

    uint64_t fb = kstack_top - (TFRAME_QWORDS * sizeof(uint64_t));
    fb &= ~((uint64_t)0xFu);  /* 16B 对齐 */
    s->frame_base = fb;
    s->frame[TFRAME_ARG]    = arg;
    s->frame[TFRAME_RIP]    = rip;
    s->frame[TFRAME_CS]     = (uint64_t)cs;
    s->frame[TFRAME_RFLAGS] = rflags;
    s->frame[TFRAME_RSP]    = rsp;
    s->frame[TFRAME_SS]     = (uint64_t)ss;
    s->fs_base = fs_base;
    s->ok = 1;
    return 0;
}

/* 复刻 start-trampoline: 从帧顶 pop arg -> rdi，其余清零 */
typedef struct { uint64_t rdi, rax, rsi, rbx, rbp; } model_regs_t;
static void model_trampoline(const model_slot_t *s, model_regs_t *r) {
    memset(r, 0, sizeof(*r));
    r->rdi = s->frame[TFRAME_ARG];  /* 唯一非零：SysV 第一参 */
}

/* 复刻 do_clone 的标志校验 */
static int model_clone_validate(uint32_t flags) {
    if ((flags & OPENOS_CLONE_THREAD_MIN) != OPENOS_CLONE_THREAD_MIN) return -1;
    return 0;
}

/* 复刻 do_exit 的 CHILD_CLEARTID 清理 */
static void model_exit_clear_tid(uint32_t flags, uint32_t *ctid_slot,
                                 uint64_t clear_child_tid) {
    if ((flags & OPENOS_CLONE_CHILD_CLEARTID) && clear_child_tid != 0) {
        *ctid_slot = 0u;
    }
}

/* ---------------- 测试用例 ---------------- */

UNIT_TEST_CASE(frame_layout) {
    model_slot_t s;
    int rc = model_spawn_thread(&s, 0x400000u, 0x7fff0000u, 0xABCDu,
                                0x2u, 0x33, 0x3b, 0xdeadbeefu, 0x90000000u);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(1, s.ok);
    ASSERT_TRUE(s.frame[TFRAME_ARG] == 0xABCDu);
    ASSERT_TRUE(s.frame[TFRAME_RIP] == 0x400000u);
    ASSERT_TRUE(s.frame[TFRAME_CS]  == 0x33u);
    ASSERT_TRUE(s.frame[TFRAME_RSP] == 0x7fff0000u);
    ASSERT_TRUE(s.frame[TFRAME_SS]  == 0x3bu);
}

UNIT_TEST_CASE(arg_to_rdi) {
    model_slot_t s;
    model_spawn_thread(&s, 0x400000u, 0x7fff0000u, 0x1234567890ABCDEFull,
                       0x2u, 0x33, 0x3b, 0u, 0x90000000u);
    model_regs_t r;
    model_trampoline(&s, &r);
    ASSERT_TRUE(r.rdi == 0x1234567890ABCDEFull);  /* arg -> rdi */
    ASSERT_TRUE(r.rax == 0u);   /* 其余寄存器清零 */
    ASSERT_TRUE(r.rsi == 0u);
    ASSERT_TRUE(r.rbx == 0u);
    ASSERT_TRUE(r.rbp == 0u);
}

UNIT_TEST_CASE(rflags_if_forced) {
    model_slot_t s;
    /* 传入 IF=0，spawn 必须强制置 IF=1 */
    model_spawn_thread(&s, 0x400000u, 0x7fff0000u, 0u,
                       0x0u, 0x33, 0x3b, 0u, 0x90000000u);
    ASSERT_TRUE((s.frame[TFRAME_RFLAGS] & 0x200ULL) != 0);
}

UNIT_TEST_CASE(frame_16b_aligned) {
    model_slot_t s;
    /* 用一个非对齐的 kstack_top，验证对齐掩码生效 */
    model_spawn_thread(&s, 0x400000u, 0x7fff0000u, 0u,
                       0x2u, 0x33, 0x3b, 0u, 0x9000000Du);
    ASSERT_TRUE((s.frame_base & 0xFu) == 0u);
}

UNIT_TEST_CASE(fs_base_recorded) {
    model_slot_t s;
    model_spawn_thread(&s, 0x400000u, 0x7fff0000u, 0u,
                       0x2u, 0x33, 0x3b, 0xCAFEBABEull, 0x90000000u);
    ASSERT_TRUE(s.fs_base == 0xCAFEBABEull);  /* 待首次派发 wrmsr */
}

UNIT_TEST_CASE(spawn_rejects_null) {
    model_slot_t s;
    ASSERT_EQ_INT(-1, model_spawn_thread(&s, 0u, 0x7fff0000u, 0u,
                                         0x2u, 0x33, 0x3b, 0u, 0x90000000u));
    ASSERT_EQ_INT(-1, model_spawn_thread(&s, 0x400000u, 0u, 0u,
                                         0x2u, 0x33, 0x3b, 0u, 0x90000000u));
}

UNIT_TEST_CASE(clone_flag_validation) {
    ASSERT_EQ_INT(0,  model_clone_validate(OPENOS_CLONE_THREAD_MIN));
    ASSERT_EQ_INT(0,  model_clone_validate(OPENOS_CLONE_THREAD_MIN | OPENOS_CLONE_SETTLS));
    ASSERT_EQ_INT(-1, model_clone_validate(OPENOS_CLONE_VM));      /* 缺 THREAD */
    ASSERT_EQ_INT(-1, model_clone_validate(OPENOS_CLONE_THREAD));  /* 缺 VM */
    ASSERT_EQ_INT(-1, model_clone_validate(0u));
}

UNIT_TEST_CASE(child_cleartid) {
    uint32_t tid_slot = 0x777u;
    /* 有 CHILD_CLEARTID + 非零地址 -> 清零 */
    model_exit_clear_tid(OPENOS_CLONE_CHILD_CLEARTID, &tid_slot, 0x1000u);
    ASSERT_TRUE(tid_slot == 0u);

    /* 无标志 -> 不动 */
    tid_slot = 0x888u;
    model_exit_clear_tid(0u, &tid_slot, 0x1000u);
    ASSERT_TRUE(tid_slot == 0x888u);

    /* 有标志但地址为 0 -> 不动 */
    tid_slot = 0x999u;
    model_exit_clear_tid(OPENOS_CLONE_CHILD_CLEARTID, &tid_slot, 0u);
    ASSERT_TRUE(tid_slot == 0x999u);
}

int main(void)
{
    UNIT_TEST_RUN(frame_layout);
    UNIT_TEST_RUN(arg_to_rdi);
    UNIT_TEST_RUN(rflags_if_forced);
    UNIT_TEST_RUN(frame_16b_aligned);
    UNIT_TEST_RUN(fs_base_recorded);
    UNIT_TEST_RUN(spawn_rejects_null);
    UNIT_TEST_RUN(clone_flag_validation);
    UNIT_TEST_RUN(child_cleartid);
    return unit_test_finish();
}
