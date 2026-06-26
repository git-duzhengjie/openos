/* SPDX-License-Identifier: MIT */
/*
 * tsc_selftest64.h — Step E.4
 *
 * Tiny in-kernel sanity check around the PIT-calibrated TSC. Verifies:
 *   - tsc_per_ms() is non-zero and within the expected sanity band.
 *   - tsc_uptime_ms() advances monotonically across a short busy-spin.
 *
 * Failure here is non-fatal — selftest_run logs PASS/FAIL and lets boot
 * continue (callers downgrade to the legacy rdtsc>>20 path automatically).
 */
#ifndef OPENOS_X86_64_TSC_SELFTEST64_H
#define OPENOS_X86_64_TSC_SELFTEST64_H

#ifdef __cplusplus
extern "C" {
#endif

void arch_x86_64_tsc_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
