#include "../include/lapic64.h"
#include "../include/acpi64.h"
#include "../include/percpu64.h"
#include "../include/sched64.h"   /* G.6.7a: check_and_dispatch tail hook */

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

/* G.6.6a — Send a fixed-delivery IPI to one CPU.
 *
 * Same ICR sequencing pattern as INIT/STARTUP (HIGH first sets physical
 * destination apic_id, LOW write triggers delivery). Encoding:
 *   delivery_mode = 000b (fixed)
 *   destination_mode = 0 (physical)
 *   level = 1 (assert)
 *   trigger = 0 (edge)        — implicit for fixed delivery
 *   shorthand = 00b           — implicit
 *   vector = caller-supplied IDT vector (0x10–0xFE per SDM)
 *
 * The target CPU must already have this vector registered in its IDT.
 * Returns true if both pre- and post- ICR busy-waits drained cleanly.
 */
bool arch_x86_64_lapic_send_fixed_ipi(uint8_t apic_id, uint8_t vector) {
    if (!g_lapic_ready) return false;

    if (!arch_x86_64_lapic_icr_wait()) return false;

    uint32_t high = ((uint32_t)apic_id) << 24;
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_ICR_HIGH, high);

    uint32_t low =
        OPENOS_X86_64_LAPIC_ICR_DELMOD_FIXED |
        OPENOS_X86_64_LAPIC_ICR_DESTMOD_PHYS |
        OPENOS_X86_64_LAPIC_ICR_LEVEL_ASSERT |
        (uint32_t)vector;
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_ICR_LOW, low);

    return arch_x86_64_lapic_icr_wait();
}

/* G.6.6a — Reschedule-IPI ISR (vector 0x41).
 *
 * G.6.7a evolution: this handler now does *three* things in strict
 * order:
 *   (1) bump resched_ipi_count   -- proves the IPI was delivered
 *   (2) latch need_resched=1     -- the cross-CPU "please reschedule"
 *                                   signal that the tail dispatcher
 *                                   will consume
 *   (3) write EOI                -- LAPIC end-of-interrupt
 *   (4) call check_and_dispatch  -- read-and-clear the latch and, if
 *                                   set, sched_yield right here. This
 *                                   is what makes the IPI an *immediate*
 *                                   reschedule rather than a hint that
 *                                   has to wait for the next timer tick.
 *
 * Ordering rationale:
 *  - EOI is LAPIC-per-CPU state, not per-thread, so it survives any
 *    context switch happening inside check_and_dispatch. We could in
 *    principle EOI *after* the dispatch (since EOI just unmasks the
 *    next LAPIC interrupt and is safe either way), but EOI-last-before-
 *    dispatch keeps the LAPIC ready for the *next* interrupt while we
 *    are still on the parked stack of the old thread -- one less window
 *    where a queued IPI could be held up.
 *  - check_and_dispatch is the *only* place inside an ISR where we may
 *    sched_yield. It must therefore be called after EOI, with IF still
 *    cleared by the ISR stub. The yielded-to thread's restored rflags
 *    will set IF=1, mirroring the timer-tick path (see F.3 proof).
 *  - The ISR stub (isr64.S) saved all caller-saved registers before
 *    calling this C handler. context_switch only saves callee-saved
 *    regs, but since iretq from the next thread eventually unwinds
 *    through *its* ISR stub (or its initial entry frame), the symmetry
 *    is preserved. Same discipline as the timer tick yield path.
 */
void arch_x86_64_lapic_resched_irq_handler(void) {
    arch_x86_64_percpu_t *pc = arch_x86_64_this_cpu_ptr();
    if (pc) {
        pc->resched_ipi_count++;
        /* Latch the local need_resched flag. Even if some future caller
         * also pre-latched it via sched_set_need_resched_remote() before
         * sending us the IPI, setting it again is idempotent. */
        pc->need_resched = 1u;
    }
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_EOI, 0);

    /* Tail dispatch: this may sched_yield and not return on this stack.
     * If it returns (need_resched was 0 by the time we read it, e.g.
     * spurious / already-handled), we just iretq back through the stub. */
    (void)arch_x86_64_sched_check_and_dispatch();
}

