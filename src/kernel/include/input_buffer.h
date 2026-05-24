/* ============================================================
 * openos - 统一输入缓冲区
 * 键盘和串口均可写入，shell 从中读取
 * ============================================================ */

#ifndef INPUT_BUFFER_H
#define INPUT_BUFFER_H

#include "types.h"

/* 向缓冲区写入一个字符（键盘ISR或串口调用） */
void input_putc(char c);

/* 从缓冲区读取一个字符，无数据返回0（非阻塞） */
char input_getc(void);

/* 缓冲区是否有数据 */
int input_has_data(void);

#endif /* INPUT_BUFFER_H */
