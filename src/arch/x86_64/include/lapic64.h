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
#define OPENOS_X86_64_LAPIC_REG_LVT_TIMER       0x320u
#define OPENOS_X86_64_LAPIC_REG_LVT_LINT0       0x350u
#define OPENOS_X86_64_LAPIC_REG_LVT_LINT1       0x360u
#define OPENOS_X86_64_LAPIC_REG_LVT_ERROR       0x370u
#define OPENOS_X86_64_LAPIC_REG_TIMER_ICR       0x380u  /* initial count */
#define OPENOS_X86_64_LAPIC_REG_TIMER_CCR       0x390u  /* current count (RO) */
#define OPENOS_X86_64_LAPIC_REG_TIMER_DCR       0x3E0u  /* divide config */

/* G.6.5a — LAPIC timer fields (LVT_TIMER bits, Intel SDM Vol.3A 10.5.4).
 *   [16]    mask
 *   [17:18] timer mode (00=one-shot, 01=periodic, 10=TSC-deadline)
 * DCR divide values (only bits [0:1] and [3]):
 *   0b1011 = divide by 1   (we use divide by 16 = 0b1010)
 */
#define OPENOS_X86_64_LAPIC_LVT_TIMER_PERIODIC  (1u << 17)
#define OPENOS_X86_64_LAPIC_TIMER_DCR_DIV16     0x0Au

/* G.6.5a — AP LAPIC timer interrupt vector. Chosen above the PIC-mapped
 * range (0x20..0x2F) and below the spurious vector 0xFF. Each AP delivers
 * its own LAPIC-timer tick on this vector via LVT_TIMER.vector. The BSP
 * does NOT program its own LAPIC timer in G.6.5a (BSP still ticks off PIT). */
#define OPENOS_X86_64_LAPIC_TIMER_VECTOR        0x40u

/* LVT field encodings. Bit layout for LINT0/LINT1 (Intel SDM 10.5.1):
 *   [ 7: 0] vector       (ignored for NMI/SMI/INIT/ExtINT)
 *   [10: 8] delivery mode (000=Fixed, 010=SMI, 100=NMI, 101=INIT, 111=ExtINT)
 *   [11]    destination mode (ignored for these LVT regs)
 *   [12]    delivery status (RO)
 *   [13]    interrupt input pin polarity (0=active high, 1=active low)
 *   [14]    remote IRR (RO)
 *   [15]    trigger mode (0=edge, 1=level)
 *   [16]    mask (1=masked)
 */
#define OPENOS_X86_64_LAPIC_LVT_DM_FIXED        (0u << 8)
#define OPENOS_X86_64_LAPIC_LVT_DM_NMI          (4u << 8)
#define OPENOS_X86_64_LAPIC_LVT_DM_EXTINT       (7u << 8)
#define OPENOS_X86_64_LAPIC_LVT_POL_LOW         (1u << 13)
#define OPENOS_X86_64_LAPIC_LVT_TRIG_LEVEL      (1u << 15)
#define OPENOS_X86_64_LAPIC_LVT_MASKED          (1u << 16)

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

/* G.6.5a — Program this CPU's LAPIC timer in periodic mode.
 *
 * Called from the AP path (after lapic_init_ap) to start a per-CPU heartbeat
 * that fires `vector` at a rate of roughly (LAPIC-bus-Hz / divider) /
 * initial_count Hz. On QEMU the LAPIC bus runs at ~1 GHz so with divider=16
 * and initial_count=1_000_000 we get ~62 Hz per AP (≈ 16 ms / tick).
 *
 * The BSP does NOT call this in G.6.5a — it continues to receive scheduler
 * ticks via the PIT IRQ0 path, leaving G.6.5a's blast radius purely AP-side.
 *
 * Returns true on success. */
bool arch_x86_64_lapic_timer_init_periodic(uint8_t vector,
                                            uint32_t initial_count,
                                            uint32_t divider_dcr);

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

/* G.3b-final — Program LVT LINT0/LINT1 to route NMI/ExtINT properly.
 *
 * Walks the ACPI MADT type-4 (Local APIC NMI) entries for the *current*
 * processor (read its LAPIC ID via REG_ID) and programs the matching
 * LINTn register with delivery_mode=NMI(100b), level/edge & active hi/lo
 * derived from MPS flags. The other LINT pin (typically LINT0) is set
 * to ExtINT(111b) only on the BSP — on APs both LINT0/LINT1 default to
 * masked unless an explicit MADT entry says otherwise.
 *
 * Returns true if at least one LINT pin was successfully programmed
 * with NMI delivery, false otherwise (still safe — masked LVT entries
 * are written either way). Callable from both BSP and AP paths.
 */
bool arch_x86_64_lapic_setup_nmi_lvt(bool is_bsp);

/* Diagnostic helpers used by apic-selftest: raw read of LVT LINT0/LINT1. */
uint32_t arch_x86_64_lapic_read_lvt_lint0(void);
uint32_t arch_x86_64_lapic_read_lvt_lint1(void);

/* G.6.6a — Send a fixed-delivery IPI to the physical-destination apic_id.
 *
 * Encoding: delivery_mode=000b (fixed), destination_mode=physical(0),
 * level=assert(1), trigger=edge(0), shorthand=00b. Vector is the IDT
 * vector that will fire on the target CPU. Used for the reschedule IPI
 * at vector 0x41 (and any future fixed-vector cross-CPU notifications).
 *
 * Caller must ensure vector is registered in the IDT on the target CPU
 * BEFORE the IPI is sent (otherwise the target gets #GP on iretq path).
 *
 * Returns true on successful delivery (ICR settled), false on timeout. */
bool arch_x86_64_lapic_send_fixed_ipi(uint8_t apic_id, uint8_t vector);

/* G.6.6a — Reschedule-IPI ISR (vector 0x41).
 *
 * Bumps this CPU's percpu_t.resched_ipi_count and EOIs. The actual
 * reschedule decision is deferred to the next timer tick: ISR-time
 * preemption of kernel-mode contexts requires a full preempt framework
 * we don't have yet. This handler's only job is to provide the
 * cross-CPU "poke" signal and prove that BSP->AP fixed IPI delivery
 * works end-to-end. */
void arch_x86_64_lapic_resched_irq_handler(void);

/* G.7g-1 — LAPIC timer bus-frequency calibration.
 *
 * Use the TSC (already PIT-calibrated by arch_x86_64_tsc_init) as a stable
 * reference clock to measure how many LAPIC-timer ticks elapse during a
 * known 50 ms window. The timer is programmed in one-shot mode with
 * divide-by-16 and the maximum initial-count (0xFFFFFFFF), then both TSC
 * and TIMER_CCR are sampled at window start and end; (CCR_start - CCR_end)
 * gives the LAPIC bus ticks consumed in the elapsed TSC-delta wall time.
 *
 * Strictly read-only w.r.t. interrupts — the timer is masked the entire
 * time, so no IRQs fire from the calibration window. Idempotent: only the
 * first successful call updates state.
 *
 * Returns true on success, false if TSC is uncalibrated or the LAPIC
 * isn't ready. Must run on the BSP before APs start their timers; APs
 * read g_lapic_bus_ticks_per_ms via arch_x86_64_lapic_timer_ticks_per_ms().
 */
bool arch_x86_64_lapic_timer_calibrate(void);

/* LAPIC bus ticks per millisecond (after divide-by-16). 0 before
 * calibration succeeds. */
uint32_t arch_x86_64_lapic_timer_ticks_per_ms(void);

#endif /* OPENOS_ARCH_X86_64_LAPIC64_H */