/* G.3b-final — program LVT LINT0 / LINT1 for NMI/ExtINT routing.
 *
 * Per Intel SDM 10.5.1 the LVT LINTn registers control how external INTR
 * pins are delivered to the local CPU. On a typical PC firmware:
 *   - BSP   : LINT0 = ExtINT (8259 cascade), LINT1 = NMI
 *   - APs   : both LINTs masked unless MADT type 4 says otherwise
 *
 * Some virtualised platforms (notably TianoCore OVMF) emit a MADT type-4
 * entry covering acpi_processor_id == 0xFF ("all CPUs") for LINT1=NMI.
 * We honour whatever the firmware tells us; if nothing is specified for
 * the BSP we still program a sane ExtINT/NMI pair, because masking LINT0
 * on the BSP breaks legacy timer interrupts during APIC bring-up before
 * we switch to IOAPIC routing.
 */
bool arch_x86_64_lapic_setup_nmi_lvt(bool is_bsp) {
    if (!g_lapic_ready) return false;

    /* Start with both LINTs masked-out. */
    uint32_t lint0 = OPENOS_X86_64_LAPIC_LVT_MASKED;
    uint32_t lint1 = OPENOS_X86_64_LAPIC_LVT_MASKED;

    bool programmed_nmi = false;

    /* Try to honour ACPI-supplied routing for this CPU. */
    uint8_t  apic_id = arch_x86_64_lapic_id();
    uint8_t  lint    = 0xFF;
    uint16_t flags   = 0;
    int rc = arch_x86_64_acpi_resolve_lapic_nmi(apic_id, &lint, &flags);
    if (rc == 1 && lint <= 1u) {
        /* MPS/ACPI flags: bits[1:0] polarity (00=conform,01=hi,11=lo)
         *                 bits[3:2] trigger  (00=conform,01=edge,11=level)
         */
        uint32_t lvt = OPENOS_X86_64_LAPIC_LVT_DM_NMI;
        if (((flags >> 0) & 0x3u) == 0x3u) lvt |= OPENOS_X86_64_LAPIC_LVT_POL_LOW;
        if (((flags >> 2) & 0x3u) == 0x3u) lvt |= OPENOS_X86_64_LAPIC_LVT_TRIG_LEVEL;

        if (lint == 0u) lint0 = lvt;
        else             lint1 = lvt;
        programmed_nmi = true;
    } else if (rc == 0 && is_bsp) {
        /* No override for the BSP — install firmware-conventional defaults:
         * LINT0=ExtINT (legacy 8259 cascade), LINT1=NMI edge/active-hi.
         */
        lint0 = OPENOS_X86_64_LAPIC_LVT_DM_EXTINT;
        lint1 = OPENOS_X86_64_LAPIC_LVT_DM_NMI;
        programmed_nmi = true;
    }

    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_LVT_LINT0, lint0);
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_LVT_LINT1, lint1);

    return programmed_nmi;
}

uint32_t arch_x86_64_lapic_read_lvt_lint0(void) {
    if (!g_lapic_ready) return 0;
    return mmio_read32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_LVT_LINT0);
}

uint32_t arch_x86_64_lapic_read_lvt_lint1(void) {
    if (!g_lapic_ready) return 0;
    return mmio_read32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_LVT_LINT1);
}

/* G.6.5a — program this CPU's LAPIC timer in periodic mode.
 *
 * Critical ordering (Intel SDM Vol.3A 10.5.4):
 *   1. write DCR (divider) FIRST.
 *   2. write LVT_TIMER (vector + periodic mode bit, unmasked).
 *   3. write ICR (initial count) LAST — writing ICR is what arms /
 *      restarts the timer. Writing ICR=0 stops it.
 *
 * Each CPU's accesses to the (architecturally global) LAPIC MMIO page are
 * routed by hardware to its own LAPIC, so this is safe to call from APs
 * without locking. */
