/*
 * Architecture-neutral syscall dispatcher for x86_64.
 *
 * This file isolates "which syscall number does what" from the two assembly
 * entry paths (int 0x80 compat trap and the native syscall instruction).
 *
 * Syscall numbers follow src/kernel/include/syscall.h (the canonical OpenOS
 * table that the i386 port has been using all along). The x86_64 port is
 * still in bring-up: most syscalls return ENOSYS until their backing kernel
 * subsystem is ported to x86_64. This file is the single place to add a
 * real backend once a subsystem becomes available.
 *
 * Calling convention: all six arguments are passed as uint64_t. The two
 * assembly entry layers are responsible for loading them from the right
 * registers (ebx/ecx/edx/esi/edi for int 0x80, rdi/rsi/rdx/r10/r8/r9 for
 * the syscall instruction).
 */

#include "../include/syscall_dispatch64.h"

#include <stddef.h>

#include "../include/early_console64.h"
#include "../include/usermode64.h"
#include "syscall.h" /* canonical SYS_* numbers (shared with i386) */

static uint64_t dispatch_total_count;
static uint64_t dispatch_enosys_count;

/* ---------------------------------------------------------------------------
 * Backends currently wired up on x86_64.
 * Each helper documents what it covers and what is still missing relative to
 * the i386 implementation, so future porting work has a clear checklist.
 * ------------------------------------------------------------------------- */

/*
 * SYS_WRITE backend.
 * Limitation: only fd=1 (stdout) and fd=2 (stderr) are recognized and the
 * output goes directly to early_console64. A real VFS-backed write path will
 * replace this once vfs64 exposes file descriptors to user mode.
 */
static uint64_t do_write(uint64_t fd, uint64_t buf_ptr, uint64_t len) {
    const char *buf;
    uint64_t i;

    if (fd != 1u && fd != 2u) {
        return (uint64_t)-1;
    }
    if (buf_ptr == 0u) {
        return (uint64_t)-1;
    }

    buf = (const char *)(uintptr_t)buf_ptr;
    for (i = 0; i < len; ++i) {
        char ch = buf[i];
        if (ch == 0) {
            break;
        }
        early_console64_putc(ch);
    }
    return i;
}

/*
 * SYS_EXIT backend.
 * Marks the current user-mode thread as exited so the kernel can reclaim it.
 * Note: the syscall-instruction entry path additionally needs to repair rcx/r11
 * before sysret; that fixup stays in syscall64.c (entry-path concern).
 */
static uint64_t do_exit(uint64_t status) {
    arch_x86_64_usermode_mark_exited((int)status);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Common dispatch table.
 * ------------------------------------------------------------------------- */

uint64_t arch_x86_64_syscall_dispatch_common(uint64_t num,
                                             uint64_t a0,
                                             uint64_t a1,
                                             uint64_t a2,
                                             uint64_t a3,
                                             uint64_t a4,
                                             uint64_t a5) {
    /* a3/a4/a5 are reserved for syscalls that take more than 3 arguments;
     * none of the wired-up cases below need them yet. Silence -Wunused. */
    (void)a3;
    (void)a4;
    (void)a5;

    ++dispatch_total_count;

    switch (num) {
    case SYS_EXIT:
        return do_exit(a0);
    case SYS_WRITE:
        return do_write(a0, a1, a2);
    case SYS_GETPID:
        /* TODO: return real PID once proc64 is wired. */
        return 1;
    default:
        ++dispatch_enosys_count;
        return (uint64_t)-1;
    }
}

void arch_x86_64_syscall_dispatch_reset(void) {
    dispatch_total_count = 0;
    dispatch_enosys_count = 0;
}

uint64_t arch_x86_64_syscall_dispatch_total(void) {
    return dispatch_total_count;
}

uint64_t arch_x86_64_syscall_dispatch_enosys(void) {
    return dispatch_enosys_count;
}
