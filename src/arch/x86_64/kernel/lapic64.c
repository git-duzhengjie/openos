#include "../include/lapic64.h"
#include "../include/acpi64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.1.1 — Local APIC (xAPIC) MMIO driver. See header for scope notes.
 *
 * Sequence:
 *   1. Read IA32_APIC_BASE MSR (0x1B); ensure bit 11 (global enable) is set
 *      (UEFI firmware always leaves it on for boot CPU).
 *   2. Probe the MMIO window at the hardcoded base (0xFEE00000). UEFI's
 *      identity map already covers this physical address, so plain volatile
 *      MMIO works.
 *   3. Clear TPR (accept all priorities), then write SVR with bit 8 (enable)
 *      and spurious vector 0xFF.
 *   4. Read back ID/Version as a sanity probe.
 */

#define IA32_APIC_BASE_MSR              0x1Bu
#define IA32_APIC_BASE_GLOBAL_ENABLE    (1ull << 11)

static volatile uint8_t *g_lapic_mmio = (void *)0;
static bool g_lapic_ready = false;

static inline uint64_t rdmsr_u64(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint32_t mmio_read32(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}

static inline void mmio_write32(volatile uint8_t *base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(base + off) = v;
}

bool arch_x86_64_lapic_init(void) {
    uint64_t apic_base_msr = rdmsr_u64(IA32_APIC_BASE_MSR);
    if ((apic_base_msr & IA32_APIC_BASE_GLOBAL_ENABLE) == 0) {
        /* Globally disabled — caller should fall back to PIC. */
        return false;
    }

    /* G.3b: Prefer the LAPIC base reported by ACPI MADT. Fall back to the
     * IA32_APIC_BASE MSR, then to the architectural default 0xFEE00000. */
    uint64_t pa = 0;
    const arch_x86_64_acpi_info_t *acpi = arch_x86_64_acpi_info();
    if (acpi && acpi->valid && acpi->lapic_address) {
        pa = acpi->lapic_address;
    }
    if (pa == 0) {
        pa = apic_base_msr & ~0xFFFull;
    }
    if (pa == 0) {
        pa = OPENOS_X86_64_LAPIC_DEFAULT_PHYS_BASE;
    }
    g_lapic_mmio = (volatile uint8_t *)(uintptr_t)pa;

    /* Clear TPR so we accept every priority (no software masking). */
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_TPR, 0);

    /* Enable LAPIC with spurious vector 0xFF. */
    uint32_t svr = mmio_read32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_SVR);
    svr |= OPENOS_X86_64_LAPIC_SVR_ENABLE;
    svr = (svr & ~0xFFu) | OPENOS_X86_64_LAPIC_SPURIOUS_VECTOR;
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_SVR, svr);

    /* Sanity readback. */
    uint32_t ver = mmio_read32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_VERSION);
    if (ver == 0xFFFFFFFFu || ver == 0u) {
        /* MMIO probe failed — bail out. */
        g_lapic_mmio = (void *)0;
        return false;
    }

    g_lapic_ready = true;
    return true;
}

bool arch_x86_64_lapic_is_ready(void) { return g_lapic_ready; }

/* G.5-lapic: AP-side LAPIC bring-up. The MMIO window was already discovered
 * and mapped by the BSP; APs share the same g_lapic_mmio pointer. We only
 * need to program TPR + SVR on each AP's local APIC unit. */
bool arch_x86_64_lapic_init_ap(void) {
    if (!g_lapic_ready || !g_lapic_mmio) {
        return false;
    }

    /* Verify the AP's IA32_APIC_BASE has the global enable bit. UEFI normally
     * leaves it on for all CPUs, but be defensive. */
    uint64_t apic_base_msr = rdmsr_u64(IA32_APIC_BASE_MSR);
    if ((apic_base_msr & IA32_APIC_BASE_GLOBAL_ENABLE) == 0) {
        return false;
    }

    /* Clear TPR. */
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_TPR, 0);

    /* Enable SVR with spurious vector 0xFF. */
    uint32_t svr = mmio_read32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_SVR);
    svr |= OPENOS_X86_64_LAPIC_SVR_ENABLE;
    svr = (svr & ~0xFFu) | OPENOS_X86_64_LAPIC_SPURIOUS_VECTOR;
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_SVR, svr);

    /* Sanity readback — must be non-zero/non-all-ones. */
    uint32_t ver = mmio_read32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_VERSION);
    if (ver == 0u || ver == 0xFFFFFFFFu) {
        return false;
    }
    return true;
}