bool arch_x86_64_lapic_timer_init_periodic(uint8_t vector,
                                            uint32_t initial_count,
                                            uint32_t divider_dcr) {
    if (!g_lapic_ready) return false;
    if (initial_count == 0u) return false;

    /* 1. divider */
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_TIMER_DCR,
                 divider_dcr & 0xFu);

    /* 2. LVT_TIMER = periodic | vector. Mask bit (16) cleared = unmasked. */
    uint32_t lvt = ((uint32_t)vector) | OPENOS_X86_64_LAPIC_LVT_TIMER_PERIODIC;
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_LVT_TIMER, lvt);

    /* 3. initial count — arms the timer */
    mmio_write32(g_lapic_mmio, OPENOS_X86_64_LAPIC_REG_TIMER_ICR, initial_count);

    return true;
}

/* G.6.5a/b — LAPIC timer IRQ handler (vector 0x40).
 *
 * Called from x86_64_irq_lapic_timer in isr64.S. Runs on whichever CPU
 * received the interrupt. Per-CPU state is accessed via this_cpu().
 *
 * G.6.5a: observation-only — bump per-CPU heartbeat counter, then EOI.
 *
 * G.6.5b: also drive the scheduler quantum on this CPU by calling
 *         arch_x86_64_sched_on_tick(). Ordering:
 *           1) bump lapic_timer_count          (raw ISR-entry observer)
 *           2) sched_on_tick()                  (may switch context!)
 *           3) lapic_send_eoi()                 (always after sched step)
 *
 *         Rationale for EOI-last:
 *           - If sched_on_tick switches contexts, we return into a
 *             different thread's stack. The EOI is per-LAPIC (not per
 *             thread), so emitting it here keeps the local APIC happy
 *             regardless of which thread we resume into.
 *           - With EOI before yield, a same-vector re-entry could fire
 *             during the switch window. Keeping EOI last delays the
 *             next timer interrupt until we have fully returned.
 *
 *         G.6.7c: sched_on_tick no longer yields inline. It now sets
 *         need_resched when quantum expires and returns. The actual
 *         dispatch happens via check_and_dispatch() called AFTER EOI,
 *         which makes the timer path indistinguishable from the
 *         resched-IPI path (both end with EOI -> check_and_dispatch).
 *         The historical "EOI last" ordering inside sched_on_tick is
 *         therefore preserved at the handler level: we still send EOI
 *         BEFORE the path that may context-switch.
 *
 *         On the BSP this handler should never run (BSP does not arm
 *         its LAPIC timer in G.6.5), but if it ever does we still
 *         do the right thing: BSP's slot 0 is the bootstrap thread,
 *         sched_on_tick will degrade gracefully if nothing else is
 *         READY for cpu_idx==0.
 */
extern uint32_t arch_x86_64_sched_on_tick(void);

void arch_x86_64_lapic_timer_irq_handler(void) {
    arch_x86_64_percpu_t *p = arch_x86_64_this_cpu_ptr();
    if (p != 0) {
        p->lapic_timer_count++;
    }

    /* G.6.5b: drive per-CPU quantum / preemption. Safe to call here
     * because:
     *   - sched_on_tick reads/writes only this CPU's percpu cursors
     *     (via %gs) plus sched_slots[] filtered by owner_cpu == us.
     *   - sched_slots[] writes during steady-state are confined to
     *     state transitions of slots already owned by this CPU; APs
     *     never spawn new kthreads in G.6.5b, so no allocator races.
     *   - The ISR entered with IF=0; we do NOT re-enable interrupts
     *     before the call. */
    (void)arch_x86_64_sched_on_tick();

    arch_x86_64_lapic_send_eoi();

    /* G.6.7c: tail dispatch -- this is where the actual context switch
     * happens if quantum expired (sched_on_tick set need_resched) AND
     * preempt_disable_depth==0 on this CPU. May not return on this
     * stack; if it does, the stub iretqs back to the interrupted
     * context. Same contract as the resched-IPI handler. */
    (void)arch_x86_64_sched_check_and_dispatch();
}
