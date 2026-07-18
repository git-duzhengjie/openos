#include "../include/pit64.h"
#include "../include/pic64.h"
#include "../include/lapic64.h"
#include "../include/sched64.h"
#include "../include/percpu64.h"

#include <stdint.h>

/* M10.8: Forward-declare NC tick sink. Weak so kernels built without the
 * notification center simply skip the call. `nc_tick(now_ms)` is the only
 * NC surface used from IRQ0 context (pure counter/state, no allocs). */
__attribute__((weak)) void nc_tick(uint32_t now_ms);

/* Step F.2 — PIT channel 0 rate generator.
 *
 * PIT_INPUT_HZ is fixed by hardware at 1193182 Hz. To produce a target
 * frequency of `hz` we load reload = round(PIT_INPUT_HZ / hz).
 * Caller guarantees hz in [19, 1193182]; we clamp anyway.
 *
 * Command byte 0x36 = channel 0, lobyte+hibyte, mode 2 (rate generator),
 * binary. Mode 2 keeps OUT high and drops it for one input cycle each time
 * the counter wraps — what we want for periodic IRQ0.
 *
 * Note: the tsc64 calibrator uses channel 2 with mode 0 and an explicit
 * gate; the two channels are independent so there is no interaction. */

#define PIT_INPUT_HZ 1193182u

#define PIT_PORT_CHANNEL0 0x40u
#define PIT_PORT_COMMAND  0x43u

static volatile uint64_t g_pit_ticks = 0;
static uint32_t g_pit_hz = 0;

static inline void pit_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

void arch_x86_64_pit_init(uint32_t hz) {
    if (hz < 19u) {
        hz = 19u; /* below this the reload would overflow 16 bits */
    }
    if (hz > PIT_INPUT_HZ) {
        hz = PIT_INPUT_HZ;
    }
    uint32_t reload = (PIT_INPUT_HZ + hz / 2u) / hz;
    if (reload == 0u) {
        reload = 1u;
    }
    if (reload > 0xFFFFu) {
        reload = 0xFFFFu;
    }

    /* Command: channel 0, access lobyte+hibyte, mode 2, binary. */
    pit_outb(PIT_PORT_COMMAND, 0x34u);
    pit_outb(PIT_PORT_CHANNEL0, (uint8_t)(reload & 0xFFu));
    pit_outb(PIT_PORT_CHANNEL0, (uint8_t)((reload >> 8) & 0xFFu));

    g_pit_hz = hz;
    g_pit_ticks = 0;
}

uint64_t arch_x86_64_pit_get_ticks(void) {
    return g_pit_ticks;
}

uint32_t arch_x86_64_pit_get_hz(void) {
    return g_pit_hz;
}

void arch_x86_64_pit_irq0_handler(uint64_t iret_cs) {
    /* IRQ0 hot path. Order matters:
     *   1. bump the visible tick counter (selftests poll this)
     *   2. EOI the PIC so a re-entrant IRQ0 can fire after we sti below
     *      (the scheduler yield will resume some other thread with
     *       IF=1, and we MUST have already EOI'd by then or the line
     *       stays masked at the controller)
     *   3. invoke the preemptive scheduler hook. If it switches away,
     *      we return *much later* once this thread is rescheduled —
     *      that is fine: IRQ0 stub will then pop caller-saved regs and
     *      iretq back to the user/kernel context that was running when
     *      THIS thread was originally preempted.
     *
     * Pre-F.3 the gate is a no-op until a kthread is spawned, so
     * existing irq-selftest behavior (delta ∈ [18,22] over 200ms) is
     * preserved bit-for-bit. */
    g_pit_ticks++;

    /* M10.8: drive the notification-center fade-out clock. Cheap: an
     * empty NC table costs 8 loop iters + no branches; a fading notif
     * is O(N_active). This must run before EOI so if we somehow crash
     * inside nc_tick the IRQ line is still masked (fail-safe). */
    if (nc_tick) {
        uint32_t hz = g_pit_hz ? g_pit_hz : 100u;
        /* Overflow-safe: (ticks * 1000) may wrap after ~46 days at 1kHz;
         * NC only cares about deltas so a wraparound is harmless. */
        uint32_t now_ms = (uint32_t)((g_pit_ticks * 1000ull) / hz);
        nc_tick(now_ms);
    }

    /* gamma.5-P1: same histogram as the LAPIC-timer handler (see
     * lapic64.c). On the BSP the PIT is the only preempt source, so
     * without this hook tick_hits_user on cpu 0 would stay at 0 even
     * if IRQ0 fired every 10ms in ring3. iret_cs's low 2 bits are RPL. */
    {
        arch_x86_64_percpu_t *pc = arch_x86_64_this_cpu_ptr();
        if (pc != 0) {
            if ((iret_cs & 0x3u) == 0x3u) {
                pc->tick_hits_user++;
            } else {
                pc->tick_hits_kernel++;
            }
        }
    }
    /* Route EOI to whichever controller is currently driving us. Once
     * Step G.1 wires the IOAPIC, IRQ0 is delivered via the LAPIC and
     * the PIC EOI is a stale write that would actually un-mask master
     * IRQs we did not authorize. Pick the right path at runtime. */
    if (arch_x86_64_lapic_is_ready()) {
        arch_x86_64_lapic_send_eoi();
    } else {
        arch_x86_64_pic_send_eoi(0x20u);
    }
    (void)arch_x86_64_sched_on_tick();

    /* G.6.7c: tail dispatch. sched_on_tick no longer yields inline; if
     * quantum expired it set need_resched and returned. We honour the
     * latch here, AFTER EOI, so the IRQ is fully acknowledged before
     * we may context-switch. Indistinguishable in shape from the
     * LAPIC-timer IRQ handler on AP and the resched-IPI handler. */
    (void)arch_x86_64_sched_check_and_dispatch();
}
