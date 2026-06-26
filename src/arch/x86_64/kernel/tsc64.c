/* SPDX-License-Identifier: MIT */
/*
 * tsc64.c — Step E.4
 *
 * Calibrate the TSC against the i8254 PIT (channel 2, gated, polled). This
 * avoids any IRQ plumbing while still giving us a hardware reference clock,
 * so we can replace the placeholder `rdtsc >> 20` uptime with real ms.
 *
 * Method
 * ------
 *   1. Program PIT channel 2 in one-shot mode (mode 0, lobyte/hibyte). The
 *      PIT input is the venerable 1193182 Hz (~1.193 MHz) crystal divisor.
 *   2. We pick a 50 ms window: ticks = 1193182 * 50 / 1000 = 59659. Channel 2
 *      uses a 16-bit reload, so the value comfortably fits.
 *   3. Open the gate via port 0x61 bit0 (channel 2 GATE), snapshot rdtsc,
 *      poll port 0x61 bit5 (OUT2) until it goes high (terminal count), then
 *      snapshot rdtsc again. delta_tsc / 50 → tsc_per_ms.
 *
 * Robustness
 * ----------
 *   - We bail out if rdtsc never advances or if OUT2 doesn't fire within
 *     ~200 ms wall (`spin_budget` iterations). Returns 0 in that case so the
 *     caller can fall back to the legacy estimate (we keep one shipping).
 *   - We sanity-clamp the calibrated value to the [100 MHz, 50 GHz] band; a
 *     calibrated tsc_per_ms outside that band almost certainly means we
 *     timed something other than the PIT.
 */

#include "../include/tsc64.h"
#include "../include/early_console64.h"

#include <stdint.h>

/* --- ports / constants ---------------------------------------------------- */
#define PIT_CHANNEL2_DATA   0x42
#define PIT_MODE_CMD        0x43
#define PORT_61             0x61   /* NMI status / PIT ch2 control on PCs */
#define PORT_61_GATE2       0x01   /* bit 0: GATE2 (enable counting) */
#define PORT_61_SPKR_EN     0x02   /* bit 1: speaker (we keep it OFF) */
#define PORT_61_OUT2        0x20   /* bit 5: OUT2 (reads high at terminal count) */

#define PIT_CMD_CH2_ACCESS_LOHI_MODE0  0xB0   /* ch2, lobyte/hibyte, mode 0, binary */

#define PIT_HZ              1193182u
#define CALIB_WINDOW_MS     50u
#define CALIB_RELOAD        ((PIT_HZ * CALIB_WINDOW_MS) / 1000u)  /* 59659 */

#define TSC_PER_MS_MIN      (100ull * 1000ull)         /* 100 MHz lower bound */
#define TSC_PER_MS_MAX      (50ull * 1000ull * 1000ull) /* 50 GHz upper bound */

/* --- module state --------------------------------------------------------- */
static uint64_t g_tsc_per_ms = 0;
static uint64_t g_tsc_boot   = 0;
static int      g_initialized = 0;

/* --- port I/O primitives (kept local to avoid leaking into headers) ------ */
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

uint64_t arch_x86_64_tsc_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t arch_x86_64_tsc_per_ms(void) {
    return g_tsc_per_ms;
}

uint64_t arch_x86_64_tsc_uptime_ms(void) {
    if (!g_initialized || g_tsc_per_ms == 0) {
        return 0;
    }
    uint64_t now = arch_x86_64_tsc_rdtsc();
    if (now <= g_tsc_boot) return 0;
    return (now - g_tsc_boot) / g_tsc_per_ms;
}

int arch_x86_64_tsc_init(void) {
    if (g_initialized) return 1;

    /* Disable speaker, drop GATE2 to known-low state. */
    uint8_t p61 = inb(PORT_61);
    p61 &= ~(PORT_61_GATE2 | PORT_61_SPKR_EN);
    outb(PORT_61, p61);

    /* Program PIT channel 2: mode 0 (interrupt on terminal count, but since
     * we don't wire the IRQ we just poll OUT2). lobyte/hibyte access. */
    outb(PIT_MODE_CMD, PIT_CMD_CH2_ACCESS_LOHI_MODE0);
    outb(PIT_CHANNEL2_DATA, (uint8_t)(CALIB_RELOAD & 0xFF));
    outb(PIT_CHANNEL2_DATA, (uint8_t)((CALIB_RELOAD >> 8) & 0xFF));

    /* Snapshot start TSC just before raising GATE2 to minimize skew. */
    uint64_t t0 = arch_x86_64_tsc_rdtsc();
    outb(PORT_61, (uint8_t)(p61 | PORT_61_GATE2));

    /* Poll OUT2 until it asserts. A 200 ms budget at ~1 ns per iteration
     * gives us a generous 2e8 iteration cap; on QEMU we typically finish
     * in well under 1e7 spins. */
    const uint64_t spin_budget = 200ull * 1000ull * 1000ull;
    uint64_t spins = 0;
    while ((inb(PORT_61) & PORT_61_OUT2) == 0) {
        if (++spins > spin_budget) {
            /* Drop the gate, declare failure. */
            outb(PORT_61, (uint8_t)(p61 & ~PORT_61_GATE2));
            return 0;
        }
    }
    uint64_t t1 = arch_x86_64_tsc_rdtsc();

    /* Drop GATE2 — leave the speaker silent and the channel idle. */
    outb(PORT_61, (uint8_t)(p61 & ~PORT_61_GATE2));

    if (t1 <= t0) return 0;
    uint64_t delta = t1 - t0;
    uint64_t per_ms = delta / CALIB_WINDOW_MS;

    if (per_ms < TSC_PER_MS_MIN || per_ms > TSC_PER_MS_MAX) {
        /* Out-of-band: keep g_initialized = 0 so callers know it failed. */
        return 0;
    }

    g_tsc_per_ms  = per_ms;
    g_tsc_boot    = t0;
    g_initialized = 1;

    early_console64_write("[tsc] calibrated per_ms=");
    early_console64_write_hex64(g_tsc_per_ms);
    early_console64_write(" delta=");
    early_console64_write_hex64(delta);
    early_console64_write("\n");
    return 1;
}
