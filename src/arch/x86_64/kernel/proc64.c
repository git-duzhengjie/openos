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

#define OPENOS_X86_64_PROC_INVALID_INDEX 0xFFFFu

static x86_64_proc_t proc_table[OPENOS_X86_64_PROC_MAX];
static uint16_t      current_index;
static uint64_t      yield_count;
static uint64_t      spawn_count;
static uint64_t      exit_count;

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
    current_index = 0;
    yield_count = 0;
    spawn_count = 0;
    exit_count = 0;
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
    p->exit_code = code;
    p->state = OPENOS_X86_64_PROC_FREE; /* immediate reap (no wait4 yet) */
    /* H.5b.1: AS pointer is always NULL today; H.5b.3 will replace
     * this with a destroy-AS call before rotating CR3 back. */
    p->as = (struct x86_64_address_space *)0;
    ++exit_count;
    /* rotate back to the kernel proc */
    current_index = 0;
}

/* ------------------------------------------------------------------- */
/* queries                                                              */
/* ------------------------------------------------------------------- */

uint32_t arch_x86_64_proc_current_pid(void)  { return proc_table[current_index].pid; }
uint32_t arch_x86_64_proc_current_tid(void)  { return proc_table[current_index].tid; }
uint32_t arch_x86_64_proc_current_ppid(void) { return proc_table[current_index].ppid; }
uint32_t arch_x86_64_proc_current_uid(void)  { return proc_table[current_index].uid; }
uint32_t arch_x86_64_proc_current_gid(void)  { return proc_table[current_index].gid; }

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
