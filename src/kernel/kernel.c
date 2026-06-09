/**
 * @file kernel.c
 * @brief OpenOS 内核主程序 (Phase 2.5 - 抢占式调度)
 */

#include "types.h"
#include "../include/syscall.h"
#include "../include/usermode.h"
#include "serial.h"
#include "process.h"
#include "../proc/process.h"
#include "../fs/vfs.h"
#include "../fs/ramfs.h"
#include "../fs/tmpfs.h"
#include "../net/net.h"
#include "../shell.h"
#include "heap.h"
#include "keyboard.h"
#include "chardev.h"
#include "blockdev.h"
#include "vga.h"
#include "embed_hello.h"  /* 嵌入的用户程序 */

/* 外部符号 */
extern void gdt_init(void);
extern void idt_init(void);
extern void pmm_init(uint32_t kernel_end);
extern void vmm_init(void);
extern void sched_init(void);
extern void tss_init(uint32_t esp0);
extern void tss_flush(void);
extern void pit_start(void);
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
    
    /* 初始化所有段寄存器 (防止 FS/GS 为无效值) */
    __asm__ volatile (
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        : : : "ax"
    );
    
    /* 初始化并加载 TSS (修复 invalid tss type) */
    extern uint32_t kernel_stack_top[];
    tss_init((uint32_t)kernel_stack_top);
    extern void tss_flush(void);
    tss_flush();
    serial_write("[OK] TSS\n");
    
    idt_init();
    serial_write("[OK] IDT\n");
    
    vmm_init();
    serial_write("[OK] VMM\n");
    
    heap_init();
    serial_write("[OK] HEAP\n");
    
    /* 初始化键盘驱动 */
    keyboard_init();
    
    /* 初始化VGA控制台 */
    vga_init();
    
    /* 测试系统调用接口 (int 0x80) */
    serial_write("[TEST] Testing syscall interface (int 0x80)...\n");
    serial_write("[TEST] Calling SYS_GETPID...\n");
    
    /* 调用系统调用：SYS_GETPID (EAX=20) */
    uint32_t pid = 0;
    asm volatile("int $0x80" : "=a"(pid) : "a"(SYS_GETPID));
    
    serial_write("[TEST] SYS_GETPID returned: ");
    serial_write_hex(pid);
    serial_write("\n");
    serial_write("[TEST] Syscall test done\n");
    
    /* 初始化进程表 */
    proc_table_init();
    serial_write("[OK] PROC TABLE\n");

    /* 初始化 VFS + ramfs + tmpfs */
    vfs_init();
    ramfs_init();
    tmpfs_init();
    serial_write("[OK] VFS + ramfs + tmpfs\n");

    /* 初始化字符设备框架与 /dev 节点 */
    chardev_init();
    chardev_register_builtin_devices();
    serial_write("[OK] CHARDEV + /dev\n");

    /* 初始化块设备框架与 RAM disk */
    blockdev_init();
    blockdev_register_builtin_devices();
    serial_write("[OK] BLOCKDEV + ram0\n");

    /* 初始化最小 TCP/IP 网络栈 */
    net_init();
    serial_write("[OK] NET\n");

    /* VFS 测试：不要预创建 /tmp，避免 shell 手动 mkdir /tmp 时误报失败 */
    int fd = vfs_open("/hello.txt", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(fd, "Hello from openos VFS!", 22);
        vfs_seek(fd, 0, SEEK_SET);
        char buf[64];
        int n = vfs_read(fd, buf, 63);
        if (n > 0) {
            buf[n] = '\0';
            serial_write("[VFS] Read back: ");
            serial_write(buf);
            serial_write("\n");
        }
    vfs_close(fd);
    }
    serial_write("[OK] VFS TEST\n");

    /* 写入嵌入的用户程序到 ramfs */
    fd = vfs_open("/hello.elf", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)hello_elf, hello_elf_size);
        vfs_close(fd);
        serial_write("[OK] Embedded hello.elf\n");

    /* 调试：检查 ramfs_file_ops 内容 */
    {
        extern file_ops_t ramfs_file_ops;
        serial_write("[DEBUG] ramfs_file_ops addr=0x");
        serial_write_hex((uint32_t)&ramfs_file_ops);
        serial_write(" open=0x");
        serial_write_hex((uint32_t)ramfs_file_ops.open);
        serial_write(" read=0x");
        serial_write_hex((uint32_t)ramfs_file_ops.read);
        serial_write(" write=0x");
        serial_write_hex((uint32_t)ramfs_file_ops.write);
        serial_write("\n");
    }
    }

    /* 初始化调度器 */
    sched_init();
    
    /* 启动定时器（调度器已初始化） */
    pit_start();
    
    /* ⚠️ 不在此处开启中断！
     * 必须等 sched_start() 通过 iret 启动第一个线程后，
     * 由线程初始 EFLAGS (IF=1) 自动开启中断。
     * 如果提前 sti，timer ISR 会把 0x90000 栈指针保存到 idle->kernel_esp，
     * 导致后续切换到 idle 时栈帧被破坏 → GPF。
     */
    
    /* 测试用户态切换已通过 Phase 2 验证，不再自动测试 */
    /* test_user_mode_switch(); */
    
    /* 启动 shell 作为内核线程 - 16KB 栈 */
    {
        extern void shell_run(void);
        uint32_t shell_stack = (uint32_t)pmm_alloc_page() + 4096;
        pmm_alloc_page(); pmm_alloc_page(); pmm_alloc_page(); /* 扩展到 16KB */
        thread_t *sh = thread_create(1, "shell", (uint32_t)shell_run, shell_stack);
        if (sh) sched_add_thread(sh);
    }

    /* 自动测试 ELF 加载 - 已禁用，避免干扰键盘输入
    {
        extern int sys_exec(const char *path, char *const argv[]);
        void test_exec_thread(void) {
            serial_write("[TEST-EXEC] Calling sys_exec...\n");
            int r = sys_exec("/hello.elf", (char *const[]){NULL});
            serial_write("[TEST-EXEC] sys_exec returned: ");
            serial_write_hex(r);
            serial_write("\n");
        }
        uint32_t exec_stack = (uint32_t)pmm_alloc_page() + 4096;
        thread_t *et = thread_create(1, "test_exec", (uint32_t)test_exec_thread, exec_stack);
        if (et) sched_add_thread(et);
    }
    */
    
    /* 创建测试线程 - 暂时注释掉，避免输出干扰
    uint32_t stack_a = (uint32_t)pmm_alloc_page() + 4096;
    uint32_t stack_b = (uint32_t)pmm_alloc_page() + 4096;
    
    thread_t *ta = thread_create(1, "test_a", (uint32_t)test_proc_a, stack_a);
    thread_t *tb = thread_create(1, "test_b", (uint32_t)test_proc_b, stack_b);
    
    if (ta) { sched_add_thread(ta); }
    if (tb) { sched_add_thread(tb);
    */
    
    /* 启动调度 - sti 由 iret 自动完成，不提前开启 */
    serial_write("[KERNEL] Starting scheduler and enabling interrupts...\n");
    
    sched_start();
    
    /* sched_start 不应该返回 */
    serial_write("[ERR] sched_start returned!\n");
    
    for (;;) {
        __asm__ volatile("hlt");
    }
}
