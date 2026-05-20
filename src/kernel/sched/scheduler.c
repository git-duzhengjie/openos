/* ============================================================
 * openos - 多级反馈队列调度器实现
 * ============================================================ */

#include "include/process.h"

/* 调度器状态 */
static struct {
    thread_t *queues[8];       /* 8个优先级队列 */
    thread_t *current;         /* 当前线程 */
    uint32_t current_ticks;    /* 当前时间片已用tick */
    int initialized;
} sched = {{0}};

#define SCHED_TICKS_PER_QUANTUM 10

/* VGA */
#define VGA ((volatile uint16_t *)0xB8000)
static void putc(char c, int x, int y, uint16_t attr) {
    if (c == '\n') { x = 0; y++; }
    else { VGA[y * 80 + x] = (attr << 8) | c; x++; }
    if (x >= 80) { x = 0; y++; }
    (void)y;
}

static void dbg_str(const char *s, int *px, int *py) {
    while (*s) { putc(*s++, (*px)++, *py, 0x07); }
}

/* ============================================================
 * 初始化调度器
 * ============================================================ */
void sched_init(void)
{
    for (int i = 0; i < 8; i++)
        sched.queues[i] = NULL;

    sched.current = NULL;
    sched.current_ticks = 0;
    sched.initialized = 1;

    /* 创建 idle 线程 */
    thread_t *idle = thread_create(0, "idle", (uint32_t)idle_entry, 0);
    if (idle) {
        idle->priority = PRIORITY_IDLE;
        sched_add_thread(idle);
    }
}

void idle_entry(void) {
    while (1) __asm__ volatile("hlt");
}

/* ============================================================
 * 开始调度 (在 kernel_main 中调用)
 * ============================================================ */
void sched_start(void)
{
    if (!sched.initialized) sched_init();
    /* 手动触发第一次调度 */
    sched_schedule();
}

/* ============================================================
 * 添加线程到就绪队列
 * ============================================================ */
void sched_add_thread(thread_t *t)
{
    if (!t) return;

    int prio = t->priority;
    if (prio > 7) prio = 7;

    t->next = sched.queues[prio];
    t->prev = NULL;
    if (sched.queues[prio])
        sched.queues[prio]->prev = t;
    sched.queues[prio] = t;
    t->state = PROC_READY;
}

/* ============================================================
 * 从队列移除线程
 * ============================================================ */
void sched_remove_thread(thread_t *t)
{
    if (!t) return;

    if (t->prev) t->prev->next = t->next;
    else {
        int prio = t->priority > 7 ? 7 : t->priority;
        sched.queues[prio] = t->next;
    }
    if (t->next) t->next->prev = t->prev;
    t->next = t->prev = NULL;
}

/* ============================================================
 * 查找最高优先级非空队列
 * ============================================================ */
static thread_t *pick_next(void)
{
    for (int i = 0; i < 8; i++) {
        if (sched.queues[i]) return sched.queues[i];
    }
    return NULL;
}

/* ============================================================
 * 调度器主体
 * ============================================================ */
void sched_schedule(void)
{
    thread_t *current = sched.current;
    thread_t *next = pick_next();

    /* 没有可运行线程 */
    if (!next) {
        if (!current) {
            /* 放一个空转线程 */
            return;
        }
        /* 继续运行当前 */
        return;
    }

    /* 如果当前线程优先级 > next，提升其优先级（正向提升） */
    if (current && current->state == PROC_RUNNING) {
        current->state = PROC_READY;
        /* 时间片用完，降级 */
        if (current->priority < 7) {
            current->priority++;
        }
        sched_add_thread(current);
    }

    /* 切换到 next */
    sched.current = next;
    sched.current_ticks = 0;
    next->state = PROC_RUNNING;

    /* 从队列摘下 */
    if (next->prev) next->prev->next = next->next;
    else {
        int p = next->priority > 7 ? 7 : next->priority;
        sched.queues[p] = next->next;
    }
    if (next->next) next->next->prev = next->prev;
    next->next = next->prev = NULL;

    /* 恢复执行 */
    if (current && current != next) {
        /* context switch 触发 */
        context_switch(current, next);
    } else if (current == next && current) {
        /* 同一线程时间片重置 */
    }
}

/* ============================================================
 * 时间片递减 (每 PIT tick 调用)
 * ============================================================ */
