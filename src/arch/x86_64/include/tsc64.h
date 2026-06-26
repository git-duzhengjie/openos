/* SPDX-License-Identifier: MIT */
/*
 * tsc64.h — Step E.4
 *
 * Minimal TSC calibration backed by the i8254 PIT channel 2 (gated, no IRQ).
 * We measure how many TSC ticks elapse during a precisely-known PIT window
 * (50 ms by default), so do_uptime_ms() can convert TSC deltas into real
 * milliseconds instead of the placeholder `rdtsc >> 20`.
 *
 * Public surface intentionally small:
 *   - arch_x86_64_tsc_init():      one-shot calibration (idempotent).
 *   - arch_x86_64_tsc_per_ms():    TSC ticks per ms (0 before init).
 *   - arch_x86_64_tsc_uptime_ms(): monotonic ms since boot calibration.
 *   - arch_x86_64_tsc_rdtsc():     raw 64-bit TSC reader (helper).
 */
#ifndef OPENOS_X86_64_TSC64_H
#define OPENOS_X86_64_TSC64_H

#include "arch64_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Run PIT-based calibration. Safe to call multiple times; only the first
 * successful call updates state. Returns 1 on success, 0 on failure (e.g.,
 * PIT didn't fire within a sane bound — should never happen on QEMU/OVMF). */
int arch_x86_64_tsc_init(void);

/* TSC ticks per millisecond. Returns 0 until init succeeds. */
uint64_t arch_x86_64_tsc_per_ms(void);

/* Monotonic ms since arch_x86_64_tsc_init() succeeded. Before calibration
 * returns 0; after, monotonic and reflective of wall-clock ms. */
uint64_t arch_x86_64_tsc_uptime_ms(void);

/* Raw TSC. Inlinable but kept as a real symbol for selftests. */
uint64_t arch_x86_64_tsc_rdtsc(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_X86_64_TSC64_H */
