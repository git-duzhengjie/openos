#ifndef OPENOS_ARCH_X86_64_LAPIC64_H
#define OPENOS_ARCH_X86_64_LAPIC64_H

#include <stdint.h>
#include <stdbool.h>

/* Step G.1.1 — Local APIC (xAPIC) MMIO layer.
 *
 * Modern x86_64 CPUs deliver interrupts through the LAPIC (per-core) and the
 * IOAPIC (chipset-wide), replacing the legacy 8259A pair. We map the LAPIC
 * MMIO window at the standard physical base (0xFEE00000) — it is already
 * identity-mapped by the early UEFI loader (the same 1GiB-page identity map
 * that covers PIC/PIT IO space).
 *
 * MVP scope (G.1.1):
 *   - SVR (Spurious Vector Register) enable + spurious vector = 0xFF
 *   - EOI register write helper (replaces PIC 0x20 OCW2 on master)
 *   - Read/write helpers for arbitrary register offsets (used by IOAPIC
 *     redir table programming in G.1.2)
 *   - ID/Version readback for sanity / selftest
 *
 * Out of scope (deferred):
 *   - Timer / TPR / LVT thermal / LVT perfcnt
 *   - x2APIC (we explicitly stick to xAPIC MMIO; MSR-mode is a Step G.x item)
 *   - MADT auto-discovery (G.2 candidate; for now we hardcode 0xFEE00000)
 */

#define OPENOS_X86_64_LAPIC_DEFAULT_PHYS_BASE   0xFEE00000ull
#define OPENOS_X86_64_LAPIC_SPURIOUS_VECTOR     0xFFu

/* Selected register offsets (Intel SDM Vol.3A Table 10-1). */
#define OPENOS_X86_64_LAPIC_REG_ID              0x020u
#define OPENOS_X86_64_LAPIC_REG_VERSION         0x030u
#define OPENOS_X86_64_LAPIC_REG_TPR             0x080u
#define OPENOS_X86_64_LAPIC_REG_EOI             0x0B0u
#define OPENOS_X86_64_LAPIC_REG_SVR             0x0F0u
#define OPENOS_X86_64_LAPIC_REG_ICR_LOW         0x300u
#define OPENOS_X86_64_LAPIC_REG_ICR_HIGH        0x310u

/* SVR bits. */
#define OPENOS_X86_64_LAPIC_SVR_ENABLE          (1u << 8)

/* ICR_LOW fields (SDM Vol.3A Figure 10-12). */
#define OPENOS_X86_64_LAPIC_ICR_DELMOD_FIXED    (0u << 8)
#define OPENOS_X86_64_LAPIC_ICR_DELMOD_INIT     (5u << 8)
#define OPENOS_X86_64_LAPIC_ICR_DELMOD_STARTUP  (6u << 8)
#define OPENOS_X86_64_LAPIC_ICR_DESTMOD_PHYS    (0u << 11)
#define OPENOS_X86_64_LAPIC_ICR_DELIVERY_STATUS (1u << 12)
#define OPENOS_X86_64_LAPIC_ICR_LEVEL_ASSERT    (1u << 14)
#define OPENOS_X86_64_LAPIC_ICR_TRIGGER_LEVEL   (1u << 15)

/* Bring LAPIC online: program SVR enable + spurious vector, clear TPR.
 * Returns true on success; false if MSR_IA32_APIC_BASE indicates LAPIC is
 * globally disabled (extremely unusual on QEMU/UEFI, but worth checking
 * before we mask out the 8259A). */
bool arch_x86_64_lapic_init(void);

/* G.5-lapic: Per-AP LAPIC bring-up.
 *
 * The BSP has already mapped the LAPIC MMIO window (UEFI identity map covers
 * 0xFEE00000), so APs reuse the same g_lapic_mmio pointer. This routine runs
 * on each AP from ap_entry() and:
 *   - clears its own TPR (accept all priorities)
 *   - writes its own SVR with enable bit + spurious vector 0xFF
 *   - reads back its own LAPIC ID for sanity
 *
 * Returns true if SVR enable was accepted (version readback != 0).
 * Must be called *after* arch_x86_64_lapic_init() has finished on the BSP. */
bool arch_x86_64_lapic_init_ap(void);

/* Returns true once init has succeeded. */
bool arch_x86_64_lapic_is_ready(void);

/* MMIO base actually in use (post-init). 0 before init. */
uint64_t arch_x86_64_lapic_mmio_base(void);

/* Register accessors (4-byte aligned offset, e.g. OPENOS_X86_64_LAPIC_REG_*). */
uint32_t arch_x86_64_lapic_read(uint32_t reg_offset);
void arch_x86_64_lapic_write(uint32_t reg_offset, uint32_t value);

/* End-of-Interrupt — write 0 to LAPIC EOI register. Replaces the PIC OCW2
 * 0x20 EOI on master/slave. Must be called from every external IRQ handler
 * once LAPIC routing is active. */
void arch_x86_64_lapic_send_eoi(void);

/* Convenience: read LAPIC ID (bits 31:24 of REG_ID). */
uint8_t arch_x86_64_lapic_id(void);

/* Convenience: read LAPIC version register raw value. */
uint32_t arch_x86_64_lapic_version_raw(void);

/* G.4.3a — IPI primitives.
 *
 * arch_x86_64_lapic_icr_wait:
 *   Spin-poll ICR_LOW.Delivery_Status (bit 12) until it clears, indicating
 *   that the LAPIC has accepted the previously written ICR command. Bounded
 *   spin (~1M iterations) so a wedged LAPIC cannot livelock the BSP.
 *   Returns true if cleared, false on timeout.
 *
 * arch_x86_64_lapic_send_init:
 *   Send an INIT IPI to the physical-destination apic_id (8-bit). Edge
 *   triggered, level=assert, vector=0. Caller is responsible for the 10ms
 *   pause and the subsequent two SIPI broadcasts (G.4.3b).
 *   Returns true on successful delivery (ICR settled), false on timeout. */
bool arch_x86_64_lapic_icr_wait(void);
bool arch_x86_64_lapic_send_init(uint8_t apic_id);

/* G.4.3b-1 — send STARTUP (SIPI) IPI.
 *
 * SIPI is encoded as delivery_mode=110b, level=assert(1), trigger=edge(0),
 * destination_mode=physical(0). The vector field (bits 7:0) carries the
 * real-mode start address: CS:IP = (vector << 8):0000  i.e. physical
 * (vector << 12). vector must be in the low 1MB range so vector ∈ [0,0xFF],
 * and we conventionally use 0x08 (→ 0x8000).
 *
 * Returns true on successful delivery (ICR settled), false on timeout. */
bool arch_x86_64_lapic_send_startup(uint8_t apic_id, uint8_t vector);

#endif /* OPENOS_ARCH_X86_64_LAPIC64_H */
