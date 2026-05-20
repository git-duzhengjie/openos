/* ============================================================
 * openos - 系统调用接口
 * ============================================================ */

#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>

/* 系统调用号 */
#define SYS_EXIT        1
#define SYS_GETPID      20
#define SYS_GETTID      21
#define SYS_WRITE       64
#define SYS_READ        63
#define SYS_MALLOC      73
#define SYS_FREE        74
#define SYS_SLEEP       200
#define SYS_YIELD       201

/* 调用号通过 EAX 传递，参数通过 EBX/ECX/EDX/ESI/EDI */
uint32_t syscall_handler(uint32_t syscall_num,
                         uint32_t arg1, uint32_t arg2, uint32_t arg3,
                         uint32_t arg4, uint32_t arg5);

/* 注册系统调用处理函数 */
typedef uint32_t (*syscall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void syscall_register(int num, syscall_fn_t fn);

/* 初始化系统调用 */
void syscall_init(void);

/* C 实现的 syscall_dispatch */
uint32_t syscall_dispatch(uint32_t num,
                          uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e);

/* 用户程序入口 */
void user_entry(void);

#endif /* KERNEL_SYSCALL_H */