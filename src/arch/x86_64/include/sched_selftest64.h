#ifndef OPENOS_ARCH_X86_64_SCHED_SELFTEST64_H
#define OPENOS_ARCH_X86_64_SCHED_SELFTEST64_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Step E.2 \u2014 cooperative kthread scheduler dry-run.
 *
 * Spawns two kthreads ("A" and "B"), each yielding a few times. The
 * boot context also yields, threading control across all three. The
 * test passes when every kthread runs to completion and the global
 * "switches" counter matches the expected sequence.
 *
 * Returns 0 on success; non-zero step id on failure (mirrors
 * syscall_selftest64 conventions for fast log-grepping).
 */
int arch_x86_64_sched_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_SCHED_SELFTEST64_H */