uint64_t arch_x86_64_lapic_mmio_base(void) {
    return (uint64_t)(uintptr_t)g_lapic_mmio;
}

uint32_t arch_x86_64_lapic_read(uint32_t reg_offset) {
    if (!g_lapic_ready) return 0;
    return mmio_read32(g_lapic_mmio, reg_offset);
}

void arch_x86_64_lapic_write(uint32_t reg_offset, uint32_t value) {
    if (!g_lapic_ready) return;
    mmio_write32(g_lapic_mmio, reg_offset, value);
}

void arch_x86_64_lapic_send_eoi(void) {
    if (!g_lapic_ready) return;
    /* Writing any value (canonical: 0) signals EOI for the currently
     * in-service interrupt. */
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_EOI, 0);
}

uint8_t arch_x86_64_lapic_id(void) {
    if (!g_lapic_ready) return 0;
    uint32_t id = mmio_read32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_ID);
    return (uint8_t)(id >> 24);
}

uint32_t arch_x86_64_lapic_version_raw(void) {
    if (!g_lapic_ready) return 0;
    return mmio_read32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_VERSION);
}

/* G.4.3a — ICR delivery-status poll with bounded spin. */
bool arch_x86_64_lapic_icr_wait(void) {
    if (!g_lapic_ready) return false;
    /* 1M iterations is plenty: a healthy LAPIC clears delivery_status within
     * a handful of bus cycles. We never block forever even if hardware is
     * wedged — the selftest will simply report FAIL. */
    for (uint32_t i = 0; i < 1000000u; ++i) {
        uint32_t low = mmio_read32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_ICR_LOW);
        if ((low & OPENOS_X86_64_LAPIC_ICR_DELIVERY_STATUS) == 0) {
            return true;
        }
        __asm__ __volatile__("pause");
    }
    return false;
}

/* G.4.3a — send INIT IPI to one physical-destination AP.
 *
 * Sequencing rules (SDM Vol.3A 10.6):
 *   - Write ICR_HIGH first (destination apic_id in bits 31:24). The act of
 *     writing ICR_LOW is what triggers delivery, so HIGH must be set first.
 *   - INIT is encoded as delivery_mode=101b, level=assert(1), trigger=edge(0),
 *     destination_mode=physical(0), vector=0.
 *   - After writing ICR_LOW, poll delivery_status until it clears.
 */
bool arch_x86_64_lapic_send_init(uint8_t apic_id) {
    if (!g_lapic_ready) return false;

    /* Make sure no prior IPI is still in flight before we clobber ICR. */
    if (!arch_x86_64_lapic_icr_wait()) return false;

    uint32_t high = ((uint32_t)apic_id) << 24;
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_ICR_HIGH, high);

    uint32_t low =
        OPENOS_X86_64_LAPIC_ICR_DELMOD_INIT |
        OPENOS_X86_64_LAPIC_ICR_DESTMOD_PHYS |
        OPENOS_X86_64_LAPIC_ICR_LEVEL_ASSERT;
    /* Vector field (bits 7:0) must be zero for INIT. */
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_ICR_LOW, low);

    return arch_x86_64_lapic_icr_wait();
}

/* G.4.3b-1 — send STARTUP (SIPI) IPI.
 *
 * Same ICR sequencing as INIT (HIGH first, then LOW triggers delivery).
 * Encoding differs only in DELMOD (110b = STARTUP) and the vector field
 * (which here carries the real-mode start page; AP will begin executing at
 * physical (vector << 12) in real mode).
 */
bool arch_x86_64_lapic_send_startup(uint8_t apic_id, uint8_t vector) {
    if (!g_lapic_ready) return false;

    if (!arch_x86_64_lapic_icr_wait()) return false;

    uint32_t high = ((uint32_t)apic_id) << 24;
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_ICR_HIGH, high);

    uint32_t low =
        OPENOS_X86_64_LAPIC_ICR_DELMOD_STARTUP |
        OPENOS_X86_64_LAPIC_ICR_DESTMOD_PHYS |
        OPENOS_X86_64_LAPIC_ICR_LEVEL_ASSERT |
        (uint32_t)vector;
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_ICR_LOW, low);

    return arch_x86_64_lapic_icr_wait();
}
