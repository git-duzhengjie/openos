/* SPDX-License-Identifier: MIT */
/*
 * tsc_selftest64.c — Step E.4
 *
 * Two cheap checks:
 *
 *   1. calibrated band  — per_ms must lie in [100 MHz, 50 GHz]; matches the
 *      acceptance window enforced inside tsc64.c so we exercise the latch.
 *
 *   2. monotonic advance — uptime_ms() must not go backwards across a busy
 *      spin tuned to burn through at least one millisecond worth of TSC
 *      ticks (a multiple of per_ms). We compare a pair of snapshots; the
 *      delta must be strictly positive (>= 1 ms) AND must be bounded above
 *      by a generous ceiling so we catch wildly-wrong calibrations too.
 *
 * Logs use the same `[tsc-selftest]` prefix the rest of the boot banner uses,
 * so anybody grepping serial output stays sane.
 */

#include "../include/tsc_selftest64.h"

#include "../include/early_console64.h"
#include "../include/tsc64.h"

#include <stdint.h>

#define BAND_LOW   (100ull * 1000ull)
#define BAND_HIGH  (50ull * 1000ull * 1000ull)

static void log_kv(const char *k, uint64_t v) {
    early_console64_write(k);
    early_console64_write_hex64(v);
}

void arch_x86_64_tsc_selftest_run(void) {
    early_console64_write("[tsc-selftest] start\n");

    uint64_t per_ms = arch_x86_64_tsc_per_ms();
    if (per_ms == 0) {
        early_console64_write("[tsc-selftest] FAIL not-calibrated\n");
        return;
    }
    if (per_ms < BAND_LOW || per_ms > BAND_HIGH) {
        log_kv("[tsc-selftest] FAIL band per_ms=", per_ms);
        early_console64_write("\n");
        return;
    }

    /* Burn ~5 ms worth of TSC ticks via a rdtsc-bounded spin. Using rdtsc as
     * its own clock means the loop terminates even if the host emulates the
     * PIT slowly — we only need monotonic progress. */
    uint64_t budget   = per_ms * 5ull;
    uint64_t deadline = arch_x86_64_tsc_rdtsc() + budget;

    uint64_t ms_a = arch_x86_64_tsc_uptime_ms();
    while (arch_x86_64_tsc_rdtsc() < deadline) {
        /* spin */
    }
    uint64_t ms_b = arch_x86_64_tsc_uptime_ms();

    if (ms_b < ms_a) {
        log_kv("[tsc-selftest] FAIL retrograde a=", ms_a);
        log_kv(" b=", ms_b);
        early_console64_write("\n");
        return;
    }

    uint64_t delta_ms = ms_b - ms_a;
    /* We spun ~5 ms; allow [1, 100] ms to soak up emulator scheduling jitter. */
    if (delta_ms < 1 || delta_ms > 100) {
        log_kv("[tsc-selftest] FAIL delta_ms=", delta_ms);
        early_console64_write("\n");
        return;
    }

    log_kv("[tsc-selftest] PASS per_ms=", per_ms);
    log_kv(" delta_ms=", delta_ms);
    early_console64_write("\n");
}
