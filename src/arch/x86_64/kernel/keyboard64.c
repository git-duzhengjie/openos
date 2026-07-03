/*
 * keyboard64.c - PS/2 keyboard driver for x86_64 UEFI kernel
 *
 * Ported from i386 src/kernel/drivers/keyboard.c.
 * Feeds key events into the GUI via gui_post_key_code_with_modifiers().
 *
 * IRQ1 path: isr64.S (x86_64_irq_keyboard) -> arch_x86_64_kbd_irq1_trampoline()
 * Interrupt routed through IOAPIC (ioapic_route_isa_irq(1,...)) since PIC is off.
 */

#include <stdint.h>
#include <stddef.h>

/* ---- GUI interface (from gui.h) ---- */
#define GUI_KEY_BACKSPACE  8
#define GUI_KEY_TAB        9
#define GUI_KEY_ENTER      13
#define GUI_KEY_ESCAPE     27
#define GUI_KEY_SPACE      32
#define GUI_KEY_DELETE     0x101
#define GUI_KEY_LEFT       0x102
#define GUI_KEY_RIGHT      0x103
#define GUI_KEY_HOME       0x104
#define GUI_KEY_END        0x105
#define GUI_KEY_UP         0x106
#define GUI_KEY_DOWN       0x107
#define GUI_KEY_ALT_TAB    0x108
#define GUI_KEY_SUPER      0x109

/* ---- GUI modifier bits (from gui_user.h) ---- */
#define KBD_MOD_SHIFT 1u
#define KBD_MOD_CTRL  2u
#define KBD_MOD_ALT   4u
#define KBD_MOD_META  8u

extern void gui_post_key_code_with_modifiers(int key, uint32_t modifiers);
extern int  gui_should_capture_key_code_with_modifiers(int key, uint32_t modifiers);

/* ---- kernel logging ---- */
extern void early_console64_write(const char *s);

/* ---- interrupt infrastructure (real x86_64 API, see mouse64.c) ---- */
extern int  arch_x86_64_lapic_is_ready(void);
extern void arch_x86_64_lapic_send_eoi(void);
extern void arch_x86_64_pic_send_eoi(unsigned char cpu_vector);
extern int  arch_x86_64_idt_register_irq(unsigned char cpu_vector, void (*handler)(void));
extern unsigned char arch_x86_64_ioapic_route_isa_irq(unsigned char isa_irq,
                                                      unsigned char vector,
                                                      unsigned char dest_lapic_id);
extern unsigned char arch_x86_64_lapic_id(void);

/* assembly IRQ stub from isr64.S */
extern void x86_64_irq_keyboard(void);

/* ---- PS/2 IO ports ---- */
#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64
#define PS2_STAT_OUTPUT_FULL 0x01
#define PS2_STAT_INPUT_FULL  0x02

/* IDT vector for IRQ1 (keyboard): master PIC base 0x20 + IRQ1 = 0x21. */
#define OPENOS_X86_64_KBD_VECTOR 0x21u

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    __asm__ __volatile__("outb %%al, $0x80" : : "a"((uint8_t)0));
}

/* ---- scancode set 1 -> ASCII tables ---- */
static const char scancode_ascii[] = {
    0,0,'1','2','3','4','5','6',
    '7','8','9','0','-','=',0x08,0x09,
    'q','w','e','r','t','y','u','i',
    'o','p','[',']',0x0A,0,'a','s',
    'd','f','g','h','j','k','l',';',
    0x27,'`',0,0x5C,'z','x','c','v',
    'b','n','m',',','.','/',0,'*',
    0,' ',0,0,0,0,0,0,
    0,0,0,0,0,0,0,'7',
    '8','9','-','4','5','6','+','1',
    '2','3','0','.',0,0,0,0
};

static const char scancode_ascii_shift[] = {
    0,0,'!','@','#','$','%','^',
    '&','*','(',')','_','+',0x08,0x09,
    'Q','W','E','R','T','Y','U','I',
    'O','P','{','}',0x0A,0,'A','S',
    'D','F','G','H','J','K','L',':',
    '\"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',
    0,' ',0,0,0,0,0,0,
    0,0,0,0,0,0,0,'7',
    '8','9','-','4','5','6','+','1',
    '2','3','0','.',0,0,0,0
};

/* ---- keyboard state ---- */
typedef struct {
    int shift;
    int ctrl;
    int alt;
    int meta;
    int caps_lock;
    int num_lock;
    int scroll_lock;
    int extended;      /* 0 = normal, 1 = E0 seen, 2 = E1 seen */
    volatile uint64_t irq_count;
    volatile uint64_t make_count;
    volatile uint64_t break_count;
} kbd_state_t;

