/*
 * proc64.c — minimal PCB pool for the x86_64 port (Step E.1).
 *
 * Implementation notes:
 *   - 8 statically-allocated slots, no heap usage. Slot 0 is reserved
 *     for the kernel proc and never freed.
 *   - PIDs are slot index + 1 so pid=0 stays an "invalid" sentinel.
 *   - `current` is a plain index; everyone reads it without locking
 *     because the port is single-CPU, non-preemptible.
 *   - No reaper / wait4 yet: an exited slot is just marked FREE so the
 *     next spawn can reuse it.
 *
 * The dispatcher consumes this through arch_x86_64_proc_current_*()
 * helpers, which keeps the "who am I?" decision in one place.
 */

#include "../include/proc64.h"
#include "../include/address_space64.h"

#include <stddef.h>

#include "../include/early_console64.h"
#include "../include/sched64.h"
#include "../include/percpu64.h"

static x86_64_proc_t proc_table[OPENOS_X86_64_PROC_MAX];

/* gamma.3b-S2a Seg-2 (岔路5方案B): current_index migrated from
 * percpu.current_proc_slot to a sched_slot reverse lookup.
 *
 * Seg-1 moved the field into %gs:0x80 so AP-side syscalls could see
 * their own PCB, but that left proc/sched with two parallel truths
 * (percpu.current_proc_slot vs sched_current_slot()->owner_proc) that
 * we would have to keep in sync forever. Seg-2 collapses this: sched
 * owns the running-slot cursor per-CPU (sched_pc_current()), and each
 * slot carries an owner_proc back-pointer set by fork_alloc_child.
 * proc_current() therefore reads sched_current_slot() -> owner_proc
 * -> proc_table[]. When the current slot is the kernel/idle slot
 * (owner_proc==NULL), we fall back to slot 0 (the kernel PCB).
 *
 * Concurrency: sched_current_slot() reads a percpu word, so it is
 * inherently per-CPU. owner_proc is set once before slot_wakeup and
 * cleared once at reap, so no cross-CPU race on this field either. */
static inline uint16_t current_index_get(void) {
    uint32_t sslot = arch_x86_64_sched_current_slot();
    struct x86_64_proc *owner =
        arch_x86_64_sched_slot_get_owner_proc(sslot);
    if (owner == NULL) {
        /* kernel / idle / bootstrap slot -> report kernel PCB (slot 0).
         * All kernel-context callers (syscall dispatcher on BSP before
         * first user dispatch, timer tick logging, sched selftest) hit
         * this path. */
        return 0u;
    }
    /* Recover slot index via pointer arithmetic against proc_table[]. */
    ptrdiff_t idx = (x86_64_proc_t *)owner - &proc_table[0];
    if (idx < 0 || idx >= (ptrdiff_t)OPENOS_X86_64_PROC_MAX) return 0u;
    return (uint16_t)idx;
}
static inline void current_index_set(uint16_t slot) {
    /* 方案B: proc侧不再直接写 running-slot cursor. sched 是唯一真相源:
     *   - AP侧的 user proc 靠 fork_alloc_child 绑 owner_proc 后 slot_wakeup,
     *     dispatcher 消费 PARKED slot 时自然把 sched_pc_current() 指到它.
     *   - BSP 法人 (kernel proc slot 0 / spawn_user 后的旧式路径) 目前
     *     都新欣写 slot 0 or spawned pid; 为了不破旧接口语义, 在这里
     *     把目标 slot 绑 sched 当前 slot 的 owner_proc, 以后反查就能拿到.
     *   - slot==0 (内核 PCB) 则把 owner_proc 置 NULL 回归默认.
     */
    uint32_t sslot = arch_x86_64_sched_current_slot();
    if (slot >= OPENOS_X86_64_PROC_MAX) return;
    if (slot == 0u) {
        (void)arch_x86_64_sched_slot_set_owner_proc(sslot, NULL);
    } else {
        (void)arch_x86_64_sched_slot_set_owner_proc(
            sslot, &proc_table[slot]);
    }
}
static uint64_t      yield_count;
static uint64_t      spawn_count;
static uint64_t      exit_count;
static uint32_t      next_child_pid;

/* ------------------------------------------------------------------- */
/* helpers                                                              */
/* ------------------------------------------------------------------- */

static void proc_copy_name(char *dst, const char *src) {
    unsigned i = 0;
    if (src != NULL) {
        for (; i + 1 < OPENOS_X86_64_PROC_NAME_MAX && src[i] != '\0'; ++i)
            dst[i] = src[i];
    }
    for (; i < OPENOS_X86_64_PROC_NAME_MAX; ++i)
        dst[i] = '\0';
}

