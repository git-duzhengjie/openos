/* ============================================================
 * openos - PS/2 Keyboard Driver
 * Handles IRQ1, scancode set 1, modifiers, locks and extended keys
 * ============================================================ */

#include "../include/keyboard.h"
#include "../include/idt.h"
#include "../include/serial.h"
#include "../include/io.h"
#include "../include/input_buffer.h"

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_CMD_LED     0xED

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
    '"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',
    0,' ',0,0,0,0,0,0,
    0,0,0,0,0,0,0,'7',
    '8','9','-','4','5','6','+','1',
    '2','3','0','.',0,0,0,0
};

static keyboard_state_t kb;

static void keyboard_put_esc3(char a, char b, char c) {
    input_putc(0x1B);
    input_putc(a);
    input_putc(b);
    if (c) {
        input_putc(c);
    }
}

static void keyboard_put_csi_tilde(char n) {
    input_putc(0x1B);
    input_putc('[');
    input_putc(n);
    input_putc('~');
}

static void keyboard_put_csi_number_tilde(const char *number) {
    input_putc(0x1B);
    input_putc('[');
    while (*number) {
        input_putc(*number++);
    }
    input_putc('~');
}

static void keyboard_wait_input(void) {
    int timeout = 10000;
    while (--timeout && (inb(KEYBOARD_STATUS_PORT) & 2) != 0) {
    }
}

static void keyboard_update_leds(void) {
    uint8_t leds = 0;
    if (kb.scroll_lock) leds |= 1;
    if (kb.num_lock) leds |= 2;
    if (kb.caps_lock) leds |= 4;
    keyboard_wait_input();
    outb(KEYBOARD_DATA_PORT, KEYBOARD_CMD_LED);
    keyboard_wait_input();
    outb(KEYBOARD_DATA_PORT, leds);
}

static int keyboard_is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char keyboard_translate(uint8_t sc) {
    char c;
    int shifted;
    if (sc >= sizeof(scancode_ascii)) {
        return 0;
    }
    shifted = kb.shift ? 1 : 0;
    c = shifted ? scancode_ascii_shift[sc] : scancode_ascii[sc];
    if (keyboard_is_letter(c) && kb.caps_lock) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return c;
}

static void keyboard_handle_function(uint8_t sc) {
    static const char fn_code[] = { 'P','Q','R','S','t','u','v','w','x','y' };
    if (sc >= 0x3B && sc <= 0x44) {
        keyboard_put_esc3('O', fn_code[sc - 0x3B], 0);
    } else if (sc == 0x57) {
        keyboard_put_csi_number_tilde("23");
    } else if (sc == 0x58) {
        keyboard_put_csi_number_tilde("24");
    }
}

static void keyboard_handle_extended(uint8_t sc, int release) {
    if (sc == 0x1D) {
        kb.ctrl = release ? 0 : 1;
        return;
    }
    if (sc == 0x38) {
        kb.alt = release ? 0 : 1;
        return;
    }
    if (release) {
        return;
    }
    switch (sc) {
        case 0x48: keyboard_put_esc3('[', 'A', 0); break;
        case 0x50: keyboard_put_esc3('[', 'B', 0); break;
        case 0x4D: keyboard_put_esc3('[', 'C', 0); break;
        case 0x4B: keyboard_put_esc3('[', 'D', 0); break;
        case 0x52: keyboard_put_csi_tilde('2'); break;
        case 0x53: keyboard_put_csi_tilde('3'); break;
        case 0x47: keyboard_put_esc3('[', 'H', 0); break;
        case 0x4F: keyboard_put_esc3('[', 'F', 0); break;
        case 0x49: keyboard_put_csi_tilde('5'); break;
        case 0x51: keyboard_put_csi_tilde('6'); break;
        case 0x1C: input_putc(0x0A); break;
        case 0x35: input_putc('/'); break;
        default: break;
    }
}

static void keyboard_handler(registers_t *regs) {
    uint8_t sc;
    int release;
    char c;

    (void)regs;
    sc = inb(KEYBOARD_DATA_PORT);
    kb.irq_count++;

    if (sc == 0xE0) {
        kb.extended = 1;
        return;
    }
    if (sc == 0xE1) {
        kb.extended = 2;
        return;
    }
    if (kb.extended == 2) {
        kb.extended = 0;
        return;
    }

    release = (sc & 0x80) != 0;
    sc &= 0x7F;
    if (release) kb.break_count++; else kb.make_count++;

    if (kb.extended == 1) {
        kb.extended = 0;
        keyboard_handle_extended(sc, release);
        return;
    }

    switch (sc) {
        case 0x2A:
        case 0x36:
            kb.shift = release ? 0 : 1;
            return;
        case 0x1D:
            kb.ctrl = release ? 0 : 1;
            return;
        case 0x38:
            kb.alt = release ? 0 : 1;
            return;
        case 0x3A:
            if (!release) { kb.caps_lock ^= 1; keyboard_update_leds(); }
            return;
        case 0x45:
            if (!release) { kb.num_lock ^= 1; keyboard_update_leds(); }
            return;
        case 0x46:
            if (!release) { kb.scroll_lock ^= 1; keyboard_update_leds(); }
            return;
        default:
            break;
    }

    if (release) {
        return;
    }

    if ((sc >= 0x3B && sc <= 0x44) || sc == 0x57 || sc == 0x58) {
        keyboard_handle_function(sc);
        return;
    }

    c = keyboard_translate(sc);
    if (c) {
        if (kb.alt) {
            input_putc(0x1B);
        }
        if (kb.ctrl && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 1);
        } else if (kb.ctrl && c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 1);
        }
        if (c == 4) {
            input_mark_eof();
        } else {
            input_putc(c);
        }
    }
}

const keyboard_state_t *keyboard_get_state(void) {
    return &kb;
}

void keyboard_init(void) {
    kb.shift = 0;
    kb.ctrl = 0;
    kb.alt = 0;
    kb.caps_lock = 0;
    kb.num_lock = 1;
    kb.scroll_lock = 0;
    kb.extended = 0;
    kb.irq_count = 0;
    kb.make_count = 0;
    kb.break_count = 0;
    keyboard_update_leds();
    isr_install_handler(33, keyboard_handler);
    serial_write("[OK] PS/2 keyboard\n");
}
