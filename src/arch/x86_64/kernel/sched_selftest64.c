/*
 * sched_selftest64.c \u2014 Step E.2 cooperative scheduler self-test.
 *
 * Design:
 *   - Two static kthreads (kt_a, kt_b) each loop N times, logging the
 *     iteration and calling sched_yield(). The boot context yields
 *     between spawns so the runqueue gets a fair shake.
 *   - We expect both kthreads to reach their "done" log line before
 *     returning here, and switch_count must be >= 2*N + 2.
 *   - All log lines are prefixed [x86_64][sched-selftest] so grep is
 *     trivial both in CI logs and during live debugging.
 *
 * Why static state (not stack):
 *   The kthread stacks are heap-allocated and modest (8 KB). Anything
 *   chunky (counters, flags) stays in .bss to mirror the convention we
 *   already follow in syscall_selftest64.c.
 */

#include "../include/sched_selftest64.h"

#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/sched64.h"

#define SCHED_SELFTEST_ITERATIONS 3u

static volatile uint32_t kt_a_iters;
static volatile uint32_t kt_b_iters;
static volatile uint32_t kt_a_done;
static volatile uint32_t kt_b_done;

static void log_kv(const char *key, uint64_t value) {
    early_console64_write(key);
    early_console64_write_hex64(value);
    early_console64_write("\n");
}

static void selftest_kthread_a(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < SCHED_SELFTEST_ITERATIONS; ++i) {
        early_console64_write("[x86_64][sched-selftest] A iter=");
        early_console64_write_hex64((uint64_t)i);
        early_console64_write("\n");
        kt_a_iters = i + 1u;
        (void)arch_x86_64_sched_yield();
    }
    kt_a_done = 1u;
    early_console64_write("[x86_64][sched-selftest] A done\n");
}

static void selftest_kthread_b(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < SCHED_SELFTEST_ITERATIONS; ++i) {
        early_console64_write("[x86_64][sched-selftest] B iter=");
        early_console64_write_hex64((uint64_t)i);
        early_console64_write("\n");
        kt_b_iters = i + 1u;
        (void)arch_x86_64_sched_yield();
    }
    kt_b_done = 1u;
    early_console64_write("[x86_64][sched-selftest] B done\n");
}

int arch_x86_64_sched_selftest_run(void) {
    early_console64_write("[x86_64][sched-selftest] step E.2 begin\n");

    kt_a_iters = kt_b_iters = 0u;
    kt_a_done  = kt_b_done  = 0u;

    uint32_t id_a = arch_x86_64_sched_spawn_kthread(selftest_kthread_a, 0);
    if (id_a == 0u) {
        early_console64_write("[x86_64][sched-selftest] FAIL spawn A\n");
        return 1;
    }
    log_kv("[x86_64][sched-selftest] spawned A slot=", (uint64_t)id_a);

    uint32_t id_b = arch_x86_64_sched_spawn_kthread(selftest_kthread_b, 0);
    if (id_b == 0u) {
        early_console64_write("[x86_64][sched-selftest] FAIL spawn B\n");
        return 2;
    }
    log_kv("[x86_64][sched-selftest] spawned B slot=", (uint64_t)id_b);

    /* Boot-context yields repeatedly until both kthreads finish. We bound
     * the loop so a bug can't wedge the kernel boot forever. */
    const uint32_t safety_budget = 4u * SCHED_SELFTEST_ITERATIONS + 4u;
    uint32_t rounds = 0u;
    while ((!kt_a_done || !kt_b_done) && rounds < safety_budget) {
        (void)arch_x86_64_sched_yield();
        ++rounds;
    }

    log_kv("[x86_64][sched-selftest] rounds=", (uint64_t)rounds);
    log_kv("[x86_64][sched-selftest] A_iters=", (uint64_t)kt_a_iters);
    log_kv("[x86_64][sched-selftest] B_iters=", (uint64_t)kt_b_iters);
    log_kv("[x86_64][sched-selftest] switches=", arch_x86_64_sched_switch_count());
    log_kv("[x86_64][sched-selftest] kthreads_live=", (uint64_t)arch_x86_64_sched_kthread_count());

    if (!kt_a_done) { early_console64_write("[x86_64][sched-selftest] FAIL A not done\n"); return 3; }
    if (!kt_b_done) { early_console64_write("[x86_64][sched-selftest] FAIL B not done\n"); return 4; }
    if (kt_a_iters != SCHED_SELFTEST_ITERATIONS) { return 5; }
    if (kt_b_iters != SCHED_SELFTEST_ITERATIONS) { return 6; }
    if (arch_x86_64_sched_switch_count() < 2u * SCHED_SELFTEST_ITERATIONS) {
        early_console64_write("[x86_64][sched-selftest] FAIL switch count low\n");
        return 7;
    }

    early_console64_write("[x86_64][sched-selftest] PASS\n");
    return 0;
}
