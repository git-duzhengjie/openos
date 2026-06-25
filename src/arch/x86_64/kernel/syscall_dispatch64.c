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
#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/fdtable64.h"
#include "../include/heap64.h"
#include "../include/initrd64.h"
#include "../include/sched64.h"
#include "../include/usermode64.h"
#include "../include/vfs64.h"
#include "syscall.h" /* canonical SYS_* numbers (shared with i386) */

static uint64_t dispatch_total_count;
static uint64_t dispatch_enosys_count;
static uint64_t dispatch_per_num_count[8];   /* SYS_EXIT..SYS_GETPID hot path */

/* ---------------------------------------------------------------------------
 * Tiny helpers (avoid pulling in libc-style headers).
 * ------------------------------------------------------------------------- */

static x86_64_size_t k_strlen(const char *s) {
    x86_64_size_t n = 0;
    if (s == NULL) return 0;
    while (s[n] != '\0') ++n;
    return n;
}

static int k_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { ++a; ++b; }
    return (int)((unsigned char)*a) - (int)((unsigned char)*b);
}

/*
 * Validate a user-mode pointer + length. Step-B implementation is permissive
 * (kernel and user share the higher-half identity map), but we still reject
 * NULL + non-zero length so bad call sites surface early.
 */
static int validate_user_buf(uint64_t ptr, uint64_t len) {
    if (len == 0) return 1;             /* zero-length is always OK */
    if (ptr == 0) return 0;             /* NULL with len > 0 is bogus */
    return 1;
}

/* ---------------------------------------------------------------------------
 * Backends currently wired up on x86_64.
 * Each helper documents what it covers and what is still missing relative to
 * the i386 implementation, so future porting work has a clear checklist.
 * ------------------------------------------------------------------------- */

/*
 * SYS_WRITE: routes through fdtable64 so fd=1/2 hit early_console64 and any
 * future writable fd (none yet) plugs in centrally.
 */
static uint64_t do_write(uint64_t fd, uint64_t buf_ptr, uint64_t len) {
    if (!validate_user_buf(buf_ptr, len)) return (uint64_t)-1;
    int n = arch_x86_64_fd_write((int)fd,
                                 (const void *)(uintptr_t)buf_ptr,
                                 (x86_64_size_t)len);
    if (n < 0) return (uint64_t)-1;
    return (uint64_t)n;
}

/*
 * SYS_READ: only initrd-backed read-only files are supported. fd=0 returns 0
 * (no input device yet); writable fds reject with -1.
 */
static uint64_t do_read(uint64_t fd, uint64_t buf_ptr, uint64_t len) {
    if (!validate_user_buf(buf_ptr, len)) return (uint64_t)-1;
    int n = arch_x86_64_fd_read((int)fd,
                                (void *)(uintptr_t)buf_ptr,
                                (x86_64_size_t)len);
    if (n < 0) return (uint64_t)-1;
    return (uint64_t)n;
}

/*
 * SYS_OPEN: read-only, looks up path inside the embedded initrd64 image.
 * Flags/mode are accepted but ignored — any write attempt later will be
 * rejected by fd_write since the node is held read-only.
 */
static uint64_t do_open(uint64_t path_ptr, uint64_t flags, uint64_t mode) {
    (void)flags;
    (void)mode;
    if (path_ptr == 0) return (uint64_t)-1;
    const char *path = (const char *)(uintptr_t)path_ptr;
    const x86_64_vfs_node_t *node = arch_x86_64_vfs_lookup(path);
    if (node == NULL) return (uint64_t)-1;
    int fd = arch_x86_64_fd_open(node);
    if (fd < 0) return (uint64_t)-1;
    return (uint64_t)fd;
}

static uint64_t do_close(uint64_t fd) {
    int r = arch_x86_64_fd_close((int)fd);
    return (r < 0) ? (uint64_t)-1 : 0;
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

/*
 * SYS_MALLOC / SYS_FREE: thin wrappers around heap64. Pointers are returned
 * as uint64_t (full 64-bit); callers must treat the value as a pointer, not
 * truncate to 32-bit as the i386 path does.
 */
static uint64_t do_malloc(uint64_t size) {
    void *p = arch_x86_64_kmalloc((x86_64_size_t)size);
    return (uint64_t)(uintptr_t)p;
}

static uint64_t do_free(uint64_t ptr) {
    arch_x86_64_kfree((void *)(uintptr_t)ptr);
    return 0;
}

/*
 * SYS_UPTIME_MS: derived from the TSC. We don't yet know the host CPU
 * frequency, so we report an artificial 1 GHz tick (TSC >> 20 ≈ ms). This is
 * good enough for monotonicity-only consumers; a real calibration will land
 * with the PIT/HPET driver.
 */
static uint64_t do_uptime_ms(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc = ((uint64_t)hi << 32) | lo;
    return tsc >> 20;
}

/*
 * SYS_YIELD: scheduler does not yet support preemption on x86_64. Until
 * arch_x86_64_sched_yield() lands we just return success so cooperative loops
 * compile and run without spinning the dispatcher.
 */
static uint64_t do_yield(void) {
    /* TODO: hook into sched64 once a yield primitive is exposed. */
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
    /* a3..a5 reserved for syscalls with more than 3 args; not used yet. */
    (void)a3;
    (void)a4;
    (void)a5;

    ++dispatch_total_count;

    switch (num) {
    /* -------- process / thread -------- */
    case SYS_EXIT:        return do_exit(a0);
    case SYS_GETPID:      return 1;     /* TODO: real PID via proc64 */
    case SYS_GETTID:      return 1;     /* TODO: real TID via sched64 */
    case SYS_GETPPID:     return 0;
    case SYS_GETUID:      return 0;     /* root-only world for now */
    case SYS_GETGID:      return 0;
    case SYS_YIELD:       return do_yield();

    /* -------- I/O (read-only initrd + early console) -------- */
    case SYS_WRITE:       return do_write(a0, a1, a2);
    case SYS_READ:        return do_read(a0, a1, a2);
    case SYS_READ_FD:     return do_read(a0, a1, a2);
    case SYS_OPEN:        return do_open(a0, a1, a2);
    case SYS_CLOSE:       return do_close(a0);

    /* -------- memory -------- */
    case SYS_MALLOC:      return do_malloc(a0);
    case SYS_FREE:        return do_free(a0);

    /* -------- time -------- */
    case SYS_UPTIME_MS:   return do_uptime_ms();

    default:
        ++dispatch_enosys_count;
        return (uint64_t)-1;
    }
}

void arch_x86_64_syscall_dispatch_reset(void) {
    dispatch_total_count = 0;
    dispatch_enosys_count = 0;
    for (unsigned i = 0; i < sizeof(dispatch_per_num_count)/sizeof(dispatch_per_num_count[0]); ++i)
        dispatch_per_num_count[i] = 0;
}

uint64_t arch_x86_64_syscall_dispatch_total(void) {
    return dispatch_total_count;
}

uint64_t arch_x86_64_syscall_dispatch_enosys(void) {
    return dispatch_enosys_count;
}

/* Silence unused-static warnings for helpers we kept for forthcoming wiring. */
static void __attribute__((unused)) _keep_alive_refs(void) {
    (void)k_strcmp;
    (void)k_strlen;
    (void)dispatch_per_num_count;
}
