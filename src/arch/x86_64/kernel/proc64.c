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

#include <stddef.h>

#include "../include/early_console64.h"
#include "../include/sched64.h"

static x86_64_proc_t proc_table[OPENOS_X86_64_PROC_MAX];
static uint16_t      current_index;
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
    current_index = 0;
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
    p->ppid = proc_table[current_index].pid;
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

    current_index = slot;
    ++spawn_count;
    return p->pid;
}

void arch_x86_64_proc_exit(int code) {
    x86_64_proc_t *p = &proc_table[current_index];
    if (current_index == 0) {
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
        /* leave state / current_index / as untouched */
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
    current_index = 0;
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
    /* current_index is initialised to 0 in arch_x86_64_proc_init() and
     * proc_table[0] is the long-lived kernel PCB, so this is always a
     * valid pointer after kernel init. Returning the address lets
     * usermode64.c stash per-thread longjmp state without leaking the
     * proc_table symbol. */
    return &proc_table[current_index];
}

uint32_t arch_x86_64_proc_current_pid(void)  { return proc_table[current_index].pid; }
uint32_t arch_x86_64_proc_current_tid(void)  { return proc_table[current_index].tid; }
uint32_t arch_x86_64_proc_current_ppid(void) { return proc_table[current_index].ppid; }
uint32_t arch_x86_64_proc_current_uid(void)  { return proc_table[current_index].uid; }
uint32_t arch_x86_64_proc_current_gid(void)  { return proc_table[current_index].gid; }

struct x86_64_address_space *arch_x86_64_proc_current_get_as(void) {
    return proc_table[current_index].as;
}

void arch_x86_64_proc_current_set_as(struct x86_64_address_space *as) {
    /* H.5b.2 step A: bind AS to current PCB. Ownership transfers in;
     * proc_exit() / future destroy paths will free it. We deliberately
     * do NOT activate CR3 here — step A keeps the boot identity path
     * live so ring3 still runs unchanged. Step B will follow with an
     * arch_x86_64_as_activate(as) in usermode_run() right before iretq. */
    proc_table[current_index].as = as;
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

    /* γ.2.a shares AS with the parent — as_clone lands in γ.2.b/γ.3. */
    c->as = parent_pcb->as;

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

    /* Wire parent -> child. Also mirror the child pid into the legacy
     * parent.child_pid so existing wait()/mark_exited() consumers keep
     * working during the γ.2.a transition. */
    parent_pcb->child_slot       = slot;
    parent_pcb->child_pid        = cpid;
    parent_pcb->child_exited     = false;
    parent_pcb->child_exit_code  = 0;
    parent_pcb->wait_in_progress = false;

    ++spawn_count;
    /* NOTE: intentionally do NOT touch current_index — the parent must
     * keep running until wait() drives the child via switch_to(). */
    return c;
}

uint16_t arch_x86_64_proc_current_slot(void) {
    return current_index;
}

uint16_t arch_x86_64_proc_switch_to(uint16_t slot) {
    if (slot >= OPENOS_X86_64_PROC_MAX) return current_index;
    if (proc_table[slot].state == OPENOS_X86_64_PROC_FREE) return current_index;
    uint16_t prev = current_index;
    current_index = slot;
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
    p->state = OPENOS_X86_64_PROC_FREE;
    p->as = (struct x86_64_address_space *)0;
    p->fork_pending = 0;
    p->parent_slot = OPENOS_X86_64_PROC_INVALID_INDEX;
    p->child_slot  = OPENOS_X86_64_PROC_INVALID_INDEX;
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
