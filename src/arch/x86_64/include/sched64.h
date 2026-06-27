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

/* G.6.4: slot pool sized to MAX_CPUS (per-CPU idle slots, slot 0 = BSP idle)
 *        + the historical 8 dynamic-kthread slots. The first MAX_CPUS slots
 *        are reserved for idle threads owned by their respective CPU; the
 *        remaining slots are the dynamic kthread pool, owned by CPU 0 until
 *        G.6.5 introduces migration. */
#define OPENOS_X86_64_SMP_MAX_CPUS_HINT  8u
#define OPENOS_X86_64_SCHED_MAX_KTHREADS 16u
#define OPENOS_X86_64_SCHED_KSTACK_BYTES 8192u

uint32_t arch_x86_64_sched_spawn_kthread(x86_64_thread_entry_t entry, void *arg);
uint32_t arch_x86_64_sched_yield(void);
void     arch_x86_64_sched_exit_self(void);
uint32_t arch_x86_64_sched_kthread_count(void);
uint32_t arch_x86_64_sched_current_slot(void);
uint64_t arch_x86_64_sched_switch_count(void);

/* -----------------------------------------------------------------
 * Step G.6.4: per-CPU idle threads.
 *
 * Each CPU owns exactly one "idle" slot. The BSP idle is slot 0
 * (the bootstrap context) and is initialised by sched_init(). APs
 * call sched_init_ap() once their %gs is installed, then
 * sched_register_ap_idle() to allocate themselves an idle slot
 * marked owner_cpu=this_cpu, is_idle=1, state=RUNNING, current=this.
 *
 * pick_next() and has_other_ready() now filter slots by
 * owner_cpu == this_cpu()->cpu_idx, so cross-CPU contention on the
 * shared sched_slots[] pool is impossible.
 *
 * APs do NOT enable interrupts yet (G.6.5 will wire per-CPU timer
 * + migration). Registering the idle slot only prepares the
 * scheduler bookkeeping.
 *
 * sched_idle_slot_for_cpu() returns the slot id of CPU `cpu_idx`'s
 * idle thread, or 0xFFFFFFFF if none registered. Slot 0 belongs to
 * CPU 0 by construction.
 *
 * sched_idle_selftest() walks online CPUs and verifies that each
 * has exactly one is_idle slot whose owner_cpu matches and whose
 * state is RUNNING. Returns 0 on success; non-zero == error code.
 * ----------------------------------------------------------------- */

void     arch_x86_64_sched_init_ap(void);
uint32_t arch_x86_64_sched_register_ap_idle(void);
uint32_t arch_x86_64_sched_idle_slot_for_cpu(uint32_t cpu_idx);
uint32_t arch_x86_64_sched_idle_selftest(uint32_t online_cpus);

/* -----------------------------------------------------------------
 * Step F.3: preemptive tick hook.
 *
 * Called from IRQ0 (PIT) hot path AFTER pic_send_eoi. Each call burns
 * one quantum tick from the running thread's budget; when the budget
 * reaches zero the hook invokes the same yield path used by
 * cooperative kthreads — safe because IRQ0 stub already saved every
 * caller-saved register, so resume-to-here is bit-identical to a
 * regular function return.
 *
 * Quantum is fixed at OPENOS_X86_64_SCHED_QUANTUM_TICKS PIT ticks.
 * At PIT_HZ=100 that is 50 ms per slice.
 *
 * Returns 1 if a preemption switch was performed this call, 0
 * otherwise. The return value is informational; the IRQ0 path
 * ignores it.
 * ----------------------------------------------------------------- */

#define OPENOS_X86_64_SCHED_QUANTUM_TICKS 5u

uint32_t arch_x86_64_sched_on_tick(void);
uint64_t arch_x86_64_sched_preempt_count(void);

/* -----------------------------------------------------------------
 * Step G.2: priority-weighted time slices (WRR-like).
 *
 * Three priority bands, expressed as weights:
 *   HIGH    quantum = 10 ticks (100 ms @ PIT_HZ=100)
 *   NORMAL  quantum =  5 ticks ( 50 ms)  ← same as F.3 baseline
 *   LOW     quantum =  2 ticks ( 20 ms)
 *
 * pick_next() KEEPS round-robin scanning. Only the quantum length
 * differs per slot, so LOW threads still run — they just run less.
 * This avoids strict-priority starvation while giving HIGH ~5x the
 * CPU of LOW.
 *
 * Default priority for newly spawned kthreads (including the
 * bootstrap slot) is NORMAL. arch_x86_64_sched_spawn_kthread()
 * remains source-compatible: it forwards to _prio() with NORMAL.
 *
 * Returns of set/get_priority: 0 on success / value;  for set,
 * UINT32_MAX (0xFFFFFFFFu) means "invalid slot or priority".
 * ----------------------------------------------------------------- */

#define OPENOS_X86_64_SCHED_PRIO_LOW    0u
#define OPENOS_X86_64_SCHED_PRIO_NORMAL 1u
#define OPENOS_X86_64_SCHED_PRIO_HIGH   2u
#define OPENOS_X86_64_SCHED_PRIO_DEFAULT OPENOS_X86_64_SCHED_PRIO_NORMAL
#define OPENOS_X86_64_SCHED_PRIO_MAX    OPENOS_X86_64_SCHED_PRIO_HIGH

#define OPENOS_X86_64_SCHED_QUANTUM_HIGH   10u
#define OPENOS_X86_64_SCHED_QUANTUM_NORMAL 5u
#define OPENOS_X86_64_SCHED_QUANTUM_LOW    2u

uint32_t arch_x86_64_sched_spawn_kthread_prio(x86_64_thread_entry_t entry,
                                              void *arg,
                                              uint32_t priority);
uint32_t arch_x86_64_sched_set_priority(uint32_t slot, uint32_t priority);
uint32_t arch_x86_64_sched_get_priority(uint32_t slot);
uint32_t arch_x86_64_sched_quantum_for_priority(uint32_t priority);

#endif /* OPENOS_ARCH_X86_64_SCHED64_H */