static kbd_state_t kb;

static uint32_t kbd_get_modifiers(void) {
    uint32_t mods = 0;
    if (kb.shift) mods |= KBD_MOD_SHIFT;
    if (kb.ctrl)  mods |= KBD_MOD_CTRL;
    if (kb.alt)   mods |= KBD_MOD_ALT;
    if (kb.meta)  mods |= KBD_MOD_META;
    return mods;
}

/* Post key to GUI. Returns 1 if delivered/consumed. */
static int kbd_gui_post(int key) {
    uint32_t mods = kbd_get_modifiers();
    if (!gui_should_capture_key_code_with_modifiers(key, mods)) return 0;
    gui_post_key_code_with_modifiers(key, mods);
    return 1;
}

static int kbd_is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char kbd_translate(uint8_t sc) {
    char c;
    if (sc >= sizeof(scancode_ascii)) return 0;
    c = kb.shift ? scancode_ascii_shift[sc] : scancode_ascii[sc];
    if (kbd_is_letter(c) && kb.caps_lock) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return c;
}

/* Handle E0-prefixed extended scancodes (arrows, delete, home/end, ...) */
static void kbd_handle_extended(uint8_t sc, int release) {
    if (sc == 0x1D) { kb.ctrl = release ? 0 : 1; return; }  /* right ctrl */
    if (sc == 0x38) { kb.alt  = release ? 0 : 1; return; }  /* right alt  */
    if (release) return;
    switch (sc) {
        case 0x48: kbd_gui_post(GUI_KEY_UP);     break;
        case 0x50: kbd_gui_post(GUI_KEY_DOWN);   break;
        case 0x4D: kbd_gui_post(GUI_KEY_RIGHT);  break;
        case 0x4B: kbd_gui_post(GUI_KEY_LEFT);   break;
        case 0x53: kbd_gui_post(GUI_KEY_DELETE); break;
        case 0x47: kbd_gui_post(GUI_KEY_HOME);   break;
        case 0x4F: kbd_gui_post(GUI_KEY_END);    break;
        case 0x1C: kbd_gui_post(GUI_KEY_ENTER);  break;  /* keypad enter */
        case 0x35: kbd_gui_post('/');            break;  /* keypad slash */
        case 0x5B: kb.meta = 1; kbd_gui_post(GUI_KEY_SUPER); break;
        default: break;
    }
}

/* Core scancode processor, called from the IRQ trampoline. */
static void kbd_process_scancode(uint8_t sc) {
    int release;
    char c;

    kb.irq_count++;

    if (sc == 0xE0) { kb.extended = 1; return; }
    if (sc == 0xE1) { kb.extended = 2; return; }
    if (kb.extended == 2) { kb.extended = 0; return; }

    release = (sc & 0x80) != 0;
    sc &= 0x7F;
    if (release) kb.break_count++; else kb.make_count++;

    if (kb.extended == 1) {
        kb.extended = 0;
        kbd_handle_extended(sc, release);
        return;
    }

    switch (sc) {
        case 0x2A: case 0x36: kb.shift = release ? 0 : 1; return;
        case 0x1D: kb.ctrl  = release ? 0 : 1; return;
        case 0x38: kb.alt   = release ? 0 : 1; return;
        case 0x5B: case 0x5C: kb.meta = release ? 0 : 1; return;
        case 0x3A: if (!release) kb.caps_lock   ^= 1; return;
        case 0x45: if (!release) kb.num_lock    ^= 1; return;
        case 0x46: if (!release) kb.scroll_lock ^= 1; return;
        default: break;
    }

    if (release) return;

    /* Alt+Tab window switching */
    if (sc == 0x0F && kb.alt) { kbd_gui_post(GUI_KEY_ALT_TAB); return; }

    c = kbd_translate(sc);
    if (c) {
        int key = (int)c;
        /* Ctrl+letter -> control code, matching i386 behavior */
        if (kb.ctrl && c >= 'a' && c <= 'z') key = (int)(c - 'a' + 1);
        else if (kb.ctrl && c >= 'A' && c <= 'Z') key = (int)(c - 'A' + 1);
        kbd_gui_post(key);
    }
}

/* ---- PS/2 controller handshake helpers ---- */
static void ps2_wait_input(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(PS2_STATUS) & PS2_STAT_INPUT_FULL) == 0) return;
    }
}

