/*
 * sched_preempt_selftest64.c — Step F.3 preemptive scheduler self-test.
 *
 * Design rationale:
 *   - The two worker kthreads MUST NOT call sched_yield(). The whole
 *     point is to prove that IRQ0 (PIT) drives the scheduler. If they
 *     yielded we'd be re-running the E.2 cooperative test.
 *   - Workers loop a bounded number of iterations and exit. Each
 *     iteration burns ~a few thousand cycles via pause() so a 50 ms
 *     quantum (5 ticks @ 100 Hz) elapses well before the worker
 *     finishes on its own.
 *   - Bootstrap sti's, unmasks IRQ0, then busy-waits on done flags
 *     with a hard TSC-based deadline so a bug cannot wedge boot.
 *   - On exit we cli + mask IRQ0 again so downstream tests (e.g.
 *     ring3 hello64) inherit the same "interrupts off" precondition
 *     they were designed against.
 *
 * Why .bss for counters: same convention as syscall/sched selftests —
 * user-style stacks are slim and we don't want surprise overflows.
 */

#include "../include/sched_preempt_selftest64.h"

#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/sched64.h"
#include "../include/pic64.h"
#include "../include/pit64.h"
#include "../include/tsc64.h"

#define PREEMPT_WORKER_ITERS    200000u
#define PREEMPT_DEADLINE_MS     2000u

static volatile uint64_t worker_a_counter;
static volatile uint64_t worker_b_counter;
static volatile uint32_t worker_a_done;
static volatile uint32_t worker_b_done;

static inline void cli(void)   { __asm__ __volatile__("cli"); }
static inline void sti(void)   { __asm__ __volatile__("sti"); }
static inline void pause_(void){ __asm__ __volatile__("pause"); }

static void log_kv(const char *key, uint64_t val) {
    early_console64_write(key);
    early_console64_write_hex64(val);
}

static void preempt_worker_a(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < PREEMPT_WORKER_ITERS; ++i) {
        /* pure CPU burn — no yield, only the IRQ0 path can preempt us */
        worker_a_counter++;
        pause_();
    }
    worker_a_done = 1u;
    /* Park as EXITED so the scheduler frees the slot. Workers in F.3
     * intentionally do NOT cooperatively yield in their hot loop —
     * exit_self() is the one and only voluntary scheduler call. */
    arch_x86_64_sched_exit_self();
    for (;;) { pause_(); } /* unreachable */
}

static void preempt_worker_b(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < PREEMPT_WORKER_ITERS; ++i) {
        worker_b_counter++;
        pause_();
    }
    worker_b_done = 1u;
    arch_x86_64_sched_exit_self();
    for (;;) { pause_(); } /* unreachable */
}

int arch_x86_64_sched_preempt_selftest_run(void) {
    early_console64_write("\n[x86_64][preempt-selftest] begin");

    /* Sanity: TSC must be calibrated; we use it for the deadline. */
    if (arch_x86_64_tsc_per_ms() == 0u) {
        early_console64_write("\n[x86_64][preempt-selftest] FAIL tsc not calibrated\n");
        return 1;
    }

    worker_a_counter = worker_b_counter = 0ull;
    worker_a_done    = worker_b_done    = 0u;

    uint64_t preempts_before = arch_x86_64_sched_preempt_count();
    uint64_t switches_before = arch_x86_64_sched_switch_count();

    uint32_t id_a = arch_x86_64_sched_spawn_kthread(preempt_worker_a, 0);
    if (id_a == 0u) {
        early_console64_write("\n[x86_64][preempt-selftest] FAIL spawn A\n");
        return 2;
    }
    uint32_t id_b = arch_x86_64_sched_spawn_kthread(preempt_worker_b, 0);
    if (id_b == 0u) {
        early_console64_write("\n[x86_64][preempt-selftest] FAIL spawn B\n");
        return 3;
    }
    log_kv("\n[x86_64][preempt-selftest] slot_a=", (uint64_t)id_a);
    log_kv(" slot_b=",                              (uint64_t)id_b);

    /* Arm IRQ0 globally. From now until cli/mask below, every PIT tick
     * runs the on_tick hook which may switch us off-CPU. */
    cli();
    arch_x86_64_pic_unmask(0u);
    sti();

    uint64_t t0 = arch_x86_64_tsc_uptime_ms();
    uint64_t deadline = t0 + PREEMPT_DEADLINE_MS;
    while ((!worker_a_done || !worker_b_done) &&
           arch_x86_64_tsc_uptime_ms() < deadline) {
        pause_();
    }
    uint64_t t1 = arch_x86_64_tsc_uptime_ms();

    cli();
    arch_x86_64_pic_mask(0u);

    uint64_t preempts_after = arch_x86_64_sched_preempt_count();
    uint64_t switches_after = arch_x86_64_sched_switch_count();
    uint64_t preempt_delta  = preempts_after - preempts_before;
    uint64_t switch_delta   = switches_after - switches_before;

    log_kv("\n[x86_64][preempt-selftest] elapsed_ms=", t1 - t0);
    log_kv(" A=",            worker_a_counter);
    log_kv(" B=",            worker_b_counter);
    log_kv(" preempts=",     preempt_delta);
    log_kv(" switches=",     switch_delta);
    log_kv(" a_done=",       (uint64_t)worker_a_done);
    log_kv(" b_done=",       (uint64_t)worker_b_done);

    if (preempt_delta == 0ull) {
        early_console64_write("\n[x86_64][preempt-selftest] FAIL no preemption observed\n");
        return 4;
    }
    if (worker_a_counter == 0ull || worker_b_counter == 0ull) {
        early_console64_write("\n[x86_64][preempt-selftest] FAIL worker made no progress\n");
        return 5;
    }
    if (!worker_a_done || !worker_b_done) {
        early_console64_write("\n[x86_64][preempt-selftest] FAIL worker not done before deadline\n");
        return 6;
    }

    early_console64_write("\n[x86_64][preempt-selftest] PASS\n");
    return 0;
}
