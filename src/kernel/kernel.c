/**
 * @file kernel.c
 * @brief OpenOS 内核主程�?(Phase 2.5 - 抢占式调�?
 */

#include "types.h"
#include "bootinfo.h"
#include "../include/syscall.h"
#include "../include/usermode.h"
#include "serial.h"
#include "process.h"
#include "core/proc/process.h"
#include "core/fs/vfs.h"
#include "core/fs/ramfs.h"
#include "core/fs/tmpfs.h"
#include "../net/net.h"
#include "../net/dhcp.h"
#include "../net/dns.h"
#include "../net/net_config.h"
#include "../net/discovery.h"
#include "../net/account.h"
#include "../net/sync.h"
#include "../net/bus.h"
#include "ai.h"
#include "devmgr.h"
#include "../shell.h"
#include "heap.h"
#include "keyboard.h"
#include "chardev.h"
#include "blockdev.h"
#include "pci.h"
#include "ata.h"
#include "ahci.h"
#include "virtio_blk.h"
#include "virtio_net.h"
#include "virtio_input.h"
#include "virtio_gpu.h"
#include "e1000.h"
#include "rtl8139.h"
#include "acpi.h"
#include "apic.h"
#include "smp.h"
#include "rtc.h"
#include "power.h"
#include "vga.h"
#include "framebuffer.h"
#include "display.h"
#include "input.h"
#include "gui.h"
#include "window_manager.h"
#include "font.h"
#include "mouse.h"
#include "usb_tablet.h"
#include "usb.h"
#include "sound.h"
#include "pmm.h"
#include "arch_ops.h"
#include "platform_ops.h"
#include "i386_arch_ops.h"
#include "pc_bios_platform_ops.h"
#include "basic_devices.h"
#include "device.h"
#include "driver.h"
#include "embed_hello.h"  /* 嵌入的用户程�?*/
#include "embed_fault.h"  /* 用户异常隔离测试程序 */
#ifndef OPENOS_EMBED_TESTS
#define OPENOS_EMBED_TESTS 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_isotest.h")
#include "embed_isotest.h"  /* 用户/内核内存隔离回归测试程序 */
#define OPENOS_HAS_ISOTEST 1
#else
#define OPENOS_HAS_ISOTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_waittest.h")
#include "embed_waittest.h"  /* spawn/waitpid 回归测试程序 */
#define OPENOS_HAS_WAITTEST 1
#else
#define OPENOS_HAS_WAITTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_forktest.h")
#include "embed_forktest.h"  /* fork/address-space 回归测试程序 */
#define OPENOS_HAS_FORKTEST 1
#else
#define OPENOS_HAS_FORKTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_threadtest.h")
#include "embed_threadtest.h"  /* user thread API 回归测试程序 */
#define OPENOS_HAS_THREADTEST 1
#else
#define OPENOS_HAS_THREADTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_mutextest.h")
#include "embed_mutextest.h"  /* mutex 回归测试程序 */
#define OPENOS_HAS_MUTEXTEST 1
#else
#define OPENOS_HAS_MUTEXTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_semtest.h")
#include "embed_semtest.h"  /* semaphore 回归测试程序 */
#define OPENOS_HAS_SEMTEST 1
#else
#define OPENOS_HAS_SEMTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_condtest.h")
#include "embed_condtest.h"  /* condition variable 回归测试程序 */
#define OPENOS_HAS_CONDTEST 1
#else
#define OPENOS_HAS_CONDTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_futextest.h")
#include "embed_futextest.h"  /* futex 回归测试程序 */
#define OPENOS_HAS_FUTEXTEST 1
#else
#define OPENOS_HAS_FUTEXTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_nicetest.h")
#include "embed_nicetest.h"  /* priority/nice 回归测试程序 */
#define OPENOS_HAS_NICETEST 1
#else
#define OPENOS_HAS_NICETEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_exit42.h")
#include "embed_exit42.h"  /* exit status 回归测试程序 */
#define OPENOS_HAS_EXIT42 1
#else
#define OPENOS_HAS_EXIT42 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_orphan.h")
#include "embed_orphan.h"  /* orphan reparent 回归测试程序 */
#define OPENOS_HAS_ORPHAN 1
#else
#define OPENOS_HAS_ORPHAN 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_argtest.h")
#include "embed_argtest.h"  /* argv 回归测试程序 */
#define OPENOS_HAS_ARGTEST 1
#else
#define OPENOS_HAS_ARGTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_envtest.h")
#include "embed_envtest.h"  /* envp 回归测试程序 */
#define OPENOS_HAS_ENVTEST 1
#else
#define OPENOS_HAS_ENVTEST 0
#endif
#if __has_include("embed_fdinherit.h")
#include "embed_fdinherit.h"  /* fd inheritance helper for chromiumcaptest */
#define OPENOS_HAS_FDINHERIT 1
#else
#define OPENOS_HAS_FDINHERIT 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_libctest.h")
#include "embed_libctest.h"  /* libc subset 回归测试程序 */
#define OPENOS_HAS_LIBCTEST 1
#else
#define OPENOS_HAS_LIBCTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_maintest.h")
#include "embed_maintest.h"  /* crt0/main 回归测试程序 */
#define OPENOS_HAS_MAINTEST 1
#else
#define OPENOS_HAS_MAINTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_systest.h")
#include "embed_systest.h"  /* syscall wrapper 回归测试程序 */
#define OPENOS_HAS_SYSTEST 1
#else
#define OPENOS_HAS_SYSTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_kaddrtest.h")
#include "embed_kaddrtest.h"  /* kernel address protection 回归测试程序 */
#define OPENOS_HAS_KADDRTEST 1
#else
#define OPENOS_HAS_KADDRTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_malloctest.h")
#include "embed_malloctest.h"  /* userspace heap 回归测试程序 */
#define OPENOS_HAS_MALLOCTEST 1
#else
#define OPENOS_HAS_MALLOCTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_errnotest.h")
#include "embed_errnotest.h"  /* userspace errno 回归测试程序 */
#define OPENOS_HAS_ERRNOTEST 1
#else
#define OPENOS_HAS_ERRNOTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_stdiotest.h")
#include "embed_stdiotest.h"  /* userspace stdio regression test */
#define OPENOS_HAS_STDIOTEST 1
#else
#define OPENOS_HAS_STDIOTEST 0
#endif
#if OPENOS_EMBED_TESTS && __has_include("embed_fstest.h")
#include "embed_fstest.h"  /* filesystem syscall 回归测试程序 */
#define OPENOS_HAS_FSTEST 1
#else
#define OPENOS_HAS_FSTEST 0
#endif
#if __has_include("embed_cxxabitest.h")
#include "embed_cxxabitest.h"  /* minimal C++ ABI hooks regression test */
#define OPENOS_HAS_CXXABITEST 1
#else
#define OPENOS_HAS_CXXABITEST 0
#endif
#if __has_include("embed_sh.h")
#include "embed_sh.h"  /* interactive userspace shell */
#define OPENOS_HAS_SH 1
#else
#define OPENOS_HAS_SH 0
#endif
#if __has_include("embed_pwd.h")
#include "embed_pwd.h"  /* pwd user command */
#define OPENOS_HAS_PWD 1
#else
#define OPENOS_HAS_PWD 0
#endif
#if __has_include("embed_ls.h")
#include "embed_ls.h"  /* ls user command */
#define OPENOS_HAS_LS 1
#else
#define OPENOS_HAS_LS 0
#endif
#if __has_include("embed_cat.h")
#include "embed_cat.h"  /* cat user command */
#define OPENOS_HAS_CAT 1
#else
#define OPENOS_HAS_CAT 0
#endif
#if __has_include("embed_echo.h")
#include "embed_echo.h"  /* echo user command */
#define OPENOS_HAS_ECHO 1
#else
#define OPENOS_HAS_ECHO 0
#endif
#if __has_include("embed_tcc.h")
#include "embed_tcc.h"  /* TinyCC C compiler command */
#define OPENOS_HAS_TCC 1
#else
#define OPENOS_HAS_TCC 0
#endif
#if __has_include("embed_tcc_resources.h")
#include "embed_tcc_resources.h"  /* TinyCC OPENOS sysroot resources */
#define OPENOS_HAS_TCC_RESOURCES 1
#else
#define OPENOS_HAS_TCC_RESOURCES 0
#endif
#if __has_include("embed_tccsmoke.h")
#include "embed_tccsmoke.h"  /* TinyCC in-system smoke test */
#define OPENOS_HAS_TCCSMOKE 1
#else
#define OPENOS_HAS_TCCSMOKE 0
#endif
#ifndef OPENOS_TCC_SMOKE_AUTORUN
#define OPENOS_TCC_SMOKE_AUTORUN 0
#endif
#if OPENOS_TCC_SMOKE_AUTORUN && !OPENOS_HAS_TCCSMOKE
#error "OPENOS_TCC_SMOKE_AUTORUN requires embed_tccsmoke.h"
#endif
#if __has_include("embed_ai.h")
#include "embed_ai.h"  /* ai user command */
#define OPENOS_HAS_AI_CMD 1
#else
#define OPENOS_HAS_AI_CMD 0
#endif
#if __has_include("embed_grep.h")
#include "embed_grep.h"  /* grep user command */
#define OPENOS_HAS_GREP 1
#else
#define OPENOS_HAS_GREP 0
#endif
#if __has_include("embed_wc.h")
#include "embed_wc.h"  /* wc user command */
#define OPENOS_HAS_WC 1
#else
#define OPENOS_HAS_WC 0
#endif
#if __has_include("embed_mkdir.h")
#include "embed_mkdir.h"  /* mkdir user command */
#define OPENOS_HAS_MKDIR 1
#else
#define OPENOS_HAS_MKDIR 0
#endif
#if __has_include("embed_rm.h")
#include "embed_rm.h"  /* rm user command */
#define OPENOS_HAS_RM 1
#else
#define OPENOS_HAS_RM 0
#endif
#if __has_include("embed_touch.h")
#include "embed_touch.h"  /* touch user command */
#define OPENOS_HAS_TOUCH 1
#else
#define OPENOS_HAS_TOUCH 0
#endif
#if __has_include("embed_cp.h")
#include "embed_cp.h"  /* cp user command */
#define OPENOS_HAS_CP 1
#else
#define OPENOS_HAS_CP 0
#endif
#if __has_include("embed_mv.h")
#include "embed_mv.h"  /* mv user command */
#define OPENOS_HAS_MV 1
#else
#define OPENOS_HAS_MV 0
#endif
#if __has_include("embed_tee.h")
#include "embed_tee.h"  /* tee user command */
#define OPENOS_HAS_TEE 1
#else
#define OPENOS_HAS_TEE 0
#endif
#if __has_include("embed_head.h")
#include "embed_head.h"  /* head user command */
#define OPENOS_HAS_HEAD 1
#else
#define OPENOS_HAS_HEAD 0
#endif
#if __has_include("embed_tail.h")
#include "embed_tail.h"  /* tail user command */
#define OPENOS_HAS_TAIL 1
#else
#define OPENOS_HAS_TAIL 0
#endif
#if __has_include("embed_sort.h")
#include "embed_sort.h"  /* sort user command */
#define OPENOS_HAS_SORT 1
#else
#define OPENOS_HAS_SORT 0
#endif
#if __has_include("embed_rmdir.h")
#include "embed_rmdir.h"  /* rmdir user command */
#define OPENOS_HAS_RMDIR 1
#else
#define OPENOS_HAS_RMDIR 0
#endif

