/* ============================================================
 * openos - PS/2 Keyboard Driver
 * Handles IRQ1, scancode to ASCII, proper break code handling
 * ============================================================ */

#include "../include/idt.h"
#include "../include/serial.h"
#include "../include/io.h"
#include "../include/input_buffer.h"

#define KEYBOARD_DATA_PORT   0x60

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

static int shift_pressed = 0;
static int release_next = 0;

static void keyboard_handler(registers_t *regs) {
    uint8_t sc;
    char c;

    (void)regs;
    sc = inb(KEYBOARD_DATA_PORT);

    if (sc == 0xE0) { release_next = 2; return; }
    if (sc == 0xF0) { release_next = 1; return; }

    if (release_next == 1) {
        if (sc == 0x2A || sc == 0x36) shift_pressed = 0;
        release_next = 0;
        return;
    }
    if (release_next == 2) { release_next = 0; return; }

    /* make code */
    if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return; }

    if (sc < sizeof(scancode_ascii)) {
        c = shift_pressed ? scancode_ascii_shift[sc] : scancode_ascii[sc];
        if (c) input_putc(c);
    }
}

void keyboard_init(void) {
    isr_install_handler(33, keyboard_handler);
    serial_write("[OK] Keyboard\n");
}
