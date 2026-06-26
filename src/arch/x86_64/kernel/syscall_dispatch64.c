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
#include "../include/net64.h"
#include "../include/proc64.h"
#include "../include/sched64.h"
#include "../include/tsc64.h"
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
    /*
     * Step D.3 fix: do NOT return -- otherwise dispatch_common returns,
     * the syscall entry path does sysretq, and ring3 keeps executing the
     * instruction after 'syscall' (a 'hlt' in the _start epilogue), which
     * #GP's at CPL=3.  Instead, unwind back to the kernel stack saved by
     * usermode_run() and let it observe usermode_exited=1.
     */
    arch_x86_64_usermode_return_to_kernel();
    return 0;  /* unreachable */
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
 * SYS_UPTIME_MS: real millisecond uptime, calibrated against the i8254 PIT
 * during early boot (see tsc64.c). If calibration somehow failed (per_ms==0)
 * we fall back to the legacy `rdtsc >> 20` placeholder so the call stays
 * monotonic instead of returning a flat 0 — that keeps existing consumers
 * working even on exotic hosts where the PIT poll didn't fire.
 */
static uint64_t do_uptime_ms(void) {
    uint64_t ms = arch_x86_64_tsc_uptime_ms();
    if (ms != 0) return ms;
    if (arch_x86_64_tsc_per_ms() != 0) return 0; /* calibrated, just early */
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc = ((uint64_t)hi << 32) | lo;
    return tsc >> 20;
}

/*
 * SYS_YIELD: Step E.1 routes the call through proc64's cooperative yield
 * counter. The dispatcher itself stays branchless so future sched64 work
 * only has to swap proc64_yield()'s body for a real reschedule. We still
 * return 0 (success) — POSIX semantics for sched_yield().
 */
static uint64_t do_yield(void) {
    (void)arch_x86_64_proc_yield();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Step E.3 — loopback socket backends.
 *
 * These thin wrappers translate the user-mode uint64_t arg vector into the
 * net64 API. The only signature subtlety is that sendto/recvfrom take a
 * destination/source port as the last argument; we use a4 (the fifth slot)
 * so that a3 (POSIX flags) stays available for a future real stack without
 * breaking the ABI.
 * ------------------------------------------------------------------------- */
static uint64_t do_socket(uint64_t domain, uint64_t type, uint64_t protocol) {
    int fd = arch_x86_64_net_socket((int)domain, (int)type, (int)protocol);
    if (fd < 0) return (uint64_t)-1;
    return (uint64_t)fd;
}

static uint64_t do_bind(uint64_t fd, uint64_t port) {
    int r = arch_x86_64_net_bind((int)fd, (uint16_t)port);
    return (r < 0) ? (uint64_t)-1 : 0;
}

static uint64_t do_sendto(uint64_t fd,
                          uint64_t buf_ptr,
                          uint64_t len,
                          uint64_t flags,
                          uint64_t dst_port) {
    (void)flags;
    if (!validate_user_buf(buf_ptr, len)) return (uint64_t)-1;
    int n = arch_x86_64_net_sendto((int)fd,
                                   (const void *)(uintptr_t)buf_ptr,
                                   (x86_64_size_t)len,
                                   (uint16_t)dst_port);
    if (n < 0) return (uint64_t)-1;
    return (uint64_t)n;
}

static uint64_t do_recvfrom(uint64_t fd,
                            uint64_t buf_ptr,
                            uint64_t len,
                            uint64_t flags,
                            uint64_t src_port_out_ptr) {
    (void)flags;
    if (!validate_user_buf(buf_ptr, len)) return (uint64_t)-1;
    /*
     * src_port_out is an optional uint16_t* in user memory. We accept NULL
     * by passing through to net64_recvfrom which already handles it.
     */
    int n = arch_x86_64_net_recvfrom((int)fd,
                                     (void *)(uintptr_t)buf_ptr,
                                     (x86_64_size_t)len,
                                     (uint16_t *)(uintptr_t)src_port_out_ptr);
    if (n < 0) return (uint64_t)-1;
    return (uint64_t)n;
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
    /* a3..a5 reserved for syscalls with more than 3 args; Step E.3 uses a3/a4
     * for sendto/recvfrom (flags + port slot). a5 is still unused. */
    (void)a5;

    ++dispatch_total_count;

    switch (num) {
    /* -------- process / thread -------- */
    case SYS_EXIT:        return do_exit(a0);
    /* Step E.1: identity syscalls now read proc64's current PCB. */
    case SYS_GETPID:      return (uint64_t)arch_x86_64_proc_current_pid();
    case SYS_GETTID:      return (uint64_t)arch_x86_64_proc_current_tid();
    case SYS_GETPPID:     return (uint64_t)arch_x86_64_proc_current_ppid();
    case SYS_GETUID:      return (uint64_t)arch_x86_64_proc_current_uid();
    case SYS_GETGID:      return (uint64_t)arch_x86_64_proc_current_gid();
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

    /* -------- net (Step E.3, loopback only) -------- */
    case SYS_SOCKET:      return do_socket(a0, a1, a2);
    case SYS_BIND:        return do_bind(a0, a1);
    case SYS_SENDTO:      return do_sendto(a0, a1, a2, a3, a4);
    case SYS_RECVFROM:    return do_recvfrom(a0, a1, a2, a3, a4);

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
