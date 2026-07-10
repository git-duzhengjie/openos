/*
 * test_futex.c — M5.2c futex 子系统单元测试（宿主机）
 *
 * futex64.c 的 parking loop 依赖调度器（arch_x86_64_proc_yield / TSC uptime），
 * 无法在宿主机运行。因此本测试在 OPENOS_UNIT_TEST 下直接 include futex64.c，
 * 走其 #else 分支暴露的 test-wrapper：
 *
 *   arch_x86_64_futex_test_claim(uaddr)   -> 占用一个等待槽，返回 idx
 *   arch_x86_64_futex_test_mark(uaddr,c)  -> 标记至多 c 个等待者为已唤醒
 *   arch_x86_64_futex_test_release(idx)   -> 释放等待槽
 *   arch_x86_64_futex_test_is_woken(idx)  -> 查询某槽是否被唤醒
 *
 * 以及公共 API：futex_wake / waiter_count / reset。
 * 这样能验证等待表状态机的完整语义，而不需要真实调度环境。
 */

#include "unit_test.h"

#include <stdint.h>
#include <stddef.h>

/* 直接编译进内核 futex 实现（OPENOS_UNIT_TEST 由 run_unit_tests.sh 定义）。 */
#include "../../src/arch/x86_64/kernel/futex64.c"

/* futex64.c 在 OPENOS_UNIT_TEST 下暴露的 test-wrapper 声明。 */
int  arch_x86_64_futex_test_claim(uint64_t uaddr);
int  arch_x86_64_futex_test_mark(uint64_t uaddr, int c);
void arch_x86_64_futex_test_release(int idx);
int  arch_x86_64_futex_test_is_woken(int idx);

/* -------------------------------------------------------------------------
 * 1. 单个等待者：claim -> waiter_count=1 -> wake 标记为唤醒 -> release
 * ------------------------------------------------------------------------- */
UNIT_TEST_CASE(futex_single_wait_wake)
{
    arch_x86_64_futex_reset();
    uint64_t addr = 0x400000;

    int idx = arch_x86_64_futex_test_claim(addr);
    ASSERT_TRUE(idx >= 0);
    ASSERT_EQ_INT(1, arch_x86_64_futex_waiter_count(addr));
    ASSERT_EQ_INT(0, arch_x86_64_futex_test_is_woken(idx));

    /* wake 1 -> 恰好唤醒 1 个 */
    ASSERT_EQ_INT(1, arch_x86_64_futex_wake(addr, 1));
    ASSERT_EQ_INT(1, arch_x86_64_futex_test_is_woken(idx));

    arch_x86_64_futex_test_release(idx);
    ASSERT_EQ_INT(0, arch_x86_64_futex_waiter_count(addr));
}

/* -------------------------------------------------------------------------
 * 2. wake 计数：3 个等待者，wake 2 只唤醒 2 个，再 wake 1 唤醒最后 1 个
 * ------------------------------------------------------------------------- */
UNIT_TEST_CASE(futex_wake_count_semantics)
{
    arch_x86_64_futex_reset();
    uint64_t addr = 0x401000;

    int a = arch_x86_64_futex_test_claim(addr);
    int b = arch_x86_64_futex_test_claim(addr);
    int c = arch_x86_64_futex_test_claim(addr);
    ASSERT_TRUE(a >= 0 && b >= 0 && c >= 0);
    ASSERT_EQ_INT(3, arch_x86_64_futex_waiter_count(addr));

    /* wake 2 -> 返回 2 */
    ASSERT_EQ_INT(2, arch_x86_64_futex_wake(addr, 2));

    /* 恰好 2 个被标记，1 个仍未唤醒 */
    int woken = arch_x86_64_futex_test_is_woken(a)
              + arch_x86_64_futex_test_is_woken(b)
              + arch_x86_64_futex_test_is_woken(c);
    ASSERT_EQ_INT(2, woken);

    /* 再 wake 5（多于剩余）-> 只剩 1 个可唤醒 */
    ASSERT_EQ_INT(1, arch_x86_64_futex_wake(addr, 5));

    arch_x86_64_futex_test_release(a);
    arch_x86_64_futex_test_release(b);
    arch_x86_64_futex_test_release(c);
}

/* -------------------------------------------------------------------------
 * 3. 地址隔离：不同 uaddr 的等待者互不影响
 * ------------------------------------------------------------------------- */
