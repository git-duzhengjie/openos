/* ============================================================
 * openos - 键盘驱动 (PS/2)
 * 处理IRQ1键盘中断，扫描码转ASCII
 * ============================================================ */

#include "../include/idt.h"
#include "../include/serial.h"
#include "../include/io.h"

/* PS/2端口 */
#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_CMD_PORT    0x64

/* 扫描码转ASCII表 (Set 1, 小写) */
static const char scancode_ascii[] = {
    0,   0,   '1', '2', '3', '4', '5', '6',  /* 0x00-0x07 */
    '7', '8', '9', '0', '-', '=', '\b', '\t', /* 0x08-0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',  /* 0x10-0x17 */
    'o', 'p', '[', ']', '\n', 0,   'a', 's', /* 0x18-0x1F */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',  /* 0x20-0x27 */
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v', /* 0x28-0x2F */
    'b', 'n', 'm', ',', '.', '/', 0,   '*',  /* 0x30-0x37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,    /* 0x38-0x3F */
    0,   0,   0,   0,   0,   0,   0,   '7',  /* 0x40-0x47 */
    '8', '9', '-', '4', '5', '6', '+', '1',  /* 0x48-0x4F */
    '2', '3', '0', '.', 0,   0,   0,   0     /* 0x50-0x57 */
};

/* 扫描码转ASCII表 (Set 1, 大写/Shift) */
static const char scancode_ascii_shift[] = {
    0,   0,   '!', '@', '#', '$', '%', '^',  /* 0x00-0x07 */
    '&', '*', '(', ')', '_', '+', '\b', '\t', /* 0x08-0x0F */
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',  /* 0x10-0x17 */
    'O', 'P', '{', '}', '\n', 0,   'A', 'S', /* 0x18-0x1F */
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',  /* 0x20-0x27 */
    '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',  /* 0x28-0x2F */
    'B', 'N', 'M', '<', '>', '?', 0,   '*',  /* 0x30-0x37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,    /* 0x38-0x3F */
    0,   0,   0,   0,   0,   0,   0,   '7',  /* 0x40-0x47 */
    '8', '9', '-', '4', '5', '6', '+', '1',  /* 0x48-0x4F */
    '2', '3', '0', '.', 0,   0,   0,   0     /* 0x50-0x57 */
};

static int shift_pressed = 0;  /* Shift键状态 */

/* ============================================================
 * 键盘中断处理函数
 * ============================================================ */
static void keyboard_handler(registers_t *regs) {
    uint8_t scancode;
    char c;
    
    (void)regs;  /* 抑制未使用参数警告 */
    /* 读取扫描码 */
    scancode = inb(KEYBOARD_DATA_PORT);
    
    /* 处理按键释放 (break code: 0xF0 + scancode) */
    if (scancode == 0xF0) {
        /* 下一个字节是释放的键扫描码 */
        return;
    }
    
    /* 处理Shift键 */
    if (scancode == 0x2A || scancode == 0x36) {
        /* Left Shift 或 Right Shift 按下 */
        shift_pressed = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        /* Left Shift 或 Right Shift 释放 */
        shift_pressed = 0;
        return;
    }
    
    /* 转换扫描码为ASCII */
    if (scancode < sizeof(scancode_ascii_shift)) {
        if (shift_pressed) {
            c = scancode_ascii_shift[scancode];
        } else {
            c = scancode_ascii[scancode];
        }
        
        /* 输出到串口 */
        if (c >= ' ') {
            serial_putc(c);
        } else if (c == '\n') {
            serial_write("\n");
        } else if (c == '\b') {
            serial_putc('\b');
        } else if (c == '\t') {
            serial_putc('\t');
        }
    }
}

/* ============================================================
 * 键盘初始化
 * ============================================================ */
void keyboard_init(void) {
    /* 注册键盘中断处理函数 (IRQ1 = INT 33) */
    isr_install_handler(33, keyboard_handler);
    
    serial_write("[OK] Keyboard\n");
}
