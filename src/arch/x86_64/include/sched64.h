#ifndef OPENOS_ARCH_X86_64_SCHED64_H
#define OPENOS_ARCH_X86_64_SCHED64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_CONTEXT_RFLAGS_IF (1ULL << 9)

typedef struct x86_64_context {
    x86_64_stack_ptr_t rsp;
    x86_64_entry_t rip;
    uint64_t rflags;

    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
} x86_64_context_t;

typedef struct x86_64_thread_context {
    x86_64_context_t regs;
    x86_64_stack_ptr_t kernel_stack_base;
    x86_64_size_t kernel_stack_size;
} x86_64_thread_context_t;

typedef void (*x86_64_thread_entry_t)(void *arg);

void arch_x86_64_sched_init(void);
void arch_x86_64_context_init(x86_64_thread_context_t *ctx,
                              x86_64_thread_entry_t entry,
                              void *arg,
                              x86_64_stack_ptr_t stack_top);
void arch_x86_64_context_switch(x86_64_context_t *from, x86_64_context_t *to);
const x86_64_context_t *arch_x86_64_current_context(void);
void arch_x86_64_sched_print_status(void);

/* -----------------------------------------------------------------
 * Cooperative kernel-thread runqueue (Step E.2).
 *
 * Semantics:
 *   - sched_spawn_kthread() allocates an internal slot + 8KB stack,
 *     wires the trampoline so `entry(arg)` is invoked on first switch,
 *     and appends the slot to the ready queue. Returns slot id (>=1)
 *     or 0 on failure. The newly spawned thread does NOT run until
 *     somebody calls sched_yield().
 *   - sched_yield() picks the next READY thread (round-robin). If the
 *     queue is empty (no other runnable kthread), it is a no-op and
 *     returns 0 — this preserves the existing ring3 yield path.
 *     Returns the slot id of the new current on a real switch.
 *   - sched_exit_self() marks the calling kthread as EXITED, then
 *     yields to the next runnable thread. If the queue drains, control
 *     returns to whoever first kicked the scheduler (the boot task).
 *   - sched_kthread_count() returns the number of slots currently
 *     allocated (READY + RUNNING + EXITED-not-reaped). Used by tests
 *     and diagnostics.
 *
 * Concurrency: none. Single-CPU, non-preemptible, never called from
 * an interrupt handler.
 * ----------------------------------------------------------------- */

#define OPENOS_X86_64_SCHED_MAX_KTHREADS 8u
#define OPENOS_X86_64_SCHED_KSTACK_BYTES 8192u

uint32_t arch_x86_64_sched_spawn_kthread(x86_64_thread_entry_t entry, void *arg);
uint32_t arch_x86_64_sched_yield(void);
void     arch_x86_64_sched_exit_self(void);
uint32_t arch_x86_64_sched_kthread_count(void);
uint32_t arch_x86_64_sched_current_slot(void);
uint64_t arch_x86_64_sched_switch_count(void);

#endif /* OPENOS_ARCH_X86_64_SCHED64_H */