void sched_tick(void)
{
    sched.current_ticks++;

    thread_t *curr = sched.current;
    if (!curr) {
        sched_schedule();
        return;
    }

    /* 降级策略: 用完一个时间片则降低优先级(优先级数字+1=更低) */
    if (sched.current_ticks >= SCHED_TICKS_PER_QUANTUM) {
        if (curr->priority < 7) {
            curr->priority++;
        }
        /* 设置下一个时间片长度 */
        curr->quantum = (curr->priority >= 5) ? QUANTUM_LOW : QUANTUM_DEFAULT;

        /* 放入就绪队列，触发调度 */
        sched_add_thread(curr);
        sched.current = NULL;
        sched_schedule();
    }
}

/* ============================================================
 * 获取当前线程
 * ============================================================ */
thread_t *sched_get_current(void)
{
    return sched.current;
}

/* ============================================================
 * 切换到目标线程 (由汇编调用)
 * ============================================================ */
void sched_switch_to(thread_t *next)
{
    (void)next;
}

/* ============================================================
 * 创建新进程
 * ============================================================ */
process_t *proc_create(const char *name, uint32_t entry, uint32_t esp)
{
    (void)name; (void)entry; (void)esp;
    static uint32_t next_pid = 1;
    process_t *p = (process_t *)pmm_alloc_page();
    if (!p) return NULL;

    p->pid = next_pid++;
    for (int i = 0; i < 31 && name[i]; i++)
        p->name[i] = name[i];
    p->name[31] = '\0';
    p->state = PROC_RUNNING;
    p->threads = NULL;
    p->thread_count = 0;
    p->total_ticks = 0;

    return p;
}

/* ============================================================
 * 创建新线程
 * ============================================================ */
thread_t *thread_create(uint32_t pid, const char *name,
                        uint32_t entry, uint32_t stack_top)
{
    static uint32_t next_tid = 1;

    thread_t *t = (thread_t *)pmm_alloc_page();
    if (!t) return NULL;

    t->id = next_tid++;
    t->pid = pid;
    t->state = PROC_READY;
    t->priority = PRIORITY_NORMAL;
    t->quantum = QUANTUM_DEFAULT;
    t->quantum_total = QUANTUM_DEFAULT;
    t->wake_time = 0;
    t->next = t->prev = NULL;

    /* 初始化寄存器上下文 */
    t->eax = t->ebx = t->ecx = t->edx = 0;
    t->esi = t->edi = t->ebp = 0;
    t->eip = entry;
    t->esp = stack_top ? stack_top : ((uint32_t)pmm_alloc_page() + 4096);
    t->eflags = 0x200;  /* IF=1 */
    t->cs = 0x08;       /* 内核代码段 */
    t->ss = 0x10;       /* 内核数据段 */
    t->ds = t->es = t->fs = t->gs = 0x10;

    /* 内核栈 */
    t->kernel_stack = t->esp;
    t->kernel_stack_top = t->kernel_stack + PROC_KERNEL_STACK_SIZE;

    for (int i = 0; i < 31 && name[i]; i++)
        ((char *)t)[offsetof(thread_t, kernel_esp) + i] = 0;
    (void)name;

    return t;
}

/* ============================================================
 * 睡眠
 * ============================================================ */
void thread_sleep(uint32_t ms)
{
    thread_t *t = sched_get_current();
    if (!t) return;
    t->state = PROC_SLEEPING;
    t->wake_time = ms;  /* 简化: 后续加tick计数器 */
    sched_remove_thread(t);
    sched_schedule();
}

/* ============================================================
 * 唤醒
 * ============================================================ */
void thread_wake(thread_t *t)
{
    if (!t) return;
    t->state = PROC_READY;
    sched_add_thread(t);
}

/* ============================================================
 * 系统调用实现
 * ============================================================ */
uint32_t sys_getpid(void)
{
    thread_t *t = sched_get_current();
    return t ? t->pid : 0;
}

uint32_t sys_gettid(void)
{
    thread_t *t = sched_get_current();
    return t ? t->id : 0;
}

void sys_exit(int code)
{
    thread_t *t = sched_get_current();
    if (!t) return;
    (void)code;
    t->state = PROC_DEAD;
    sched_remove_thread(t);
    sched_schedule();
    while (1) __asm__ volatile("hlt");
}