#if __has_include("embed_ln.h")
#include "embed_ln.h"  /* ln user command */
#define OPENOS_HAS_LN 1
#else
#define OPENOS_HAS_LN 0
#endif

#if __has_include("embed_kill.h")
#include "embed_kill.h"  /* kill user command */
#define OPENOS_HAS_KILL 1
#else
#define OPENOS_HAS_KILL 0
#endif

#if OPENOS_EMBED_TESTS && __has_include("embed_alarmtest.h")
#include "embed_alarmtest.h"  /* alarmtest user command */
#define OPENOS_HAS_ALARMTEST 1
#else
#define OPENOS_HAS_ALARMTEST 0
#endif

#if OPENOS_EMBED_TESTS && __has_include("embed_mmaptest.h")
#include "embed_mmaptest.h"  /* mmaptest user command */
#define OPENOS_HAS_MMAPTEST 1
#else
#define OPENOS_HAS_MMAPTEST 0
#endif

#if OPENOS_EMBED_TESTS && __has_include("embed_sbrktest.h")
#include "embed_sbrktest.h"  /* sbrktest user command */
#define OPENOS_HAS_SBRKTEST 1
#else
#define OPENOS_HAS_SBRKTEST 0
#endif

#if __has_include("embed_guiprobe.h")
#include "embed_guiprobe.h"  /* guiprobe user command */
#define OPENOS_HAS_GUIPROBE 1
#else
#define OPENOS_HAS_GUIPROBE 0
#endif

