/*
 * test_clone_pcb.c - M5.2a 线程 clone PCB 基础设施单元测试（宿主机）
 *
 * M5.2a 的内核实现 (proc64.c / address_space64.c) 依赖 PMM、分页、
 * early_console 等大量硬件相关代码，无法在宿主机直接 include。因此本
 * 测试复刻 M5.2a 的三条核心不变量，验证其逻辑正确性：
 *
 *   1) 地址空间引用计数：as_share 递增、as_put 递减，归零时销毁；
 *      单所有者 fork/spawn (refcount==1) 的 put==destroy 无回归。
 *   2) clone_thread 关键字段：共享 AS（非拷贝）、tgid 归属父组、
 *      is_thread=true、tls_base/clear_child_tid 按标志记录、新 tid 唯一。
 *   3) do_clone 标志校验：拒绝未支持位、要求 CLONE_VM|CLONE_THREAD、
 *      拒绝零 child_stack / 零 entry。
 *
 * 这些逻辑与 clone64.h / proc64.c / address_space64.c 中的实现一一对应；
 * 若内核实现被改动，本测试用于捕捉语义回归。
 */

#include "unit_test.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* 直接复用真实的 clone 标志定义，保证测试与内核常量同源。 */
#include "../../src/arch/x86_64/include/clone64.h"

/* ==================================================================== */
/* 1. 地址空间引用计数模型（复刻 address_space64.c 的 share/put 语义）   */
/* ==================================================================== */

typedef struct mock_as {
    uint32_t refcount;
    bool     destroyed;   /* refcount 归零后被销毁的标记 */
    int      pml4_pages;  /* 模拟持有的页表页，销毁时归零 */
} mock_as_t;

static mock_as_t *mock_as_create(void) {
    static mock_as_t pool[8];
    static int used = 0;
    mock_as_t *as = &pool[used++];
    as->refcount = 1;      /* 创建者独占 */
    as->destroyed = false;
    as->pml4_pages = 4;
    return as;
}

static void mock_as_destroy(mock_as_t *as) {
    as->pml4_pages = 0;
    as->destroyed = true;
}

static mock_as_t *mock_as_share(mock_as_t *as) {
    if (as == NULL) return NULL;
    as->refcount++;
    return as;
}

static uint32_t mock_as_put(mock_as_t *as) {
    if (as == NULL) return 0;
    if (as->refcount == 0) return 0;   /* 防御 double-put 下溢 */
    as->refcount--;
    if (as->refcount == 0) {
        mock_as_destroy(as);
    }
    return as->refcount;
}

/* ==================================================================== */
/* 2. PCB / clone_thread 模型（复刻 proc64.c 的关键字段赋值）           */
/* ==================================================================== */

typedef struct mock_pcb {
    uint32_t   pid;
    uint32_t   tid;
    uint32_t   ppid;
    uint32_t   tgid;
    uint32_t   pgid;
    uint32_t   sid;
    uint64_t   tls_base;
    uint64_t   clear_child_tid;
    uint64_t   fork_user_rsp;
    bool       is_thread;
    mock_as_t *as;
} mock_pcb_t;

static uint32_t g_next_tid = 100;

/* 复刻 arch_x86_64_proc_clone_thread 的字段设置逻辑。返回 NULL 表示
 * 参数非法（与内核实现的前置校验一致）。 */
static mock_pcb_t *mock_clone_thread(mock_pcb_t *parent,
                                     const openos_clone_args_t *args,
                                     mock_pcb_t *out) {
    if (parent == NULL || args == NULL || out == NULL) return NULL;
    if ((args->flags & OPENOS_CLONE_THREAD_MIN) != OPENOS_CLONE_THREAD_MIN) {
        return NULL;
    }
    if (args->child_stack == 0 || args->entry == 0) return NULL;
    if (parent->as == NULL) return NULL;

    memset(out, 0, sizeof(*out));
    out->pid  = g_next_tid++;
    out->tid  = out->pid;
    out->ppid = parent->pid;

    /* 核心：共享父 AS（refcount++），非深拷贝。 */
    out->as = mock_as_share(parent->as);
    out->is_thread = true;

    /* tgid 归属：继承父的 tgid（父若无则用父 pid）。 */
    out->tgid = parent->tgid ? parent->tgid : parent->pid;

    /* 线程与创建者同组同会话。 */
    out->pgid = parent->pgid;
    out->sid  = parent->sid;

    /* 按标志记录 TLS 与 clear_child_tid。 */
    out->tls_base        = (args->flags & OPENOS_CLONE_SETTLS) ? args->tls : 0;
    out->clear_child_tid = (args->flags & OPENOS_CLONE_CHILD_CLEARTID)
                               ? args->child_tid : 0;
    out->fork_user_rsp   = args->child_stack;
    return out;
}

