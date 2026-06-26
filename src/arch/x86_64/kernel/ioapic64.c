#include "../include/ioapic64.h"
#include "../include/acpi64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.1.2 — IOAPIC indirect register driver.
 * G.3b: base / gsi_base sourced from ACPI MADT when available. */

static volatile uint8_t *g_ioapic_mmio = (void *)0;
static bool g_ioapic_ready = false;
static uint8_t g_ioapic_entries = 0;
static uint32_t g_ioapic_gsi_base = 0;

static inline uint32_t ioapic_read_reg(uint8_t index) {
    *(volatile uint32_t *)(g_ioapic_mmio + OPENOS_X86_64_IOAPIC_IOREGSEL) = index;
    return *(volatile uint32_t *)(g_ioapic_mmio + OPENOS_X86_64_IOAPIC_IOWIN);
}

static inline void ioapic_write_reg(uint8_t index, uint32_t value) {
    *(volatile uint32_t *)(g_ioapic_mmio + OPENOS_X86_64_IOAPIC_IOREGSEL) = index;
    *(volatile uint32_t *)(g_ioapic_mmio + OPENOS_X86_64_IOAPIC_IOWIN) = value;
}

bool arch_x86_64_ioapic_init(void) {
    /* G.3b: ACPI-discovered base wins; else default 0xFEC00000. */
    uint64_t pa = arch_x86_64_acpi_first_ioapic_base();
    if (pa == 0) pa = OPENOS_X86_64_IOAPIC_DEFAULT_PHYS_BASE;
    g_ioapic_mmio = (volatile uint8_t *)(uintptr_t)pa;
    g_ioapic_gsi_base = arch_x86_64_acpi_first_ioapic_gsi_base();

    uint32_t ver = ioapic_read_reg(OPENOS_X86_64_IOAPIC_REG_VERSION);
    if (ver == 0xFFFFFFFFu || ver == 0u) {
        g_ioapic_mmio = (void *)0;
        return false;
    }

    /* Bits [23:16] of VERSION register = max redir entry index. */
    uint8_t max_redir = (uint8_t)((ver >> 16) & 0xFFu);
    g_ioapic_entries = (uint8_t)(max_redir + 1u);

    /* Mask every line as the safe baseline. */
    for (uint8_t gsi = 0; gsi < g_ioapic_entries; ++gsi) {
        uint8_t low_idx = (uint8_t)(OPENOS_X86_64_IOAPIC_REG_REDTBL_BASE + 2u * gsi);
        uint8_t high_idx = (uint8_t)(low_idx + 1u);
        ioapic_write_reg(low_idx, OPENOS_X86_64_IOAPIC_REDIR_MASKED);
        ioapic_write_reg(high_idx, 0);
    }

    g_ioapic_ready = true;
    return true;
}

bool arch_x86_64_ioapic_is_ready(void) { return g_ioapic_ready; }
uint64_t arch_x86_64_ioapic_mmio_base(void) { return (uint64_t)(uintptr_t)g_ioapic_mmio; }
uint8_t arch_x86_64_ioapic_entry_count(void) { return g_ioapic_entries; }

uint32_t arch_x86_64_ioapic_gsi_base(void) { return g_ioapic_gsi_base; }

uint8_t arch_x86_64_ioapic_id(void) {
    if (!g_ioapic_ready) return 0;
    uint32_t id = ioapic_read_reg(OPENOS_X86_64_IOAPIC_REG_ID);
    return (uint8_t)((id >> 24) & 0x0Fu);
}

uint32_t arch_x86_64_ioapic_version_raw(void) {
    if (!g_ioapic_ready) return 0;
    return ioapic_read_reg(OPENOS_X86_64_IOAPIC_REG_VERSION);
}

void arch_x86_64_ioapic_set_redir(uint8_t gsi, uint8_t vector, uint8_t dest_lapic_id) {
    if (!g_ioapic_ready || gsi >= g_ioapic_entries) return;
    uint8_t low_idx = (uint8_t)(OPENOS_X86_64_IOAPIC_REG_REDTBL_BASE + 2u * gsi);
    uint8_t high_idx = (uint8_t)(low_idx + 1u);

    /* High = destination LAPIC ID in bits [31:24], physical dest mode. */
    uint32_t high = ((uint32_t)dest_lapic_id) << 24;
    /* Low: vector in [7:0], delivery=fixed (000), dest=physical (0),
     * polarity=active-high (0), trigger=edge (0), masked initially. */
    uint32_t low = (uint32_t)vector | OPENOS_X86_64_IOAPIC_REDIR_MASKED;

    /* Always write high first to avoid a transient mis-routed interrupt
     * (mask bit still set in low at this point). */
    ioapic_write_reg(high_idx, high);
    ioapic_write_reg(low_idx, low);
}