static uint16_t proc_alloc_slot(void) {
    /* slot 0 is reserved for the kernel proc */
    for (uint16_t i = 1; i < OPENOS_X86_64_PROC_MAX; ++i) {
        if (proc_table[i].state == OPENOS_X86_64_PROC_FREE)
            return i;
    }
    return OPENOS_X86_64_PROC_INVALID_INDEX;
}

/* ------------------------------------------------------------------- */
/* lifecycle                                                            */
/* ------------------------------------------------------------------- */

void arch_x86_64_proc_init(void) {
    for (unsigned i = 0; i < OPENOS_X86_64_PROC_MAX; ++i) {
        proc_table[i].state = OPENOS_X86_64_PROC_FREE;
        proc_table[i].as = (struct x86_64_address_space *)0;
    }
    /* slot 0 = kernel proc (pid=1, tid=1, ppid=0). */
    proc_table[0].pid = 1;
    proc_table[0].tid = 1;
    proc_table[0].ppid = 0;
    proc_table[0].uid = 0;
    proc_table[0].gid = 0;
    proc_table[0].exit_code = 0;
    proc_table[0].state = OPENOS_X86_64_PROC_RUNNING;
    proc_copy_name(proc_table[0].name, "kernel");
    proc_table[0].child_slot  = OPENOS_X86_64_PROC_INVALID_INDEX;
    proc_table[0].parent_slot = OPENOS_X86_64_PROC_INVALID_INDEX;
    proc_table[0].children_head = OPENOS_X86_64_PROC_INVALID_INDEX;
    proc_table[0].zombie_head   = OPENOS_X86_64_PROC_INVALID_INDEX;
    proc_table[0].sibling_next  = OPENOS_X86_64_PROC_INVALID_INDEX;
    proc_table[0].zombie_next   = OPENOS_X86_64_PROC_INVALID_INDEX;
    proc_table[0].fork_child_sched_slot = 0u; /* gamma.3b-alpha: invalid */
    /* M4.4b: the kernel/init process is its own session + group leader. */
    proc_table[0].pgid = proc_table[0].pid;
    proc_table[0].sid  = proc_table[0].pid;
    /* BSS zero-init already leaves every percpu.current_proc_slot at 0,
     * so this store is redundant but explicit for BSP clarity. */
    current_index_set(0);
    yield_count = 0;
    spawn_count = 0;
    exit_count = 0;
    next_child_pid = 2;
}

uint32_t arch_x86_64_proc_spawn_user(const char *name) {
    uint16_t slot = proc_alloc_slot();
    if (slot == OPENOS_X86_64_PROC_INVALID_INDEX) return 0;

    x86_64_proc_t *p = &proc_table[slot];
    p->pid = (uint32_t)slot + 1;
    p->tid = p->pid; /* one thread per proc for now */
    /* parent is whoever is current at spawn time */
    p->ppid = proc_table[current_index_get()].pid;
    p->uid = 0;
    p->gid = 0;
    p->exit_code = 0;
    p->state = OPENOS_X86_64_PROC_RUNNING;
    proc_copy_name(p->name, name);
    /* M4.2: fresh signal state — no pending, nothing blocked, all
     * dispositions default. Prevents inheriting stale bits from a
     * previously-freed slot. */
    x86_64_signal_state_init(&p->sig);
    /* M4.4b: a freshly spawned process leads its own group + session
     * (pgid == sid == pid) until setpgid()/setsid() move it. */
    p->pgid = p->pid;
    p->sid  = p->pid;
    /* H.5b.1: AS slot reserved; CR3 still points at the shared
     * boot-time PML4 until H.5b.3 wires up per-process AS. */
    p->as = (struct x86_64_address_space *)0;

    /* A2.P3-B: ensure a freshly-spawned proc never inherits stale
     * fork-pending state from a previously-freed slot. */
    p->fork_pending     = 0;
    p->fork_via_syscall = 0;
    p->fork_user_rsp    = 0;
    p->child_pid        = 0;
    p->child_exited     = false;
    p->child_exit_code  = 0;
    p->wait_in_progress = false;
    p->child_slot       = OPENOS_X86_64_PROC_INVALID_INDEX;
    p->parent_slot      = OPENOS_X86_64_PROC_INVALID_INDEX;
    p->children_head    = OPENOS_X86_64_PROC_INVALID_INDEX;
    p->zombie_head      = OPENOS_X86_64_PROC_INVALID_INDEX;
    p->sibling_next     = OPENOS_X86_64_PROC_INVALID_INDEX;
    p->zombie_next      = OPENOS_X86_64_PROC_INVALID_INDEX;
    p->fork_child_sched_slot = 0u; /* gamma.3b-alpha: PARKED slot idx */

    current_index_set(slot);
    ++spawn_count;
    return p->pid;
}