#if __has_include("embed_guicomponenttest.h")
#include "embed_guicomponenttest.h"  /* GUI component smoke test */
#define OPENOS_HAS_GUICOMPONENTTEST 1
#else
#define OPENOS_HAS_GUICOMPONENTTEST 0
#endif

#if __has_include("embed_skia_demo.h")
#include "embed_skia_demo.h"  /* Skia-style software raster demo */
#define OPENOS_HAS_SKIA_DEMO 1
#else
#define OPENOS_HAS_SKIA_DEMO 0
#endif

#if __has_include("embed_v8_shell.h")
#include "embed_v8_shell.h"  /* jitless JavaScript shell smoke */
#define OPENOS_HAS_V8_SHELL 1
#else
#define OPENOS_HAS_V8_SHELL 0
#endif

#if __has_include("embed_blink_smoke.h")
#include "embed_blink_smoke.h"  /* minimal HTML/CSS layout smoke */
#define OPENOS_HAS_BLINK_SMOKE 1
#else
#define OPENOS_HAS_BLINK_SMOKE 0
#endif

#if __has_include("embed_content_shell.h")
#include "embed_content_shell.h"  /* single-process content shell smoke */
#define OPENOS_HAS_CONTENT_SHELL 1
#else
#define OPENOS_HAS_CONTENT_SHELL 0
#endif

#if __has_include("embed_browser.h")
#include "embed_browser.h"  /* browser user command */
#define OPENOS_HAS_USER_BROWSER 1
#else
#define OPENOS_HAS_USER_BROWSER 0
#endif

#if __has_include("embed_stickynote.h")
#include "embed_stickynote.h"  /* desktop sticky note user command */
#define OPENOS_HAS_USER_STICKYNOTE 1
#else
#define OPENOS_HAS_USER_STICKYNOTE 0
#endif

#if __has_include("embed_chromium.h")
#include "embed_chromium.h"  /* Chromium-like single-window browser */
#define OPENOS_HAS_USER_CHROMIUM 1
#else
#define OPENOS_HAS_USER_CHROMIUM 0
#endif

#if __has_include("embed_fontprobe.h")
#include "embed_fontprobe.h"  /* fontprobe user command */
#define OPENOS_HAS_FONTPROBE 1
#else
#define OPENOS_HAS_FONTPROBE 0
#endif

#if __has_include("embed_chromiumcaptest.h")
#include "embed_chromiumcaptest.h"  /* Chromium core capability test */
#define OPENOS_HAS_CHROMIUMCAPTEST 1
#else
#define OPENOS_HAS_CHROMIUMCAPTEST 0
#endif

#if __has_include("embed_ping.h")
#include "embed_ping.h"  /* ping user command */
#define OPENOS_HAS_PING 1
#else
#define OPENOS_HAS_PING 0
#endif

#if __has_include("embed_ifconfig.h")
#include "embed_ifconfig.h"  /* ifconfig user command */
#define OPENOS_HAS_IFCONFIG 1
#else
#define OPENOS_HAS_IFCONFIG 0
#endif

#if __has_include("embed_netstat.h")
#include "embed_netstat.h"  /* netstat user command */
#define OPENOS_HAS_NETSTAT 1
#else
#define OPENOS_HAS_NETSTAT 0
#endif

#if __has_include("embed_wget.h")
#include "embed_wget.h"  /* wget user command */
#define OPENOS_HAS_WGET 1
#else
#define OPENOS_HAS_WGET 0
#endif

#if __has_include("embed_curl.h")
#include "embed_curl.h"  /* curl user command */
#define OPENOS_HAS_CURL 1
#else
#define OPENOS_HAS_CURL 0
#endif

