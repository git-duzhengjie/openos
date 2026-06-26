/*
 * sched_prio_selftest64.h — Step G.2 priority-weighted scheduling self-test.
 *
 * Spawns three CPU-burn kthreads (HIGH / NORMAL / LOW), lets the
 * PIT-driven preemptive scheduler run them for a fixed window, then
 * verifies that:
 *   - all three made progress (no priority starvation),
 *   - HIGH count > NORMAL count > LOW count,
 *   - the spread is in the expected ballpark (HIGH >= ~2x LOW).
 */
#ifndef OPENOS_ARCH_X86_64_SCHED_PRIO_SELFTEST64_H
#define OPENOS_ARCH_X86_64_SCHED_PRIO_SELFTEST64_H

#ifdef __cplusplus
extern "C" {
#endif

int arch_x86_64_sched_prio_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_SCHED_PRIO_SELFTEST64_H */