void arch_x86_64_proc_exit(int code) {
    uint16_t cur = current_index_get();
    x86_64_proc_t *p = &proc_table[cur];
    if (cur == 0) {
        /* Kernel proc never really exits; record the code for diagnostics. */
        p->exit_code = code;
        return;
    }
    /* A2.P3-B-alpha: if this exiting proc has a pending fork queued, do
     * NOT free its slot or rotate back to the kernel proc yet. The main
     * loop needs to find the same PCB with fork_pending still set so it
     * can re-enter ring3 as the child. The child's own SYS_EXIT will
     * fall through to this function with fork_pending cleared (resume
     * path zeros it) and complete the teardown. */
    if (p->fork_pending) {
        p->exit_code = code;
        /* leave state / current_proc_slot / as untouched */
        ++exit_count;
        return;
    }
    p->exit_code = code;
    p->state = OPENOS_X86_64_PROC_FREE; /* immediate reap (no wait4 yet) */
    /* H.5b.1: AS pointer is always NULL today; H.5b.3 will replace
     * this with a destroy-AS call before rotating CR3 back. */
    p->as = (struct x86_64_address_space *)0;
    ++exit_count;
    /* rotate back to the kernel proc */
    current_index_set(0);
}

uint32_t arch_x86_64_proc_alloc_child_pid(x86_64_proc_t *parent) {
    if (parent == NULL) return 0;
    if (parent->child_pid != 0 && !parent->child_exited) {
        return 0;
    }

    uint32_t pid = next_child_pid++;
    if (next_child_pid == 0) {
        next_child_pid = 2;
    }

    parent->child_pid = pid;
    parent->child_exited = false;
    parent->child_exit_code = 0;
    parent->wait_in_progress = false;
    return pid;
}

/* ------------------------------------------------------------------- */
/* queries                                                              */
/* ------------------------------------------------------------------- */

x86_64_proc_t *arch_x86_64_proc_current(void) {
    /* per-CPU: each CPU has its own `current_proc_slot` in %gs:0x80,
     * BSS-zeroed to point at slot 0 (the long-lived kernel PCB). Safe
     * to call from any CPU once arch_x86_64_percpu_install_gs() has
     * run for that CPU (BSP: kernel64.c:71; APs: smp64.c). */
    return &proc_table[current_index_get()];
}

uint32_t arch_x86_64_proc_current_pid(void)  { return proc_table[current_index_get()].pid; }
uint32_t arch_x86_64_proc_current_tid(void)  { return proc_table[current_index_get()].tid; }
uint32_t arch_x86_64_proc_current_ppid(void) { return proc_table[current_index_get()].ppid; }
uint32_t arch_x86_64_proc_current_uid(void)  { return proc_table[current_index_get()].uid; }
uint32_t arch_x86_64_proc_current_gid(void)  { return proc_table[current_index_get()].gid; }

struct x86_64_address_space *arch_x86_64_proc_current_get_as(void) {
    return proc_table[current_index_get()].as;
}

void arch_x86_64_proc_current_set_as(struct x86_64_address_space *as) {
    /* H.5b.2 step A: bind AS to current PCB. Ownership transfers in;
     * proc_exit() / future destroy paths will free it. We deliberately
     * do NOT activate CR3 here — step A keeps the boot identity path
     * live so ring3 still runs unchanged. Step B will follow with an
     * arch_x86_64_as_activate(as) in usermode_run() right before iretq. */
    proc_table[current_index_get()].as = as;
}

int arch_x86_64_proc_yield(void) {
    ++yield_count;
    /* Hand off to the cooperative kthread runqueue. If no other
     * kthread is READY, sched_yield() returns 0 and we behave as a
     * counted no-op — preserving the legacy ring3 path. */
    uint32_t switched_to = arch_x86_64_sched_yield();
    return (int)switched_to;
}

/* ------------------------------------------------------------------- */
/* diagnostics                                                          */
/* ------------------------------------------------------------------- */

uint64_t arch_x86_64_proc_yield_count(void) { return yield_count; }
uint64_t arch_x86_64_proc_spawn_count(void) { return spawn_count; }
uint64_t arch_x86_64_proc_exit_count(void)  { return exit_count;  }

