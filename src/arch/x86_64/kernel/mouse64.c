/* ============================================================
 * openos/src/arch/x86_64/kernel/mouse64.c
 *
 * PS/2 mouse driver for the x86_64 UEFI kernel.
 * Ported from the i386 src/kernel/drivers/mouse.c. Logic is
 * platform-neutral (port IO only); only the IRQ plumbing and the
 * 32-bit inline asm (pushfl/popfl) were adapted to 64-bit.
 *
 * The GUI subsystem (gui.c) consumes the mouse purely through
 * mouse_snapshot_and_clear_delta(). This file provides the real
 * driver backing that API, replacing the gui64_stubs.c stub.
 * ============================================================ */

#include "mouse.h"

/* ---- serial log helper (provided by early_console64) ---- */
extern void early_console64_write(const char *s);
extern void early_console64_write_hex64(uint64_t v);

/* ---- port IO primitives (self-contained) ---- */
static inline unsigned char mouse_inb(unsigned short port) {
    unsigned char v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void mouse_outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

/* PS/2 controller ports */
#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

/* Status register bits */
#define PS2_STAT_OUTPUT_FULL 0x01  /* data available to read from 0x60 */
#define PS2_STAT_INPUT_FULL  0x02  /* controller input buffer busy */

/* global mouse state (shared with GUI via snapshot API) */
static volatile mouse_state_t g_mouse;

/* IRQ12 packet assembly state */
static unsigned char g_packet[4];
static int g_packet_idx = 0;
static int g_has_wheel = 0;   /* IntelliMouse (4-byte packet) detected */

/* ---- PS/2 controller handshake helpers ---- */

/* wait until the controller is ready to accept a byte we write */
static void ps2_wait_input(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((mouse_inb(PS2_STATUS) & PS2_STAT_INPUT_FULL) == 0)
            return;
    }
}

/* wait until the controller has a byte for us to read */
static void ps2_wait_output(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (mouse_inb(PS2_STATUS) & PS2_STAT_OUTPUT_FULL)
            return;
    }
}

/* send a command byte to the mouse (prefixed with 0xD4) */
static void mouse_write(unsigned char val) {
    ps2_wait_input();
    mouse_outb(PS2_CMD, 0xD4);   /* tell controller: next byte is for the mouse */
    ps2_wait_input();
    mouse_outb(PS2_DATA, val);
}

/* read one byte of mouse ACK/response */
static unsigned char mouse_read(void) {
    ps2_wait_output();
    return mouse_inb(PS2_DATA);
}

/* ---- IRQ12 handler: called from the assembly stub ---- */
/* Assembles 3-byte (or 4-byte IntelliMouse) packets and updates state. */
void arch_x86_64_mouse_irq12_handler(void) {
    unsigned char status = mouse_inb(PS2_STATUS);
    /* only consume if the byte actually came from the mouse (bit5 set) */
    if (!(status & PS2_STAT_OUTPUT_FULL))
        return;

    unsigned char data = mouse_inb(PS2_DATA);

    int packet_len = g_has_wheel ? 4 : 3;

    /* byte 0 must have the 'always 1' bit (0x08) set; else resync */
    if (g_packet_idx == 0 && !(data & 0x08)) {
        return; /* discard out-of-sync byte */
    }

    g_packet[g_packet_idx++] = data;
    if (g_packet_idx < packet_len)
        return;
    g_packet_idx = 0;

    unsigned char flags = g_packet[0];

    /* discard packets flagged with X/Y overflow */
    if (flags & 0xC0)
        return;

    /* sign-extend the 9-bit relative deltas */
    int dx = (int)g_packet[1];
    int dy = (int)g_packet[2];
    if (flags & 0x10) dx |= 0xFFFFFF00;  /* X sign bit */
    if (flags & 0x20) dy |= 0xFFFFFF00;  /* Y sign bit */

    /* PS/2 Y grows upward; screen Y grows downward -> invert */
    int ddx = dx;
    int ddy = -dy;
    g_mouse.dx += ddx;
    g_mouse.dy += ddy;

    /* wheel (IntelliMouse): byte 3 low nibble is a signed 4-bit value */
    if (g_has_wheel) {
        int wheel = (int)(g_packet[3] & 0x0F);
        if (wheel & 0x08) wheel |= 0xFFFFFFF0; /* sign-extend 4-bit */
        g_mouse.wheel += wheel;
    }

    /* button state (bit0=left, bit1=right, bit2=middle) */
    g_mouse.buttons = flags & 0x07;

    /* integrate absolute position within bounds using THIS packet's delta.
     * (g_mouse.dx/dy are cumulative and only cleared at snapshot time, so
     *  using them here would over-integrate and slam the cursor to a edge.) */
    g_mouse.x += ddx;
    g_mouse.y += ddy;
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.x > g_mouse.max_x) g_mouse.x = g_mouse.max_x;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.y > g_mouse.max_y) g_mouse.y = g_mouse.max_y;

    g_mouse.absolute_mode = 0;
    g_mouse.packet_count++;
}

