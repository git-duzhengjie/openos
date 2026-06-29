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
#include "../include/elf64_loader.h"
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
 * H.3 SYS_EXEC backend.
 *
 * execve(path, argv, envp) -- the POSIX-like "replace this process image"
 * call. argv/envp are accepted but ignored for now (H.x will wire them up
 * once we have a real userspace stack-arg builder).
 *
 * Flow:
 *   1) Look up `path` in the initrd. ENOENT -> -1 (no replacement happened,
 *      caller may handle gracefully).
 *   2) Hand the file off to elf64_loader. If load fails -> -1.
 *   3) Stash the new entry in usermode64 via mark_exec() and longjmp back
 *      to the kernel stack via return_to_kernel(). The outer driver in
 *      kernel64.c observes pending_exec and loops back into ring3 on the
 *      new image -- pid stays the same.
 *
 * On the -1 paths we explicitly do NOT touch usermode state, so a failing
 * execve looks just like a normal failing syscall: ring3 keeps running on
 * the *current* image and can handle it (typically by calling exit()).
 */
static uint64_t do_exec(uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr) {
    (void)envp_ptr;
    if (path_ptr == 0) {
        arch_x86_64_usermode_note_exec_fail();
        return (uint64_t)-1;
    }
    /*
     * IMPORTANT: copy `path` onto the kernel stack BEFORE we touch the
     * loader. Reason: under the H.2/H.3 identity-mapped layout the ring3
     * .rodata lives inside the very VA range (e.g. 0x400000..) that
     * elf64_load_image is about to overwrite with the *new* image. If we
     * keep using the original ring3 pointer past load_image, subsequent
     * dereferences (logging, find()) would read garbage from the new image
     * mapped at the same address.
     */
    char path_buf[128];
    {
        const char *src = (const char *)(uintptr_t)path_ptr;
        x86_64_size_t i = 0;
        for (; i < sizeof(path_buf) - 1u; ++i) {
            char c = src[i];
            path_buf[i] = c;
            if (c == '\0') break;
        }
        path_buf[sizeof(path_buf) - 1u] = '\0';
    }
    const char *path = path_buf;

    /*
     * H.4: snapshot argv onto kernel-side storage BEFORE elf load. Same
     * hazard as `path` above -- the source pointers may live in the
     * ring3 image about to be overwritten. We cap at
     * X86_64_USER_ARGV_MAX entries and X86_64_USER_ARG_MAX-1 bytes per
     * entry; anything beyond is silently truncated (POSIX-equivalent
     * E2BIG is not exposed yet). NULL argv is acceptable -> argc=0.
     * The storage is `static` because do_exec is single-entrant under
     * the current uniprocessor scheduling model and we want it out of
     * the (limited) kernel stack budget.
     */
    static char argv_storage[X86_64_USER_ARGV_MAX][X86_64_USER_ARG_MAX];
    static const char *argv_ptrs[X86_64_USER_ARGV_MAX];
    int argc_kern = 0;
    if (argv_ptr != 0) {
        const char *const *uargv =
            (const char *const *)(uintptr_t)argv_ptr;
        for (unsigned i = 0; i < X86_64_USER_ARGV_MAX; ++i) {
            const char *u = uargv[i];
            if (u == NULL) break;
            x86_64_size_t j = 0;
            for (; j < X86_64_USER_ARG_MAX - 1u; ++j) {
                char c = u[j];
                argv_storage[i][j] = c;
                if (c == '\0') break;
            }
            argv_storage[i][X86_64_USER_ARG_MAX - 1u] = '\0';
            argv_ptrs[i] = argv_storage[i];
            argc_kern = (int)i + 1;
        }
    }

    const x86_64_initrd_file_t *file = arch_x86_64_initrd_find(path);
    if (file == NULL) {
        arch_x86_64_usermode_note_exec_fail();
        early_console64_write("[x86_64][exec] ENOENT path=");
        early_console64_write(path);
        early_console64_write("\n");
        return (uint64_t)-1;
    }
    elf64_load_result_t lr = arch_x86_64_elf64_load_image(file->data, file->size);
    if (lr.status != ELF64_LOADER_OK) {
        arch_x86_64_usermode_note_exec_fail();
        early_console64_write("[x86_64][exec] elf-load-failed path=");
        early_console64_write(path);
        early_console64_write(" status=");
        early_console64_write_hex64((uint64_t)(uint32_t)lr.status);
        early_console64_write("\n");
        return (uint64_t)-1;
    }
    early_console64_write("[x86_64][exec] path=");
    early_console64_write(path);
    early_console64_write(" entry=");
    early_console64_write_hex64((uint64_t)lr.entry);
    early_console64_write(" size=");
    early_console64_write_hex64((uint64_t)file->size);
    early_console64_write(" argc=");
    early_console64_write_hex64((uint64_t)(uint32_t)argc_kern);
    early_console64_write("\n");

    /*
     * Commit the replacement. set_args queues argv for the next
     * usermode_run; mark_exec stashes the new entry and flips
     * usermode_exited=1 / pending_exec=1, then return_to_kernel longjmps
     * back to usermode_run()'s saved kernel frame. Crucially we do NOT
     * call proc_exit -- the pid lives on.
     */
    arch_x86_64_usermode_set_args(argc_kern, argv_ptrs);
    arch_x86_64_usermode_mark_exec(lr.entry);
    arch_x86_64_usermode_return_to_kernel();
    return 0;  /* unreachable */
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
    /* -------- H.3 execve -------- */
    case SYS_EXEC:        return do_exec(a0, a1, a2);

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
