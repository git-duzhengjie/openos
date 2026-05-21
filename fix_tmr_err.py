#!/usr/bin/env python3
# 修复 scheduler.c: 将 sched.initialized = 1 移到 sched_init() 开头

file_path = '/mnt/e/openos/src/kernel/sched/scheduler.c'

with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

# 修复 sched_init(): 将 initialized = 1 移到开头
old_init = """void sched_init(void) {
    for (int i = 0; i < 8; i++) sched.queues[i] = NULL;
    sched.current = NULL;
    sched.current_ticks = 0;
    sched.need_resched = 0;

    /* 创建 idle 线程并加入运行队列 */
    uint32_t idle_stack = (uint32_t)pmm_alloc_page() + 4096;
    thread_t *idle = thread_create(0, "idle", (uint32_t)idle_entry, idle_stack);
    if (idle) {
        idle->priority = PRIORITY_IDLE;
        idle->state = PROC_READ_Y;
        enqueue(idle);
    }
    
    sched.initialized = 1;
}"""

new_init = """void sched_init(void) {
    /* 最先设置 initialized = 1，防止定时器中断在初始化前触发 */
    sched.initialized = 1;
    
    for (int i = 0; i < 8; i++) sched.queues[i] = NULL;
    sched.current = NULL;
    sched.current_ticks = 0;
    sched.need_resched = 0;

    /* 创建 idle 线程并加入运行队列 */
    uint32_t idle_stack = (uint32_t)pmm_alloc_page() + 4096;
    thread_t *idle = thread_create(0, "idle", (uint32_t)idle_entry, idle_stack);
    if (idle) {
        idle->priority = PRIORITY_IDLE;
        idle->state = PROC_READ_Y;
        enqueue(idle);
    }
}"""

content = content.replace(old_init, new_init)

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(content)

print('scheduler.c fixed: sched.initialized = 1 moved to start of sched_init()')