/* ---- mouse_init: PS/2 controller + mouse enable sequence ---- */
void mouse_init(void) {
    /* zero the state */
    g_mouse.x = 0;
    g_mouse.y = 0;
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;
    g_mouse.z = 0;
    g_mouse.buttons = 0;
    g_mouse.max_x = 1279;   /* sensible default; refined by mouse_set_bounds */
    g_mouse.max_y = 799;
    g_mouse.present = 0;
    g_mouse.packet_count = 0;
    g_mouse.desync_count = 0;
    g_packet_idx = 0;
    g_has_wheel = 0;

    /* 1. enable the auxiliary (mouse) PS/2 port */
    ps2_wait_input();
    mouse_outb(PS2_CMD, 0xA8);

    /* 2. enable IRQ12 in the controller config byte */
    ps2_wait_input();
    mouse_outb(PS2_CMD, 0x20);          /* read config byte */
    unsigned char status = mouse_read();
    status |= 0x02;                      /* bit1: enable mouse IRQ */
    status &= ~0x20;                     /* bit5: clear mouse clock disable */
    ps2_wait_input();
    mouse_outb(PS2_CMD, 0x60);          /* write config byte */
    ps2_wait_input();
    mouse_outb(PS2_DATA, status);

    /* 3. reset the mouse and use default settings */
    mouse_write(0xFF);                   /* reset */
    (void)mouse_read();                  /* ACK 0xFA */
    (void)mouse_read();                  /* self-test 0xAA */
    (void)mouse_read();                  /* device id 0x00 */

    mouse_write(0xF6);                   /* set defaults */
    g_mouse.last_ack = mouse_read();     /* ACK */

    /* 4. try to enable the scroll wheel (IntelliMouse magic sequence) */
    mouse_write(0xF3); (void)mouse_read(); mouse_write(200); (void)mouse_read();
    mouse_write(0xF3); (void)mouse_read(); mouse_write(100); (void)mouse_read();
    mouse_write(0xF3); (void)mouse_read(); mouse_write(80);  (void)mouse_read();
    mouse_write(0xF2); (void)mouse_read();          /* get device id */
    unsigned char id = mouse_read();
    if (id == 0x03) {
        g_has_wheel = 1;
        early_console64_write("[x86_64][mouse] IntelliMouse wheel enabled\n");
    }

    /* 5. enable packet streaming */
    mouse_write(0xF4);                   /* enable data reporting */
    g_mouse.last_ack = mouse_read();     /* ACK */

    g_mouse.present = 1;
    early_console64_write("[x86_64][mouse] PS/2 mouse initialized\n");
}

/* ---- GUI-facing API ---- */

mouse_state_t *mouse_get_state(void) {
    return (mouse_state_t *)&g_mouse;
}

