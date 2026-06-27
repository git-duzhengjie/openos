/*
 * sched_prio_selftest64.c — Step G.2 priority-weighted scheduling self-test.
 *
 * Design rationale:
 *   - Three CPU-burn workers, one per priority band. They never yield;
 *     PIT IRQ0 + on_tick is the only way they can be preempted.
 *   - Each worker just bumps its own counter in a pause()-paced loop.
 *     The RATIO of the three counters at the end of the window reveals
 *     how much CPU each priority band actually got.
 *   - We size the window so that NONE of the workers can self-complete
 *     within the deadline. The counters are therefore proportional to
 *     CPU share, not to iteration cap.
 *   - Expected quantum split (HIGH:NORMAL:LOW = 10:5:2 ticks) ⇒
 *     ideal CPU share 10/17 : 5/17 : 2/17 ≈ 58.8% : 29.4% : 11.8%.
 *     We assert the loose monotonic invariant HIGH > NORMAL > LOW and
 *     HIGH >= 2 * LOW so we don't get flaky on CPU-frequency jitter.
 *   - cli()/mask IRQ0 on exit so downstream tests inherit the same
 *     "interrupts off" precondition.
 */

#include "../include/sched_prio_selftest64.h"

#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/sched64.h"
#include "../include/pic64.h"
#include "../include/pit64.h"
#include "../include/tsc64.h"
#include "../include/lapic64.h"
#include "../include/ioapic64.h"

#define PRIO_DEADLINE_MS    600u
#define PRIO_WORKER_ITERS   100000000u   /* never reached: window-bounded */

static volatile uint64_t prio_high_counter;
static volatile uint64_t prio_normal_counter;
static volatile uint64_t prio_low_counter;
static volatile uint32_t prio_high_done;
static volatile uint32_t prio_normal_done;
static volatile uint32_t prio_low_done;

static inline void cli(void)    { __asm__ __volatile__("cli"); }
static inline void sti(void)    { __asm__ __volatile__("sti"); }
static inline void pause_(void) { __asm__ __volatile__("pause"); }

static void log_kv(const char *key, uint64_t val) {
    early_console64_write(key);
    early_console64_write_hex64(val);
}

static void burner_high(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < PRIO_WORKER_ITERS; ++i) {
        prio_high_counter++;
        pause_();
    }
    prio_high_done = 1u;
    arch_x86_64_sched_exit_self();
    for (;;) { pause_(); }
}

static void burner_normal(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < PRIO_WORKER_ITERS; ++i) {
        prio_normal_counter++;
        pause_();
    }
    prio_normal_done = 1u;
    arch_x86_64_sched_exit_self();
    for (;;) { pause_(); }
}

static void burner_low(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < PRIO_WORKER_ITERS; ++i) {
        prio_low_counter++;
        pause_();
    }
    prio_low_done = 1u;
    arch_x86_64_sched_exit_self();
    for (;;) { pause_(); }
}

