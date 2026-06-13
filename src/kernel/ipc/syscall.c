/* ============================================================
 * openos - 系统调用实现
 * ============================================================ */

#include "../include/syscall.h"
#include "../include/process.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/serial.h"
#include "../include/vga.h"
#include "../proc/process.h"
#include "../fs/vfs.h"
#include <stddef.h>  /* NULL */

/* VGA */
#define VGA ((volatile uint16_t *)0xB8000)

#define USER_PTR_MIN 0x00100000u
#define USER_PTR_MAX 0xC0000000u
#define USER_CSTR_MAX 4096u

static int user_range_ok(const void *ptr, uint32_t len)
{
    uint32_t start = (uint32_t)ptr;
    uint32_t end;
    if (len == 0)
        return 1;
    if (start < USER_PTR_MIN || start >= USER_PTR_MAX)
        return 0;
    end = start + len - 1;
    if (end < start || end >= USER_PTR_MAX)
        return 0;
    return 1;
}

static int user_cstr_ok(const char *s)
{
    if (!user_range_ok(s, 1))
        return 0;
    for (uint32_t i = 0; i < USER_CSTR_MAX; i++) {
        if (!user_range_ok(s + i, 1))
            return 0;
        if (s[i] == '\0')
            return 1;
    }
    return 0;
}

static void vga_write_str(const char *s, uint8_t color) {
    int row = 1;
    static int col = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\n') {
            col = 0;
            row++;
        } else {
            VGA[row * 80 + col] = (uint16_t)((color << 8) | s[i]);
            col++;
            if (col >= 80) { col = 0; row++; }
        }
    }
}

/* ============================================================
 * 系统调用分发
 * ============================================================ */
uint32_t syscall_dispatch(uint32_t num,
                          uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    switch (num) {
    case SYS_GETPID:
        return sys_getpid();

    case SYS_GETTID:
        return sys_gettid();

    case SYS_WRITE:
        if (!user_range_ok((const void *)b, c))
            return (uint32_t)-1;
        /* fd=1: stdout, 输出到 VGA+串口 */
        if ((int)a == 1) {
            const char *s = (const char *)b;
            int len = (int)c;
            serial_write("[USER] ");
            for (int i = 0; i < len; i++) {
                serial_putc(s[i]);
                vga_putc(s[i]);
            }
            return (uint32_t)len;
        }
        /* 其他 fd: vfs_write */
        return (uint32_t)vfs_write((int)a, (const void *)b, (uint32_t)c);

    case SYS_READ:
        if (!user_range_ok((void *)b, c))
            return (uint32_t)-1;
        return (uint32_t)vfs_read((int)a, (void *)b, (uint32_t)c);

    case SYS_EXIT:
        sys_exit((int)a);
        return 0;

    case SYS_SLEEP:
        thread_sleep(a);
        return 0;

    case SYS_YIELD:
        sched_yield();
        return 0;

    case SYS_MALLOC:
        {
            void *p = pmm_alloc_page();
            return (uint32_t)p;
        }

    case SYS_FREE:
        pmm_free_page((void *)a);
        return 0;

    case SYS_FORK:
        return sys_fork();

    case SYS_WAIT:
        if (a && !user_range_ok((void *)a, sizeof(int)))
            return (uint32_t)-1;
        return sys_wait((int *)a);

    case SYS_WAITPID:
        if (b && !user_range_ok((void *)b, sizeof(int)))
            return (uint32_t)-1;
        return sys_waitpid(a, (int *)b, (int)c);

    case SYS_GETPPID:
        return sys_getppid();

    case SYS_OPEN:
        if (!user_cstr_ok((const char *)a))
            return (uint32_t)-1;
        return (uint32_t)vfs_open((const char *)a, (int)b, (int)c);

    case SYS_CLOSE:
        return (uint32_t)vfs_close((int)a);

    case SYS_READ_FD:
        {
            int fd = (int)a;
            char *buf = (char *)b;
            uint32_t count = c;
            if (!user_range_ok(buf, count))
                return (uint32_t)-1;
            return (uint32_t)vfs_read(fd, buf, count);
        }

    case SYS_WRITE_FD:
        if (!user_range_ok((const void *)b, c))
            return (uint32_t)-1;
        return (uint32_t)vfs_write((int)a, (const void *)b, (uint32_t)c);

    case SYS_SEEK:
        return (uint32_t)vfs_seek((int)a, (int)b, (int)c);

    case SYS_MKDIR:
        if (!user_cstr_ok((const char *)a))
            return (uint32_t)-1;
        return (uint32_t)vfs_mkdir((const char *)a, (int)b);

    case SYS_UNLINK:
        if (!user_cstr_ok((const char *)a))
            return (uint32_t)-1;
        return (uint32_t)vfs_unlink((const char *)a);

    case SYS_RMDIR:
        if (!user_cstr_ok((const char *)a))
            return (uint32_t)-1;
        return (uint32_t)vfs_rmdir((const char *)a);

    case SYS_EXEC:
        if (!user_cstr_ok((const char *)a))
            return (uint32_t)-1;
        return (uint32_t)sys_exec((const char *)a, NULL);

    default:
        return 0xFFFFFFFF;
    }
}

/* ============================================================
 * 初始化系统调用
 * ============================================================ */
void syscall_init(void)
{
    /* IDT 中断 0x80 设置在 idt.c 中 */
    /* 汇编入口在 isr.asm 中作为 sysenter / int 0x80 处理 */
    vga_write_str("[SYSCALL] initialized\n", 0x0A);
}