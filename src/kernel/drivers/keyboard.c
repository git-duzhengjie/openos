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

/* 键盘状态机（PS/2 Set 1 扫描码）：
 * 0 = 正常
 * 1 = 收到 0xE0 (扩展键前缀)，下一个字节是 make/break
 */
static int kb_state = 0;
static int shift_pressed = 0;
static int ctrl_pressed = 0;

static void keyboard_handler(registers_t *regs) {
    uint8_t sc;
    char c;

    (void)regs;
    sc = inb(KEYBOARD_DATA_PORT);

    /* Set 1 扩展键前缀 */
    if (sc == 0xE0) {
        kb_state = 1;
        return;
    }

    /* Set 1 断码：最高位为1表示键释放，直接忽略 */
    if (sc & 0x80) {
        /* Shift / Ctrl 释放 */
        if (sc == 0xAA || sc == 0xB6) {
            shift_pressed = 0;
        } else if (sc == 0x9D) {
            ctrl_pressed = 0;
        }
        kb_state = 0;
        return;
    }

    /* 扩展键 make 码（方向键、编辑键等）：转换为 ANSI 转义序列 */
    if (kb_state == 1) {
        kb_state = 0;
        switch (sc) {
            case 0x48: /* Up    -> ESC [ A */
                input_putc(0x1B); input_putc('['); input_putc('A');
                break;
            case 0x50: /* Down  -> ESC [ B */
                input_putc(0x1B); input_putc('['); input_putc('B');
                break;
            case 0x4D: /* Right -> ESC [ C */
                input_putc(0x1B); input_putc('['); input_putc('C');
                break;
            case 0x4B: /* Left  -> ESC [ D */
                input_putc(0x1B); input_putc('['); input_putc('D');
                break;
            case 0x52: /* Insert  -> ESC [ 2 ~ */
                input_putc(0x1B); input_putc('['); input_putc('2'); input_putc('~');
                break;
            case 0x53: /* Delete  -> ESC [ 3 ~ */
                input_putc(0x1B); input_putc('['); input_putc('3'); input_putc('~');
                break;
            case 0x47: /* Home    -> ESC [ H */
                input_putc(0x1B); input_putc('['); input_putc('H');
                break;
            case 0x4F: /* End     -> ESC [ F */
                input_putc(0x1B); input_putc('['); input_putc('F');
                break;
            case 0x49: /* Page Up   -> ESC [ 5 ~ */
                input_putc(0x1B); input_putc('['); input_putc('5'); input_putc('~');
                break;
            case 0x51: /* Page Down -> ESC [ 6 ~ */
                input_putc(0x1B); input_putc('['); input_putc('6'); input_putc('~');
                break;
            default:
                break;
        }
        return;
    }

    /* 普通 make code */
    if (sc == 0x2A || sc == 0x36) {
        shift_pressed = 1;
        return;
    }
    if (sc == 0x1D) {
        ctrl_pressed = 1;
        return;
    }
    if (sc < sizeof(scancode_ascii)) {
        c = shift_pressed ? scancode_ascii_shift[sc] : scancode_ascii[sc];
        if (c) {
            if (ctrl_pressed && c >= 'a' && c <= 'z')
                input_putc(c - 'a' + 1);
            else if (ctrl_pressed && c >= 'A' && c <= 'Z')
                input_putc(c - 'A' + 1);
            else
                input_putc(c);
        }
    }
}

void keyboard_init(void) {
    isr_install_handler(33, keyboard_handler);
    serial_write("[OK] Keyboard\n");
}
