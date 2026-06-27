#include "../include/sched64.h"

#include <stddef.h>
#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/heap64.h"
#include "../include/percpu64.h"
#include "../include/smp64.h"  /* G.6.5c: OPENOS_X86_64_SMP_MAX_CPUS for spawn_on clamp */

/* G.6.3: per-CPU scheduler cursors / counters live in arch_x86_64_percpu_t,
 * addressable via %gs. The sched_slots[] pool itself is still a single
 * shared array (slot ownership stays global; only the running-cursor and
 * the bookkeeping counters are per-CPU). These shims keep the rest of
 * this file readable. */
static inline uint32_t sched_pc_current(void) {
    return arch_x86_64_this_cpu_ptr()->sched_current_idx;
}
static inline void sched_pc_set_current(uint32_t v) {
    arch_x86_64_this_cpu_ptr()->sched_current_idx = v;
}
static inline uint32_t sched_pc_quantum(void) {
    return arch_x86_64_this_cpu_ptr()->sched_quantum_left;
}
static inline void sched_pc_set_quantum(uint32_t v) {
    arch_x86_64_this_cpu_ptr()->sched_quantum_left = v;
}
static inline void sched_pc_dec_quantum(void) {
    arch_x86_64_this_cpu_ptr()->sched_quantum_left--;
}
static inline uint64_t sched_pc_switches(void) {
    return arch_x86_64_this_cpu_ptr()->sched_switch_count;
}
static inline void sched_pc_inc_switches(void) {
    arch_x86_64_this_cpu_ptr()->sched_switch_count++;
}
static inline uint64_t sched_pc_preempts(void) {
    return arch_x86_64_this_cpu_ptr()->sched_preempt_count;
}
static inline void sched_pc_inc_preempts(void) {
    arch_x86_64_this_cpu_ptr()->sched_preempt_count++;
}

static x86_64_context_t bootstrap_context;
static x86_64_context_t *current_context = &bootstrap_context;
static uint8_t sched64_ready;