/* ------------------------------------------------------------------- */
/* γ.2.a — fork child slot management                                  */
/* ------------------------------------------------------------------- */

static uint16_t proc_index_of(x86_64_proc_t *p) {
    if (p == NULL) return OPENOS_X86_64_PROC_INVALID_INDEX;
    uintptr_t off = (uintptr_t)p - (uintptr_t)&proc_table[0];
    uint16_t idx = (uint16_t)(off / sizeof(x86_64_proc_t));
    if (idx >= OPENOS_X86_64_PROC_MAX) return OPENOS_X86_64_PROC_INVALID_INDEX;
    return idx;
}

x86_64_proc_t *arch_x86_64_proc_fork_alloc_child(x86_64_proc_t *parent_pcb) {
    if (parent_pcb == NULL) return NULL;
    uint16_t parent_slot = proc_index_of(parent_pcb);
    if (parent_slot == OPENOS_X86_64_PROC_INVALID_INDEX) return NULL;

    uint16_t slot = proc_alloc_slot();
    if (slot == OPENOS_X86_64_PROC_INVALID_INDEX) return NULL;

    x86_64_proc_t *c = &proc_table[slot];

    /* Child pid: rolling counter (identical scheme to alloc_child_pid()). */
    uint32_t cpid = next_child_pid++;
    if (next_child_pid == 0) next_child_pid = 2;

    c->pid  = cpid;
    c->tid  = cpid;
    c->ppid = parent_pcb->pid;
    c->uid  = parent_pcb->uid;
    c->gid  = parent_pcb->gid;
    c->exit_code = 0;
    c->state = OPENOS_X86_64_PROC_RUNNING;
    proc_copy_name(c->name, parent_pcb->name);
    /* M4.2: POSIX fork() signal inheritance — the child keeps the
     * parent's blocked mask and disposition table but starts with an
     * EMPTY pending set (pending signals are NOT inherited). */
    c->sig = parent_pcb->sig;
    c->sig.pending = 0;

    /* M4.4b: POSIX fork() inherits the parent's process group and session
     * (the child joins the same job). setpgid()/setsid() may relocate it
     * afterwards. */
    c->pgid = parent_pcb->pgid;
    c->sid  = parent_pcb->sid;

    /* γ.2.b: deep-clone the parent AS so the child has its own PML4[1]
     * subtree (user pages eagerly copied). Kernel-half PML4[0] stays
     * shared via as_create()'s boot PML4 copy, so kernel stacks / PCBs
     * / heap remain reachable under CR3=child.
     *
     * On OOM, roll the freshly-allocated slot back and bubble NULL. */
    early_console64_write("[fork:as_clone] before parent_as=");
    early_console64_write_hex64((uint64_t)(uintptr_t)parent_pcb->as);
    early_console64_write(" parent_pml4_pa=");
    early_console64_write_hex64((uint64_t)parent_pcb->as->pml4_phys);
    early_console64_write(" parent_pml4[1]=");
    early_console64_write_hex64(parent_pcb->as->pml4_va[1]);
    early_console64_write("\n");
    /*
     * γ.2.b: SYSCALL entry does not swap CR3, so we are still walking
     * the parent's user AS. as_create() copies PML4 entries via the
     * identity map (phys_to_va(phys)==phys, valid via PML4[0]), but
     * the destination pool[] itself lives in the kernel high half
     * (PML4[511]) -- and every user AS created by exec has PML4[511]
     * pointer-copied from the boot PML4, so writes hit the same page
     * tables. That IS safe. The hazard is _stale TLB_: right after
     * this fork returns, sysretq restores parent's user code page,
     * which needs the parent's PML4[1]. Switch CR3 to boot PML4 for
     * the duration of the clone so alloc_table()/phys_to_va are
     * unambiguous, then re-activate the parent AS before returning.
     */
    arch_x86_64_as_activate_boot();
    x86_64_address_space_t *child_as = arch_x86_64_as_clone(parent_pcb->as);
    /* Restore parent's CR3: SYSCALL rip / user stack live in parent.as. */
    arch_x86_64_as_activate(parent_pcb->as);
    early_console64_write("[fork:as_clone] after child_as=");
    early_console64_write_hex64((uint64_t)(uintptr_t)child_as);
    early_console64_write("\n");
    if (child_as == NULL) {
        c->state = OPENOS_X86_64_PROC_FREE;
        c->as = (struct x86_64_address_space *)0;
        return NULL;
    }
    c->as = child_as;

    /* Clean fork state on the child; caller (fork_capture) will populate
     * fork_pending / saved_fork_frame_* and fork_user_rsp on this PCB. */
    c->fork_pending     = 0;
    c->fork_via_syscall = 0;
    c->fork_user_rsp    = 0;
    c->child_pid        = 0;
    c->child_exited     = false;
    c->child_exit_code  = 0;
    c->wait_in_progress = false;
    c->child_slot       = OPENOS_X86_64_PROC_INVALID_INDEX;
    c->parent_slot      = parent_slot;
    c->children_head    = OPENOS_X86_64_PROC_INVALID_INDEX;
    c->zombie_head      = OPENOS_X86_64_PROC_INVALID_INDEX;
    c->zombie_next      = OPENOS_X86_64_PROC_INVALID_INDEX;
    c->fork_child_sched_slot = 0u; /* gamma.3b-alpha: filled by fork_capture */

    /* γ.4 S2b — push the new child onto parent->children_head. This is the
     * *live children* list (drained on exit into zombie_head, further drained
     * by wait()). Prepending keeps the O(1) cost regardless of fan-out. */
    c->sibling_next            = parent_pcb->children_head;
    parent_pcb->children_head  = slot;

    /* Wire parent -> child. Also mirror the child pid into the legacy
     * parent.child_pid so existing wait()/mark_exited() consumers keep
     * working during the γ.2.a transition. Legacy singletons now reflect
     * the *most recent* child so single-child callers stay compatible. */
    parent_pcb->child_slot       = slot;
    parent_pcb->child_pid        = cpid;
    parent_pcb->child_exited     = false;
    parent_pcb->child_exit_code  = 0;
    parent_pcb->wait_in_progress = false;

    ++spawn_count;
    /* NOTE: intentionally do NOT touch current_proc_slot — the parent
     * must keep running until wait() drives the child via switch_to(). */
    return c;
}

