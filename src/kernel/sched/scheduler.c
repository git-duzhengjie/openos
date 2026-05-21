/* ============================================================
 * openos - 多级反馈队列调度器
 * ============================================================ */

#include "../include/process.h"
#include "../include/pmm.h"
#include "../include/idt.h"
#include "../include/serial.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

void idle_entry(void) { while (1) { __asm__ volatile("hlt"); } }
extern void context_switch(thread_t *prev, thread_t *next);

static struct {
    thread_t *queues[8];
    thread_t *current;
    uint32_t current_ticks;
    int initialized;
} sched = {{0}, 0, 0, 0};

static void timer_handler(registers_t *regs) {
    (void)regs;
    serial_write("T");
    sched_tick();
}

void sched_init(void) {
    for (int i = 0; i < 8; i++) sched.queues[i] = NULL;
    sched.current = NULL;
    sched.current_ticks = 0;
    sched.initialized = 1;

    isr_install_handler(32, timer_handler);

    /* 创建idle但不加入队列 - 只在没有其他线程时使用 */
    uint32_t idle_stack = (uint32_t)pmm_alloc_page() + 4096;
    thread_t *idle = thread_create(0, "idle", (uint32_t)idle_entry, idle_stack);
    if (idle) {
        idle->priority = PRIORITY_IDLE;  /* 7 */
        idle->state = PROC_RUNNING;
        sched.current = idle;  /* idle作为初始current，但不在队列里 */
        serial_write("[INIT] idle=current (not in queue)\n");
    }
}

/* 内部函数：加入队列尾部 */
static void enqueue(thread_t *t) {
    int prio = t->priority;
    t->next = NULL;
    t->prev = NULL;
    
    if (!sched.queues[prio]) {
        sched.queues[prio] = t;
    } else {
        thread_t *h = sched.queues[prio];
        while (h->next) h = h->next;
        h->next = t;
        t->prev = h;
    }
}

/* 内部函数：从队列头部取出并移除 */
static thread_t *dequeue_head(int prio) {
    thread_t *t = sched.queues[prio];
    if (t) {
        sched.queues[prio] = t->next;
        if (t->next) t->next->prev = NULL;
        t->next = NULL;
        t->prev = NULL;
    }
    return t;
}

/* 内部函数：从队列移除指定线程 */
static void remove_from_queue(thread_t *t) {
    if (!t) return;
    int prio = t->priority;
    if (t->prev) t->prev->next = t->next;
    else sched.queues[prio] = t->next;
    if (t->next) t->next->prev = t->prev;
    t->next = NULL;
    t->prev = NULL;
}

void sched_add_thread(thread_t *t) {
    if (!t || t->state == PROC_RUNNING) return;
    t->state = PROC_READY;
    enqueue(t);
    serial_write("[ADD] thread added\n");
}

void sched_remove_thread(thread_t *t) {
    remove_from_queue(t);
}

/* 从优先级0-7找第一个就绪线程 */
static thread_t *pick_next(void) {
    for (int i = 0; i < 8; i++) {
        if (sched.queues[i]) {
            return sched.queues[i];
        }
    }
    return NULL;
}

void sched_schedule(void) {
    if (!sched.initialized || !sched.current) return;
    
    thread_t *next = pick_next();
    if (!next || next == sched.current) return;
    
    /* 从队列移除next */
    remove_from_queue(next);
    
    /* 当前线程重新入队 */
    thread_t *prev = sched.current;
    if (prev->priority != PRIORITY_IDLE) {  /* idle不加入队列 */
        prev->state = PROC_READY;
        enqueue(prev);
    }
    
    /* 切换 */
    sched.current = next;
    next->state = PROC_RUNNING;
    sched.current_ticks = 0;
    
    context_switch(prev, next);
}

void sched_tick(void) {
    if (!sched.current) return;
    sched.current_ticks++;
    if (sched.current_ticks >= 10) {
        sched.current_ticks = 0;
        sched_schedule();
    }
}

