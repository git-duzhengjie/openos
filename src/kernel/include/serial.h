/* ============================================================
 * openos - 串口调试输出
 * COM1 = 0x3F8
 * ============================================================ */

#ifndef KERNEL_SERIAL_H
#define KERNEL_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);

#endif /* KERNEL_SERIAL_H */