uint16_t arch_x86_64_proc_current_slot(void) {
    return current_index_get();
}

uint16_t arch_x86_64_proc_switch_to(uint16_t slot) {
    uint16_t cur = current_index_get();
    if (slot >= OPENOS_X86_64_PROC_MAX) return cur;
    if (proc_table[slot].state == OPENOS_X86_64_PROC_FREE) return cur;
    uint16_t prev = cur;
    current_index_set(slot);
    return prev;
}

x86_64_proc_t *arch_x86_64_proc_slot(uint16_t slot) {
    if (slot >= OPENOS_X86_64_PROC_MAX) return NULL;
    if (proc_table[slot].state == OPENOS_X86_64_PROC_FREE) return NULL;
    return &proc_table[slot];
}

/* γ.4 S2b — reverse lookup used by do_exit/do_wait to identify "self"
 * so children can unlink themselves from parent->children_head. Kept in
 * this TU because proc_table is file-scoped static. Deliberately does
 * not skip FREE slots: callers hold a PCB pointer they just wrote to,
 * even if the state slot is being torn down. */
uint16_t arch_x86_64_proc_slot_of(const x86_64_proc_t *p) {
    if (p == NULL) return OPENOS_X86_64_PROC_INVALID_INDEX;
    if (p < &proc_table[0] || p >= &proc_table[OPENOS_X86_64_PROC_MAX])
        return OPENOS_X86_64_PROC_INVALID_INDEX;
    return (uint16_t)(p - &proc_table[0]);
}

void arch_x86_64_proc_release_slot(uint16_t slot) {
    if (slot == 0 || slot >= OPENOS_X86_64_PROC_MAX) return;
    x86_64_proc_t *p = &proc_table[slot];
    if (p->state == OPENOS_X86_64_PROC_FREE) return;
    /* γ.2.b: if this slot owns a private AS (child of a fork), tear it
     * down before the pointer is cleared. Note: the caller must have
     * already switched CR3 off of `p->as` (usermode_run_pending_child_
     * for_wait re-activates parent.as right before calling release_slot),
     * otherwise we would free page tables that CR3 still walks. */
    if (p->as != (struct x86_64_address_space *)0) {
        arch_x86_64_as_destroy(p->as);
        p->as = (struct x86_64_address_space *)0;
    }
    p->state = OPENOS_X86_64_PROC_FREE;
    p->fork_pending = 0;
    p->parent_slot = OPENOS_X86_64_PROC_INVALID_INDEX;
    p->child_slot  = OPENOS_X86_64_PROC_INVALID_INDEX;

    /* gamma.3b-alpha: release the PARKED sched_slot allocated at
     * fork_capture time (if any). Alpha never flips it to READY, so
     * it is always still in PARKED state here -- slot_release accepts
     * PARKED. Log any refusal (should never fire in alpha). */
    if (p->fork_child_sched_slot != 0u) {
        uint32_t rc = arch_x86_64_sched_slot_release(p->fork_child_sched_slot);
        if (rc != 0u) {
            early_console64_write(
                "[proc:release][alpha] sched_slot_release REFUSED idx=");
            early_console64_write_hex64((uint64_t)p->fork_child_sched_slot);
            early_console64_write(" rc=");
            early_console64_write_hex64((uint64_t)rc);
            early_console64_write("\n");
        } else {
            early_console64_write(
                "[proc:release][alpha] freed parked slot=");
            early_console64_write_hex64((uint64_t)p->fork_child_sched_slot);
            early_console64_write("\n");
        }
        p->fork_child_sched_slot = 0u;
    }
}

