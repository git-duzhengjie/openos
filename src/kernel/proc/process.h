/* ============================================================
 * openos - 进程管理 (Phase 3)
 * 
 * 进程表 + fork/exec/wait/exit 完整生命周期
 * ============================================================ */

#ifndef KERNEL_PROC_PROCESS_H
#define KERNEL_PROC_PROCESS_H

#include <stdint.h>
#include "../include/process.h"

/* 进程表 */
#define MAX_PROCS 64

void proc_table_init(void);

/* 进程生命周期 */
process_t *proc_alloc(void);                          /* 分配一个空闲 PCB */
void proc_free(process_t *proc);                      /* 释放 PCB */
void proc_reap_zombie(process_t *proc);               /* 回收僵尸进程资源 */
uint32_t proc_reap_zombies_for_parent(uint32_t ppid); /* 回收指定父进程的僵尸子进程 */
void proc_mark_exit(uint32_t pid, int code);          /* 标记进程退出为僵尸 */
process_t *proc_find(uint32_t pid);                   /* 按 PID 查找 */
process_t *proc_find_by_state(process_state_t state); /* 按状态查找 */

/* fork: 复制当前进程（地址空间 + 文件描述符） */
uint32_t sys_fork(void);

/* exec: 替换当前进程的内存映像 */
int sys_exec(const char *path, char *const argv[]);

/* spawn: 创建独立用户态进程并执行 ELF */
int spawn_user_process(const char *path, char *const argv[]);

/* wait: 等待子进程退出 */
uint32_t sys_wait(int *status);

/* waitpid: 等待指定子进程 */
uint32_t sys_waitpid(int pid, int *status, int options);

/* getpid 增强 */
uint32_t sys_getppid(void);

/* 进程树 */
process_t *proc_get_parent(uint32_t pid);
void proc_add_child(process_t *parent, process_t *child);

#endif /* KERNEL_PROC_PROCESS_H */
