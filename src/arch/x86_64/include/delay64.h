/*
 * delay64.h — Step G.4.3b-1
 *
 * Busy-wait delays for x86_64. Backed by the TSC calibration done in tsc64,
 * so callers must ensure arch_x86_64_tsc_init() has succeeded first
 * (kernel64.c already does this before smp-selftest).
 *
 * These are *busy* waits: interrupts are not toggled, the BSP just spins on
 * rdtsc. Intended for short SMP-bringup timings (INIT->SIPI 10ms, SIPI->SIPI
 * 200us) where we cannot rely on the scheduler.
 */
#ifndef OPENOS_X86_64_DELAY64_H
#define OPENOS_X86_64_DELAY64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Busy-wait approximately `us` microseconds. Falls back to a coarse loop
 * if the TSC has not been calibrated (per_ms == 0). */
void arch_x86_64_delay_us(uint32_t us);

/* Busy-wait approximately `ms` milliseconds. */
void arch_x86_64_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_X86_64_DELAY64_H */