void arch_x86_64_proc_print_status(void) {
    early_console64_write("[x86_64][proc] current pid=");
    early_console64_write_hex64((uint64_t)arch_x86_64_proc_current_pid());
    early_console64_write(" tid=");
    early_console64_write_hex64((uint64_t)arch_x86_64_proc_current_tid());
    early_console64_write(" ppid=");
    early_console64_write_hex64((uint64_t)arch_x86_64_proc_current_ppid());
    early_console64_write(" spawns=");
    early_console64_write_hex64(spawn_count);
    early_console64_write(" exits=");
    early_console64_write_hex64(exit_count);
    early_console64_write(" yields=");
    early_console64_write_hex64(yield_count);
    early_console64_write("\n");
}

/* --------------------------------------------------------------------------
 * M4.1d: SYS_KILL backing. Minimal signal delivery.
 *
 * Signal numbers we honour in this alpha:
 *   SIGKILL = 9, SIGTERM = 15  -> force target into EXITED with code 128+sig,
 *                                 so the scheduler/waitpid path can reap it.
 *   any other sig              -> accepted (return 0) but treated as a no-op
 *                                 (no handler registry yet; that is M4.2).
 * sig == 0                     -> "existence probe": return 0 iff pid exists.
 *
 * Returns 0 on success, -1 if no live process carries that pid.
 * ------------------------------------------------------------------------ */
/* Apply one already-validated signal to a specific live PCB. Records the
 * pending bit (respecting SIG_IGN for catchable signals; SIGKILL/SIGSTOP are
 * always recorded) and, for signals whose disposition is SIG_DFL with a
 * terminating default action, forces the PCB into EXITED(128+sig) right away
 * so the scheduler/waitpid path can reap it. Handler-registered signals stay
 * pending for the M4.2b user-mode trampoline. Returns 0 (always succeeds for
 * a valid sig on a live PCB). */
static int proc_deliver_signal(x86_64_proc_t *p, int sig) {
    if (x86_64_signal_send(&p->sig, sig) != 0) {
        return -1;
    }
    uint64_t disp = p->sig.actions[sig].handler;
    bool default_disp = (disp == OPENOS_SIG_DFL);
    if (x86_64_signal_uncatchable(sig)) {
        default_disp = true; /* SIGKILL/SIGSTOP ignore any handler */
    }
    if (default_disp) {
        x86_64_sig_default_t act = x86_64_signal_default_action(sig);
        if (act == OPENOS_SIG_ACT_TERM || act == OPENOS_SIG_ACT_CORE) {
            if (p->state != OPENOS_X86_64_PROC_EXITED) {
                p->state = OPENOS_X86_64_PROC_EXITED;
                p->exit_code = 128 + sig;
                exit_count++;
            }
            x86_64_signal_consume(&p->sig, sig);
        }
        /* STOP/CONT/IGN default actions: recorded, no reap here. */
    }
    return 0;
}

int arch_x86_64_proc_signal(uint32_t pid, int sig) {
    if (pid == 0u) {
        return -1; /* pid 0 (idle/kernel) is not a valid kill target here */
    }
    for (uint32_t i = 0; i < OPENOS_X86_64_PROC_MAX; i++) {
        x86_64_proc_t *p = &proc_table[i];
        if (p->state == OPENOS_X86_64_PROC_FREE) {
            continue;
        }
        if (p->pid != pid) {
            continue;
        }
        /* sig 0: existence probe only. */
        if (sig == 0) {
            return 0;
        }
        if (!x86_64_signal_valid(sig)) {
            return -1;
        }
        return proc_deliver_signal(p, sig);
    }
    return -1; /* no such pid */
}

/* M4.2: sigaction on the CURRENT process. Forwards to the signal64 API
 * against arch_x86_64_proc_current()->sig. */
