/**
 * @file kernel.c
 * @brief OpenOS 内核主程序 (Phase 2)
 */

#include "types.h"
#include "serial.h"

/* 外部符号 */
extern void gdt_init(void);
extern void idt_init(void);
extern void pmm_init(uint32_t kernel_end);
extern void vmm_init(void);
extern void sched_init(void);
extern void sched_add_thread(void *t);
extern void sched_yield(void);
extern struct thread *thread_create(uint32_t pid, const char *name, uint32_t entry, uint32_t stack_top);
extern uint32_t pmm_alloc_page(void);

/* 链接脚本定义的内核结束地址 */
extern char __kernel_end[];

/* 测试线程 A */
void test_proc_a(void) {
    while (1) {
        serial_write("A");
        for (volatile int i = 0; i < 50000; i++);
        serial_write("<");
        sched_yield();
        serial_write(">");
    }
}

/* 测试线程 B */
void test_proc_b(void) {
    while (1) {
        serial_write("B");
        for (volatile int i = 0; i < 50000; i++);
        serial_write("{");
        sched_yield();
        serial_write("}");
    }
}

void kernel_main(void) {
    serial_init();
    serial_write("\n[OpenOS] Phase 2 - Scheduler Test\n");
    
    /* 1. 初始化物理内存管理 */
    pmm_init((uint32_t)__kernel_end);
    serial_write("[OK] PMM initialized\n");
    
    /* 2. 初始化 GDT */
    gdt_init();
    serial_write("[OK] GDT initialized\n");
    
    /* 3. 初始化 IDT */
    idt_init();
    serial_write("[OK] IDT initialized\n");
    
    /* 4. 初始化虚拟内存管理 */
    vmm_init();
    serial_write("[OK] VMM initialized\n");
    
    /* 5. 初始化调度器 */
    sched_init();
    serial_write("[OK] Scheduler initialized\n");
    
    /* 6. 创建测试线程 */
    uint32_t stack_a = (uint32_t)pmm_alloc_page() + 4096;
    uint32_t stack_b = (uint32_t)pmm_alloc_page() + 4096;
    
    serial_write("[INFO] Creating threads...\n");
    
    struct thread *ta = thread_create(1, "test_a", (uint32_t)test_proc_a, stack_a);
    struct thread *tb = thread_create(1, "test_b", (uint32_t)test_proc_b, stack_b);
    
    if (ta) { sched_add_thread(ta); serial_write("[OK] Thread A created\n"); }
    else { serial_write("[ERR] Thread A create failed\n"); }
    
    if (tb) { sched_add_thread(tb); serial_write("[OK] Thread B created\n"); }
    else { serial_write("[ERR] Thread B create failed\n"); }
    
    /* 7. 启用中断 */
    serial_write("[OK] Enabling interrupts...\n");
    __asm__ volatile("sti");
    
    serial_write("[OK] System running\n");
    
    for (;;) {
        __asm__ volatile("hlt");
    }
}
