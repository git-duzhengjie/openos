#ifndef OPENOS_ARCH_X86_64_SCHED_PREEMPT_SELFTEST64_H
#define OPENOS_ARCH_X86_64_SCHED_PREEMPT_SELFTEST64_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Step F.3 — preemptive scheduler self-test.
 *
 * Spawns two non-yielding spin kthreads, globally enables IRQs, then
 * lets the IRQ0 (PIT @100Hz) path drive context switches via the new
 * arch_x86_64_sched_on_tick() hook.
 *
 * PASS criteria:
 *   - Both spin kthreads make forward progress (counter > 0).
 *   - sched_preempt_count > 0 (IRQ-driven switches actually occurred,
 *     not just cooperative yield).
 *   - Both kthreads reach their done sentinel before deadline.
 *
 * Returns 0 on success, non-zero step id on failure.
 */
int arch_x86_64_sched_preempt_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_SCHED_PREEMPT_SELFTEST64_H */
