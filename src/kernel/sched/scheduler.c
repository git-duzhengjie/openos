/* ============================================================
 * openos - 多级反馈队列调度器实现
 * ============================================================ */

#include "../include/process.h"
#include "../include/pmm.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* idle 线程入口 (空循环) */
void idle_entry(void) {
    while (1) { __asm__ volatile("hlt"); }
}
extern void idle_entry(void);

/* 上下文切换 (汇编实现) */
extern void context_switch(thread_t *prev, thread_t *next);

/* 调度器状态 */
static struct {
    thread_t *queues[8];       /* 8个优先级队列 */
    thread_t *current;         /* 当前线程 */
    uint32_t current_ticks;    /* 当前时间片已用tick */
    int initialized;
} sched = {{0}, 0, 0, 0};

#define SCHED_TICKS_PER_QUANTUM 10

/* 初始化调度器 */
void sched_init(void) {
    for (int i = 0; i < 8; i++) sched.queues[i] = NULL;
    sched.current = NULL;
    sched.current_ticks = 0;
    sched.initialized = 1;

    /* 创建 idle 线程 */
    uint32_t idle_stack_top = (uint32_t)pmm_alloc_page() + 4096;
    thread_t *idle = thread_create(0, "idle", (uint32_t)idle_entry, idle_stack_top);
    if (idle) {
        idle->priority = PRIORITY_IDLE;
        idle->state = PROC_RUNNING;
        sched_add_thread(idle);
        sched.current = idle;
    }
}

/* 添加线程到就绪队列 */
void sched_add_thread(thread_t *t) {
    if (!t || t->state == PROC_RUNNING) return;
    t->next = NULL;
    t->prev = NULL;
    if (!sched.queues[t->priority]) {
        sched.queues[t->priority] = t;
        t->prev = NULL;
    } else {
        thread_t *h = sched.queues[t->priority];
        while (h->next) h = h->next;
        h->next = t;
        t->prev = h;
    }
    if (t->state != PROC_RUNNING)
        t->state = PROC_READY;
}

/* 从队列移除线程 */
void sched_remove_thread(thread_t *t) {
    if (!t) return;
    if (t->prev) t->prev->next = t->next;
    else sched.queues[t->priority] = t->next;
    if (t->next) t->next->prev = t->prev;
    t->next = NULL;
    t->prev = NULL;
}

/* 选下一个线程 */
static thread_t *pick_next(void) {
    for (int i = 0; i < 8; i++) {
        if (sched.queues[i]) return sched.queues[i];
    }
    return NULL;
}

/* 调度主函数 */
void sched_schedule(void) {
    if (!sched.initialized || !sched.current) return;
    thread_t *next = pick_next();
    if (!next || next == sched.current) return;
    thread_t *prev = sched.current;
    sched.current = next;
    context_switch(prev, next);
}

/* 线程时间片递减 */
void sched_tick(void) {
    sched.current_ticks++;
    if (sched.current_ticks >= SCHED_TICKS_PER_QUANTUM) {
        sched.current_ticks = 0;
        /* 时间片耗尽，降级 */
        if (sched.current && sched.current->priority < 7) {
            sched_remove_thread(sched.current);
            sched.current->priority++;
            sched_add_thread(sched.current);
        }
        sched_schedule();
    }
}

/* 获取当前线程 */
thread_t *sched_get_current(void) {
    return sched.current;
}

/* 切换到目标线程 */
void sched_switch_to(thread_t *next) {
    if (!next) return;
    thread_t *prev = sched.current;
    sched.current = next;
    context_switch(prev, next);
}

/* 创建进程 */
process_t *proc_create(const char *name, uint32_t entry, uint32_t esp) {
    (void)entry; (void)esp;
    process_t *p = (process_t *)pmm_alloc_page();
    if (!p) return NULL;
    for (int i = 0; i < (int)sizeof(process_t); i++) ((char *)p)[i] = 0;
    p->pid = (uint32_t)p;
    p->state = PROC_RUNNING;
    p->cr3 = 0;
    /* 复制名字 */
    for (int i = 0; name[i] && i < 31; i++) p->name[i] = name[i];
    return p;
}

/* 创建线程 */
thread_t *thread_create(uint32_t pid, const char *name, uint32_t entry, uint32_t stack_top) {
    thread_t *t = (thread_t *)pmm_alloc_page();
    if (!t) return NULL;
    for (int i = 0; i < (int)sizeof(thread_t); i++) ((char *)t)[i] = 0;
    t->id = (uint32_t)t;
    t->pid = pid;
    t->priority = PRIORITY_NORMAL;
    t->state = PROC_READY;
    /* 清空栈 */
    char *stack_base = (char *)((uint32_t)stack_top - 4096);
    for (int i = 0; i < 4096; i++) stack_base[i] = 0;
    /* 构建上下文帧 */
    uint32_t *sp = (uint32_t *)stack_top;
    *(--sp) = entry;      /* EIP */
    *(--sp) = 0;          /* EBP */
    *(--sp) = 0;          /* EBX */
    *(--sp) = 0;          /* ESI */
    *(--sp) = 0;          /* EDI */
    t->kernel_esp = (uint32_t)sp;
    t->kernel_eip = entry;
    t->kernel_stack = (uint32_t)stack_base;
    t->kernel_stack_top = stack_top;
    /* 复制名字到 name 字段... (thread_t 没有 name，直接跳过) */
    (void)name;
    return t;
}

/* 线程睡眠 */
void thread_sleep(uint32_t ms) {
    (void)ms;
    if (sched.current) {
        sched_remove_thread(sched.current);
        sched.current->state = PROC_BLOCKED;
        sched_schedule();
    }
}

/* 线程唤醒 */
void thread_wake(thread_t *thread) {
    if (!thread) return;
    if (thread->state == PROC_BLOCKED || thread->state == PROC_SLEEPING) {
        thread->state = PROC_READY;
        sched_add_thread(thread);
    }
}

/* 系统调用 */
uint32_t sys_getpid(void) { return sched.current ? sched.current->pid : 0; }
uint32_t sys_gettid(void) { return sched.current ? sched.current->id : 0; }

void sys_exit(int code) {
    (void)code;
    if (sched.current) {
        sched_remove_thread(sched.current);
        sched.current->state = PROC_ZOMBIE;
        sched_schedule();
    }
    while (1) { __asm__ volatile("hlt"); }
}