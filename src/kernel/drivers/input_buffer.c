/* ============================================================
 * openos - 统一输入缓冲区实现
 * 环形缓冲区，键盘和串口均可写入
 * ============================================================ */

#include "input_buffer.h"
#include <stdint.h>

#define INPUT_BUF_SIZE 512

static char buf[INPUT_BUF_SIZE];
static volatile int head = 0;  /* 写入位置 */
static volatile int tail = 0;  /* 读取位置 */
static volatile int count = 0; /* 当前数据量 */

static inline uint32_t input_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void input_irq_restore(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

void input_putc(char c) {
    uint32_t flags = input_irq_save();
    if (count < INPUT_BUF_SIZE) {
        buf[head] = c;
        head = (head + 1) % INPUT_BUF_SIZE;
        count++;
    }
    input_irq_restore(flags);
}

char input_getc(void) {
    char c = 0;
    uint32_t flags = input_irq_save();
    if (count > 0) {
        c = buf[tail];
        tail = (tail + 1) % INPUT_BUF_SIZE;
        count--;
    }
    input_irq_restore(flags);
    return c;
}

int input_has_data(void) {
    int has;
    uint32_t flags = input_irq_save();
    has = count > 0;
    input_irq_restore(flags);
    return has;
}
