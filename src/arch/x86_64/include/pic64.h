#ifndef OPENOS_ARCH_X86_64_PIC64_H
#define OPENOS_ARCH_X86_64_PIC64_H

#include <stdint.h>

/* Step F.2 — 8259A PIC remap layer.
 *
 * Legacy BIOS leaves the master PIC raising IRQs on CPU vectors 0x08..0x0F,
 * which collide with x86 reserved exception vectors (#DF=8, #TS=10, ...).
 * Long-mode kernels must remap before enabling interrupts. We follow the
 * canonical sequence:
 *   - master PIC base => 0x20 (IRQ0..IRQ7  -> vec 0x20..0x27)
 *   - slave  PIC base => 0x28 (IRQ8..IRQ15 -> vec 0x28..0x2F)
 *
 * After remap all lines are masked; callers explicitly unmask with
 * arch_x86_64_pic_unmask(irq) when their ISR is wired up.
 */

#define OPENOS_X86_64_PIC_MASTER_VECTOR_BASE 0x20u
#define OPENOS_X86_64_PIC_SLAVE_VECTOR_BASE  0x28u

#define OPENOS_X86_64_PIC_MASTER_COMMAND     0x20u
#define OPENOS_X86_64_PIC_MASTER_DATA        0x21u
#define OPENOS_X86_64_PIC_SLAVE_COMMAND      0xA0u
#define OPENOS_X86_64_PIC_SLAVE_DATA         0xA1u

#define OPENOS_X86_64_PIC_EOI                0x20u

void arch_x86_64_pic_init(void);

/* irq: 0..15 (logical IRQ line, not CPU vector). */
void arch_x86_64_pic_mask(uint8_t irq);
void arch_x86_64_pic_unmask(uint8_t irq);

/* Signal end-of-interrupt for the given CPU vector (must be in 0x20..0x2F). */
void arch_x86_64_pic_send_eoi(uint8_t cpu_vector);

/* Read the current 16-bit IRQ mask (master in low byte, slave in high). */
uint16_t arch_x86_64_pic_get_mask(void);

#endif /* OPENOS_ARCH_X86_64_PIC64_H */