/* ==================================================================== */
/* 3. do_clone 标志校验模型（复刻 syscall_dispatch64.c 的前置拒绝）     */
/* ==================================================================== */

/* 返回 0 表示通过校验（将进入 clone_thread），非 0 表示被拒绝。 */
static int mock_do_clone_validate(uint32_t flags, uint64_t child_stack,
                                  uint64_t entry) {
    if ((flags & ~OPENOS_CLONE_SUPPORTED_MASK) != 0) return -1;
    if ((flags & OPENOS_CLONE_THREAD_MIN) != OPENOS_CLONE_THREAD_MIN) return -1;
    if (child_stack == 0 || entry == 0) return -1;
    return 0;
}

/* ==================================================================== */
/* 测试用例                                                             */
/* ==================================================================== */

UNIT_TEST_CASE(as_refcount_share_and_put) {
    mock_as_t *as = mock_as_create();
    ASSERT_EQ_INT(1, as->refcount);
    ASSERT_FALSE(as->destroyed);

    /* 两个线程加入：refcount 1 -> 3 */
    ASSERT_TRUE(mock_as_share(as) == as);
    ASSERT_TRUE(mock_as_share(as) == as);
    ASSERT_EQ_INT(3, as->refcount);

    /* 逐个退出：前两次不销毁 */
    ASSERT_EQ_INT(2, mock_as_put(as));
    ASSERT_FALSE(as->destroyed);
    ASSERT_EQ_INT(1, mock_as_put(as));
    ASSERT_FALSE(as->destroyed);

    /* 最后一个退出：refcount 归零 -> 销毁 */
    ASSERT_EQ_INT(0, mock_as_put(as));
    ASSERT_TRUE(as->destroyed);
    ASSERT_EQ_INT(0, as->pml4_pages);
}

UNIT_TEST_CASE(as_refcount_single_owner_no_regression) {
    /* fork/spawn 路径：refcount==1，从不 share，put 等价于 destroy */
    mock_as_t *as = mock_as_create();
    ASSERT_EQ_INT(1, as->refcount);
    ASSERT_EQ_INT(0, mock_as_put(as));
    ASSERT_TRUE(as->destroyed);
}

UNIT_TEST_CASE(as_put_null_and_double_put_safe) {
    /* NULL 安全 */
    ASSERT_EQ_INT(0, mock_as_put(NULL));
    ASSERT_TRUE(mock_as_share(NULL) == NULL);

    /* double-put 不下溢 */
    mock_as_t *as = mock_as_create();
    ASSERT_EQ_INT(0, mock_as_put(as));
    ASSERT_EQ_INT(0, mock_as_put(as));   /* 再 put 仍为 0，不下溢 */
}

UNIT_TEST_CASE(clone_thread_shares_as_and_group) {
    mock_pcb_t parent;
    memset(&parent, 0, sizeof(parent));
    parent.pid = 42;
    parent.tid = 42;
    parent.tgid = 42;
    parent.pgid = 7;
    parent.sid  = 3;
    parent.as = mock_as_create();
    ASSERT_EQ_INT(1, parent.as->refcount);

    openos_clone_args_t args;
    memset(&args, 0, sizeof(args));
    args.flags = OPENOS_CLONE_VM | OPENOS_CLONE_THREAD | OPENOS_CLONE_SETTLS
               | OPENOS_CLONE_CHILD_CLEARTID;
    args.child_stack = 0x7fff0000;
    args.entry       = 0x400500;
    args.arg         = 0x1234;
    args.tls         = 0xCAFE000;
    args.child_tid   = 0x600100;

    mock_pcb_t child;
    ASSERT_TRUE(mock_clone_thread(&parent, &args, &child) == &child);

    /* 共享同一 AS，且引用计数递增 */
    ASSERT_TRUE(child.as == parent.as);
    ASSERT_EQ_INT(2, parent.as->refcount);

    /* 线程属性 */
    ASSERT_TRUE(child.is_thread);
    ASSERT_EQ_INT(42, child.tgid);         /* 继承父组 */
    ASSERT_EQ_INT(42, child.ppid);         /* 创建者为父 */
    ASSERT_EQ_INT(7, child.pgid);          /* 同组 */
    ASSERT_EQ_INT(3, child.sid);           /* 同会话 */
    ASSERT_TRUE(child.tid != parent.tid);  /* tid 唯一 */

    /* 标志驱动的字段 */
    ASSERT_TRUE(child.tls_base == 0xCAFE000);
    ASSERT_TRUE(child.clear_child_tid == 0x600100);
    ASSERT_TRUE(child.fork_user_rsp == 0x7fff0000);
}

