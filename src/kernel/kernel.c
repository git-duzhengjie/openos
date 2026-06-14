/**
 * @file kernel.c
 * @brief OpenOS 内核主程�?(Phase 2.5 - 抢占式调�?
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
#include "../net/discovery.h"
#include "../net/sync.h"
#include "../net/bus.h"
#include "ai.h"
#include "devmgr.h"
#include "../shell.h"
#include "heap.h"
#include "keyboard.h"
#include "chardev.h"
#include "blockdev.h"
#include "vga.h"
#include "framebuffer.h"
#include "gui.h"
#include "mouse.h"
#include "usb_tablet.h"
#include "pmm.h"
#include "embed_hello.h"  /* 嵌入的用户程�?*/
#include "embed_fault.h"  /* 用户异常隔离测试程序 */

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

extern void serial_write_hex(uint32_t val);
extern void serial_putc(char c);

/* 链接脚本定义的内核结束地址 */
extern char __kernel_end[];

/* 简单延�?*/
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

static int g_shell_thread_started = 0;

#define KERNEL_UI_THREAD_STACK_PAGES 4u
#define KERNEL_UI_THREAD_STACK_SIZE  (KERNEL_UI_THREAD_STACK_PAGES * 4096u)

static uint32_t kernel_alloc_stack_pages(uint32_t pages) {
    void *base;

    if (pages == 0) pages = 1;
    base = pmm_alloc_pages(pages);
    if (!base) return 0;

    return (uint32_t)base + pages * 4096u;
}

void kernel_start_shell_thread(void) {
    extern void shell_run(void);
    uint32_t shell_stack;
    thread_t *sh;

    if (g_shell_thread_started) return;

    shell_stack = kernel_alloc_stack_pages(KERNEL_UI_THREAD_STACK_PAGES);
    if (!shell_stack) {
        serial_write("[SHELL] Failed to allocate shell stack.\n");
        return;
    }

    sh = thread_create_sized(1, "shell", (uint32_t)shell_run, shell_stack, KERNEL_UI_THREAD_STACK_SIZE);
    if (sh) {
        sh->priority = PRIORITY_LOW;
        g_shell_thread_started = 1;
        sched_add_thread(sh);
        serial_write("[SHELL] Shell thread started on demand.\n");
    } else {
        serial_write("[SHELL] Failed to create shell thread.\n");
    }
}

static void desktop_thread(void) {
    serial_write("[GUI] Starting graphical desktop...\n");
    if (gui_start_desktop() != 0) {
        serial_write("[GUI] Failed to start graphical desktop; starting shell fallback.\n");
        kernel_start_shell_thread();
        while (1) sched_yield();
    }

    while (1) {
        gui_poll();
        sched_yield();
    }
}

void kernel_main(void) {
    serial_init();
    serial_write("\n[OpenOS] Phase 2.5 - Preemptive Scheduler\n");
    
    /* 初始化硬�?*/
    pmm_init((uint32_t)__kernel_end);
    serial_write("[OK] PMM\n");
    
    gdt_init();
    serial_write("[OK] GDT\n");
    
    /* 初始化所有段寄存�?(防止 FS/GS 为无效�? */
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
    
    /* 初始化键盘驱�?*/
    keyboard_init();

    /* 初始化 PS/2 鼠标驱动 */
    mouse_init();

    /* 初始化 QEMU USB Tablet 绝对坐标鼠标；失败时自动回退 PS/2 */
    usb_tablet_init();
    
    /* 初始化VGA控制�?*/
    vga_init();

    /* 探测图形 framebuffer；默认不切图形模式，避免影响文本 Shell */
    framebuffer_init();

    /* 初始化 GUI/窗口系统对象池；默认不切图形模式 */
    gui_init();
    
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

    /* 初始�?VFS + ramfs + tmpfs */
    vfs_init();
    ramfs_init();
    tmpfs_init();
    serial_write("[OK] VFS + ramfs + tmpfs\n");

    /* 初始化设备管理器 */
    devmgr_init();
    serial_write("[OK] DEVMGR\n");

    /* 初始化字符设备框架与 /dev 节点 */
    chardev_init();
    chardev_register_builtin_devices();
    serial_write("[OK] CHARDEV + /dev\n");

    /* 初始化块设备框架�?RAM disk */
    blockdev_init();
    blockdev_register_builtin_devices();
    serial_write("[OK] BLOCKDEV + ram0\n");

    /* 初始化最�?TCP/IP 网络�?*/
    net_init();
    serial_write("[OK] NET\n");

    /* 初始化跨端设备发现协�?*/
    discovery_init();
    serial_write("[OK] DISCOVERY\n");

    /* 初始化跨端数据同步与任务流转协议 */
    sync_init();
    serial_write("[OK] SYNC\n");

    /* 初始化统一消息总线 */
    bus_init();
    serial_write("[OK] BUS\n");

    /* 初始�?AI 引擎框架 MVP */
    ai_init();
    serial_write("[OK] AI\n");

    /* VFS 测试：不要预创建 /tmp，避�?shell 手动 mkdir /tmp 时误报失�?*/
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

    /* Install the embedded user ELF into ramfs. */
    vfs_mkdir("/bin", 0755);

    fd = vfs_open("/bin/hello", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)hello_elf, hello_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/hello user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/hello\n");
    }

    fd = vfs_open("/hello.elf", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)hello_elf, hello_elf_size);
        vfs_close(fd);
    }

    fd = vfs_open("/bin/fault", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)fault_elf, fault_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/fault user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/fault\n");
    }

    /* 初始化调度器 */
    sched_init();
    
    /* 启动定时器（调度器已初始化） */
    pit_start();
    
    /* Do not enable interrupts here.
     * sched_start() must enter the first thread through iret, and the
     * thread's initial EFLAGS (IF=1) will enable interrupts safely.
     * If sti runs too early, timer ISR may save the bootstrap stack into
     * idle->kernel_esp and later context switches can restore a bad stack.
     */
    
    /* 测试用户态切换已通过 Phase 2 验证，不再自动测�?*/
    /* test_user_mode_switch(); */
    
    /* Start graphical desktop as the foreground UI. */
    {
        uint32_t desktop_stack = kernel_alloc_stack_pages(KERNEL_UI_THREAD_STACK_PAGES);
        thread_t *desk = NULL;
        if (desktop_stack) {
            desk = thread_create_sized(1, "desktop", (uint32_t)desktop_thread, desktop_stack, KERNEL_UI_THREAD_STACK_SIZE);
        }
        if (desk) {
            sched_add_thread(desk);
        } else {
            serial_write("[GUI] Failed to create desktop thread; starting shell fallback.\n");
            kernel_start_shell_thread();
        }
    }

    /* Shell is launched on demand from the GUI Terminal tool, or as a fallback if GUI fails. */

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
    
    /* 启动调度 - sti �?iret 自动完成，不提前开�?*/
    serial_write("[KERNEL] Starting scheduler and enabling interrupts...\n");
    
    sched_start();
    
    /* sched_start 不应该返�?*/
    serial_write("[ERR] sched_start returned!\n");
    
    for (;;) {
        __asm__ volatile("hlt");
    }
}
