/* scheduler.c - Preemptive Round-Robin Scheduler */

#include "../include/process.h"
#include "../include/pmm.h"
#include "../include/idt.h"
#include "../include/usermode.h"
#include "../include/serial.h"
#include "../include/vmm.h"
#include "../net/sync.h"
#include "../net/bus.h"
#include "../net/net.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#define SCHED_QUEUE_COUNT 8u
#define THREAD_KERNEL_SP_OFFSET 72u
#define OPENOS_STATIC_ASSERT_CONCAT_(a, b) a##b
#define OPENOS_STATIC_ASSERT_CONCAT(a, b) OPENOS_STATIC_ASSERT_CONCAT_(a, b)
#define OPENOS_STATIC_ASSERT(cond) typedef char OPENOS_STATIC_ASSERT_CONCAT(openos_static_assert_, __LINE__)[(cond) ? 1 : -1]

OPENOS_STATIC_ASSERT((uint32_t)&(((thread_t *)0)->kernel_sp) == THREAD_KERNEL_SP_OFFSET);

void idle_entry(void) { while (1) { __asm__ volatile("hlt"); } }

static struct {
    thread_t *queues[8];
    thread_t *current;
    uint32_t current_ticks;
    uint32_t time_ms;
    int initialized;
    int need_resched;
    uint32_t current_cr3;
    uint64_t context_switches;
} sched = {{0}, 0, 0, 0, 0, 0, 0, 0};

static uint32_t thread_target_cr3(thread_t *t) {
    if (!t) return vmm_kernel_cr3();
    process_t *proc = proc_find(t->pid);
    if (proc && proc->cr3) return proc->cr3;
    return vmm_kernel_cr3();
}

static void sched_switch_cr3_for_thread(thread_t *t) {
    uint32_t target = thread_target_cr3(t);
    uint32_t actual = vmm_get_cr3();
    if (target && target != actual) {
        vmm_load_cr3(target);
        actual = target;
    }
    sched.current_cr3 = actual;
}

void sched_ensure_not_running_cr3(uint32_t cr3) {
    if (!cr3) return;
    if (vmm_get_cr3() == cr3) {
        uint32_t kernel_cr3 = vmm_kernel_cr3();
        vmm_load_cr3(kernel_cr3);
        sched.current_cr3 = kernel_cr3;
    }
}

static uint32_t sched_quantum_for_priority(uint32_t priority) {
    if (priority <= PRIORITY_REALTIME) return 4;
    if (priority <= PRIORITY_HIGH) return QUANTUM_HIGH;
    if (priority <= PRIORITY_NORMAL) return QUANTUM_DEFAULT;
    if (priority <= PRIORITY_LOW) return 15;
    return QUANTUM_LOW;
}

static void sched_refresh_quantum(thread_t *t) {
    if (!t) return;
    t->quantum_total = sched_quantum_for_priority(t->priority);
    if (t->quantum == 0 || t->quantum > t->quantum_total)
        t->quantum = t->quantum_total;
}

static int sched_has_higher_ready(uint32_t priority) {
    uint32_t limit = priority;
    if (limit > SCHED_QUEUE_COUNT) limit = SCHED_QUEUE_COUNT;
    for (uint32_t i = 0; i < limit; i++) {
        if (sched.queues[i]) return 1;
    }
    return 0;
}

static void enqueue(thread_t *t) {
    if (!t) return;
    if (t->priority >= SCHED_QUEUE_COUNT) t->priority = PRIORITY_IDLE;
    sched_refresh_quantum(t);
    int prio = (int)t->priority;
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
    if (prio < 0 || prio >= 8) return;

    thread_t *cur = sched.queues[prio];
    while (cur && cur != t) cur = cur->next;
    if (!cur) return;

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
    sched.time_ms = 0;
    sched.need_resched = 0;
    sched.current_cr3 = vmm_kernel_cr3();
    sched.context_switches = 0;

    /* 创建 idle 线程并加入运行队列 */
    uint32_t idle_stack = (uint32_t)pmm_alloc_page() + 4096;
    thread_t *idle = thread_create(0, "idle", (uint32_t)idle_entry, idle_stack);
    if (idle) {
        idle->priority = PRIORITY_IDLE;
        idle->state = PROC_READY;
        idle->quantum_total = sched_quantum_for_priority(idle->priority);
        idle->quantum = idle->quantum_total;
        enqueue(idle);
        sched.current = idle;
    }

    sched.initialized = 1;
}

