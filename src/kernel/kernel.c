/**
 * @file kernel.c
 * @brief OpenOS 内核主程序 (Phase 2.5 - 抢占式调度)
 */

#include "types.h"
#include "serial.h"
#include "process.h"
#include "heap.h"
#include "keyboard.h"

/* 外部符号 */
extern void gdt_init(void);
extern void idt_init(void);
extern void pmm_init(uint32_t kernel_end);
extern void vmm_init(void);
extern void sched_init(void);
extern void sched_start(void);
extern void sched_add_thread(thread_t *t);
extern thread_t *thread_create(uint32_t pid, const char *name, uint32_t entry, uint32_t stack_top);
extern void sched_yield(void);

extern uint32_t pmm_alloc_page(void);
extern void serial_write_hex(uint32_t val);
extern void serial_putc(char c);

/* 链接脚本定义的内核结束地址 */
extern char __kernel_end[];

/* 简单延迟 */
static void delay(int count) {
    for (volatile int i = 0; i < count; i++);
}

/* 测试线程 A */
void test_proc_a(void) {
    while (1) {
        serial_write("A");
        delay(50000);
    }
}

/* 测试线程 B */
void test_proc_b(void) {
    while (1) {
        serial_write("B");
        delay(50000);
    }
}

void kernel_main(void) {
    serial_init();
    serial_write("\n[OpenOS] Phase 2.5 - Preemptive Scheduler\n");
    
    /* 初始化硬件 */
    pmm_init((uint32_t)__kernel_end);
    serial_write("[OK] PMM\n");
    
    gdt_init();
    serial_write("[OK] GDT\n");
    
    idt_init();
    serial_write("[OK] IDT\n");
    
    vmm_init();
    serial_write("[OK] VMM\n");
    
    heap_init();
    serial_write("[OK] HEAP\n");
    
    /* 初始化键盘驱动 */
    keyboard_init();
    
    /* 初始化调度器 */
    sched_init();
    
    /* 创建测试线程 */
    uint32_t stack_a = (uint32_t)pmm_alloc_page() + 4096;
    uint32_t stack_b = (uint32_t)pmm_alloc_page() + 4096;
    
    thread_t *ta = thread_create(1, "test_a", (uint32_t)test_proc_a, stack_a);
    thread_t *tb = thread_create(1, "test_b", (uint32_t)test_proc_b, stack_b);
    
    if (ta) { sched_add_thread(ta); }
    if (tb) { sched_add_thread(tb); }
    
    /* 启动调度 */
    sched_start();
    
    /* sched_start 不应该返回 */
    serial_write("[ERR] sched_start returned!\n");
    
    for (;;) {
        __asm__ volatile("hlt");
    }
}