#if __has_include("embed_firewall.h")
#include "embed_firewall.h"  /* firewall user command */
#define OPENOS_HAS_FIREWALL 1
#else
#define OPENOS_HAS_FIREWALL 0
#endif

#if __has_include("embed_id.h")
#include "embed_id.h"  /* id user command */
#define OPENOS_HAS_ID 1
#else
#define OPENOS_HAS_ID 0
#endif

#if __has_include("embed_groups.h")
#include "embed_groups.h"  /* groups user command */
#define OPENOS_HAS_GROUPS 1
#else
#define OPENOS_HAS_GROUPS 0
#endif

#if __has_include("embed_cap.h")
#include "embed_cap.h"  /* cap user command */
#define OPENOS_HAS_CAP 1
#else
#define OPENOS_HAS_CAP 0
#endif

#if __has_include("embed_sandbox.h")
#include "embed_sandbox.h"  /* sandbox user command */
#define OPENOS_HAS_SANDBOX 1
#else
#define OPENOS_HAS_SANDBOX 0
#endif


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
extern char _binary_cjk_ofnt_start;
extern char _binary_cjk_ofnt_end;
extern char _binary_cjk_ofnt_size;

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
static int g_desktop_thread_started = 0;

static void install_embedded_cjk_font_resource(void) {
    uint32_t size = (uint32_t)(&_binary_cjk_ofnt_end - &_binary_cjk_ofnt_start);
    if (size == 0) {
        serial_write("[INFO] Embedded CJK font resource disabled\n");
        return;
    }

    vfs_mkdir("/fonts", 0755);
    int fd = vfs_open("/fonts/cjk.ofnt", O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        serial_write("[WARN] Failed to create /fonts/cjk.ofnt\n");
        return;
    }

    int written = vfs_write(fd, &_binary_cjk_ofnt_start, size);
    vfs_close(fd);
    if (written == (int)size) {
        serial_write("[OK] Installed /fonts/cjk.ofnt\n");
    } else {
        serial_write("[WARN] Incomplete /fonts/cjk.ofnt install\n");
    }
}

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

    sh = thread_create_sized(INIT_PID, "shell", (uint32_t)shell_run, shell_stack, KERNEL_UI_THREAD_STACK_SIZE);
    if (sh) {
        sh->priority = PRIORITY_NORMAL;
        g_shell_thread_started = 1;
        sched_add_thread(sh);
        serial_write("[SHELL] Shell thread started on demand.\n");
    } else {
        serial_write("[SHELL] Failed to create shell thread.\n");
    }
}

static void desktop_thread(void) {
    serial_write("[GUI] Starting graphical desktop...\n");
    if (window_manager_start_desktop() != 0) {
        serial_write("[GUI] Failed to start graphical desktop; starting shell fallback.\n");
        kernel_start_shell_thread();
        while (1) sched_yield();
    }

    while (1) {
        window_manager_poll();
        sched_yield();
    }
}

static int kernel_start_desktop_thread(void) {
    uint32_t desktop_stack;
    thread_t *desk;

    if (g_desktop_thread_started) return 0;

    desktop_stack = kernel_alloc_stack_pages(KERNEL_UI_THREAD_STACK_PAGES);
    if (!desktop_stack) {
        serial_write("[GUI] Failed to allocate desktop stack.\n");
        return -1;
    }

    desk = thread_create_sized(INIT_PID, "desktop", (uint32_t)desktop_thread,
                               desktop_stack, KERNEL_UI_THREAD_STACK_SIZE);
    if (!desk) {
        serial_write("[GUI] Failed to create desktop thread.\n");
        return -1;
    }

    g_desktop_thread_started = 1;
    sched_add_thread(desk);
    serial_write("[INIT] Desktop thread started.\n");
    return 0;
}

static void init_thread(void) {
    serial_write("[INIT] PID1 init/reaper started.\n");

    if (kernel_start_desktop_thread() != 0) {
        serial_write("[INIT] Desktop unavailable; starting shell fallback.\n");
        kernel_start_shell_thread();
    }

    for (;;) {
        int status = 0;
        uint32_t reaped = sys_waitpid(-1, &status, WAITPID_WNOHANG);
        if (reaped != 0 && reaped != (uint32_t)-1) {
            serial_write("[INIT] Reaped orphan pid=");
            serial_write_hex(reaped);
            serial_write(" status=");
            serial_write_hex((uint32_t)status);
            serial_write("\n");
            continue;
        }
        sched_yield();
    }
}

static int kernel_start_init_thread(void) {
    uint32_t init_stack = kernel_alloc_stack_pages(KERNEL_UI_THREAD_STACK_PAGES);
    thread_t *init;

    if (!init_stack) {
        serial_write("[INIT] Failed to allocate init stack.\n");
        return -1;
    }

    init = thread_create_sized(INIT_PID, "init", (uint32_t)init_thread,
                               init_stack, KERNEL_UI_THREAD_STACK_SIZE);
    if (!init) {
        serial_write("[INIT] Failed to create init thread.\n");
        return -1;
    }

    init->priority = PRIORITY_NORMAL;
    sched_add_thread(init);
    serial_write("[INIT] Init thread scheduled.\n");
    return 0;
}

#if OPENOS_TCC_SMOKE_AUTORUN
static void kernel_tccsmoke_thread(void)
{
    extern int spawn_user_process(const char *path, char *const argv[]);
    char *argv[] = { "/bin/tccsmoke", NULL };
    serial_write("[tccsmoke] AUTORUN spawning /bin/tccsmoke\n");
    int pid = spawn_user_process("/bin/tccsmoke", argv);
    serial_write("[tccsmoke] AUTORUN spawn result ");
    serial_write_hex((uint32_t)pid);
    serial_write("\n");
    for (;;) {
        sched_yield();
    }
}
#endif