int arch_x86_64_proc_sigaction(int sig,
                               const openos_sigaction_t *act,
                               openos_sigaction_t *old) {
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == 0) {
        return -1;
    }
    return x86_64_signal_sigaction(&p->sig, sig, act, old);
}

/* M4.2: sigprocmask on the CURRENT process. */
int arch_x86_64_proc_sigprocmask(int how, uint64_t set, uint64_t *oldset) {
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == 0) {
        return -1;
    }
    return x86_64_signal_procmask(&p->sig, how, set, oldset);
}

/* M4.2: pump pending signals for the CURRENT process. Enacts default
 * actions for terminating/core signals (marking the PCB EXITED with
 * 128+sig); leaves handler-registered signals pending for M4.2b. Returns
 * the signal acted upon (>0) or 0 if nothing was deliverable. */
/* ---- M4.2b: user-mode signal handler trampoline glue ------------------ *
 *
 * These two functions bridge signal64's pure frame builder/restorer to the
 * live ring3 context and user stack. They do NOT touch the trap frame or
 * paging directly: the caller (syscall glue) supplies the interrupted
 * register set in `regs` and callbacks to read/write the target process's
 * user memory, keeping this layer independent of any specific entry path.
 */
int arch_x86_64_proc_signal_deliver_user(
        x86_64_sigregs_t *regs,
        int (*uwrite)(void *uctx, uint64_t dst, const void *src, uint64_t n),
        void *uctx)
{
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == 0 || regs == 0 || uwrite == 0) {
        return -1;
    }
    /* A handler is already in flight: don't nest a second delivery until the
     * running one returns via sigreturn (classic non-SA_NODEFER behaviour). */
    if (p->sig.in_handler != 0) {
        return 0;
    }

    /* Pick the lowest-numbered deliverable signal that has a USER handler
     * installed (not DFL/IGN) and is not currently blocked. Default-action
     * signals are handled elsewhere (signal_pump / immediate terminate). */
    int sig = 0;
    for (int s = 1; s < OPENOS_NSIG; ++s) {
        uint64_t bit = (uint64_t)1 << s;
        if (!(p->sig.pending & bit)) {
            continue;
        }
        if (p->sig.blocked & bit) {
            continue;
        }
        uint64_t disp = p->sig.actions[s].handler;
        if (disp == OPENOS_SIG_DFL || disp == OPENOS_SIG_IGN) {
            continue;
        }
        sig = s;
        break;
    }
    if (sig == 0) {
        return 0;   /* nothing with a user handler to deliver */
    }

    uint64_t handler  = p->sig.actions[sig].handler;
    /* SA_RESTORER convention: the restorer trampoline VA rides in `flags`.
     * A userland that registers a handler must supply a restorer that
     * issues SYS_RT_SIGRETURN; without one the handler's `ret` would fault. */
    uint64_t restorer = p->sig.actions[sig].flags;

    x86_64_sigcontext_t frame;
    uint64_t sp_out = 0;
    int rc = x86_64_signal_build_frame(&p->sig, sig, handler, restorer,
                                       regs, &frame, &sp_out);
    if (rc != 0) {
        return -1;
    }

    /* build_frame laid out: [sigcontext] at ctx_addr, [restorer] at sp_out,
     * with sp_out = ctx_addr - 8. Materialise both on the user stack. */
    uint64_t ctx_addr = sp_out + 8;
    if (uwrite(uctx, ctx_addr, &frame, sizeof(frame)) != 0) {
        /* Roll back the mask/in_handler bookkeeping build_frame applied so a
         * failed delivery leaves the process observably unchanged. */
        p->sig.blocked    = frame.saved_mask;
        p->sig.saved_mask = 0;
        p->sig.in_handler = 0;
        p->sig.pending   |= ((uint64_t)1 << sig);   /* still pending */
        return -1;
    }
    if (uwrite(uctx, sp_out, &restorer, sizeof(restorer)) != 0) {
        p->sig.blocked    = frame.saved_mask;
        p->sig.saved_mask = 0;
        p->sig.in_handler = 0;
        p->sig.pending   |= ((uint64_t)1 << sig);
        return -1;
    }

    /* regs now points rip->handler, rsp->sp_out, rdi->signo (per build_frame).
     * The caller writes these back into the trap frame before returning to
     * ring3, so execution resumes inside the handler. */
    return sig;
}

