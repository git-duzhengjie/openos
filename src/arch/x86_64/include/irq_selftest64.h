#ifndef OPENOS_ARCH_X86_64_IRQ_SELFTEST64_H
#define OPENOS_ARCH_X86_64_IRQ_SELFTEST64_H

/* Step F.2 — verify the PIC + PIT + IRQ0 chain end-to-end:
 *   1. Unmask IRQ0 only, leave everything else masked.
 *   2. Enable interrupts (sti).
 *   3. Spin for ~200 ms (measured via the already-calibrated TSC).
 *   4. Confirm g_pit_ticks landed in [18, 22] for a 100 Hz timer
 *      (4-tick tolerance absorbs PIT phase + TSC jitter).
 *   5. cli + remask IRQ0 to leave the chain quiescent for the rest of
 *      kernel bring-up. F.3 is responsible for re-enabling once the
 *      preemptive scheduler is wired.
 *
 * Returns 0 on PASS, non-zero on FAIL (caller may halt or just log). */
int arch_x86_64_irq_selftest_run(void);

#endif /* OPENOS_ARCH_X86_64_IRQ_SELFTEST64_H */
