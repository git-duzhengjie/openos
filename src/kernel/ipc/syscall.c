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
        /* fd=1: stdout, 输出到 VGA+串口 */
        if ((int)a == 1) {
            const char *s = (const char *)b;
            int len = (int)c;
            serial_write("[USER] ");
            for (int i = 0; i < len && s[i]; i++) {
                serial_putc(s[i]);
                vga_putc(s[i]);
            }
            serial_write("\n");
            return (uint32_t)len;
        }
        /* 其他 fd: vfs_write */
        return (uint32_t)vfs_write((int)a, (const void *)b, (uint32_t)c);

    case SYS_READ:
        return 0;

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
        return sys_wait((int *)a);

    case SYS_WAITPID:
        return sys_waitpid(a, (int *)b, (int)c);

    case SYS_GETPPID:
        return sys_getppid();

    case SYS_OPEN:
        return (uint32_t)vfs_open((const char *)a, (int)b, (int)c);

    case SYS_CLOSE:
        return (uint32_t)vfs_close((int)a);

    case SYS_READ_FD:
        {
            /* 简化：直接读取到用户缓冲区 */
            int fd = (int)a;
            char *buf = (char *)b;
            uint32_t count = c;
            return (uint32_t)vfs_read(fd, buf, count);
        }

    case SYS_WRITE_FD:
        return (uint32_t)vfs_write((int)a, (const void *)b, (uint32_t)c);

    case SYS_SEEK:
        return (uint32_t)vfs_seek((int)a, (int)b, (int)c);

    case SYS_MKDIR:
        return (uint32_t)vfs_mkdir((const char *)a, (int)b);

    case SYS_UNLINK:
        return (uint32_t)vfs_unlink((const char *)a);

    case SYS_RMDIR:
        return (uint32_t)vfs_rmdir((const char *)a);

    case SYS_EXEC:
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