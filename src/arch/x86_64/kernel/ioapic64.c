#include "../include/ioapic64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.1.2 — IOAPIC indirect register driver. */

static volatile uint8_t *g_ioapic_mmio = (void *)0;
static bool g_ioapic_ready = false;
static uint8_t g_ioapic_entries = 0;

static inline uint32_t ioapic_read_reg(uint8_t index) {
    *(volatile uint32_t *)(g_ioapic_mmio + OPENOS_X86_64_IOAPIC_IOREGSEL) = index;
    return *(volatile uint32_t *)(g_ioapic_mmio + OPENOS_X86_64_IOAPIC_IOWIN);
}

static inline void ioapic_write_reg(uint8_t index, uint32_t value) {
    *(volatile uint32_t *)(g_ioapic_mmio + OPENOS_X86_64_IOAPIC_IOREGSEL) = index;
    *(volatile uint32_t *)(g_ioapic_mmio + OPENOS_X86_64_IOAPIC_IOWIN) = value;
}

bool arch_x86_64_ioapic_init(void) {
    g_ioapic_mmio = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_IOAPIC_DEFAULT_PHYS_BASE;

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