static const openos_bootinfo_t *kernel_bootinfo_legacy(void) {
    static openos_bootinfo_t bootinfo;

    for (uint32_t i = 0; i < sizeof(bootinfo); ++i) {
        ((uint8_t *)&bootinfo)[i] = 0;
    }

    bootinfo.kernel_phys_start = 0x00100000u;
    bootinfo.kernel_phys_end = (uint32_t)__kernel_end;
    openos_bootinfo_finalize(&bootinfo);
    return &bootinfo;
}

void kernel_main(void) {
    openos_i386_arch_ops_init();
    openos_pc_bios_platform_ops_init();
    serial_init();
    serial_write("\n[OpenOS] Phase 2.5 - Preemptive Scheduler\n");
    serial_write("[OpenOS] arch_ops=");
    serial_write(openos_arch_ops_name());
    serial_write(" platform_ops=");
    serial_write(openos_platform_ops_name());
    serial_write("\n");
    openos_basic_devices_register();
    serial_write("[OpenOS] device_model initialized\n");

    const openos_bootinfo_t *bootinfo = kernel_bootinfo_legacy();
    if (openos_bootinfo_is_valid(bootinfo)) {
        serial_write("[OK] OpenOSBootInfo legacy adapter\n");
    } else {
        serial_write("[WARN] OpenOSBootInfo legacy adapter invalid\n");
    }

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

    /* 初始化统一输入抽象层，供后续 PC/Mobile compositor 使用 */
    input_init();

    /* 初始化 PS/2 鼠标驱动 */
    mouse_init();

    /* 初始化 QEMU USB Tablet 绝对坐标鼠标；失败时自动回退 PS/2 */
    usb_tablet_init();

    /* 初始化VGA控制�?*/
    vga_init();

    /* 探测图形 framebuffer；默认不切图形模式，避免影响文本 Shell */
    framebuffer_init();

    /* 初始化统一显示抽象层；v1 包装 legacy framebuffer，不改变旧 GUI 行为 */
    display_init();

    /* 初始化窗口管理器/GUI 对象池；默认不切图形模式 */
    window_manager_init();

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

    install_embedded_cjk_font_resource();

    if (font_load_cjk_resource_from_file("/fonts/cjk.ofnt") == 0) {
        serial_write("[OK] Loaded external CJK font resource\n");
    } else {
        serial_write("[INFO] External CJK font resource unavailable; using built-in fallback\n");
    }

    /* 初始化设备管理器 */
    devmgr_init();
    serial_write("[OK] DEVMGR\n");

    /* 初始化字符设备框架与 /dev 节点 */
    chardev_init();
    chardev_register_builtin_devices();
    serial_write("[OK] CHARDEV + /dev\n");

    /* 扫描 ACPI 表 */
    acpi_init();
    serial_write("[OK] ACPI\n");

    /* 探测 APIC / IOAPIC */
    apic_init();
    serial_write("[OK] APIC\n");

    /* 初始化 SMP CPU 拓扑 */
    smp_init();
    serial_write("[OK] SMP\n");

    /* 读取 RTC 启动时间 */
    rtc_init();
    serial_write("[OK] RTC\n");

    /* 初始化 ACPI 电源管理 */
    power_init();
    serial_write("[OK] POWER\n");

    /* 扫描 PCI 总线 */
    pci_scan_all();
    serial_write("[OK] PCI SCAN\n");

    /* 初始化 USB 通用栈：发现 USB host controller，建立 USB bus/device 模型 */
    usb_init();

    /* 初始化声卡驱动：发现 PCI 音频设备并注册 PC Speaker 兜底设备 */
    sound_init();

    /* 初始化块设备框架�?RAM disk */
    blockdev_init();
    blockdev_register_builtin_devices();
    ata_init();
    ahci_init();
    virtio_blk_init();
    serial_write("[OK] BLOCKDEV + ram0 + ATA + AHCI + virtio-blk\n");

    /* 初始化 VirtIO 跨架构输入/显示探测骨架 */
    virtio_input_init();
    virtio_gpu_init();
    serial_write("[OK] VIRTIO input/gpu probe\n");

    /* 初始化最�?TCP/IP 网络�?*/
    net_init();
    dhcp_init();
    dns_init();
    virtio_net_init();
    e1000_init();
    rtl8139_init();
    net_config_init();
    (void)net_config_apply_saved();
    serial_write("[OK] NET + DHCP + DNS + virtio-net + e1000 + rtl8139\n");

    /* 初始化跨端设备发现协�?*/
    discovery_init();
    account_init();
    serial_write("[OK] DISCOVERY + ACCOUNT\n");

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
    vfs_mkdir("/home", 0755);
    vfs_mkdir("/home/browser", 0755);
    vfs_mkdir("/home/browser/cache", 0755);
    vfs_mkdir("/home/browser/cookies", 0755);
    vfs_mkdir("/home/browser/certs", 0755);
    vfs_mkdir("/home/browser/profiles", 0755);
    vfs_mkdir("/home/browser/downloads", 0755);
    vfs_mkdir("/home/examples", 0755);
    vfs_mkdir("/usr", 0755);
    vfs_mkdir("/usr/include", 0755);
    vfs_mkdir("/usr/include/tcc", 0755);
    vfs_mkdir("/usr/lib", 0755);
    vfs_mkdir("/usr/lib/tcc", 0755);
    vfs_mkdir("/usr/share", 0755);
    vfs_mkdir("/usr/share/openos", 0755);
    vfs_mkdir("/usr/share/openos/browser", 0755);
    vfs_mkdir("/usr/share/openos/browser/pak", 0755);
    serial_write("[OK] Browser profile directories\n");

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

#if OPENOS_HAS_ISOTEST
    fd = vfs_open("/bin/isotest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)isotest_elf, isotest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/isotest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/isotest\n");
    }
