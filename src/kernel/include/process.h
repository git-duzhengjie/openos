/* ============================================================
 * openos - 进程控制块 (PCB) 定义
 * ============================================================ */

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>
#include "fd.h"

/* 进程状态 */
typedef enum {
    PROC_RUNNING  = 0x01,
    PROC_READY    = 0x02,
    PROC_BLOCKED  = 0x03,
    PROC_SLEEPING = 0x04,
    PROC_ZOMBIE   = 0x05,
    PROC_DEAD     = 0x06
} process_state_t;

/* 栈大小 (8KB per thread) */
#define PROC_KERNEL_STACK_SIZE 8192
#define PROC_USER_STACK_SIZE    (2 * 1024 * 1024)  /* 2MB 用户栈 */

/* 最大进程/线程数 */
#define MAX_PROCESSES 64
#define MAX_THREADS  256

/* Reserved system process IDs and wait options */
#define INIT_PID 1u
#define WAITPID_WNOHANG 1
#define OPENOS_SIGKILL 9
#define OPENOS_SIGALRM 14
#define OPENOS_SIGTERM 15
#define OPENOS_SIGNAL_MAX 31

/* ============================================================
 * 线程控制块 (TCB)
 * ============================================================ */
typedef struct thread {
    uint32_t id;              /* 线程ID */
    uint32_t pid;             /* 所属进程ID */
    process_state_t state;    /* 线程状态 */
    uint32_t priority;        /* 优先级 (0=最高, 7=最低) */
    uint32_t quantum;          /* 时间片剩余 */
    uint32_t quantum_total;   /* 时间片总长 */
    uint64_t total_ticks;     /* 累计运行 tick 数 */
    uint32_t wake_time;       /* 睡眠唤醒时间 */
    struct thread *next;      /* 调度队列下一项 */
    struct thread *prev;      /* 调度队列上一项 */
    struct thread *proc_next; /* 所属进程线程链下一项 */

    /* 用户线程入口元数据 */
    uint32_t user_entry;      /* 用户态线程函数 */
    uint32_t user_arg;        /* 用户态线程参数 */
    uint32_t user_stack_top;  /* 用户栈顶 */
    uint32_t user_stack_slot; /* 用户栈槽位，用于退出时释放 */
    uint32_t user_return_entry;/* 用户线程函数自然返回后的入口 */
    uint8_t is_user_thread;   /* 是否为 pthread 风格用户线程 */

    /* 上下文 (内核栈) */
    uint32_t kernel_esp;      /* 内核栈指针 */
    uint32_t kernel_eip;      /* 恢复执行点 */
    uint32_t kernel_stack;    /* 内核栈基址 */
    uint32_t kernel_stack_top;

    /* 寄存器快照 */
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp;
    uint32_t eip, esp, eflags;
    uint32_t cs, ss, ds, es, fs, gs;
} thread_t;

/* ============================================================
 * 进程控制块 (PCB)
 * ============================================================ */
typedef struct process {
    uint32_t pid;             /* 进程ID */
    char name[32];            /* 进程名 */
    process_state_t state;    /* 进程状态 */
    uint32_t ppid;            /* 父进程ID */
    thread_t *threads;        /* 线程链表 */
    uint32_t thread_count;    /* 线程数量 */

    /* 内存 */
    uint32_t cr3;             /* 页目录基址 */
    uint8_t owns_address_space; /* 是否拥有独立用户地址空间 */
    uint32_t code_addr;       /* 代码段起始 */
    uint32_t code_size;       /* 代码段大小 */
    uint32_t heap_start;      /* 堆起始 */
    uint32_t heap_end;        /* 堆结束 */
    uint32_t mmap_base;       /* 匿名 mmap 区起始 */
    uint32_t mmap_end;        /* 匿名 mmap 区当前结束 */

    /* 文件系统 */
    void *fds[MAX_FD];        /* 文件描述符表 (per-process fd table) */
    uint32_t fd_flags[MAX_FD]; /* fd flags, e.g. FD_CLOEXEC */
    char cwd[256];            /* 当前工作目录 (绝对路径) */

    /* 凭据 */
    uint32_t uid;             /* 用户 ID */
    uint32_t gid;             /* 用户组 ID */

    /* 信号 */
    uint32_t pending_signals; /* 待处理信号掩码 */
    uint32_t alarm_deadline_ms; /* alarm 到期时间，单位 ms */
    uint8_t alarm_active;       /* alarm 是否启用 */
    uint32_t next_user_stack_slot; /* 下一个用户线程栈槽位 */

    /* 统计 */
    uint64_t total_ticks;     /* 总运行时间 */
    uint32_t exit_code;       /* 退出码 */
} process_t;