/* atomically copy state and clear relative deltas (called by gui_poll) */
void mouse_snapshot_and_clear_delta(mouse_state_t *out) {
    if (!out) return;
    /* disable interrupts around the read-modify-write to avoid tearing */
    unsigned long flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");

    *out = *(mouse_state_t *)&g_mouse;

    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;

    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

void mouse_set_bounds(int width, int height) {
    g_mouse.max_x = (width  > 0) ? (width  - 1) : 0;
    g_mouse.max_y = (height > 0) ? (height - 1) : 0;
    if (g_mouse.x > g_mouse.max_x) g_mouse.x = g_mouse.max_x;
    if (g_mouse.y > g_mouse.max_y) g_mouse.y = g_mouse.max_y;
}

void mouse_set_position(int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > g_mouse.max_x) x = g_mouse.max_x;
    if (y > g_mouse.max_y) y = g_mouse.max_y;
    g_mouse.x = x;
    g_mouse.y = y;
}

void mouse_set_absolute_position(int x, int y, uint8_t buttons) {
    mouse_set_position(x, y);
    g_mouse.buttons = buttons;
    g_mouse.absolute_mode = 1;
}

void mouse_set_absolute_position_with_wheel(int x, int y, uint8_t buttons, int wheel) {
    mouse_set_position(x, y);
    g_mouse.buttons = buttons;
    g_mouse.wheel += wheel;
    g_mouse.absolute_mode = 1;
}

void mouse_print_info(void) {
    early_console64_write("[x86_64][mouse] info\n");
}

/* legacy alias used by some i386 call sites */
void mouse_irq_handle(void) {
    arch_x86_64_mouse_irq12_handler();
}

/* ---- EOI plumbing ---- */
#define OPENOS_X86_64_MOUSE_VECTOR 0x2Cu   /* slave PIC base 0x28 + IRQ12%8=4 */

extern int  arch_x86_64_lapic_is_ready(void);
extern void arch_x86_64_lapic_send_eoi(void);
extern void arch_x86_64_pic_send_eoi(unsigned char cpu_vector);

/* Called from the x86_64_irq_mouse assembly stub. Does the packet work
 * then acknowledges the interrupt. Mirrors the PIT IRQ0 trampoline:
 * prefer the LAPIC EOI once the APIC is live, else fall back to the 8259A. */
void arch_x86_64_mouse_irq12_trampoline(void) {
    arch_x86_64_mouse_irq12_handler();
    if (arch_x86_64_lapic_is_ready()) {
        arch_x86_64_lapic_send_eoi();
    } else {
        arch_x86_64_pic_send_eoi(OPENOS_X86_64_MOUSE_VECTOR);
    }
}

/* ---- one-shot install: IDT gate + IOAPIC route + mouse enable ---- */
extern void x86_64_irq_mouse(void);
extern int  arch_x86_64_idt_register_irq(unsigned char cpu_vector, void (*handler)(void));
extern unsigned char arch_x86_64_ioapic_route_isa_irq(unsigned char isa_irq,
                                                      unsigned char vector,
                                                      unsigned char dest_lapic_id);
extern unsigned char arch_x86_64_lapic_id(void);

/* Wire the PS/2 mouse into the live x86_64 interrupt system and start it.
 * Safe to call once, after the LAPIC/IOAPIC are initialized. */
int arch_x86_64_mouse_install(void) {
    /* 1. install the IRQ12 gate */
    if (arch_x86_64_idt_register_irq(OPENOS_X86_64_MOUSE_VECTOR, x86_64_irq_mouse) != 0) {
        early_console64_write("[x86_64][mouse] FAIL idt_register_irq\n");
        return -1;
    }

    /* 2. initialize the device (does controller handshake + enable stream) */
    mouse_init();

    /* 3. route ISA IRQ12 -> vector 0x2C -> boot LAPIC via the IOAPIC.
     *    route_isa_irq consults the MADT override table and leaves the
     *    redirection entry UNMASKED on success. */
    unsigned char dest = arch_x86_64_lapic_id();
    unsigned char pin = arch_x86_64_ioapic_route_isa_irq(12u, OPENOS_X86_64_MOUSE_VECTOR, dest);
    if (pin == 0xFFu) {
        early_console64_write("[x86_64][mouse] FAIL ioapic route irq12\n");
        return -2;
    }

    early_console64_write("[x86_64][mouse] installed (irq12 -> vector 0x2C)\n");
    return 0;
}
