#include "../include/apic_selftest64.h"
#include "../include/lapic64.h"
#include "../include/ioapic64.h"
#include "../include/pic64.h"
#include "../include/pit64.h"
#include "../include/tsc64.h"
#include "../include/early_console64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.1.3 — APIC bring-up + IRQ0 reroute selftest.
 *
 * Phases (any failure short-circuits and returns false; legacy PIC path
 * remains usable):
 *   1. LAPIC init        — SVR enable, TPR=0, version readback sanity
 *   2. IOAPIC init       — read version (entry count); mask all 24 GSIs
 *   3. Reroute IRQ0      — program GSI2 (legacy ISA IRQ0 maps to GSI2 on
 *                          modern chipsets per APIC INT override; QEMU
 *                          q35/i440fx both honor this) to vector 0x20,
 *                          dest = boot LAPIC ID, masked.
 *   4. Mask 8259A        — write 0xFF/0xFF to mask every legacy line.
 *   5. Unmask GSI2       — open the IOAPIC redir for IRQ0.
 *   6. Tick verification — record g_pit_ticks, sti, wait 200ms, cli;
 *                          delta must land in [18,22] @ 100Hz, exactly the
 *                          same band F.2 used for the PIC path.
 *
 * On success the system stays in LAPIC-routed mode (PIC masked, IOAPIC
 * delivering, LAPIC EOI in pit IRQ handler picks the right path
 * automatically via lapic_is_ready()).
 */

#define APIC_SELFTEST_WAIT_MS    200u
#define APIC_SELFTEST_MIN_TICKS  18u
#define APIC_SELFTEST_MAX_TICKS  22u

/* Legacy ISA IRQ0 routes through the IOAPIC at GSI2 on q35/i440fx
 * thanks to the APIC interrupt source override that PIIX/ICH emit in
 * their MADTs (IRQ0 -> GSI2). Hardcode this; G.2 will read it from
 * ACPI properly. */
#define APIC_SELFTEST_PIT_GSI    2u
#define APIC_SELFTEST_PIT_VECTOR 0x20u

static inline void cli(void) { __asm__ __volatile__("cli"); }
static inline void sti(void) { __asm__ __volatile__("sti"); }
static inline void pause(void) { __asm__ __volatile__("pause"); }

static void log_kv_hex(const char *key, uint64_t val) {
    early_console64_write(key);
    early_console64_write_hex64(val);
}

bool arch_x86_64_apic_selftest_run(void) {
    early_console64_write("\n[x86_64][apic-selftest] begin");

    /* Phase 1: LAPIC. */
    if (!arch_x86_64_lapic_init()) {
        early_console64_write("\n[x86_64][apic-selftest] FAIL lapic_init\n");
        return false;
    }
    log_kv_hex("\n[x86_64][apic-selftest] lapic base=", arch_x86_64_lapic_mmio_base());
    log_kv_hex(" id=", arch_x86_64_lapic_id());
    log_kv_hex(" ver=", arch_x86_64_lapic_version_raw());

    /* Phase 2: IOAPIC. */
    if (!arch_x86_64_ioapic_init()) {
        early_console64_write("\n[x86_64][apic-selftest] FAIL ioapic_init\n");
        return false;
    }
    log_kv_hex("\n[x86_64][apic-selftest] ioapic base=", arch_x86_64_ioapic_mmio_base());
    log_kv_hex(" id=", arch_x86_64_ioapic_id());
    log_kv_hex(" entries=", arch_x86_64_ioapic_entry_count());

    if (arch_x86_64_ioapic_entry_count() < (APIC_SELFTEST_PIT_GSI + 1u)) {
        early_console64_write("\n[x86_64][apic-selftest] FAIL ioapic too few entries\n");
        return false;
    }

    /* Phase 3: program GSI2 -> vector 0x20 -> dest = boot LAPIC. */
    uint8_t boot_lapic_id = arch_x86_64_lapic_id();
    arch_x86_64_ioapic_set_redir(APIC_SELFTEST_PIT_GSI,
                                 APIC_SELFTEST_PIT_VECTOR,
                                 boot_lapic_id);
    uint64_t redir = arch_x86_64_ioapic_read_redir(APIC_SELFTEST_PIT_GSI);
    log_kv_hex("\n[x86_64][apic-selftest] redir[GSI2]=", redir);

    /* Verify: low byte = vector, high dword bits[31:24] = dest. */
    if ((redir & 0xFFull) != APIC_SELFTEST_PIT_VECTOR) {
        early_console64_write("\n[x86_64][apic-selftest] FAIL redir vector mismatch\n");
        return false;
    }
    if (((redir >> 56) & 0xFFull) != boot_lapic_id) {
        early_console64_write("\n[x86_64][apic-selftest] FAIL redir dest mismatch\n");
        return false;
    }

    /* Phase 4: mask the 8259A entirely. From this point lapic_is_ready()
     * is true, so pit's IRQ0 handler will EOI via LAPIC. */
    cli();
    arch_x86_64_pic_disable();

    /* Phase 5: unmask GSI2 so IRQ0 actually delivers. */
    arch_x86_64_ioapic_unmask(APIC_SELFTEST_PIT_GSI);

    /* Phase 6: tick verification. Mirror the F.2 IRQ selftest. */
    uint64_t per_ms = arch_x86_64_tsc_per_ms();
    if (per_ms == 0u) {
        early_console64_write("\n[x86_64][apic-selftest] FAIL tsc not calibrated\n");
        return false;
    }

    uint64_t before_ticks = arch_x86_64_pit_get_ticks();
    sti();

    uint64_t t0 = arch_x86_64_tsc_uptime_ms();
    uint64_t deadline = t0 + APIC_SELFTEST_WAIT_MS;
    while (arch_x86_64_tsc_uptime_ms() < deadline) {
        pause();
    }
    uint64_t t1 = arch_x86_64_tsc_uptime_ms();

    cli();
    /* Re-mask GSI2 so we leave the system in the same "IRQs disabled"
     * posture every other selftest expects. The scheduler will unmask
     * again when it starts driving preemption. */
    arch_x86_64_ioapic_mask(APIC_SELFTEST_PIT_GSI);

    uint64_t after_ticks = arch_x86_64_pit_get_ticks();
    uint64_t delta = after_ticks - before_ticks;

    log_kv_hex("\n[x86_64][apic-selftest] before_ticks=", before_ticks);
    log_kv_hex(" after_ticks=", after_ticks);
    log_kv_hex(" delta=", delta);
    log_kv_hex(" t0_ms=", t0);
    log_kv_hex(" t1_ms=", t1);

    if (delta < APIC_SELFTEST_MIN_TICKS || delta > APIC_SELFTEST_MAX_TICKS) {
        early_console64_write("\n[x86_64][apic-selftest] FAIL out-of-range\n");
        return false;
    }

    early_console64_write("\n[x86_64][apic-selftest] PASS\n");
    return true;
}