void sched_yield(void) {
    if (!sched.initialized || !sched.current) return;
    
    thread_t *next = pick_next();
    if (!next) return;  /* 没有其他线程 */
    
    if (next == sched.current) {
        serial_write("[YIELD] only current in queue\n");
        return;
    }
    
    /* 从队列移除next */
    remove_from_queue(next);
    
    /* 当前线程入队（除非是idle） */
    thread_t *prev = sched.current;
    if (prev->priority != PRIORITY_IDLE) {
        prev->state = PROC_READY;
        enqueue(prev);
    }
    
    /* 切换 */
    sched.current = next;
    next->state = PROC_RUNNING;
    sched.current_ticks = 0;
    
    serial_write("[YIELD] switched\n");
    context_switch(prev, next);
}

thread_t *sched_get_current(void) { return sched.current; }

void sched_switch_to(thread_t *next) {
    if (!next || next == sched.current) return;
    thread_t *prev = sched.current;
    sched.current = next;
    context_switch(prev, next);
}

process_t *proc_create(const char *name, uint32_t entry, uint32_t esp) {
    (void)entry; (void)esp;
    process_t *p = (process_t *)pmm_alloc_page();
    if (!p) return NULL;
    for (int i = 0; i < (int)sizeof(process_t); i++) ((char *)p)[i] = 0;
    p->pid = (uint32_t)p;
    p->state = PROC_RUNNING;
    p->cr3 = 0;
    for (int i = 0; name[i] && i < 31; i++) p->name[i] = name[i];
    return p;
}

thread_t *thread_create(uint32_t pid, const char *name, uint32_t entry, uint32_t stack_top) {
    thread_t *t = (thread_t *)pmm_alloc_page();
    if (!t) return NULL;
    for (int i = 0; i < (int)sizeof(thread_t); i++) ((char *)t)[i] = 0;
    
    t->id = (uint32_t)t;
    t->pid = pid;
    t->priority = PRIORITY_NORMAL;  /* 3 */
    t->state = PROC_READY;
    
    /* 清空栈 */
    char *stack_base = (char *)((uint32_t)stack_top - 4096);
    for (int i = 0; i < 4096; i++) stack_base[i] = 0;
    
    /* 栈帧：pushad + EFLAGS + EIP */
    uint32_t *sp = (uint32_t *)stack_top;
    *(--sp) = entry;      /* EIP */
    *(--sp) = 0x202;     /* EFLAGS (IF=1) */
    *(--sp) = 0;         /* EAX */
    *(--sp) = 0;         /* ECX */
    *(--sp) = 0;         /* EDX */
    *(--sp) = 0;         /* EBX */
    *(--sp) = 0;         /* ESP (skip) */
    *(--sp) = 0;         /* EBP */
    *(--sp) = 0;         /* ESI */
    *(--sp) = 0;         /* EDI */
    
    t->kernel_esp = (uint32_t)sp;
    t->kernel_eip = entry;
    t->kernel_stack = (uint32_t)stack_base;
    t->kernel_stack_top = stack_top;
    (void)name;
    return t;
}

void thread_sleep(uint32_t ms) {
    (void)ms;
    if (sched.current) {
        remove_from_queue(sched.current);
        sched.current->state = PROC_BLOCKED;
        sched_schedule();
    }
}

void thread_wake(thread_t *thread) {
    if (!thread) return;
    if (thread->state == PROC_BLOCKED || thread->state == PROC_SLEEPING) {
        thread->state = PROC_READY;
        enqueue(thread);
    }
}

uint32_t sys_getpid(void) { return sched.current ? sched.current->pid : 0; }
uint32_t sys_gettid(void) { return sched.current ? sched.current->id : 0; }

void sys_exit(int code) {
    (void)code;
    if (sched.current) {
        remove_from_queue(sched.current);
        sched.current->state = PROC_ZOMBIE;
        sched_schedule();
    }
    while (1) { __asm__ volatile("hlt"); }
}
