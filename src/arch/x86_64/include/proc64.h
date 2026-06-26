#ifndef OPENOS_ARCH_X86_64_PROC64_H
#define OPENOS_ARCH_X86_64_PROC64_H

/*
 * proc64 — minimal process/thread table for the x86_64 port (Step E.1).
 *
 * Scope: replace the hard-coded GETPID/GETTID/GETPPID/YIELD constants in
 * syscall_dispatch64.c with values that come from a real (if tiny) PCB
 * table.  Today the port runs exactly one kernel context plus at most
 * one ring3 thread, so the table is a fixed 8-slot static array — no
 * dynamic allocation, no preemption, no SMP. The single source of truth
 * is the `current` cursor: every syscall that asks "who am I?" reads it.
 *
 * Lifecycle:
 *   - boot       -> arch_x86_64_proc_init() registers slot 0 as the
 *                   kernel proc (pid=1, ppid=0, tid=1) and points
 *                   current at it.
 *   - ring3 run  -> arch_x86_64_proc_spawn_user(name) allocates the
 *                   next free slot (pid=2, ppid=1, tid=2) and rotates
 *                   current to it. Called from kernel64.c right before
 *                   arch_x86_64_usermode_run().
 *   - ring3 exit -> arch_x86_64_proc_exit(code) is called from inside
 *                   arch_x86_64_usermode_mark_exited(); it frees the
 *                   slot and rotates current back to the kernel proc.
 *
 * Concurrency: none. The table is touched only from the BSP, never
 * from an interrupt handler (we have no preemption yet).
 */

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_PROC_MAX 8u
#define OPENOS_X86_64_PROC_NAME_MAX 16u

typedef enum x86_64_proc_state {
    OPENOS_X86_64_PROC_FREE = 0,
    OPENOS_X86_64_PROC_RUNNING = 1,
    OPENOS_X86_64_PROC_EXITED = 2,
} x86_64_proc_state_t;

typedef struct x86_64_proc {
    uint32_t pid;
    uint32_t tid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t gid;
    int32_t  exit_code;
    x86_64_proc_state_t state;
    char     name[OPENOS_X86_64_PROC_NAME_MAX];
} x86_64_proc_t;

/* Lifecycle ---------------------------------------------------------- */

void arch_x86_64_proc_init(void);
/* Allocate a ring3 PCB, rotate current to it. Returns pid (>0) or 0 on
 * failure. name may be NULL. */
uint32_t arch_x86_64_proc_spawn_user(const char *name);
/* Mark current as exited, rotate current back to the kernel proc. */
void arch_x86_64_proc_exit(int code);

/* Queries used by the syscall dispatcher ----------------------------- */

uint32_t arch_x86_64_proc_current_pid(void);
uint32_t arch_x86_64_proc_current_tid(void);
uint32_t arch_x86_64_proc_current_ppid(void);
uint32_t arch_x86_64_proc_current_uid(void);
uint32_t arch_x86_64_proc_current_gid(void);

/* Cooperative yield. No other runnable thread exists yet, so this just
 * bumps the global yield counter and returns. Wired up so callers can
 * compile against the real syscall today; sched64 will plug in later. */
int arch_x86_64_proc_yield(void);

/* Diagnostics -------------------------------------------------------- */

uint64_t arch_x86_64_proc_yield_count(void);
uint64_t arch_x86_64_proc_spawn_count(void);
uint64_t arch_x86_64_proc_exit_count(void);
void     arch_x86_64_proc_print_status(void);

#endif /* OPENOS_ARCH_X86_64_PROC64_H */