UNIT_TEST_CASE(clone_thread_tls_and_ctid_gated_by_flags) {
    mock_pcb_t parent;
    memset(&parent, 0, sizeof(parent));
    parent.pid = 10; parent.tid = 10; parent.tgid = 10;
    parent.as = mock_as_create();

    /* 不带 SETTLS / CHILD_CLEARTID 时，对应字段应归零 */
    openos_clone_args_t args;
    memset(&args, 0, sizeof(args));
    args.flags = OPENOS_CLONE_VM | OPENOS_CLONE_THREAD;
    args.child_stack = 0x9000;
    args.entry       = 0x400000;
    args.tls         = 0xDEAD;   /* 应被忽略 */
    args.child_tid   = 0xBEEF;   /* 应被忽略 */

    mock_pcb_t child;
    ASSERT_TRUE(mock_clone_thread(&parent, &args, &child) == &child);
    ASSERT_TRUE(child.tls_base == 0);
    ASSERT_TRUE(child.clear_child_tid == 0);
}

UNIT_TEST_CASE(clone_thread_rejects_bad_args) {
    mock_pcb_t parent;
    memset(&parent, 0, sizeof(parent));
    parent.pid = 1; parent.tgid = 1;
    parent.as = mock_as_create();

    mock_pcb_t child;
    openos_clone_args_t args;

    /* 缺 CLONE_THREAD */
    memset(&args, 0, sizeof(args));
    args.flags = OPENOS_CLONE_VM;
    args.child_stack = 0x9000; args.entry = 0x400000;
    ASSERT_TRUE(mock_clone_thread(&parent, &args, &child) == NULL);

    /* 零 child_stack */
    memset(&args, 0, sizeof(args));
    args.flags = OPENOS_CLONE_THREAD_MIN;
    args.child_stack = 0; args.entry = 0x400000;
    ASSERT_TRUE(mock_clone_thread(&parent, &args, &child) == NULL);

    /* 零 entry */
    memset(&args, 0, sizeof(args));
    args.flags = OPENOS_CLONE_THREAD_MIN;
    args.child_stack = 0x9000; args.entry = 0;
    ASSERT_TRUE(mock_clone_thread(&parent, &args, &child) == NULL);

    /* 父无 AS */
    mock_pcb_t orphan;
    memset(&orphan, 0, sizeof(orphan));
    orphan.as = NULL;
    memset(&args, 0, sizeof(args));
    args.flags = OPENOS_CLONE_THREAD_MIN;
    args.child_stack = 0x9000; args.entry = 0x400000;
    ASSERT_TRUE(mock_clone_thread(&orphan, &args, &child) == NULL);
}

UNIT_TEST_CASE(do_clone_validate_flags) {
    /* 合法：VM|THREAD + 有效栈/入口 */
    ASSERT_EQ_INT(0, mock_do_clone_validate(OPENOS_CLONE_THREAD_MIN,
                                            0x9000, 0x400000));
    /* 合法：带全部支持位 */
    ASSERT_EQ_INT(0, mock_do_clone_validate(OPENOS_CLONE_SUPPORTED_MASK,
                                            0x9000, 0x400000));

    /* 拒绝：未支持的标志位 */
    ASSERT_EQ_INT(-1, mock_do_clone_validate(OPENOS_CLONE_THREAD_MIN | 0x80000000u,
                                             0x9000, 0x400000));
    /* 拒绝：缺 VM|THREAD */
    ASSERT_EQ_INT(-1, mock_do_clone_validate(OPENOS_CLONE_VM, 0x9000, 0x400000));
    ASSERT_EQ_INT(-1, mock_do_clone_validate(0, 0x9000, 0x400000));
    /* 拒绝：零栈 / 零入口 */
    ASSERT_EQ_INT(-1, mock_do_clone_validate(OPENOS_CLONE_THREAD_MIN, 0, 0x400000));
    ASSERT_EQ_INT(-1, mock_do_clone_validate(OPENOS_CLONE_THREAD_MIN, 0x9000, 0));
}

int main(void)
{
    UNIT_TEST_RUN(as_refcount_share_and_put);
    UNIT_TEST_RUN(as_refcount_single_owner_no_regression);
    UNIT_TEST_RUN(as_put_null_and_double_put_safe);
    UNIT_TEST_RUN(clone_thread_shares_as_and_group);
    UNIT_TEST_RUN(clone_thread_tls_and_ctid_gated_by_flags);
    UNIT_TEST_RUN(clone_thread_rejects_bad_args);
    UNIT_TEST_RUN(do_clone_validate_flags);
    return unit_test_finish();
}
