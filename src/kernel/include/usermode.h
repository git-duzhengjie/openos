/* ============================================================
 * openos - 用户态切换头文件
 * ============================================================ */

#ifndef KERNEL_USERMODE_H
#define KERNEL_USERMODE_H

#include <stdint.h>

#define USER_SPACE_START      0x40000000u
#define USER_SPACE_END        0xC0000000u

/* 用户栈布局: 最底部一页不映射作为 guard page */
#define USER_STACK_SIZE        8192u       /* 可用用户栈 8KB */
#define USER_STACK_GUARD_SIZE  4096u
#define USER_STACK_ADDR        0xBF000000u /* guard page 起始地址 */
#define USER_STACK_BASE        (USER_STACK_ADDR + USER_STACK_GUARD_SIZE)
#define USER_STACK_TOP         (USER_STACK_BASE + USER_STACK_SIZE)

/* 切换到用户态 */
void switch_to_user(uint32_t eip, uint32_t esp);

/* 设置 TSS 的内核栈 (用于从用户态返回) */
void tss_set_kernel_stack(uint32_t esp0);

/* 分配/释放用户栈 */
uint32_t alloc_user_stack(void);
uint32_t alloc_user_stack_slot(uint32_t slot);
uint32_t alloc_user_stack_randomized(uint32_t slot);
void free_user_stack_slot(uint32_t slot);

/* 测试用户态切换 */
void test_user_mode_switch(void);

#endif /* KERNEL_USERMODE_H */
