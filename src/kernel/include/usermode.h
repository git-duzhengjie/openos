/* ============================================================
 * openos - 用户态切换头文件
 * ============================================================ */

#ifndef KERNEL_USERMODE_H
#define KERNEL_USERMODE_H

#include <stdint.h>

/* 用户栈大小 */
#define USER_STACK_SIZE  8192   /* 8KB */
#define USER_STACK_ADDR  0xBF000000  /* 512MB 地址空间的顶部 */

/* 切换到用户态 */
void switch_to_user(uint32_t eip, uint32_t esp);

/* 设置 TSS 的内核栈 (用于从用户态返回) */
void tss_set_kernel_stack(uint32_t esp0);

/* 分配用户栈 */
uint32_t alloc_user_stack(void);

/* 测试用户态切换 */
void test_user_mode_switch(void);

#endif /* KERNEL_USERMODE_H */