static void ps2_wait_output(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(PS2_STATUS) & PS2_STAT_OUTPUT_FULL) return;
    }
}

/* Drain any stale bytes sitting in the controller output buffer. */
static void ps2_flush(void) {
    int timeout = 1000;
    while ((inb(PS2_STATUS) & PS2_STAT_OUTPUT_FULL) && timeout-- > 0) {
        (void)inb(PS2_DATA);
    }
}

/* Enable the first PS/2 port and its IRQ1, then flush pending bytes. */
static void kbd_init(void) {
    /* enable first port */
    ps2_wait_input();
    outb(PS2_CMD, 0xAE);

    /* read controller config byte, set bit0 (IRQ1 enable), clear bit4 (clock) */
    ps2_wait_input();
    outb(PS2_CMD, 0x20);          /* read config byte */
    ps2_wait_output();
    uint8_t cfg = inb(PS2_DATA);
    cfg |= 0x01;                  /* enable first-port interrupt (IRQ1) */
    cfg &= (uint8_t)~0x10;        /* clear first-port clock disable */
    cfg |= 0x40;                  /* enable translation: keyboard set2 -> CPU set1 */
    ps2_wait_input();
    outb(PS2_CMD, 0x60);          /* write config byte */
    ps2_wait_input();
    outb(PS2_DATA, cfg);

    /*
     * NOTE: do NOT force scancode set here.
     * The 8042 controller has translation enabled by default (config bit 6):
     * the keyboard emits set 2, the controller translates it to set 1 for the
     * CPU. Our scancode_ascii[] tables are set 1, so this is exactly what we
     * want. Forcing the keyboard into set 1 via 0xF0/0x01 while translation is
     * still on causes a double-translation and produces garbage/mismatched
     * make/break codes. Leave the default alone.
     */

    /* enable scanning */
    ps2_wait_input();
    outb(PS2_DATA, 0xF4);
    (void)ps2_wait_output();
    (void)inb(PS2_DATA);          /* ACK */

    ps2_flush();
    early_console64_write("[x86_64][kbd] controller initialized\n");
}

/* ---- IRQ1 trampoline: called from x86_64_irq_keyboard asm stub ---- */
void arch_x86_64_kbd_irq1_trampoline(void) {
    uint8_t status = inb(PS2_STATUS);
    if (status & PS2_STAT_OUTPUT_FULL) {
        uint8_t sc = inb(PS2_DATA);
        /* debug: log first several scancodes to prove IRQ1 delivery */
        if (kb.irq_count < 24) {
            char buf[48];
            const char *hex = "0123456789abcdef";
            int n = 0;
            buf[n++]='['; buf[n++]='k'; buf[n++]='b'; buf[n++]='d';
            buf[n++]=']'; buf[n++]=' '; buf[n++]='s'; buf[n++]='c';
            buf[n++]='='; buf[n++]='0'; buf[n++]='x';
            buf[n++]=hex[(sc>>4)&0xF]; buf[n++]=hex[sc&0xF];
            buf[n++]='\n'; buf[n]='\0';
            early_console64_write(buf);
        }
        kbd_process_scancode(sc);
    }
    if (arch_x86_64_lapic_is_ready()) {
        arch_x86_64_lapic_send_eoi();
    } else {
        arch_x86_64_pic_send_eoi((unsigned char)OPENOS_X86_64_KBD_VECTOR);
    }
}

/* ---- one-shot install: IDT gate + IOAPIC route + controller enable ---- */
int arch_x86_64_keyboard_install(void) {
    /* 1. install the IRQ1 gate */
    if (arch_x86_64_idt_register_irq((unsigned char)OPENOS_X86_64_KBD_VECTOR,
                                     x86_64_irq_keyboard) != 0) {
        early_console64_write("[x86_64][kbd] FAIL idt_register_irq\n");
        return -1;
    }

    /* 2. initialize the controller (enable port + IRQ1 + scanning) */
    kbd_init();

    /* 3. route ISA IRQ1 -> vector 0x21 -> boot LAPIC via the IOAPIC */
    unsigned char dest = arch_x86_64_lapic_id();
    unsigned char pin = arch_x86_64_ioapic_route_isa_irq(1u,
                            (unsigned char)OPENOS_X86_64_KBD_VECTOR, dest);
    if (pin == 0xFFu) {
        early_console64_write("[x86_64][kbd] FAIL ioapic route irq1\n");
        return -2;
    }

    early_console64_write("[x86_64][kbd] installed (irq1 -> vector 0x21)\n");
    return 0;
}
