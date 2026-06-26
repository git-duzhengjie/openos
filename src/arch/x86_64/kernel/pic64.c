#include "../include/pic64.h"

#include <stdint.h>

/* Step F.2 — 8259A master/slave remap.
 *
 * Init sequence (ICW1..ICW4) per Intel/IBM datasheet:
 *   ICW1: 0x11 (init + ICW4 expected, edge-triggered, cascade)
 *   ICW2: vector base (master=0x20, slave=0x28)
 *   ICW3: master tells slave is on IRQ2 (0x04), slave tells its cascade id (2)
 *   ICW4: 0x01 (8086/88 mode, normal EOI, non-buffered)
 * After init we mask everything (0xFF/0xFF). The kernel will explicitly
 * unmask each line as it brings up an ISR (IRQ0 PIT first, in pit64.c).
 *
 * io_wait() bursts a write to port 0x80 to give the legacy PIC enough
 * settling time on real hardware — QEMU does not need it but it costs
 * essentially nothing and matches every textbook driver. */

static inline void pic_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t pic_inb(uint16_t port) {
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void io_wait(void) {
    __asm__ __volatile__("outb %%al, $0x80" : : "a"((uint8_t)0));
}

void arch_x86_64_pic_init(void) {
    /* Save current masks — strictly speaking unused since we force 0xFF,
     * but it keeps the canonical 4-step shape and aids future debugging. */
    uint8_t mask_master = pic_inb(OPENOS_X86_64_PIC_MASTER_DATA);
    uint8_t mask_slave = pic_inb(OPENOS_X86_64_PIC_SLAVE_DATA);
    (void)mask_master;
    (void)mask_slave;

    /* ICW1 — start init, cascade, edge, expect ICW4. */
    pic_outb(OPENOS_X86_64_PIC_MASTER_COMMAND, 0x11u); io_wait();
    pic_outb(OPENOS_X86_64_PIC_SLAVE_COMMAND, 0x11u);  io_wait();

    /* ICW2 — vector base. */
    pic_outb(OPENOS_X86_64_PIC_MASTER_DATA, OPENOS_X86_64_PIC_MASTER_VECTOR_BASE); io_wait();
    pic_outb(OPENOS_X86_64_PIC_SLAVE_DATA,  OPENOS_X86_64_PIC_SLAVE_VECTOR_BASE);  io_wait();

    /* ICW3 — cascade wiring. */
    pic_outb(OPENOS_X86_64_PIC_MASTER_DATA, 0x04u); io_wait(); /* slave is on IRQ2 */
    pic_outb(OPENOS_X86_64_PIC_SLAVE_DATA,  0x02u); io_wait(); /* slave cascade id */

    /* ICW4 — 8086/88 mode, normal EOI. */
    pic_outb(OPENOS_X86_64_PIC_MASTER_DATA, 0x01u); io_wait();
    pic_outb(OPENOS_X86_64_PIC_SLAVE_DATA,  0x01u); io_wait();

    /* Mask everything by default. */
    pic_outb(OPENOS_X86_64_PIC_MASTER_DATA, 0xFFu);
    pic_outb(OPENOS_X86_64_PIC_SLAVE_DATA,  0xFFu);
}

void arch_x86_64_pic_mask(uint8_t irq) {
    if (irq >= 16u) {
        return;
    }
    uint16_t port;
    uint8_t bit;
    if (irq < 8u) {
        port = OPENOS_X86_64_PIC_MASTER_DATA;
        bit = (uint8_t)(1u << irq);
    } else {
        port = OPENOS_X86_64_PIC_SLAVE_DATA;
        bit = (uint8_t)(1u << (irq - 8u));
    }
    uint8_t cur = pic_inb(port);
    pic_outb(port, (uint8_t)(cur | bit));
}

void arch_x86_64_pic_unmask(uint8_t irq) {
    if (irq >= 16u) {
        return;
    }
    uint16_t port;
    uint8_t bit;
    if (irq < 8u) {
        port = OPENOS_X86_64_PIC_MASTER_DATA;
        bit = (uint8_t)(1u << irq);
    } else {
        port = OPENOS_X86_64_PIC_SLAVE_DATA;
        bit = (uint8_t)(1u << (irq - 8u));
    }
    uint8_t cur = pic_inb(port);
    pic_outb(port, (uint8_t)(cur & ~bit));
}

void arch_x86_64_pic_send_eoi(uint8_t cpu_vector) {
    /* For vectors 0x28..0x2F we must EOI the slave first, then the master
     * (master still sees IRQ2 asserted as a cascade). For 0x20..0x27 we
     * only EOI the master. Anything outside the PIC range is a programming
     * bug — silently ignore so a stray call from a spurious-IRQ path does
     * not wedge the machine. */
    if (cpu_vector >= OPENOS_X86_64_PIC_SLAVE_VECTOR_BASE &&
        cpu_vector < (OPENOS_X86_64_PIC_SLAVE_VECTOR_BASE + 8u)) {
        pic_outb(OPENOS_X86_64_PIC_SLAVE_COMMAND, OPENOS_X86_64_PIC_EOI);
        pic_outb(OPENOS_X86_64_PIC_MASTER_COMMAND, OPENOS_X86_64_PIC_EOI);
    } else if (cpu_vector >= OPENOS_X86_64_PIC_MASTER_VECTOR_BASE &&
               cpu_vector < (OPENOS_X86_64_PIC_MASTER_VECTOR_BASE + 8u)) {
        pic_outb(OPENOS_X86_64_PIC_MASTER_COMMAND, OPENOS_X86_64_PIC_EOI);
    }
}

uint16_t arch_x86_64_pic_get_mask(void) {
    uint8_t lo = pic_inb(OPENOS_X86_64_PIC_MASTER_DATA);
    uint8_t hi = pic_inb(OPENOS_X86_64_PIC_SLAVE_DATA);
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

void arch_x86_64_pic_disable(void) {
    /* Mask all 16 lines. PIC remap stays in place — if a spurious IRQ
     * still slips through, it will be vectored to 0x20..0x2F and our IDT
     * has those stubs wired (they will EOI and return). */
    pic_outb(OPENOS_X86_64_PIC_MASTER_DATA, 0xFFu);
    pic_outb(OPENOS_X86_64_PIC_SLAVE_DATA,  0xFFu);
}
