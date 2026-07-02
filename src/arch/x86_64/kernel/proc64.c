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
    proc_table[0].fork_child_sched_slot = 0u; /* gamma.3b-alpha: invalid */
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
    c->fork_child_sched_slot = 0u; /* gamma.3b-alpha: filled by fork_capture */

    /* Wire parent -> child. Also mirror the child pid into the legacy
     * parent.child_pid so existing wait()/mark_exited() consumers keep
     * working during the γ.2.a transition. */
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

