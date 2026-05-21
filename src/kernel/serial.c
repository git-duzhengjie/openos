/* ============================================================
 * openos - 串口调试输出实现 (COM1 = 0x3F8)
 * ============================================================ */

#include "../include/serial.h"

#define COM1 0x3F8

/* I/O 端口读写 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* 初始化 COM1：波特率 38400，8N1 */
void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* 关闭中断 */
    outb(COM1 + 3, 0x80);   /* DLAB=1，允许设置波特率 */
    outb(COM1 + 0, 0x03);   /* 除数锁存低位：115200/38400 = 3 */
    outb(COM1 + 1, 0x00);   /* 除数锁存高位 */
    outb(COM1 + 3, 0x03);   /* 8位数据，无校验，1停止位 */
    outb(COM1 + 2, 0xC7);   /* 启用FIFO，清除队列 */
    outb(COM1 + 4, 0x0B);   /* RTS/DSR 置位，启用中断 */
}

/* 发送一个字符 */
void serial_putc(char c) {
    /* 等待发送保持寄存器空 */
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, (uint8_t)c);
}

/* 发送字符串（自动处理 \n → \r\n） */
void serial_write(const char *s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\n')
            serial_putc('\r');
        serial_putc(s[i]);
    }
}

/* 发送 32 位十六进制数 */
void serial_write_hex(uint32_t val) {
    serial_write("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
}