int arch_x86_64_sched_prio_selftest_run(void) {
    early_console64_write("\n[x86_64][prio-selftest] begin");

    if (arch_x86_64_tsc_per_ms() == 0u) {
        early_console64_write("\n[x86_64][prio-selftest] FAIL tsc not calibrated\n");
        return 1;
    }

    prio_high_counter = prio_normal_counter = prio_low_counter = 0ull;
    prio_high_done    = prio_normal_done    = prio_low_done    = 0u;

    uint64_t preempts_before = arch_x86_64_sched_preempt_count();
    uint64_t switches_before = arch_x86_64_sched_switch_count();

    /* G.6.7-pre fix: pin all three burners to BSP (CPU 0) so they
     * actually contend for the same CPU's quantum. Otherwise the
     * G.6.5c round-robin spawner would scatter them across cores and
     * each runs ~unthrottled, collapsing H/N/L into ratio ~= 1:1:1
     * and failing the H > N > L invariant on SMP_N>=2. The priority
     * scheduler is per-CPU; this test is a per-CPU invariant. */
    uint32_t id_h = arch_x86_64_sched_spawn_kthread_prio_on(
        burner_high,   0, OPENOS_X86_64_SCHED_PRIO_HIGH,   0u);
    uint32_t id_n = arch_x86_64_sched_spawn_kthread_prio_on(
        burner_normal, 0, OPENOS_X86_64_SCHED_PRIO_NORMAL, 0u);
    uint32_t id_l = arch_x86_64_sched_spawn_kthread_prio_on(
        burner_low,    0, OPENOS_X86_64_SCHED_PRIO_LOW,    0u);
    if (id_h == 0u || id_n == 0u || id_l == 0u) {
        early_console64_write("\n[x86_64][prio-selftest] FAIL spawn\n");
        return 2;
    }
    log_kv("\n[x86_64][prio-selftest] slot_h=", (uint64_t)id_h);
    log_kv(" slot_n=",                          (uint64_t)id_n);
    log_kv(" slot_l=",                          (uint64_t)id_l);
    log_kv(" prio_h=", (uint64_t)arch_x86_64_sched_get_priority(id_h));
    log_kv(" prio_n=", (uint64_t)arch_x86_64_sched_get_priority(id_n));
    log_kv(" prio_l=", (uint64_t)arch_x86_64_sched_get_priority(id_l));

    /* G.6.7-pre fix #2: demote the bootstrap thread (slot 0, the kernel's
     * current execution context on BSP) to LOW for the measurement window.
     * Bootstrap defaults to NORMAL priority, so before this fix it competed
     * for quantum 1:1 against burner_normal in the round-robin pick_next.
     * Even worse, our wait loop below is a busy pause-loop (no hlt), so
     * bootstrap actually consumes its full NORMAL quantum every visit,
     * shrinking the gap H>N below the 12.5% guard band. Demoting to LOW
     * makes bootstrap's quantum 2x smaller than burner_low's quantum-share
     * never-mind burner_normal/burner_high, so the H:N:L ratio cleanly
     * reflects only the three burners. Restored at the end. */
    uint32_t boot_slot = arch_x86_64_sched_current_slot();
    uint32_t boot_prio = arch_x86_64_sched_get_priority(boot_slot);
    (void)arch_x86_64_sched_set_priority(boot_slot, OPENOS_X86_64_SCHED_PRIO_LOW);

    cli();
    /* G.2 runs AFTER apic_selftest, so IRQ0 delivery goes through IOAPIC GSI2
     * (PIC is fully masked from apic_selftest phase 4). Fall back to the PIC
     * path on the rare branch where LAPIC didn't come up. */
    if (arch_x86_64_lapic_is_ready() && arch_x86_64_ioapic_is_ready()) {
        arch_x86_64_ioapic_unmask(2u);
    } else {
        arch_x86_64_pic_unmask(0u);
    }
    sti();

    uint64_t t0 = arch_x86_64_tsc_uptime_ms();
    uint64_t deadline = t0 + PRIO_DEADLINE_MS;
    while (arch_x86_64_tsc_uptime_ms() < deadline) {
        pause_();
    }
    uint64_t t1 = arch_x86_64_tsc_uptime_ms();

    cli();
    if (arch_x86_64_lapic_is_ready() && arch_x86_64_ioapic_is_ready()) {
        arch_x86_64_ioapic_mask(2u);
    } else {
        arch_x86_64_pic_mask(0u);
    }

    /* G.6.7-pre fix #2: restore bootstrap thread's original priority. */
    (void)arch_x86_64_sched_set_priority(boot_slot, boot_prio);

    uint64_t preempts_after = arch_x86_64_sched_preempt_count();
    uint64_t switches_after = arch_x86_64_sched_switch_count();
    uint64_t preempt_delta  = preempts_after - preempts_before;
    uint64_t switch_delta   = switches_after - switches_before;

    log_kv("\n[x86_64][prio-selftest] elapsed_ms=", t1 - t0);
    log_kv(" H=",          prio_high_counter);
    log_kv(" N=",          prio_normal_counter);
    log_kv(" L=",          prio_low_counter);
    log_kv(" preempts=",   preempt_delta);
    log_kv(" switches=",   switch_delta);

    /* Invariants: each priority band must have made some progress, and
     * the canonical ordering H > N > L must hold. The HIGH band should
     * be at least twice the LOW band (10/2 quantum ratio gives ~5x in
     * the ideal model; demand 2x to absorb jitter / context-switch
     * overhead). */
    if (prio_high_counter == 0ull || prio_normal_counter == 0ull
        || prio_low_counter == 0ull) {
        early_console64_write("\n[x86_64][prio-selftest] FAIL band starved\n");
        return 3;
    }
    if (!(prio_high_counter > prio_normal_counter)) {
        early_console64_write("\n[x86_64][prio-selftest] FAIL H<=N\n");
        return 4;
    }
    if (!(prio_normal_counter > prio_low_counter)) {
        early_console64_write("\n[x86_64][prio-selftest] FAIL N<=L\n");
        return 5;
    }
    if (!(prio_high_counter >= 2ull * prio_low_counter)) {
        early_console64_write("\n[x86_64][prio-selftest] FAIL H<2*L\n");
        return 6;
    }
    if (preempt_delta == 0ull) {
        early_console64_write("\n[x86_64][prio-selftest] FAIL no preemption\n");
        return 7;
    }

    early_console64_write("\n[x86_64][prio-selftest] PASS\n");
    return 0;
}
