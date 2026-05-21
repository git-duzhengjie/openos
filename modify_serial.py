#!/usr/bin/env python3
# 修改 serial.c: 添加 VGA 输出支持

file_path = '/mnt/e/openos/src/kernel/serial.c'

with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. 添加 vga.h 头文件
old_include = '#include "serial.h"'
new_include = '#include "serial.h"\n#include "vga.h"'
content = content.replace(old_include, new_include)

# 2. 修改 serial_putc(): 添加 VGA 输出
old_serial_putc = """void serial_putc(char c) {
\t/* 等待发送缓冲区空 */
\twhile (!(inb(PORT + 5) & 0x20));
\t
\t/* 发送字符 */
\toutb(PORT, c);
}"""

new_serial_putc = """void serial_putc(char c) {
\t/* 等待发送缓冲区空 */
\twhile (!(inb(PORT + 5) & 0x20));
\t
\t/* 发送字符到串口 */
\toutb(PORT, c);
\t
\t/* 同时输出到 VGA 控制台 */
\tvga_putc(c);
}"""

content = content.replace(old_serial_putc, new_serial_putc)

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(content)

print('serial.c modified: added VGA output to serial_putc()')
