/* scheduler.c - Preemptive Round-Robin Scheduler */

#include "../include/process.h"
#include "../include/pmm.h"
#include "../include/idt.h"
#include "../include/serial.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

void idle_entry(void) { while (1) { __asm__ volatile("hlt"); } }

static struct {
    thread_t *queues[8];
    thread_t *current;
    uint32_t current_ticks;
    int initialized;
    int need_resched;
} sched = {{0}, 0, 0, 0, 0};

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

static void remove_from_queue(thread_t *t) {
    if (!t) return;
    int prio = t->priority;
    if (t->prev) t->prev->next = t->next;
    else sched.queues[prio] = t->next;
    if (t->next) t->next->prev = t->prev;
    t->next = NULL;
    t->prev = NULL;
}

static thread_t *pick_next(void) {
    for (int i = 0; i < 8; i++) {
        if (sched.queues[i]) return sched.queues[i];
    }
    return NULL;
}

void sched_init(void) {
    for (int i = 0; i < 8; i++) sched.queues[i] = NULL;
    sched.current = NULL;
    sched.current_ticks = 0;
    sched.need_resched = 0;

    /* 创建 idle 线程并加入运行队列 */
    uint32_t idle_stack = (uint32_t)pmm_alloc_page() + 4096;
    thread_t *idle = thread_create(0, "idle", (uint32_t)idle_entry, idle_stack);
    if (idle) {
        idle->priority = PRIORITY_IDLE;
        idle->state = PROC_READY;
        enqueue(idle);
        sched.current = idle;
    }

    sched.initialized = 1;
}

void sched_add_thread(thread_t *t) {
    if (!t || t->state == PROC_RUNNING) return;
    t->state = PROC_READY;
    enqueue(t);
}

void sched_remove_thread(thread_t *t) {
    remove_from_queue(t);
}

thread_t *sched_get_current(void) {
    return sched.current;
}

int sched_need_resched(void) {
    return sched.need_resched;
}

void sched_set_need_resched(int need) {
    sched.need_resched = need;
}

void sched_start(void) {
    thread_t *first = pick_next();
    if (!first) {
        serial_write("[SCHED] No threads!\n");
        return;
    }

    remove_from_queue(first);
    sched.current = first;
    first->state = PROC_RUNNING;
    sched.current_ticks = 0;

    serial_write("[SCHED] Starting first thread\n");

    __asm__ volatile(
        "cli\n"
        "mov %0, %%esp\n"
        "pop %%gs\n"
        "pop %%fs\n"
        "pop %%es\n"
        "pop %%ds\n"
        "popa\n"
        "iret\n"
        : : "r"(first->kernel_esp)
    );
}

void sched_tick(void) {
    if (!sched.current) return;
    sched.current_ticks++;
    if (sched.current_ticks >= 10) {
        sched.current_ticks = 0;
        sched.need_resched = 1;
    }
}

thread_t *timer_schedule_handler(void) {
    if (!sched.initialized || !sched.current) {
        serial_write("[TMR-ERR]\n");
        return NULL;
    }
    if (!sched.need_resched) return NULL;

    thread_t *next = pick_next();
    if (!next || next == sched.current) {
        sched.need_resched = 0;
        return NULL;
    }

    remove_from_queue(next);

    thread_t *prev = sched.current;
    if (prev->priority != PRIORITY_IDLE) {
        prev->state = PROC_READY;
        enqueue(prev);
    }

    sched.current = next;
    next->state = PROC_RUNNING;
    sched.current_ticks = 0;
    sched.need_resched = 0;

    return next;
}

void sched_yield(void) {
    if (!sched.initialized || !sched.current) return;

    thread_t *next = pick_next();
    if (!next) return;
    if (next == sched.current) return;

    remove_from_queue(next);

    thread_t *prev = sched.current;
    if (prev->priority != PRIORITY_IDLE) {
        prev->state = PROC_READY;
        enqueue(prev);
    }

    sched.current = next;
    next->state = PROC_RUNNING;
    sched.current_ticks = 0;

    /* 构建完整栈帧，格式与 timer_isr + thread_create 一致：
     * 从低到高: [EFLAGS][CS][EIP][pushad: EAX..EDI][DS][ES][FS][GS]
     * ESP 指向 GS（栈顶）
     * 必须关中断防止 timer_isr 在切换中途触发嵌套上下文切换 */
    __asm__ volatile(
        "cli\n"
        "pushfl\n"          /* EFLAGS */
        "pushl $0x08\n"     /* CS */
        "pushl $1f\n"       /* EIP */
        "pushal\n"          /* EAX ECX EDX EBX ESP EBP ESI EDI */
        "push %%ds\n"
        "push %%es\n"
        "push %%fs\n"
        "push %%gs\n"
        "mov %%esp, %0\n"   /* 保存到 prev->kernel_esp */
        "mov %1, %%esp\n"   /* 切换到 next->kernel_esp */
        "pop %%gs\n"
        "pop %%fs\n"
        "pop %%es\n"
        "pop %%ds\n"
        "popa\n"
        "iret\n"
        "1:\n"
        : "=m"(prev->kernel_esp)
        : "m"(next->kernel_esp)
    );
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
    t->priority = PRIORITY_NORMAL;
    t->state = PROC_READY;
    (void)name;

    char *stack_base = (char *)((uint32_t)stack_top - 4096);
    for (int i = 0; i < 4096; i++) stack_base[i] = 0;

    /* Stack frame matching timer_isr save format:
     * [GS][FS][ES][DS][EDI][ESI][EBP][ESP_skip][EBX][EDX][ECX][EAX][EIP][CS][EFLAGS]
     */
    uint32_t *sp = (uint32_t *)stack_top;

    *(--sp) = 0x202;     /* EFLAGS: IF=1 */
    *(--sp) = 0x08;      /* CS */
    *(--sp) = entry;     /* EIP */
    *(--sp) = 0;          /* EAX */
    *(--sp) = 0;          /* ECX */
    *(--sp) = 0;          /* EDX */
    *(--sp) = 0;          /* EBX */
    *(--sp) = 0;          /* ESP (skip) */
    *(--sp) = 0;          /* EBP */
    *(--sp) = 0;          /* ESI */
    *(--sp) = 0;          /* EDI */
    *(--sp) = 0x10;       /* DS */
    *(--sp) = 0x10;       /* ES */
    *(--sp) = 0x10;       /* FS */
    *(--sp) = 0x10;       /* GS */

    t->kernel_esp = (uint32_t)sp;
    t->kernel_eip = entry;
    t->kernel_stack = (uint32_t)stack_base;
    t->kernel_stack_top = stack_top;
    return t;
}

void thread_sleep(uint32_t ms) {
    (void)ms;
    if (sched.current) {
        remove_from_queue(sched.current);
        sched.current->state = PROC_BLOCKED;
        sched.need_resched = 1;
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
        sched.need_resched = 1;
    }
    while (1) { __asm__ volatile("hlt"); }
}
