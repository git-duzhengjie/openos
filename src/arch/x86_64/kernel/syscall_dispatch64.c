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
#include "../include/address_space64.h"
#include "../include/fdtable64.h"
#include "../include/heap64.h"
#include "../include/initrd64.h"
#include "../include/net64.h"
#include "../include/proc64.h"
#include "../include/sched64.h"
#include "../include/tsc64.h"
#include "../include/usermode64.h"
#include "../include/vfs64.h"
#include "../include/percpu64.h"
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

    /*
     * H.5a: snapshot envp onto kernel-side storage with the same lifetime
     * rationale as argv above. NULL envp -> envc=0. We reuse the
     * ARG_MAX-sized slots for env strings; KEY=VALUE pairs longer than
     * that get truncated silently (no E2BIG yet).
     */
    static char envp_storage[X86_64_USER_ENVP_MAX][X86_64_USER_ENV_MAX];
    static const char *envp_ptrs[X86_64_USER_ENVP_MAX];
    int envc_kern = 0;
    if (envp_ptr != 0) {
        const char *const *uenvp =
            (const char *const *)(uintptr_t)envp_ptr;
        for (unsigned i = 0; i < X86_64_USER_ENVP_MAX; ++i) {
            const char *u = uenvp[i];
            if (u == NULL) break;
            x86_64_size_t j = 0;
            for (; j < X86_64_USER_ENV_MAX - 1u; ++j) {
                char c = u[j];
                envp_storage[i][j] = c;
                if (c == '\0') break;
            }
            envp_storage[i][X86_64_USER_ENV_MAX - 1u] = '\0';
            envp_ptrs[i] = envp_storage[i];
            envc_kern = (int)i + 1;
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
    /*
     * H.5b.2 step A: create a fresh AS for the replacement image and mirror
     * the PT_LOADs into it via _load_image_into. CR3 is NOT switched here;
     * the legacy boot-identity ring3 path continues to back the actual
     * iretq. Step B will: (1) destroy old PCB.as, (2) set PCB.as=new_as,
     * (3) arch_x86_64_as_activate(new_as) right before iretq.
     */
    struct x86_64_address_space *new_as = arch_x86_64_as_create();
    elf64_load_result_t lr = arch_x86_64_elf64_load_image_into(file->data, file->size, new_as);
    if (lr.status != ELF64_LOADER_OK) {
        arch_x86_64_usermode_note_exec_fail();
        early_console64_write("[x86_64][exec] elf-load-failed path=");
        early_console64_write(path);
        early_console64_write(" status=");
        early_console64_write_hex64((uint64_t)(uint32_t)lr.status);
        early_console64_write("\n");
        if (new_as != ((struct x86_64_address_space *)0)) {
            arch_x86_64_as_destroy(new_as);
        }
        return (uint64_t)-1;
    }
    {
        /*
         * A2.P3-B-beta fix: CR3 must point at new_as BEFORE we destroy old_as.
         *
         * Bug found in P3-C investigation (was misdiagnosed as a high-half PT
         * gap): old_as's PML4 physical page is freed by as_destroy(). If CR3
         * still references it, the next PMM allocation can hand that page
         * back out and a subsequent write will corrupt the in-flight PML4 --
         * including PML4[511] which maps the kernel image. The next IRQ then
         * triple-faults trying to fetch the ISR.
         *
         * Order: (1) set PCB.as=new_as, (2) load CR3 from new_as, (3) free old.
         */
        struct x86_64_address_space *old_as = arch_x86_64_proc_current_get_as();
        arch_x86_64_proc_current_set_as(new_as);
        arch_x86_64_as_activate(new_as);
        if (old_as != ((struct x86_64_address_space *)0)) {
            arch_x86_64_as_destroy(old_as);
        }
    }
    early_console64_write("[x86_64][exec] path=");
    early_console64_write(path);
    early_console64_write(" entry=");
    early_console64_write_hex64((uint64_t)lr.entry);
    early_console64_write(" size=");
    early_console64_write_hex64((uint64_t)file->size);
    early_console64_write(" argc=");
    early_console64_write_hex64((uint64_t)(uint32_t)argc_kern);
    early_console64_write(" envc=");
    early_console64_write_hex64((uint64_t)(uint32_t)envc_kern);
    early_console64_write("\n");

    /*
     * Commit the replacement. set_args / set_envs queue argv/envp for the
     * next usermode_run; mark_exec stashes the new entry and flips
     * usermode_exited=1 / pending_exec=1, then return_to_kernel longjmps
     * back to usermode_run()'s saved kernel frame. Crucially we do NOT
     * call proc_exit -- the pid lives on.
     */
    arch_x86_64_usermode_set_args(argc_kern, argv_ptrs);
    arch_x86_64_usermode_set_envs(envc_kern, envp_ptrs);
    arch_x86_64_usermode_mark_exec(lr.entry);
    arch_x86_64_usermode_return_to_kernel();
    return 0;  /* unreachable */
}

/*
 * SYS_EXIT backend.
 * Marks the current user-mode thread as exited so the kernel can reclaim it.
 *
 * CONTRACT (A2.P0): this function MUST NOT return. It longjmps via
 * arch_x86_64_usermode_return_to_kernel(), restoring the kernel stack saved
 * by arch_x86_64_usermode_run(). Returning would let the syscall entry
 * wrapper sysretq into a stale ring3 RIP (#GP at CPL=3); the wrapper in
 * syscall64.c trips a loud assert if that ever happens.
 */
static uint64_t do_exit(uint64_t status) {
    /* gamma.3b-S2a Seg-6: sched-dispatched USER slot exit path.
     *
     * If we got here because a child user thread that had been dispatched
     * onto an AP via a PARKED sched_slot called exit(), we are running on
     * that AP inside the syscall wrapper. There is no usermode_run() frame
     * on our kernel stack to longjmp back to -- the AP entered ring3 from
     * sched_run's dispatch, not from usermode_run.
     *
     * Detect this condition by asking sched: is my current slot USER-typed
     * AND does its owner_proc == me AND am I not on the BSP kmain path
     * (usermode_running flag is only ever set on the BSP that owns
     * usermode_run)? If yes:
     *   1. Record the exit code + child_exited flag on the PCB so the
     *      parent's do_wait busy-loop (Seg-7) can observe it.
     *   2. Mark the sched_slot EXITED so the dispatcher will reap it on
     *      its next tick and pick another READY slot (or fall back to
     *      the AP's idle slot).
     *   3. NULL out slot.owner_proc so any subsequent proc_current() on
     *      this CPU (before reap) sees the kernel PCB, not our stale
     *      pointer.
     *   4. Call sched_run() -- it will pick another slot and
     *      context_switch away, never returning here. The dying kernel
     *      stack is safe to free asynchronously (Seg-6 punts this: the
     *      slot's stack lives until beta reap; we do not free it inline).
     *
     * The legacy longjmp branch below is untouched for BSP-hosted user
     * procs (kmain / usermode_run() loop). */
    {
        uint32_t sslot = arch_x86_64_sched_current_slot();
        struct x86_64_proc *owner =
            arch_x86_64_sched_slot_get_owner_proc(sslot);
        x86_64_proc_t *me = arch_x86_64_proc_current();
        /* usermode_is_running is a global set by BSP's usermode_run() -- it does
     * NOT tell us whether the CURRENT cpu is a BSP-usermode caller. On APs
     * child do_exit must go through the sched-USER path even while BSP is
     * still spinning inside usermode_run. Gate by CPU id. */
    int on_bsp_usermode_run =
            (arch_x86_64_this_cpu_idx() == 0u) &&
            (int)arch_x86_64_usermode_is_running();
        if (owner != NULL && owner == me && !on_bsp_usermode_run &&
            sslot != 0u) {
            me->child_exit_code = (int)status;
            me->child_exited    = true;  /* legacy self-flag; harmless */
            /* γ.4: notify the *parent* PCB so its do_wait Mode-A poll
             * on parent->child_exited actually observes the exit. me is
             * the CHILD PCB; the parent PCB was linked via me->parent_slot
             * in fork_capture (see proc64.c fork_alloc_child). */
            x86_64_proc_t *parent =
                arch_x86_64_proc_slot(me->parent_slot);
            if (parent != NULL) {
                /* γ.4 S2b — thread this child onto the parent's zombie
                 * list.
                 *
                 * 1. Remove me from parent->children_head (singly-linked
                 *    via child.sibling_next). O(N) but N is tiny.
                 * 2. Prepend me to parent->zombie_head.
                 * 3. Mirror my exit_code/pid into the parent's legacy
                 *    singleton fields so old wait() code paths keep
                 *    seeing SOMETHING (the most recent zombie). do_wait
                 *    below now consumes zombie_head, not the singleton.
                 * 4. Raise parent->child_exited so any Mode-A poller
                 *    still spinning wakes up.
                 *
                 * NB: no locking here. Every fork/wait syscall is
                 * serviced under a single-CPU dispatcher (Mode A spins,
                 * Mode B is BSP-only) and the parent isn't concurrently
                 * mutating its own children/zombie list — parent is
                 * either running (won't be here) or spinning on
                 * child_exited (won't touch the lists). do_wait's list
                 * mutation happens *after* it observes child_exited,
                 * i.e. strictly after this store. */
                uint16_t my_slot = arch_x86_64_proc_slot_of(me);
                if (parent->children_head == my_slot) {
                    parent->children_head = me->sibling_next;
                } else {
                    uint16_t cur = parent->children_head;
                    while (cur != OPENOS_X86_64_PROC_INVALID_INDEX) {
                        x86_64_proc_t *cp = arch_x86_64_proc_slot(cur);
                        if (cp == NULL) break;
                        if (cp->sibling_next == my_slot) {
                            cp->sibling_next = me->sibling_next;
                            break;
                        }
                        cur = cp->sibling_next;
                    }
                }
                me->sibling_next = OPENOS_X86_64_PROC_INVALID_INDEX;

                me->zombie_next    = parent->zombie_head;
                parent->zombie_head = my_slot;

                parent->child_exit_code = (int)status;
                parent->child_exited    = true;
            }

            (void)arch_x86_64_sched_slot_set_owner_proc(sslot, NULL);

            /* Hand control back to the scheduler. sched_exit_self marks
             * the current slot EXITED, picks the next runnable, and
             * context_switches away. Never returns. */
            arch_x86_64_sched_exit_self();

            /* Safety net: if sched_exit_self ever returned we would sysret
             * into a dying ring3 context. Halt loudly. */
            for (;;) __asm__ volatile ("cli; hlt");
        }
    }

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

/* γ.4 S2c — helper: unlink a zombie child from parent->zombie_head by pid.
 * Returns the zombie PCB pointer with zombie_next cleared, or NULL if no
 * zombie in the chain has that pid. Walks the singly-linked list with a
 * `prev` cursor so we can splice out interior nodes. */
static x86_64_proc_t *try_reap_zombie_by_pid(x86_64_proc_t *parent, uint32_t pid) {
    uint16_t prev = OPENOS_X86_64_PROC_INVALID_INDEX;
    uint16_t cur  = parent->zombie_head;
    while (cur != OPENOS_X86_64_PROC_INVALID_INDEX) {
        x86_64_proc_t *z = arch_x86_64_proc_slot(cur);
        if (z == NULL) break;
        if (z->pid == pid) {
            uint16_t nxt = z->zombie_next;
            if (prev == OPENOS_X86_64_PROC_INVALID_INDEX) {
                parent->zombie_head = nxt;
            } else {
                x86_64_proc_t *pp = arch_x86_64_proc_slot(prev);
                if (pp != NULL) pp->zombie_next = nxt;
            }
            z->zombie_next = OPENOS_X86_64_PROC_INVALID_INDEX;
            return z;
        }
        prev = cur;
        cur  = z->zombie_next;
    }
    return NULL;
}

/* γ.4 S2c — helper: is `pid` currently in parent's live children list?
 * Used by waitpid(specific_pid) to decide between "block until this one
 * exits" and "ECHILD, we've never heard of it". */
static bool is_live_child_pid(x86_64_proc_t *parent, uint32_t pid) {
    uint16_t cur = parent->children_head;
    while (cur != OPENOS_X86_64_PROC_INVALID_INDEX) {
        x86_64_proc_t *c = arch_x86_64_proc_slot(cur);
        if (c == NULL) break;
        if (c->pid == pid) return true;
        cur = c->sibling_next;
    }
    return false;
}

/* γ.4 S2c — write the classic wait status word (exit code in bits 8..15)
 * to user memory, tolerating USER_VBASE mapping. Shared between the
 * wait-any and waitpid(pid) return paths. */
static void wait_write_status(uint64_t status_ptr, int code) {
    if (status_ptr == 0) return;
    uintptr_t status_addr = (uintptr_t)status_ptr;
    if (status_addr >= OPENOS_X86_64_USER_VBASE) {
        status_addr -= OPENOS_X86_64_USER_VBASE;
    }
    int *status_out = (int *)status_addr;
    *status_out = (code & 0xFF) << 8;
}

static uint64_t do_wait_common(uint64_t pid_arg, uint64_t status_ptr, int use_pid) {
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == NULL) return (uint64_t)-1;

    /* γ.4 S2c — waitpid(specific_pid) fast path. Separate from wait-any
     * because the semantics differ: we don't pop the zombie_head LIFO,
     * we pluck the matching pid out of the chain (possibly interior).
     * If the pid is a live child we busy-poll until do_exit moves it
     * onto zombie_head, then splice-out.  A pid that is neither live
     * nor zombie is ECHILD. */
    if (use_pid && pid_arg != (uint64_t)-1 && pid_arg != 0) {
        uint32_t target_pid = (uint32_t)pid_arg;
        /* Already dead? */
        x86_64_proc_t *z = try_reap_zombie_by_pid(p, target_pid);
        if (z == NULL) {
            /* Not dead. Live child? If not, ECHILD. */
            if (!is_live_child_pid(p, target_pid)) {
                return (uint64_t)-1;
            }
            /* Live child — spin until do_exit lifts it onto zombie_head,
             * then splice out. Same STI/CLI dance as the wait-any path so
             * the LAPIC keeps ticking. */
            __asm__ volatile ("sti" ::: "memory");
            while ((z = try_reap_zombie_by_pid(p, target_pid)) == NULL) {
                __asm__ volatile ("pause" ::: "memory");
            }
            __asm__ volatile ("cli" ::: "memory");
        }
        /* Got the zombie. Drain flag mirrors zombie_head emptiness. */
        int code      = z->child_exit_code;
        int child_pid = (int)z->pid;
        if (p->zombie_head == OPENOS_X86_64_PROC_INVALID_INDEX) {
            p->child_exited    = false;
            p->child_pid       = 0;
            p->child_exit_code = 0;
        }
        wait_write_status(status_ptr, code);
        p->fork_pending = 0;
        return (uint64_t)(uint32_t)child_pid;
    }

    /* γ.4 S2b — no live children AND no zombies => nothing to wait for.
     * children_head is the live-child list; zombie_head is the
     * exited-but-not-reaped list. Either being non-empty means there is
     * (or will be) something to reap. Legacy: if a caller hand-set
     * child_pid without going through fork() we still honor it via the
     * old singleton fallback below. */
    if (p->children_head == OPENOS_X86_64_PROC_INVALID_INDEX &&
        p->zombie_head   == OPENOS_X86_64_PROC_INVALID_INDEX &&
        p->child_pid == 0) {
        return (uint64_t)-1;
    }
    /* waitpid(-1) falls through to the wait-any path below; specific-pid
     * cases were handled by the S2c fast path above. */
    (void)use_pid;

    int code;
    int child_pid;
    /* gamma.3b-S2a Seg-7: two-mode wait.
     *
     * Mode A (S2a new): child is running as a real sched_slot on an AP.
     *   fork_pending was cleared by fork_capture after slot_wakeup, so we
     *   detect Mode A by (fork_pending == 0 && child_pid != 0). In this
     *   mode we simply spin on p->child_exited, which the AP-side do_exit
     *   Seg-6 branch will set via arch_x86_64_proc_slot(ppid). Busy-loop
     *   with a pause hint; hlt / IPI wakeup is S2b territory.
     *
     * Mode B (legacy): fork_pending is still set -> child never went to
     *   an AP, so kmain/usermode_run has to hand-dispatch it under the
     *   parent's kernel stack. This is the pre-S2a path; unchanged. */
    if (p->fork_pending == 0u) {
        /* Mode A: busy-poll until AP child sets parent->child_exited via
         * cross-CPU write in do_exit. Interrupts were masked on syscall
         * entry by FMASK; re-enable IF for the wait so the LAPIC can keep
         * time-slicing this CPU (not strictly required for correctness —
         * the AP-side write is cache-coherent — but keeps the CPU
         * responsive to other work). */
        __asm__ volatile ("sti" ::: "memory");
        while (p->zombie_head == OPENOS_X86_64_PROC_INVALID_INDEX &&
               !p->child_exited) {
            __asm__ volatile ("pause" ::: "memory");
        }
        __asm__ volatile ("cli" ::: "memory");

        /* γ.4 S2b — pop one zombie off the head. Order is LIFO (newest
         * exit first) which is fine because user-space is expected to
         * treat wait() results as an unordered set. If zombie_head is
         * empty here it means the legacy singleton path fired (fork
         * without list linkage, or Mode B fallback); fall through. */
        if (p->zombie_head != OPENOS_X86_64_PROC_INVALID_INDEX) {
            uint16_t zslot = p->zombie_head;
            x86_64_proc_t *z = arch_x86_64_proc_slot(zslot);
            if (z != NULL) {
                p->zombie_head = z->zombie_next;
                z->zombie_next = OPENOS_X86_64_PROC_INVALID_INDEX;
                code      = z->child_exit_code;
                child_pid = (int)z->pid; /* child's own pid */

                /* Once drained, clear the legacy "has zombie" flag so a
                 * subsequent wait() call blocks correctly. If there are
                 * more zombies still queued, keep it set. */
                if (p->zombie_head == OPENOS_X86_64_PROC_INVALID_INDEX) {
                    p->child_exited = false;
                }
            } else {
                code      = p->child_exit_code;
                child_pid = p->child_pid;
                p->child_exited = false;
            }
        } else {
            code      = p->child_exit_code;
            child_pid = p->child_pid;
            p->child_exited = false;
        }
    } else {
        /* Mode B: legacy hand-dispatch. */
        code = arch_x86_64_usermode_run_pending_child_for_wait();
        if (!p->child_exited) return (uint64_t)-1;
        child_pid = p->child_pid;
        p->child_exited    = false;
        p->child_exit_code = 0;
    }

    /* Minimal wait status encoding: normal exit, code in bits 8..15.
     * P4.c keeps user-visible stack pointers in USER_VBASE while the
     * kernel writes the same pages through their low-half identity alias. */
    wait_write_status(status_ptr, code);

    /* Legacy singleton mirrors: only clear when *no* zombies remain queued;
     * otherwise subsequent wait() calls would treat the parent as having no
     * children left. */
    if (p->zombie_head == OPENOS_X86_64_PROC_INVALID_INDEX) {
        p->child_pid       = 0;
        p->child_exit_code = 0;
    }
    p->fork_pending = 0;
    return (uint64_t)(uint32_t)child_pid;
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
    case SYS_WAIT:        return do_wait_common(0, a0, 0);
    case SYS_WAITPID:     return do_wait_common(a0, a1, 1);

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