void sched_add_thread(thread_t *t) {
    if (!t || t->state == PROC_RUNNING) return;
    t->state = PROC_READY;
    enqueue(t);
    if (sched.current && t->priority < sched.current->priority)
        sched.need_resched = 1;
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

int sched_set_thread_priority(thread_t *thread, uint32_t priority) {
    if (!thread || priority > PRIORITY_IDLE) return -1;

    uint32_t saved_eflags;
    __asm__ volatile("pushfl; pop %0; cli" : "=r"(saved_eflags) :: "memory");

    uint32_t old_priority = thread->priority;
    if (old_priority == priority) {
        __asm__ volatile("push %0; popfl" :: "r"(saved_eflags) : "memory");
        return 0;
    }

    if (thread->state == PROC_READY) {
        remove_from_queue(thread);
        thread->priority = priority;
        thread->quantum_total = sched_quantum_for_priority(priority);
        thread->quantum = thread->quantum_total;
        enqueue(thread);
    } else {
        thread->priority = priority;
        thread->quantum_total = sched_quantum_for_priority(priority);
        if (thread->quantum == 0 || thread->quantum > thread->quantum_total)
            thread->quantum = thread->quantum_total;
    }

    if (thread == sched.current && priority > old_priority) {
        sched.need_resched = 1;
    } else if (thread != sched.current && sched.current && priority < sched.current->priority) {
        sched.need_resched = 1;
    }

    __asm__ volatile("push %0; popfl" :: "r"(saved_eflags) : "memory");
    return 0;
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
    tss_set_kernel_stack(first->kernel_stack_top);
    sched_switch_cr3_for_thread(first);

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
        : : "r"(first->kernel_sp)
    );
}

uint32_t sched_time_ms(void) {
    return sched.time_ms;
}

/* PIT 配置为 100Hz，每次中断对应 10ms。
 * 之前每 tick 只 +1，导致内核时间走得比真实慢 10 倍（1 现实秒 ≈ 0.1 内核秒）。
 * 修复：按 PIT 实际周期累加。 */
#define SCHED_TICK_MS 10u