static void sched64_thread_trampoline(void) {
    x86_64_thread_entry_t entry;
    void *arg;

    __asm__ __volatile__(
        "mov %%r12, %0\n"
        "mov %%r13, %1\n"
        : "=r"(entry), "=r"(arg)
        :
        : "memory");

    if (entry != NULL) {
        entry(arg);
    }

    /* Kthread fell out of its entry -- self-reap via cooperative scheduler. */
    arch_x86_64_sched_exit_self();

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void arch_x86_64_sched_init(void) {
    bootstrap_context.rflags = OPENOS_X86_64_CONTEXT_RFLAGS_IF;
    current_context = &bootstrap_context;
    sched64_ready = 1u;
    /* G.6.3: quantum was previously a module-static initialized to
     * QUANTUM_NORMAL. Now lives in percpu_t (zeroed by percpu_setup),
     * so seed it here for the BSP. APs will seed via their own
     * sched bring-up in G.6.4. */
    sched_pc_set_quantum(OPENOS_X86_64_SCHED_QUANTUM_NORMAL);
}

void arch_x86_64_context_init(x86_64_thread_context_t *ctx,
                              x86_64_thread_entry_t entry,
                              void *arg,
                              x86_64_stack_ptr_t stack_top) {
    if (ctx == NULL) {
        return;
    }

    ctx->regs.rsp = stack_top & ~0xFULL;
    ctx->regs.rip = (x86_64_entry_t)(uintptr_t)sched64_thread_trampoline;
    ctx->regs.rflags = OPENOS_X86_64_CONTEXT_RFLAGS_IF;
    ctx->regs.rbx = 0;
    ctx->regs.rbp = 0;
    ctx->regs.r12 = (uint64_t)(uintptr_t)entry;
    ctx->regs.r13 = (uint64_t)(uintptr_t)arg;
    ctx->regs.r14 = 0;
    ctx->regs.r15 = 0;
    ctx->regs.r8 = 0;
    ctx->regs.r9 = 0;
    ctx->regs.r10 = 0;
    ctx->regs.r11 = 0;
}

const x86_64_context_t *arch_x86_64_current_context(void) {
    return current_context;
}

void arch_x86_64_sched_note_switch(x86_64_context_t *next) {
    if (next != NULL) {
        current_context = next;
    }
}

/* =================================================================
 * Step E.2 — cooperative kernel-thread runqueue.
 *
 * One ring buffer of slots. Slot 0 is the bootstrap (kmain) context.
 * Slots 1..N-1 are dynamically-spawned kthreads. Round-robin pick.
 * ================================================================= */

typedef enum {
    SCHED_SLOT_FREE = 0,
    SCHED_SLOT_READY,
    SCHED_SLOT_RUNNING,
    SCHED_SLOT_EXITED,
} sched_slot_state_t;

typedef struct {
    sched_slot_state_t  state;
    x86_64_context_t    ctx;          /* slot 0 mirrors BSP bootstrap_context */
    void               *stack_base;   /* heap-allocated stack (kthread slots) */
    uint32_t            id;           /* 1-based slot id; 0 == BSP bootstrap */
    uint32_t            priority;     /* G.2: scheduling priority band */
    uint32_t            owner_cpu;    /* G.6.4: per-CPU affinity */
    uint32_t            is_idle;      /* G.6.4: 1 = idle thread for owner_cpu */
} sched_slot_t;

static sched_slot_t  sched_slots[OPENOS_X86_64_SCHED_MAX_KTHREADS];
/* G.6.3: sched_current_idx, sched_switch_count, sched_quantum_left,
 * sched_preempt_count moved into arch_x86_64_percpu_t (this_cpu()->...). */

static void sched_ensure_bootstrap_slot(void) {
    if (sched_slots[0].state != SCHED_SLOT_FREE) {
        return;
    }
    sched_slots[0].state     = SCHED_SLOT_RUNNING;
    sched_slots[0].id        = 0u;
    sched_slots[0].priority  = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    sched_slots[0].owner_cpu = 0u;   /* G.6.4: BSP owns slot 0 */
    /* NOTE: slot 0 is the BSP's bootstrap thread (running selftests,
     * etc.) -- it is *not* a dedicated idle thread. AP cpus each get
     * their own real idle slot via sched_register_ap_idle, where
     * is_idle=1. The BSP has no separate idle thread: when no other
     * work is ready, slot 0 simply stays RUNNING and the bootstrap
     * code continues to spin/yield/hlt as it sees fit. */
    sched_slots[0].is_idle   = 0u;
    sched_pc_set_current(0u);
}

static uint32_t sched_alloc_slot(void) {
    sched_ensure_bootstrap_slot();
    /* G.6.4: dynamic kthreads live above the reserved idle band so that
     * slot ids [0 .. MAX_CPUS_HINT-1] are forever owned by their own CPU. */
    for (uint32_t i = OPENOS_X86_64_SMP_MAX_CPUS_HINT;
         i < OPENOS_X86_64_SCHED_MAX_KTHREADS; ++i) {
        if (sched_slots[i].state == SCHED_SLOT_FREE
            || sched_slots[i].state == SCHED_SLOT_EXITED) {
            if (sched_slots[i].state == SCHED_SLOT_EXITED
                && sched_slots[i].stack_base != NULL) {
                arch_x86_64_kfree(sched_slots[i].stack_base);
                sched_slots[i].stack_base = NULL;
            }
            sched_slots[i].state    = SCHED_SLOT_FREE;
            sched_slots[i].id       = i;
            sched_slots[i].is_idle  = 0u;
            /* owner_cpu set by caller (spawn pins to spawning CPU). */
            return i;
        }
    }
    return 0u;
}

static uint32_t sched_pick_next(uint32_t from_idx) {
    /* G.6.4: only consider slots whose owner_cpu matches this CPU.
     *
     * BSP (cpu_idx==0) is special: it has no separate idle thread, so
     * slot 0 (the bootstrap thread) is itself the fallback when nothing
     * else is READY. APs get a dedicated is_idle=1 slot via
     * sched_register_ap_idle; their fallback is slot[cpu_idx]. */
    uint32_t my_cpu  = arch_x86_64_this_cpu_ptr()->cpu_idx;
    uint32_t fallback = (my_cpu == 0u) ? 0u
                                       : ((my_cpu < OPENOS_X86_64_SMP_MAX_CPUS_HINT) ? my_cpu : 0u);

    /* Pass 1: round-robin over non-idle ready slots owned by this CPU. */
    for (uint32_t step = 1u; step <= OPENOS_X86_64_SCHED_MAX_KTHREADS; ++step) {
        uint32_t i = (from_idx + step) % OPENOS_X86_64_SCHED_MAX_KTHREADS;
        if (sched_slots[i].owner_cpu != my_cpu) continue;
        if (sched_slots[i].is_idle)             continue;
        if (sched_slots[i].state == SCHED_SLOT_READY) {
            return i;
        }
        /* slot 0 on the BSP is RUNNING (not READY) when we are switching
         * away from it; allow round-robin to land back on it as a normal
         * thread, not just a fallback. */
        if (my_cpu == 0u && i == 0u
            && sched_slots[0].state == SCHED_SLOT_RUNNING
            && from_idx != 0u) {
            return 0u;
        }
    }

    /* Pass 2: fall back to this CPU's idle/bootstrap slot. */
    if (sched_slots[fallback].state == SCHED_SLOT_RUNNING
        || sched_slots[fallback].state == SCHED_SLOT_READY) {
        return fallback;
    }

    return from_idx;
}

uint32_t arch_x86_64_sched_spawn_kthread(x86_64_thread_entry_t entry, void *arg) {
    return arch_x86_64_sched_spawn_kthread_prio(entry, arg,
                                                OPENOS_X86_64_SCHED_PRIO_DEFAULT);
}

uint32_t arch_x86_64_sched_quantum_for_priority(uint32_t priority) {
    switch (priority) {
    case OPENOS_X86_64_SCHED_PRIO_HIGH:   return OPENOS_X86_64_SCHED_QUANTUM_HIGH;
    case OPENOS_X86_64_SCHED_PRIO_LOW:    return OPENOS_X86_64_SCHED_QUANTUM_LOW;
    case OPENOS_X86_64_SCHED_PRIO_NORMAL: /* fallthrough */
    default:                              return OPENOS_X86_64_SCHED_QUANTUM_NORMAL;
    }
}

uint32_t arch_x86_64_sched_spawn_kthread_prio(x86_64_thread_entry_t entry,
                                              void *arg,
                                              uint32_t priority) {
    return arch_x86_64_sched_spawn_kthread_prio_on(
        entry, arg, priority, arch_x86_64_this_cpu_ptr()->cpu_idx);
}

uint32_t arch_x86_64_sched_spawn_kthread_prio_on(x86_64_thread_entry_t entry,
                                                 void *arg,
                                                 uint32_t priority,
                                                 uint32_t target_cpu) {
    if (entry == NULL) {
        return 0u;
    }
    if (priority > OPENOS_X86_64_SCHED_PRIO_MAX) {
        priority = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    }
    /* G.6.5c: clamp out-of-range target_cpu to the spawning CPU.
     * MAX_CPUS is the hard ceiling; the smp layer reports the actual
     * online count via arch_x86_64_smp_cpu_count(), but we tolerate any
     * valid percpu slot index here (idle threads for cpu_idx >=
     * cpu_count would simply never be picked). */
    if (target_cpu >= OPENOS_X86_64_SMP_MAX_CPUS) {
        target_cpu = arch_x86_64_this_cpu_ptr()->cpu_idx;
    }
    uint32_t idx = sched_alloc_slot();
    if (idx == 0u) {
        return 0u;
    }

    void *stack = arch_x86_64_kmalloc(OPENOS_X86_64_SCHED_KSTACK_BYTES);
    if (stack == NULL) {
        sched_slots[idx].state = SCHED_SLOT_FREE;
        return 0u;
    }
    sched_slots[idx].stack_base = stack;

    uintptr_t stack_top = (uintptr_t)stack + OPENOS_X86_64_SCHED_KSTACK_BYTES;
    x86_64_thread_context_t tctx;
    arch_x86_64_context_init(&tctx, entry, arg, (x86_64_stack_ptr_t)stack_top);

    sched_slots[idx].ctx       = tctx.regs;
    sched_slots[idx].state     = SCHED_SLOT_READY;
    sched_slots[idx].id        = idx;
    sched_slots[idx].priority  = priority;
    sched_slots[idx].owner_cpu = target_cpu; /* G.6.5c: explicit pin */
    sched_slots[idx].is_idle   = 0u;
    return idx;
}

uint32_t arch_x86_64_sched_set_priority(uint32_t slot, uint32_t priority) {
    if (slot >= OPENOS_X86_64_SCHED_MAX_KTHREADS) {
        return 0xFFFFFFFFu;
    }
    if (priority > OPENOS_X86_64_SCHED_PRIO_MAX) {
        return 0xFFFFFFFFu;
    }
    if (sched_slots[slot].state == SCHED_SLOT_FREE
        || sched_slots[slot].state == SCHED_SLOT_EXITED) {
        return 0xFFFFFFFFu;
    }
    sched_slots[slot].priority = priority;
    return 0u;
}

uint32_t arch_x86_64_sched_get_priority(uint32_t slot) {
    if (slot >= OPENOS_X86_64_SCHED_MAX_KTHREADS) {
        return OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    }
    return sched_slots[slot].priority;
}

uint32_t arch_x86_64_sched_yield(void) {
    sched_ensure_bootstrap_slot();
    uint32_t cur = sched_pc_current();
    uint32_t nxt = sched_pick_next(cur);
    if (nxt == cur) {
        return 0u;
    }

    /* Mark current READY (unless it just exited). */
    if (sched_slots[cur].state == SCHED_SLOT_RUNNING) {
        sched_slots[cur].state = SCHED_SLOT_READY;
    }
    sched_slots[nxt].state    = SCHED_SLOT_RUNNING;
    sched_pc_set_current(nxt);
    sched_pc_inc_switches();

    x86_64_context_t *from = (cur == 0u) ? &bootstrap_context : &sched_slots[cur].ctx;
    x86_64_context_t *to   = (nxt == 0u) ? &bootstrap_context : &sched_slots[nxt].ctx;
    arch_x86_64_context_switch(from, to);
    /* When we resume here, sched_current_idx points back to `cur`. */
    return nxt;
}

void arch_x86_64_sched_exit_self(void) {
    uint32_t cur = sched_pc_current();
    if (cur == 0u) {
        /* Bootstrap must not exit through here. */
        return;
    }
    sched_slots[cur].state = SCHED_SLOT_EXITED;

    /* Find next runnable; if none, fall back to slot 0 (bootstrap). */
    uint32_t nxt = sched_pick_next(cur);
    if (nxt == cur) {
        nxt = 0u;
    }
    sched_slots[nxt].state = SCHED_SLOT_RUNNING;
    sched_pc_set_current(nxt);
    sched_pc_inc_switches();

    x86_64_context_t *to = (nxt == 0u) ? &bootstrap_context : &sched_slots[nxt].ctx;
    /* `from == NULL` tells context_switch64.S to skip saving. */
    arch_x86_64_context_switch(NULL, to);

    /* Should not return. */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

uint32_t arch_x86_64_sched_kthread_count(void) {
    uint32_t n = 0u;
    for (uint32_t i = 1u; i < OPENOS_X86_64_SCHED_MAX_KTHREADS; ++i) {
        if (sched_slots[i].state != SCHED_SLOT_FREE) {
            ++n;
        }
    }
    return n;
}

uint32_t arch_x86_64_sched_current_slot(void)   { return sched_pc_current(); }
uint64_t arch_x86_64_sched_switch_count(void)   { return sched_pc_switches(); }

/* -----------------------------------------------------------------
 * Step F.3: preemptive tick hook.
 *
 * Invariants enforced here (NOT inside yield, so cooperative yield
 * keeps its existing behavior):
 *   - Only preempt when at least one OTHER kthread is READY. The
 *     bootstrap slot is always present; lone-bootstrap means "nothing
 *     to preempt to", so we cheaply early-return.
 *   - Quantum counter is per-CPU (G.6.3: lives in percpu_t). On switch
 *     we reset it; the new RUNNING thread also starts with a fresh
 *     budget.
 *   - Counted via sched_preempt_count (per-CPU) so selftests can prove
 *     the IRQ-driven path was actually exercised (vs. cooperative yield).
 * ----------------------------------------------------------------- */

static int sched_has_other_ready(uint32_t cur) {
    /* G.6.4: filter by owner_cpu == this_cpu()->cpu_idx. is_idle slots
     * are explicitly NOT "other work" -- they are only the fallback. */
    uint32_t my_cpu = arch_x86_64_this_cpu_ptr()->cpu_idx;
    uint32_t fallback = (my_cpu == 0u) ? 0u
                                       : ((my_cpu < OPENOS_X86_64_SMP_MAX_CPUS_HINT) ? my_cpu : 0u);

    for (uint32_t i = 0u; i < OPENOS_X86_64_SCHED_MAX_KTHREADS; ++i) {
        if (i == cur) continue;
        if (sched_slots[i].owner_cpu != my_cpu) continue;
        if (sched_slots[i].is_idle)             continue;
        if (sched_slots[i].state == SCHED_SLOT_READY ||
            sched_slots[i].state == SCHED_SLOT_RUNNING) {
            return 1;
        }
    }
    /* Fallback slot (BSP slot 0 / AP idle) is always a valid landing,
     * but it is not "other ready work" if we are already on it. */
    if (cur != fallback && sched_slots[fallback].state != SCHED_SLOT_EXITED) {
        return 1;
    }
    return 0;
}

uint32_t arch_x86_64_sched_on_tick(void) {
    sched_ensure_bootstrap_slot();
    /* G.6.5b: count every entry into this function on a per-CPU basis.
     * This is the direct counterpart of the LAPIC-timer raw counter:
     *   - On BSP: bumped by PIT IRQ0 path -> sched_on_tick.
     *   - On AP:  bumped by LAPIC-timer vector 0x40 -> sched_on_tick.
     * Selftest uses this to prove sched_on_tick is reachable from each
     * CPU's own IRQ path (vs. only from cooperative yield). */
    arch_x86_64_this_cpu_ptr()->sched_tick_calls++;
    if (sched_pc_quantum() > 0u) {
        sched_pc_dec_quantum();
    }
    if (sched_pc_quantum() != 0u) {
        return 0u;
    }
    /* G.2: re-arm quantum from incoming (post-switch) thread's priority.
     * We compute it twice — once now using the current (about-to-leave)
     * thread, and once after yield using the newly-current thread. The
     * value that sticks is the one we set AFTER yield. */
    sched_pc_set_quantum(arch_x86_64_sched_quantum_for_priority(
        sched_slots[sched_pc_current()].priority));

    if (!sched_has_other_ready(sched_pc_current())) {
        return 0u;
    }
    uint32_t prev = sched_pc_current();
    uint32_t nxt  = arch_x86_64_sched_yield();
    /* Re-arm again using the NEW current thread's priority so HIGH
     * threads get their full slice on resume. */
    sched_pc_set_quantum(arch_x86_64_sched_quantum_for_priority(
        sched_slots[sched_pc_current()].priority));
    if (nxt != 0u || sched_pc_current() != prev) {
        sched_pc_inc_preempts();
        return 1u;
    }
    return 0u;
}

uint64_t arch_x86_64_sched_preempt_count(void) { return sched_pc_preempts(); }

void arch_x86_64_sched_print_status(void) {
    early_console64_write("[x86_64][sched] context switch supports rsp/rip/rflags and r8-r15\n");
    early_console64_write("[x86_64][sched] ready=");
    early_console64_write_hex64(sched64_ready);
    early_console64_write(" current=");
    early_console64_write_hex64((uint64_t)(uintptr_t)current_context);
    early_console64_write(" slot=");
    early_console64_write_hex64((uint64_t)sched_pc_current());
    early_console64_write(" switches=");
    early_console64_write_hex64(sched_pc_switches());
    early_console64_write(" kthreads=");
    early_console64_write_hex64((uint64_t)arch_x86_64_sched_kthread_count());
    early_console64_write("\n");
}

/* -----------------------------------------------------------------
 * Step G.6.4: per-CPU idle thread bookkeeping.
 *
 * sched_init_ap()          : called from ap_main once %gs is loaded.
 *                            Seeds sched_quantum_left so a future sti
 *                            + timer tick on this AP will not divide
 *                            by zero or fire immediately.
 *
 * sched_register_ap_idle() : reserves slot[cpu_idx] as the AP's idle
 *                            thread (RUNNING, owner_cpu=cpu_idx,
 *                            is_idle=1, sched_current_idx=cpu_idx).
 *                            Returns the slot id.
 *
 * sched_idle_slot_for_cpu(): returns the slot id of CPU C's idle
 *                            thread, or 0xFFFFFFFF if none.
 *
 * sched_idle_selftest()    : verifies every online CPU owns exactly
 *                            one is_idle slot in RUNNING state, and
 *                            reserved slots above online_cpus stay
 *                            pristine FREE.
 * ----------------------------------------------------------------- */

void arch_x86_64_sched_init_ap(void) {
    arch_x86_64_this_cpu_ptr()->sched_quantum_left = OPENOS_X86_64_SCHED_QUANTUM_NORMAL;
}

uint32_t arch_x86_64_sched_register_ap_idle(void) {
    uint32_t cpu = arch_x86_64_this_cpu_ptr()->cpu_idx;
    if (cpu == 0u || cpu >= OPENOS_X86_64_SMP_MAX_CPUS_HINT) {
        return 0xFFFFFFFFu;
    }
    if (sched_slots[cpu].state != SCHED_SLOT_FREE) {
        return 0xFFFFFFFFu;
    }
    sched_slots[cpu].state      = SCHED_SLOT_RUNNING;
    sched_slots[cpu].id         = cpu;
    sched_slots[cpu].priority   = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    sched_slots[cpu].owner_cpu  = cpu;
    sched_slots[cpu].is_idle    = 1u;
    sched_slots[cpu].stack_base = NULL;  /* AP runs on its trampoline stack */
    sched_pc_set_current(cpu);
    return cpu;
}

uint32_t arch_x86_64_sched_idle_slot_for_cpu(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_SMP_MAX_CPUS_HINT) return 0xFFFFFFFFu;
    if (sched_slots[cpu_idx].is_idle  != 1u)        return 0xFFFFFFFFu;
    if (sched_slots[cpu_idx].owner_cpu != cpu_idx)  return 0xFFFFFFFFu;
    return cpu_idx;
}

uint32_t arch_x86_64_sched_idle_selftest(uint32_t online_cpus) {
    if (online_cpus == 0u || online_cpus > OPENOS_X86_64_SMP_MAX_CPUS_HINT) {
        return 1u;
    }
    /* CPU 0 / BSP: slot 0 is the bootstrap thread, not a dedicated idle.
     * It must be RUNNING with owner_cpu=0 and is_idle=0. */
    if (sched_slots[0].owner_cpu != 0u)             return 0x01u;
    if (sched_slots[0].is_idle != 0u)               return 0x02u;
    if (sched_slots[0].state != SCHED_SLOT_RUNNING) return 0x03u;

    /* APs (CPU 1..online_cpus-1): each must own a dedicated is_idle
     * slot at slot[cpu_idx], RUNNING. */
    for (uint32_t c = 1u; c < online_cpus; ++c) {
        if (sched_slots[c].is_idle != 1u)              return 0x10u | c;
        if (sched_slots[c].owner_cpu != c)             return 0x20u | c;
        if (sched_slots[c].state != SCHED_SLOT_RUNNING) return 0x30u | c;
    }
    /* Reserved band above online_cpus must remain pristine FREE. */
    for (uint32_t c = online_cpus; c < OPENOS_X86_64_SMP_MAX_CPUS_HINT; ++c) {
        if (sched_slots[c].state != SCHED_SLOT_FREE)   return 0x40u | c;
        if (sched_slots[c].is_idle != 0u)              return 0x50u | c;
    }
    return 0u;
}

/* G.6.5b: read per-CPU sched_on_tick entry counter for arbitrary CPU.
 * Uses the percpu_slot() accessor (NOT %gs) so any CPU can observe any
 * other CPU's counter. Safe to call without locks because:
 *   - The counter is a 64-bit aligned scalar, written by exactly one
 *     CPU (the owner) from its own IRQ context.
 *   - We only need monotonic-eventual-consistency for the selftest's
 *     polling loop: a slightly stale read is fine, torn read on x86_64
 *     for naturally aligned u64 is not possible. */
uint64_t arch_x86_64_sched_tick_calls_for_cpu(uint32_t cpu_idx) {
    arch_x86_64_percpu_t *p = arch_x86_64_percpu_slot(cpu_idx);
    if (p == (void *)0) return 0;
    return p->sched_tick_calls;
}

/* G.6.5c: per-CPU sched_switch_count reader (no %gs, cross-CPU safe).
 * Same alignment/torn-read argument as tick_calls_for_cpu: u64 natural
 * alignment on x86_64 guarantees atomic load; we only need eventual
 * consistency for polling. */
uint64_t arch_x86_64_sched_switch_count_for_cpu(uint32_t cpu_idx) {
    arch_x86_64_percpu_t *p = arch_x86_64_percpu_slot(cpu_idx);
    if (p == (void *)0) return 0;
    return p->sched_switch_count;
}

/* G.6.6b: peek at a slot's owner_cpu. Returns 0xFFFFFFFFu on FREE or
 * out-of-bounds. Slots are static storage so reads need no locking;
 * owner_cpu is u32 aligned and observers tolerate eventual consistency. */
uint32_t arch_x86_64_sched_slot_owner(uint32_t slot_idx) {
    if (slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) return 0xFFFFFFFFu;
    sched_slot_t *s = &sched_slots[slot_idx];
    if (s->state == SCHED_SLOT_FREE) return 0xFFFFFFFFu;
    return s->owner_cpu;
}

/* G.6.6b: migrate a READY slot to target_cpu and poke the target with a
 * reschedule IPI so it picks the new work on its next tick.
 *
 * Returns:
 *   0  success
 *   1  slot_idx out of bounds
 *   2  slot is FREE / EXITED (nothing to migrate)
 *   3  slot is RUNNING (would race a context save on its current owner)
 *   4  slot is the per-CPU idle (idle is pinned by design)
 *   5  target_cpu out of bounds or not online
 *   6  no-op (already owned by target_cpu)
 *   7  IPI send failed (target apic_id unknown / cpu not alive)
 *
 * Owner mutation happens first; if the target ever ticks before we send
 * the IPI it would still discover the new owner -- the IPI just shortens
 * latency from O(tick) to O(IPI). */
uint32_t arch_x86_64_sched_migrate(uint32_t slot_idx, uint32_t target_cpu) {
    if (slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) return 1u;
    sched_slot_t *s = &sched_slots[slot_idx];

    if (s->state == SCHED_SLOT_FREE || s->state == SCHED_SLOT_EXITED) return 2u;
    if (s->state == SCHED_SLOT_RUNNING) return 3u;
    if (s->is_idle) return 4u;

    uint32_t online = arch_x86_64_smp_cpu_count();
    if (online == 0u) online = 1u; /* defensive: at least BSP exists */
    if (target_cpu >= online || target_cpu >= OPENOS_X86_64_SMP_MAX_CPUS_HINT) return 5u;

    if (s->owner_cpu == target_cpu) return 6u;

    s->owner_cpu = target_cpu;

    /* BSP-bound migrations need no IPI: BSP will pick up on its next PIT
     * tick. APs need a kick because they only run on LAPIC timer ticks
     * (~6Hz) and we want the migration latency to be O(microseconds). */
    if (target_cpu != 0u) {
        if (!arch_x86_64_smp_send_resched_ipi(target_cpu)) return 7u;
    }
    return 0u;
}