UNIT_TEST_CASE(futex_address_isolation)
{
    arch_x86_64_futex_reset();
    uint64_t a1 = 0x500000;
    uint64_t a2 = 0x600000;

    int x = arch_x86_64_futex_test_claim(a1);
    int y = arch_x86_64_futex_test_claim(a2);
    ASSERT_TRUE(x >= 0 && y >= 0);

    /* wake a1 不影响 a2 */
    ASSERT_EQ_INT(1, arch_x86_64_futex_wake(a1, 10));
    ASSERT_EQ_INT(1, arch_x86_64_futex_test_is_woken(x));
    ASSERT_EQ_INT(0, arch_x86_64_futex_test_is_woken(y));

    /* 对没有等待者的地址 wake 返回 0 */
    ASSERT_EQ_INT(0, arch_x86_64_futex_wake(0x700000, 5));

    arch_x86_64_futex_test_release(x);
    arch_x86_64_futex_test_release(y);
}

/* -------------------------------------------------------------------------
 * 4. wake 非法参数：NULL 地址 -> -EINVAL；count=0 -> 0
 * ------------------------------------------------------------------------- */
UNIT_TEST_CASE(futex_wake_rejects_bad_args)
{
    arch_x86_64_futex_reset();

    ASSERT_EQ_INT(-OPENOS_FUTEX_EINVAL, arch_x86_64_futex_wake(0, 1));
    ASSERT_EQ_INT(0, arch_x86_64_futex_wake(0x800000, 0));
}

/* -------------------------------------------------------------------------
 * 5. wake 已唤醒者不重复计数：wake 后再 wake 同一批返回 0
 * ------------------------------------------------------------------------- */
UNIT_TEST_CASE(futex_wake_no_double_count)
{
    arch_x86_64_futex_reset();
    uint64_t addr = 0x900000;

    int idx = arch_x86_64_futex_test_claim(addr);
    ASSERT_TRUE(idx >= 0);

    ASSERT_EQ_INT(1, arch_x86_64_futex_wake(addr, 10));
    /* 已被唤醒，不应再次计入 */
    ASSERT_EQ_INT(0, arch_x86_64_futex_wake(addr, 10));

    arch_x86_64_futex_test_release(idx);
}

/* -------------------------------------------------------------------------
 * 6. 等待表容量上限：claim 超过 MAX_WAITERS 返回 -1
 * ------------------------------------------------------------------------- */
UNIT_TEST_CASE(futex_table_capacity_bound)
{
    arch_x86_64_futex_reset();
    uint64_t addr = 0xA00000;

    int idxs[OPENOS_FUTEX_MAX_WAITERS];
    for (int i = 0; i < OPENOS_FUTEX_MAX_WAITERS; ++i) {
        idxs[i] = arch_x86_64_futex_test_claim(addr);
        ASSERT_TRUE(idxs[i] >= 0);
    }
    ASSERT_EQ_INT(OPENOS_FUTEX_MAX_WAITERS,
                  arch_x86_64_futex_waiter_count(addr));

    /* 表满：再 claim 失败 */
    ASSERT_EQ_INT(-1, arch_x86_64_futex_test_claim(addr));

    /* 释放一个后可再 claim */
    arch_x86_64_futex_test_release(idxs[0]);
    int again = arch_x86_64_futex_test_claim(addr);
    ASSERT_TRUE(again >= 0);

    /* 清理 */
    arch_x86_64_futex_reset();
}

/* -------------------------------------------------------------------------
 * 7. clear_child_tid 唤醒语义：join 者用 addr 等待，退出线程 wake 全部
 *    （模拟 do_exit 里 futex_wake(clear_child_tid, 0x7fffffff)）
 * ------------------------------------------------------------------------- */
UNIT_TEST_CASE(futex_clear_child_tid_wake_all)
{
    arch_x86_64_futex_reset();
    uint64_t ctid = 0xB00000;

    /* 两个 joiner 都 park 在同一 tid 地址上 */
    int j1 = arch_x86_64_futex_test_claim(ctid);
    int j2 = arch_x86_64_futex_test_claim(ctid);
    ASSERT_TRUE(j1 >= 0 && j2 >= 0);

    /* 线程退出：wake 全部（INT_MAX 语义） */
    ASSERT_EQ_INT(2, arch_x86_64_futex_wake(ctid, 0x7fffffff));
    ASSERT_EQ_INT(1, arch_x86_64_futex_test_is_woken(j1));
    ASSERT_EQ_INT(1, arch_x86_64_futex_test_is_woken(j2));

    arch_x86_64_futex_test_release(j1);
    arch_x86_64_futex_test_release(j2);
}

int main(void)
{
    UNIT_TEST_RUN(futex_single_wait_wake);
    UNIT_TEST_RUN(futex_wake_count_semantics);
    UNIT_TEST_RUN(futex_address_isolation);
    UNIT_TEST_RUN(futex_wake_rejects_bad_args);
    UNIT_TEST_RUN(futex_wake_no_double_count);
    UNIT_TEST_RUN(futex_table_capacity_bound);
    UNIT_TEST_RUN(futex_clear_child_tid_wake_all);
    return unit_test_finish();
}
