/* ============================================================
 * openos - 统一输入缓冲区实现
 * 环形缓冲区，键盘和串口均可写入
 * ============================================================ */

#include "input_buffer.h"

#define INPUT_BUF_SIZE 512

static char buf[INPUT_BUF_SIZE];
static volatile int head = 0;  /* 写入位置 */
static volatile int tail = 0;  /* 读取位置 */
static volatile int count = 0; /* 当前数据量 */

void input_putc(char c) {
    if (count >= INPUT_BUF_SIZE) return; /* 满则丢弃 */
    buf[head] = c;
    head = (head + 1) % INPUT_BUF_SIZE;
    count++;
}

char input_getc(void) {
    if (count == 0) return 0;
    char c = buf[tail];
    tail = (tail + 1) % INPUT_BUF_SIZE;
    count--;
    return c;
}

int input_has_data(void) {
    return count > 0;
}