void arch_x86_64_ioapic_mask(uint8_t gsi) {
    if (!g_ioapic_ready || gsi >= g_ioapic_entries) return;
    uint8_t low_idx = (uint8_t)(OPENOS_X86_64_IOAPIC_REG_REDTBL_BASE + 2u * gsi);
    uint32_t low = ioapic_read_reg(low_idx);
    low |= OPENOS_X86_64_IOAPIC_REDIR_MASKED;
    ioapic_write_reg(low_idx, low);
}

void arch_x86_64_ioapic_unmask(uint8_t gsi) {
    if (!g_ioapic_ready || gsi >= g_ioapic_entries) return;
    uint8_t low_idx = (uint8_t)(OPENOS_X86_64_IOAPIC_REG_REDTBL_BASE + 2u * gsi);
    uint32_t low = ioapic_read_reg(low_idx);
    low &= ~OPENOS_X86_64_IOAPIC_REDIR_MASKED;
    ioapic_write_reg(low_idx, low);
}

uint64_t arch_x86_64_ioapic_read_redir(uint8_t gsi) {
    if (!g_ioapic_ready || gsi >= g_ioapic_entries) return 0;
    uint8_t low_idx = (uint8_t)(OPENOS_X86_64_IOAPIC_REG_REDTBL_BASE + 2u * gsi);
    uint8_t high_idx = (uint8_t)(low_idx + 1u);
    uint32_t lo = ioapic_read_reg(low_idx);
    uint32_t hi = ioapic_read_reg(high_idx);
    return ((uint64_t)hi << 32) | lo;
}

/* G.3b: ACPI-aware ISA IRQ routing.
 *
 * Resolves the legacy ISA `irq` (0..15) into a GSI plus polarity/trigger via
 * the MADT IRQ-override table, then programs the matching redirection entry
 * with the LAPIC destination. The redirection is left UNMASKED so the caller
 * gets immediate edge/level delivery (matches the PIT path that previously
 * had explicit unmask).
 *
 * Returns the GSI that was programmed, or 0xFF on failure. */
uint8_t arch_x86_64_ioapic_route_isa_irq(uint8_t isa_irq,
                                         uint8_t vector,
                                         uint8_t dest_lapic_id) {
    if (!g_ioapic_ready) return 0xFF;

    uint32_t gsi = isa_irq;
    uint16_t flags = 0;
    /* Ignored when ACPI absent; we still get identity mapping below. */
    (void)arch_x86_64_acpi_resolve_isa_gsi(isa_irq, &gsi, &flags);

    /* Translate absolute GSI to this IOAPIC's local pin index. */
    if (gsi < g_ioapic_gsi_base) return 0xFF;
    uint32_t pin = gsi - g_ioapic_gsi_base;
    if (pin >= g_ioapic_entries) return 0xFF;

    /* MPS-style flag decode:
     *   polarity: 00=conform(ISA edge->hi), 01=hi, 10=reserved, 11=lo
     *   trigger : 00=conform(ISA edge),     01=edge,10=reserved, 11=level
     */
    uint8_t pol = (uint8_t)(flags & 0x3u);
    uint8_t trg = (uint8_t)((flags >> 2) & 0x3u);
    bool active_low = (pol == 0x3);
    bool level      = (trg == 0x3);

    uint8_t low_idx  = (uint8_t)(OPENOS_X86_64_IOAPIC_REG_REDTBL_BASE + 2u * pin);
    uint8_t high_idx = (uint8_t)(low_idx + 1u);
    uint32_t high = ((uint32_t)dest_lapic_id) << 24;
    uint32_t low  = (uint32_t)vector;
    if (active_low) low |= (1u << 13);  /* INTPOL */
    if (level)      low |= (1u << 15);  /* TRIGGER */
    /* delivery=fixed, dest=physical, NOT masked. */

    ioapic_write_reg(high_idx, high);
    ioapic_write_reg(low_idx,  low);
    return (uint8_t)pin;
}
