/*
 * delay64.c — Step G.4.3b-1
 *
 * TSC-based busy-wait used by the SMP startup path. tsc64 has already
 * calibrated cycles-per-ms via PIT before we run, so we convert (us -> ticks)
 * via ticks_per_ms / 1000 and spin on rdtsc.
 *
 * If tsc64 calibration has not run (per_ms==0) we fall back to a coarse loop
 * sized for QEMU; this path is never taken in normal boot but keeps the
 * function safe if someone calls it too early.
 */

#include "../include/delay64.h"
#include "../include/tsc64.h"

static inline uint64_t rdtsc_raw(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void arch_x86_64_delay_us(uint32_t us) {
    if (us == 0) return;

    uint64_t per_ms = arch_x86_64_tsc_per_ms();
    if (per_ms == 0) {
        /* TSC not calibrated yet — coarse pause loop. ~1ns/iter on modern
         * hardware, ~5ns on QEMU; deliberately over-counts. */
        volatile uint64_t spins = (uint64_t)us * 1000ULL;
        for (volatile uint64_t i = 0; i < spins; i++) {
            __asm__ volatile("pause");
        }
        return;
    }

    /* ticks = us * per_ms / 1000 */
    uint64_t ticks = ((uint64_t)us * per_ms) / 1000ULL;
    uint64_t start = rdtsc_raw();
    while ((rdtsc_raw() - start) < ticks) {
        __asm__ volatile("pause");
    }
}

void arch_x86_64_delay_ms(uint32_t ms) {
    if (ms == 0) return;

    uint64_t per_ms = arch_x86_64_tsc_per_ms();
    if (per_ms == 0) {
        for (uint32_t i = 0; i < ms; i++) {
            arch_x86_64_delay_us(1000);
        }
        return;
    }

    uint64_t ticks = (uint64_t)ms * per_ms;
    uint64_t start = rdtsc_raw();
    while ((rdtsc_raw() - start) < ticks) {
        __asm__ volatile("pause");
    }
}
