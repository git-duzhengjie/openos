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

/* 键盘状态机：
 * 0 = 正常
 * 1 = 收到 0xF0 (break code 前缀)，下一个字节是断开的键
 * 2 = 收到 0xE0 (扩展键前缀)，下一个字节是 make/break
 * 3 = 收到 0xE0 0xF0 (扩展键 break)，下一个字节是断开的键
 */
static int kb_state = 0;
static int shift_pressed = 0;

static void keyboard_handler(registers_t *regs) {
    uint8_t sc;
    char c;

    (void)regs;
    sc = inb(KEYBOARD_DATA_PORT);

    /* 状态机处理扫描码 */
    switch (kb_state) {
        case 0:
            /* 正常状态 */
            if (sc == 0xE0) {
                kb_state = 2;
                return;
            }
            if (sc == 0xF0) {
                kb_state = 1;
                return;
            }
            /* make code */
            if (sc == 0x2A || sc == 0x36) {
                shift_pressed = 1;
                return;
            }
            if (sc < sizeof(scancode_ascii)) {
                c = shift_pressed ? scancode_ascii_shift[sc] : scancode_ascii[sc];
                if (c) {
                    input_putc(c);
                }
            }
            return;

        case 1:
            /* 收到 0xF0 后：键释放 */
            if (sc == 0x2A || sc == 0x36) {
                shift_pressed = 0;
            }
            /* 其他键释放：忽略 */
            kb_state = 0;
            return;

        case 2:
            /* 收到 0xE0 后：扩展键 make */
            /* 扩展键目前不处理（箭头、小键盘等） */
            kb_state = 0;
            return;

        case 3:
            /* 收到 0xE0 0xF0 后：扩展键 break */
            /* 扩展键释放：忽略 */
            kb_state = 0;
            return;
    }
}

void keyboard_init(void) {
    isr_install_handler(33, keyboard_handler);
    serial_write("[OK] Keyboard\n");
}