/* ============================================================
 * 调度优先级
 * ============================================================ */
#define PRIORITY_REALTIME 0   /* 实时 - 最高 */
#define PRIORITY_HIGH     1
#define PRIORITY_NORMAL   3   /* 默认 */
#define PRIORITY_LOW      5
#define PRIORITY_IDLE     7   /* 最低 */

/* 时间片 (调度周期) */
#define QUANTUM_DEFAULT   10  /* 10ms per slice */
#define QUANTUM_HIGH      5
#define QUANTUM_LOW       20

/* ============================================================
 * 函数声明
 * ============================================================ */
void sched_init(void);                     /* 初始化调度器 */
void sched_start(void);                   /* 开始调度 */
void sched_schedule(void);                 /* 调度入口 (在中断中调用) */
void sched_add_thread(thread_t *thread);   /* 添加线程到就绪队列 */
void sched_remove_thread(thread_t *thread); /* 从队列移除线程 */
void sched_tick(void);                    /* 时间片递减 (PIT IRQ0) */
thread_t *sched_get_current(void);        /* 获取当前线程 */
void sched_switch_to(thread_t *next);     /* 切换到目标线程 */

/* 创建进程/线程 */
process_t *proc_create(const char *name, uint32_t entry, uint32_t esp);
thread_t *thread_create(uint32_t pid, const char *name,
                        uint32_t entry, uint32_t stack_top);
thread_t *thread_create_sized(uint32_t pid, const char *name,
                              uint32_t entry, uint32_t stack_top,
                              uint32_t stack_size);
void proc_mark_exit(uint32_t pid, int code);
process_t *proc_find(uint32_t pid);
void proc_reap_zombie(process_t *proc);
uint32_t proc_reap_zombies_for_parent(uint32_t ppid);
uint32_t proc_reparent_children(uint32_t old_ppid, uint32_t new_ppid);
void proc_wake_sleepers(uint32_t now_ms);
int proc_terminate(uint32_t pid, int exit_code);
int proc_send_signal(uint32_t pid, int sig);
int proc_handle_pending_signals(uint32_t pid);
int proc_set_alarm(uint32_t pid, uint32_t seconds, uint32_t now_ms);
void proc_check_alarms(uint32_t now_ms);
uint32_t proc_current_pid(void);
uint32_t proc_current_uid(void);
uint32_t proc_current_gid(void);
int proc_set_current_uid(uint32_t uid);
int proc_set_current_gid(uint32_t gid);

/* 睡眠/唤醒 */
void thread_sleep(uint32_t ms);
void thread_wake(thread_t *thread);

/* 系统调用 */
uint32_t sys_getpid(void);
uint32_t sys_gettid(void);
void sys_exit(int code);
int sys_kill(int pid, int sig);
int sys_alarm(unsigned int seconds);
int sys_thread_create(uint32_t entry, uint32_t arg, uint32_t return_entry);
void sys_thread_exit(int code);

/* 调度器核心函数 */
void sched_init(void);
void sched_start(void);
void sched_add_thread(thread_t *t);
void sched_remove_thread(thread_t *t);
void sched_yield(void);
thread_t *sched_get_current(void);
int sched_need_resched(void);
void sched_set_need_resched(int need);
void sched_ensure_not_running_cr3(uint32_t cr3);
int sched_set_thread_priority(thread_t *thread, uint32_t priority);
void sched_tick(void);
uint32_t sched_time_ms(void);

#endif /* KERNEL_PROCESS_H */