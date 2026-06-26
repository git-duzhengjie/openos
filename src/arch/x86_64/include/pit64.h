#ifndef OPENOS_ARCH_X86_64_PIT64_H
#define OPENOS_ARCH_X86_64_PIT64_H

#include <stdint.h>

/* Step F.2 — i8254 PIT channel-0 100 Hz tick.
 *
 * The tsc64 module already uses channel 2 (gated, polled) for one-shot
 * calibration. Channel 0 is now permanently programmed in rate-generator
 * mode (mode 2) at 100 Hz so it fires IRQ0 every 10 ms.
 *
 * In F.2 the IRQ0 ISR only increments g_ticks and EOIs the PIC — it does
 * NOT touch the scheduler. F.3 will hang sched_tick_from_irq() off the
 * tail of the same ISR. */

#define OPENOS_X86_64_PIT_HZ_DEFAULT 100u

void arch_x86_64_pit_init(uint32_t hz);

/* Returns the monotonic tick counter incremented by IRQ0. */
uint64_t arch_x86_64_pit_get_ticks(void);

/* Returns the configured frequency (Hz). */
uint32_t arch_x86_64_pit_get_hz(void);

/* Called from the IRQ0 assembly stub — public only so isr64.S can wire
 * the C entry point. Do not call from regular code. */
void arch_x86_64_pit_irq0_handler(void);

#endif /* OPENOS_ARCH_X86_64_PIT64_H */