void sched_tick(void) {
    sync_tick(SCHED_TICK_MS);
    bus_reliable_tick(SCHED_TICK_MS);
    net_tick(SCHED_TICK_MS);
    sched.time_ms += SCHED_TICK_MS;
    proc_check_alarms(sched.time_ms);
    proc_wake_sleepers(sched.time_ms);
    if (!sched.current) return;
    sched.current_ticks++;
    sched.current->total_ticks++;
    if (sched.current->quantum == 0)
        sched.current->quantum = sched.current->quantum_total ? sched.current->quantum_total : sched_quantum_for_priority(sched.current->priority);
    if (sched.current->quantum > 0)
        sched.current->quantum--;
    if (sched.current->quantum == 0 || sched_has_higher_ready(sched.current->priority)) {
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
    if (prev->state == PROC_RUNNING && prev->priority != PRIORITY_IDLE) {
        prev->state = PROC_READY;
        enqueue(prev);
    }

    sched.current = next;
    next->state = PROC_RUNNING;
    next->quantum = next->quantum_total ? next->quantum_total : sched_quantum_for_priority(next->priority);
    sched.current_ticks = 0;
    sched.need_resched = 0;
    sched.context_switches++;
    tss_set_kernel_stack(next->kernel_stack_top);
    sched_switch_cr3_for_thread(next);

    return next;
}

void sched_yield(void) {
    uint32_t saved_eflags;
    __asm__ volatile("pushfl; pop %0; cli" : "=r"(saved_eflags) :: "memory");

#define SCHED_RESTORE_FLAGS_AND_RETURN() \
    do { \
        __asm__ volatile("push %0; popfl" :: "r"(saved_eflags) : "memory"); \
        return; \
    } while (0)

    if (!sched.initialized || !sched.current) SCHED_RESTORE_FLAGS_AND_RETURN();
    if (sched.current->pid)
        proc_handle_pending_signals(sched.current->pid);

    thread_t *next = pick_next();
    if (!next) SCHED_RESTORE_FLAGS_AND_RETURN();
    if (next == sched.current) SCHED_RESTORE_FLAGS_AND_RETURN();

    remove_from_queue(next);

    thread_t *prev = sched.current;
    if (prev->state == PROC_RUNNING && prev->priority != PRIORITY_IDLE) {
        prev->state = PROC_READY;
        enqueue(prev);
    }

    sched.current = next;
    next->state = PROC_RUNNING;
    next->quantum = next->quantum_total ? next->quantum_total : sched_quantum_for_priority(next->priority);
    sched.current_ticks = 0;
    sched.context_switches++;
    tss_set_kernel_stack(next->kernel_stack_top);
    sched_switch_cr3_for_thread(next);

    /* Build a full interrupt-compatible frame:
     * low to high: [EFLAGS][CS][EIP][pushad: EAX..EDI][DS][ES][FS][GS]
     * ESP points at GS.
     *
     * The whole scheduler state update must run with interrupts disabled.
     * Otherwise timer_isr may save the current ESP into the wrong thread after
     * sched.current has been changed but before this function switches stacks. */
    uint32_t *prev_kernel_sp_slot = &prev->kernel_sp;
    uint32_t next_kernel_sp = next->kernel_sp;
    uint32_t resume_eflags = saved_eflags | 0x200;

    __asm__ volatile(
        "pushl %2\n"         /* EFLAGS */
        "pushl $0x08\n"      /* CS */
        "pushl $1f\n"        /* EIP */
        "pushal\n"           /* EAX ECX EDX EBX ESP EBP ESI EDI */
        "push %%ds\n"
        "push %%es\n"
        "push %%fs\n"
        "push %%gs\n"
        "mov %%esp, (%0)\n"  /* save prev->kernel_sp */
        "mov %1, %%esp\n"    /* switch to next->kernel_sp */
        "pop %%gs\n"
        "pop %%fs\n"
        "pop %%es\n"
        "pop %%ds\n"
        "popa\n"
        "iret\n"
        "1:\n"
        :
        : "r"(prev_kernel_sp_slot), "r"(next_kernel_sp), "r"(resume_eflags)
        : "memory"
    );

#undef SCHED_RESTORE_FLAGS_AND_RETURN
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

thread_t *thread_create_sized(uint32_t pid, const char *name, uint32_t entry, uint32_t stack_top, uint32_t stack_size) {
    thread_t *t = (thread_t *)pmm_alloc_page();
    if (!t) return NULL;
    for (int i = 0; i < (int)sizeof(thread_t); i++) ((char *)t)[i] = 0;

    t->id = (uint32_t)t;
    t->pid = pid;
    t->priority = PRIORITY_NORMAL;
    t->quantum_total = sched_quantum_for_priority(t->priority);
    t->quantum = t->quantum_total;
    t->state = PROC_READY;
    (void)name;

    if (stack_size < 4096) stack_size = 4096;
    char *stack_base = (char *)((uint32_t)stack_top - stack_size);
    for (uint32_t i = 0; i < stack_size; i++) stack_base[i] = 0;

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

    t->kernel_sp = (uint32_t)sp;
    t->kernel_ip = entry;
    t->kernel_stack = (uint32_t)stack_base;
    t->kernel_stack_top = stack_top;
    return t;
}

thread_t *thread_create(uint32_t pid, const char *name, uint32_t entry, uint32_t stack_top) {
    return thread_create_sized(pid, name, entry, stack_top, 4096);
}

void thread_sleep(uint32_t ms) {
    if (sched.current) {
        remove_from_queue(sched.current);
        sched.current->wake_time = sched.time_ms + ms;
        sched.current->state = PROC_SLEEPING;
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
    if (sched.current) {
        proc_mark_exit(sched.current->pid, code);
        sched.current->state = PROC_ZOMBIE;
        sched.need_resched = 1;
        sched_yield();
    }
    while (1) { __asm__ volatile("hlt"); }
}
