#ifndef OPENOS_ARCH_X86_64_IOAPIC64_H
#define OPENOS_ARCH_X86_64_IOAPIC64_H

#include <stdint.h>
#include <stdbool.h>

/* Step G.1.2 — IOAPIC MMIO driver.
 *
 * IOAPIC routes chipset-level interrupt lines (GSI = Global System Interrupt)
 * to CPU vectors via a programmable redirection table. Q35/i440FX both ship
 * with one IOAPIC at the standard physical base (0xFEC00000) with 24 entries
 * (GSI 0..23, covering legacy ISA IRQs 0..15 plus PCI INTx).
 *
 * MVP scope (G.1.2):
 *   - Indirect register access (IOREGSEL + IOWIN)
 *   - Read IOAPIC ID / version (entry count)
 *   - Program a redirection entry (vector + destination CPU + masked bit)
 *   - Mask/unmask helper for an arbitrary GSI
 *
 * For Step G.1 we hardcode base 0xFEC00000 and assume 1 IOAPIC. MADT-based
 * discovery is Step G.2.
 */

#define OPENOS_X86_64_IOAPIC_DEFAULT_PHYS_BASE  0xFEC00000ull

/* MMIO offsets within the IOAPIC window. */
#define OPENOS_X86_64_IOAPIC_IOREGSEL           0x00u
#define OPENOS_X86_64_IOAPIC_IOWIN              0x10u

/* Indirect register indices. */
#define OPENOS_X86_64_IOAPIC_REG_ID             0x00u
#define OPENOS_X86_64_IOAPIC_REG_VERSION        0x01u
#define OPENOS_X86_64_IOAPIC_REG_REDTBL_BASE    0x10u  /* +2*GSI low, +2*GSI+1 high */

/* Redirection entry low (bits 0..31): vector + delivery + masked. */
#define OPENOS_X86_64_IOAPIC_REDIR_MASKED       (1u << 16)
/* Delivery mode: 000 = fixed. Dest mode: 0 = physical. Trigger/Polarity: 0 = edge / active-high. */

bool arch_x86_64_ioapic_init(void);
bool arch_x86_64_ioapic_is_ready(void);
uint64_t arch_x86_64_ioapic_mmio_base(void);

/* Return number of redirection entries (max GSI = entries - 1). */
uint8_t arch_x86_64_ioapic_entry_count(void);

uint8_t arch_x86_64_ioapic_id(void);
uint32_t arch_x86_64_ioapic_version_raw(void);

/* Program GSI (0..entry_count-1) to deliver to (vector) on (dest_lapic_id).
 * Edge-triggered, active-high, fixed delivery, physical dest mode.
 * Initial state: masked. Caller must unmask separately. */
void arch_x86_64_ioapic_set_redir(uint8_t gsi, uint8_t vector, uint8_t dest_lapic_id);

void arch_x86_64_ioapic_mask(uint8_t gsi);
void arch_x86_64_ioapic_unmask(uint8_t gsi);

/* Read raw 64-bit redirection entry (for selftest verification). */
uint64_t arch_x86_64_ioapic_read_redir(uint8_t gsi);

/* G.3b additions. */

/* Returns the GSI base of the first IOAPIC (0 on a standard PC). */
uint32_t arch_x86_64_ioapic_gsi_base(void);

/* ACPI-aware ISA IRQ routing.
 *
 * Resolves `isa_irq` (0..15) via the MADT IRQ-override table to a GSI plus
 * polarity/trigger flags, then programs the matching redirection entry on
 * this IOAPIC, leaving it UNMASKED for immediate delivery to `vector` on
 * `dest_lapic_id`.
 *
 * Returns the local pin index that was programmed, or 0xFF on error
 * (IOAPIC not ready / GSI out of this IOAPIC's range). */
uint8_t arch_x86_64_ioapic_route_isa_irq(uint8_t isa_irq,
                                         uint8_t vector,
                                         uint8_t dest_lapic_id);

#endif /* OPENOS_ARCH_X86_64_IOAPIC64_H */