int arch_x86_64_proc_sigreturn(
        x86_64_sigregs_t *regs,
        int (*uread)(void *uctx, void *dst, uint64_t src, uint64_t n),
        void *uctx)
{
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == 0 || regs == 0 || uread == 0) {
        return -1;
    }
    if (p->sig.in_handler == 0) {
        return -1;   /* sigreturn with no handler in flight */
    }

    /* At handler entry %rsp pointed at the restorer return address; the
     * handler's `ret` popped it, so on the sigreturn syscall %rsp points
     * just above the sigcontext build_frame stored (ctx_addr). */
    uint64_t ctx_addr = regs->rsp;
    x86_64_sigcontext_t frame;
    if (uread(uctx, &frame, ctx_addr, sizeof(frame)) != 0) {
        return -1;
    }

    int rc = x86_64_signal_restore_frame(&p->sig, &frame, regs);
    if (rc != 0) {
        return -1;
    }
    /* regs is now the fully-restored pre-signal context; the caller writes
     * it back to the trap frame and returns to ring3 at the interrupted
     * instruction. */
    return 0;
}

int arch_x86_64_proc_signal_pump(void) {
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == 0) {
        return 0;
    }
    int sig = x86_64_signal_next_pending(&p->sig);
    if (sig == 0) {
        return 0;
    }
    uint64_t disp = p->sig.actions[sig].handler;
    if (disp > OPENOS_SIG_IGN && !x86_64_signal_uncatchable(sig)) {
        return 0; /* user handler registered: defer to M4.2b trampoline */
    }
    if (disp == OPENOS_SIG_IGN && !x86_64_signal_uncatchable(sig)) {
        x86_64_signal_consume(&p->sig, sig);
        return 0;
    }
    x86_64_sig_default_t act = x86_64_signal_default_action(sig);
    x86_64_signal_consume(&p->sig, sig);
    if (act == OPENOS_SIG_ACT_TERM || act == OPENOS_SIG_ACT_CORE) {
        if (p->state != OPENOS_X86_64_PROC_EXITED) {
            p->state = OPENOS_X86_64_PROC_EXITED;
            p->exit_code = 128 + sig;
            exit_count++;
        }
    }
    return sig;
}

/* ------------------------------------------------------------------ */
/* M4.4b: job control — process groups & sessions.                     */

/* Locate a live PCB by pid (returns NULL if free/absent). */
static x86_64_proc_t *proc_find_live(uint32_t pid) {
    for (uint32_t i = 0; i < OPENOS_X86_64_PROC_MAX; i++) {
        x86_64_proc_t *p = &proc_table[i];
        if (p->state == OPENOS_X86_64_PROC_FREE) continue;
        if (p->pid == pid) return p;
    }
    return 0;
}

/* Resolve pid==0 to the current process; else find the named live PCB. */
static x86_64_proc_t *proc_resolve(uint32_t pid) {
    if (pid == 0u) return arch_x86_64_proc_current();
    return proc_find_live(pid);
}

uint32_t arch_x86_64_proc_getpgid(uint32_t pid) {
    x86_64_proc_t *p = proc_resolve(pid);
    if (p == 0) return (uint32_t)-1;
    return p->pgid;
}

int arch_x86_64_proc_setpgid(uint32_t pid, uint32_t pgid) {
    x86_64_proc_t *p = proc_resolve(pid);
    if (p == 0) return -1;
    /* A session leader (sid == pid) cannot change its process group. */
    if (p->sid == p->pid) return -1;
    /* pgid == 0 means "start a new group whose id is the target's pid". */
    p->pgid = (pgid == 0u) ? p->pid : pgid;
    return 0;
}

uint32_t arch_x86_64_proc_getsid(uint32_t pid) {
    x86_64_proc_t *p = proc_resolve(pid);
    if (p == 0) return (uint32_t)-1;
    return p->sid;
}

int arch_x86_64_proc_setsid(void) {
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == 0) return -1;
    /* Already a group leader -> cannot create a new session (POSIX EPERM). */
    if (p->pgid == p->pid) return -1;
    p->sid  = p->pid;
    p->pgid = p->pid;
    return (int)p->sid;
}

int arch_x86_64_proc_signal_group(uint32_t pgid, int sig) {
    if (pgid == 0u) return -1;
    if (sig != 0 && !x86_64_signal_valid(sig)) return -1;
    int hits = 0;
    for (uint32_t i = 0; i < OPENOS_X86_64_PROC_MAX; i++) {
        x86_64_proc_t *p = &proc_table[i];
        if (p->state == OPENOS_X86_64_PROC_FREE) continue;
        if (p->pgid != pgid) continue;
        if (sig == 0) { ++hits; continue; } /* existence probe */
        if (proc_deliver_signal(p, sig) == 0) ++hits;
    }
    return hits;
}

