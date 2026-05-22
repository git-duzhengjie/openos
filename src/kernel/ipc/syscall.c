/* ============================================================
 * openos - 系统调用实现
 * ============================================================ */

#include "../include/syscall.h"
#include "../include/process.h"
#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../proc/process.h"

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
        /* 简化: 输出到 VGA (arg1=字符串, arg2=长度) */
        {
            const char *s = (const char *)a;
            int len = (int)b;
            for (int i = 0; i < len && s[i]; i++) {
                int row = 23;
                static int col = 40;
                if (s[i] == '\n') {
                    col = 40;
                    row++;
                } else {
                    VGA[row * 80 + col] = (uint16_t)(s[i] | (0x0F << 8));
                    col++;
                    if (col >= 80) { col = 40; row++; }
                }
            }
        }
        return 0;

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