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
#define SYS_FORK        220
#define SYS_EXEC        221
#define SYS_WAIT        222
#define SYS_WAITPID     223
#define SYS_GETPPID     224
#define SYS_OPEN        225
#define SYS_CLOSE       226
#define SYS_READ_FD     227
#define SYS_WRITE_FD    228
#define SYS_SEEK        229
#define SYS_MKDIR       230
#define SYS_UNLINK      231
#define SYS_RMDIR       232
#define SYS_SPAWN       233
#define SYS_EXEC_ENV    234
#define SYS_SPAWN_ENV   235
#define SYS_STAT        236
#define SYS_GETCWD      237
#define SYS_CHDIR       238
#define SYS_READDIR     239
#define SYS_FSTAT       240
#define SYS_LSTAT       241
#define SYS_DUP         242
#define SYS_DUP2        243
#define SYS_PIPE        244
#define SYS_KILL        245
#define SYS_ALARM       246

typedef struct openos_stat {
    uint32_t ino;
    uint32_t mode;
    uint32_t size;
    uint32_t nlinks;
    uint32_t fs_type;
} openos_stat_t;

typedef struct openos_dirent {
    uint32_t ino;
    uint32_t mode;
    uint32_t size;
    char name[32];
} openos_dirent_t;

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