#endif

#if OPENOS_HAS_WAITTEST
    fd = vfs_open("/bin/waittest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)waittest_elf, waittest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/waittest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/waittest\n");
    }
#endif

#if OPENOS_HAS_FORKTEST
    fd = vfs_open("/bin/forktest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)forktest_elf, forktest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/forktest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/forktest\n");
    }
#endif

#if OPENOS_HAS_THREADTEST
    fd = vfs_open("/bin/threadtest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)threadtest_elf, threadtest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/threadtest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/threadtest\n");
    }
#endif

#if OPENOS_HAS_MUTEXTEST
    fd = vfs_open("/bin/mutextest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)mutextest_elf, mutextest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/mutextest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/mutextest\n");
    }
#endif

#if OPENOS_HAS_SEMTEST
    fd = vfs_open("/bin/semtest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)semtest_elf, semtest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/semtest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/semtest\n");
    }
#endif

#if OPENOS_HAS_CONDTEST
    fd = vfs_open("/bin/condtest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)condtest_elf, condtest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/condtest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/condtest\n");
    }
#endif

#if OPENOS_HAS_FUTEXTEST
    fd = vfs_open("/bin/futextest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)futextest_elf, futextest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/futextest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/futextest\n");
    }
#endif

#if OPENOS_HAS_NICETEST
    fd = vfs_open("/bin/nicetest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)nicetest_elf, nicetest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/nicetest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/nicetest\n");
    }
#endif

#if OPENOS_HAS_EXIT42
    fd = vfs_open("/bin/exit42", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)exit42_elf, exit42_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/exit42 user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/exit42\n");
    }
#endif

#if OPENOS_HAS_ORPHAN
    fd = vfs_open("/bin/orphan", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)orphan_elf, orphan_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/orphan user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/orphan\n");
    }
#endif

#if OPENOS_HAS_ARGTEST
    fd = vfs_open("/bin/argtest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)argtest_elf, argtest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/argtest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/argtest\n");
    }
#endif

#if OPENOS_HAS_ENVTEST
    fd = vfs_open("/bin/envtest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)envtest_elf, envtest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/envtest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/envtest\n");
    }
#endif

#if OPENOS_HAS_FDINHERIT
    fd = vfs_open("/bin/fdinherit", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)fdinherit_elf, fdinherit_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/fdinherit user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/fdinherit\n");
    }
#endif

#if OPENOS_HAS_LIBCTEST
    fd = vfs_open("/bin/libctest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)libctest_elf, libctest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/libctest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/libctest\n");
    }
#endif

#if OPENOS_HAS_MAINTEST
    fd = vfs_open("/bin/maintest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)maintest_elf, maintest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/maintest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/maintest\n");
    }
#endif

#if OPENOS_HAS_SYSTEST
    fd = vfs_open("/bin/systest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)systest_elf, systest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/systest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/systest\n");
    }
#endif

#if OPENOS_HAS_KADDRTEST
    fd = vfs_open("/bin/kaddrtest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)kaddrtest_elf, kaddrtest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/kaddrtest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/kaddrtest\n");
    }
#endif

#if OPENOS_HAS_MALLOCTEST
    fd = vfs_open("/bin/malloctest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)malloctest_elf, malloctest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/malloctest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/malloctest\n");
    }
#endif

#if OPENOS_HAS_ERRNOTEST
    fd = vfs_open("/bin/errnotest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)errnotest_elf, errnotest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/errnotest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/errnotest\n");
    }
#endif

#if OPENOS_HAS_STDIOTEST
    fd = vfs_open("/bin/stdiotest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)stdiotest_elf, stdiotest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/stdiotest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/stdiotest\n");
    }
#endif

#if OPENOS_HAS_FSTEST
    fd = vfs_open("/bin/fstest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)fstest_elf, fstest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/fstest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/fstest\n");
    }
#endif

#if OPENOS_HAS_CXXABITEST
    fd = vfs_open("/bin/cxxabitest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)cxxabitest_elf, cxxabitest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/cxxabitest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/cxxabitest\n");
    }
#endif

#if OPENOS_HAS_SH
    fd = vfs_open("/bin/sh", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)sh_elf, sh_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/sh user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/sh\n");
    }
#endif

#if OPENOS_HAS_PWD
    fd = vfs_open("/bin/pwd", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)pwd_elf, pwd_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/pwd user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/pwd\n");
    }
#endif

#if OPENOS_HAS_LS
    fd = vfs_open("/bin/ls", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)ls_elf, ls_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/ls user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/ls\n");
    }
#endif

#if OPENOS_HAS_CAT
    fd = vfs_open("/bin/cat", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)cat_elf, cat_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/cat user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/cat\n");
    }
#endif

#if OPENOS_HAS_ECHO
    fd = vfs_open("/bin/echo", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)echo_elf, echo_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/echo user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/echo\n");
    }
#endif

#if OPENOS_HAS_TCC
    fd = vfs_open("/bin/tcc", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        int written = vfs_write(fd, (const char *)tcc_elf, tcc_elf_size);
        vfs_close(fd);
        if (written == (int)tcc_elf_size) {
            serial_write("[OK] Installed /bin/tcc user ELF\n");
        } else {
            serial_write("[ERR] Failed to write complete /bin/tcc user ELF\n");
        }
    } else {
        serial_write("[WARN] Failed to install /bin/tcc\n");
    }
#endif

#if OPENOS_HAS_TCCSMOKE
    fd = vfs_open("/bin/tccsmoke", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        int written = vfs_write(fd, (const char *)tccsmoke_elf, tccsmoke_elf_size);
        vfs_close(fd);
        if (written == (int)tccsmoke_elf_size) {
            serial_write("[OK] Installed /bin/tccsmoke user ELF\n");
        } else {
            serial_write("[ERR] Failed to write complete /bin/tccsmoke user ELF\n");
        }
    } else {
        serial_write("[WARN] Failed to install /bin/tccsmoke\n");
    }
#endif

#if OPENOS_HAS_TCC_RESOURCES
    fd = vfs_open("/usr/include/openos.h", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tcc_res_openos_h, tcc_res_openos_h_len);
        vfs_close(fd);
    }
    fd = vfs_open("/usr/include/tcc/openos.h", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tcc_res_openos_h, tcc_res_openos_h_len);
        vfs_close(fd);
    }
    fd = vfs_open("/usr/include/tccdefs.h", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tcc_res_tccdefs_h, tcc_res_tccdefs_h_len);
        vfs_close(fd);
    }
    fd = vfs_open("/usr/include/tcc/tccdefs.h", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tcc_res_tccdefs_h, tcc_res_tccdefs_h_len);
        vfs_close(fd);
    }
    fd = vfs_open("/usr/lib/tcc/user.ld", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tcc_res_user_ld, tcc_res_user_ld_len);
        vfs_close(fd);
    }
    fd = vfs_open("/usr/lib/tcc/crt0.o", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tcc_res_crt0_o, tcc_res_crt0_o_len);
        vfs_close(fd);
    }
    fd = vfs_open("/usr/lib/tcc/openos_runtime.c", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tcc_res_runtime_c, tcc_res_runtime_c_len);
        vfs_close(fd);
    }
    fd = vfs_open("/home/examples/hello.c", O_CREAT | O_RDWR, 0644);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tcc_res_example_hello_c, tcc_res_example_hello_c_len);
        vfs_close(fd);
    }
    serial_write("[OK] Installed TinyCC OPENOS sysroot resources\n");
#endif

#if OPENOS_HAS_AI_CMD
    fd = vfs_open("/bin/ai", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)ai_elf, ai_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/ai user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/ai\n");
    }
#endif

#if OPENOS_HAS_GREP
    fd = vfs_open("/bin/grep", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)grep_elf, grep_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/grep user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/grep\n");
    }
#endif

#if OPENOS_HAS_WC
    fd = vfs_open("/bin/wc", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)wc_elf, wc_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/wc user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/wc\n");
    }
#endif

#if OPENOS_HAS_MKDIR
    fd = vfs_open("/bin/mkdir", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)mkdir_elf, mkdir_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/mkdir user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/mkdir\n");
    }
#endif

#if OPENOS_HAS_RM
    fd = vfs_open("/bin/rm", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)rm_elf, rm_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/rm user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/rm\n");
    }
#endif

#if OPENOS_HAS_CP
    fd = vfs_open("/bin/cp", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)cp_elf, cp_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/cp user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/cp\n");
    }
#endif

#if OPENOS_HAS_MV
    fd = vfs_open("/bin/mv", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)mv_elf, mv_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/mv user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/mv\n");
    }
#endif

#if OPENOS_HAS_TEE
    fd = vfs_open("/bin/tee", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tee_elf, tee_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/tee user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/tee\n");
    }
#endif

#if OPENOS_HAS_HEAD
    fd = vfs_open("/bin/head", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)head_elf, head_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/head user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/head\n");
    }
#endif

#if OPENOS_HAS_TAIL
    fd = vfs_open("/bin/tail", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)tail_elf, tail_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/tail user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/tail\n");
    }
#endif

#if OPENOS_HAS_SORT
    fd = vfs_open("/bin/sort", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)sort_elf, sort_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/sort user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/sort\n");
    }
#endif

#if OPENOS_HAS_ENV
    fd = vfs_open("/bin/env", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)env_elf, env_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/env user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/env\n");
    }
#endif

#if OPENOS_HAS_RMDIR
    fd = vfs_open("/bin/rmdir", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)rmdir_elf, rmdir_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/rmdir user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/rmdir\n");
    }
#endif

#if OPENOS_HAS_LN
    fd = vfs_open("/bin/ln", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)ln_elf, ln_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/ln user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/ln\n");
    }
#endif

#if OPENOS_HAS_KILL
    fd = vfs_open("/bin/kill", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)kill_elf, kill_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/kill user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/kill\n");
    }
#endif

#if OPENOS_HAS_ALARMTEST
    fd = vfs_open("/bin/alarmtest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)alarmtest_elf, alarmtest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/alarmtest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/alarmtest\n");
    }
#endif

#if OPENOS_HAS_MMAPTEST
    fd = vfs_open("/bin/mmaptest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)mmaptest_elf, mmaptest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/mmaptest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/mmaptest\n");
    }
#endif

#if OPENOS_HAS_SBRKTEST
    fd = vfs_open("/bin/sbrktest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)sbrktest_elf, sbrktest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/sbrktest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/sbrktest\n");
    }
#endif

#if OPENOS_HAS_GUIPROBE
    fd = vfs_open("/bin/guiprobe", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)guiprobe_elf, guiprobe_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/guiprobe user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/guiprobe\n");
    }
#endif

#if OPENOS_HAS_GUICOMPONENTTEST
    fd = vfs_open("/bin/guicomponenttest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)guicomponenttest_elf, guicomponenttest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/guicomponenttest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/guicomponenttest\n");
    }
#endif

#if OPENOS_HAS_SKIA_DEMO
    fd = vfs_open("/bin/skia_demo", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)skia_demo_elf, skia_demo_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/skia_demo user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/skia_demo\n");
    }
#endif

#if OPENOS_HAS_V8_SHELL
    fd = vfs_open("/bin/v8_shell", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)v8_shell_elf, v8_shell_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/v8_shell user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/v8_shell\n");
    }
#endif

#if OPENOS_HAS_BLINK_SMOKE
    fd = vfs_open("/bin/blink_smoke", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)blink_smoke_elf, blink_smoke_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/blink_smoke user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/blink_smoke\n");
    }
#endif

#if OPENOS_HAS_CONTENT_SHELL
    fd = vfs_open("/bin/content_shell", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)content_shell_elf, content_shell_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/content_shell user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/content_shell\n");
    }
#endif

#if OPENOS_HAS_USER_BROWSER
    fd = vfs_open("/bin/browser", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)browser_elf, browser_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/browser user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/browser\n");
    }
#endif

#if OPENOS_HAS_USER_STICKYNOTE
    fd = vfs_open("/bin/stickynote", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)stickynote_elf, stickynote_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/stickynote user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/stickynote\n");
    }
#endif

#if OPENOS_HAS_USER_CHROMIUM
    fd = vfs_open("/bin/chromium", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)chromium_elf, chromium_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/chromium user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/chromium\n");
    }
#endif

#if OPENOS_HAS_FONTPROBE
    fd = vfs_open("/bin/fontprobe", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)fontprobe_elf, fontprobe_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/fontprobe user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/fontprobe\n");
    }
#endif

#if OPENOS_HAS_CHROMIUMCAPTEST
    fd = vfs_open("/bin/chromiumcaptest", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)chromiumcaptest_elf, chromiumcaptest_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/chromiumcaptest user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/chromiumcaptest\n");
    }
#endif


#if OPENOS_HAS_PING
    fd = vfs_open("/bin/ping", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)ping_elf, ping_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/ping user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/ping\n");
    }
#endif

#if OPENOS_HAS_IFCONFIG
    fd = vfs_open("/bin/ifconfig", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)ifconfig_elf, ifconfig_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/ifconfig user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/ifconfig\n");
    }
#endif

#if OPENOS_HAS_NETSTAT
    fd = vfs_open("/bin/netstat", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)netstat_elf, netstat_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/netstat user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/netstat\n");
    }
#endif

#if OPENOS_HAS_WGET
    fd = vfs_open("/bin/wget", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)wget_elf, wget_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/wget user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/wget\n");
    }
#endif

#if OPENOS_HAS_CURL
    fd = vfs_open("/bin/curl", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)curl_elf, curl_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/curl user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/curl\n");
    }
#endif

#if OPENOS_HAS_FIREWALL
    fd = vfs_open("/bin/firewall", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)firewall_elf, firewall_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/firewall user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/firewall\n");
    }
#endif

#if OPENOS_HAS_ID
    fd = vfs_open("/bin/id", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)id_elf, id_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/id user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/id\n");
    }
#endif

#if OPENOS_HAS_GROUPS
    fd = vfs_open("/bin/groups", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)groups_elf, groups_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/groups user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/groups\n");
    }
#endif

#if OPENOS_HAS_CAP
    fd = vfs_open("/bin/cap", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)cap_elf, cap_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/cap user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/cap\n");
    }
#endif

#if OPENOS_HAS_SANDBOX
    fd = vfs_open("/bin/sandbox", O_CREAT | O_RDWR, 0755);
    if (fd >= 0) {
        vfs_write(fd, (const char *)sandbox_elf, sandbox_elf_size);
        vfs_close(fd);
        serial_write("[OK] Installed /bin/sandbox user ELF\n");
    } else {
        serial_write("[WARN] Failed to install /bin/sandbox\n");
    }
#endif

    /* 初始化调度器 */
    sched_init();

    /* 启动定时器（调度器已初始化） */
    pit_start();

    /* Do not enable interrupts here.
     * sched_start() must enter the first thread through iret, and the
     * thread's initial EFLAGS (IF=1) will enable interrupts safely.
     * If sti runs too early, timer ISR may save the bootstrap stack into
     * idle->kernel_sp and later context switches can restore a bad stack.
     */

    /* 测试用户态切换已通过 Phase 2 验证，不再自动测�?*/
    /* test_user_mode_switch(); */

    /* Start PID1 init/reaper; init owns desktop/shell startup and orphan cleanup. */
    if (kernel_start_init_thread() != 0) {
        serial_write("[INIT] Failed to start init; falling back to shell thread.\n");
        kernel_start_shell_thread();
    }

    /* Shell is launched on demand from the GUI Terminal tool, or as a fallback if GUI/init fails. */

#if OPENOS_TCC_SMOKE_AUTORUN
    {
        uint32_t tccsmoke_stack = (uint32_t)pmm_alloc_page() + 4096;
        thread_t *tccsmoke_thread = thread_create(1, "tccsmoke", (uint32_t)kernel_tccsmoke_thread, tccsmoke_stack);
        if (tccsmoke_thread) {
            sched_add_thread(tccsmoke_thread);
            serial_write("[tccsmoke] AUTORUN scheduled /bin/tccsmoke\n");
        } else {
            serial_write("[tccsmoke] AUTORUN failed to create thread\n");
        }
    }
#endif

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
