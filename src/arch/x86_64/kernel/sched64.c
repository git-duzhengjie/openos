#include "../include/sched64.h"

#include <stddef.h>
#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/heap64.h"
#include "../include/percpu64.h"

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
    x86_64_context_t    ctx;          /* slot 0 mirrors bootstrap_context */
    void               *stack_base;   /* heap-allocated stack (slot >=1) */
    uint32_t            id;           /* 1-based slot id; 0 == bootstrap */
    uint32_t            priority;     /* G.2: scheduling priority band */
} sched_slot_t;

static sched_slot_t  sched_slots[OPENOS_X86_64_SCHED_MAX_KTHREADS];
/* G.6.3: sched_current_idx, sched_switch_count, sched_quantum_left,
 * sched_preempt_count moved into arch_x86_64_percpu_t (this_cpu()->...). */

static void sched_ensure_bootstrap_slot(void) {
    if (sched_slots[0].state != SCHED_SLOT_FREE) {
        return;
    }
    sched_slots[0].state    = SCHED_SLOT_RUNNING;
    sched_slots[0].id       = 0u;
    sched_slots[0].priority = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    /* slot 0 "ctx" is unused for save; bootstrap_context handles that. */
    sched_pc_set_current(0u);
}

static uint32_t sched_alloc_slot(void) {
    sched_ensure_bootstrap_slot();
    for (uint32_t i = 1u; i < OPENOS_X86_64_SCHED_MAX_KTHREADS; ++i) {
        if (sched_slots[i].state == SCHED_SLOT_FREE
            || sched_slots[i].state == SCHED_SLOT_EXITED) {
            if (sched_slots[i].state == SCHED_SLOT_EXITED
                && sched_slots[i].stack_base != NULL) {
                arch_x86_64_kfree(sched_slots[i].stack_base);
                sched_slots[i].stack_base = NULL;
            }
            sched_slots[i].state = SCHED_SLOT_FREE;
            sched_slots[i].id    = i;
            return i;
        }
    }
    return 0u;
}

static uint32_t sched_pick_next(uint32_t from_idx) {
    /* Round-robin: scan slots starting after `from_idx`. */
    for (uint32_t step = 1u; step <= OPENOS_X86_64_SCHED_MAX_KTHREADS; ++step) {
        uint32_t i = (from_idx + step) % OPENOS_X86_64_SCHED_MAX_KTHREADS;
        if (i == 0u) {
            if (sched_slots[0].state == SCHED_SLOT_RUNNING
                || sched_slots[0].state == SCHED_SLOT_READY) {
                return 0u;
            }
            continue;
        }
        if (sched_slots[i].state == SCHED_SLOT_READY) {
            return i;
        }
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
    if (entry == NULL) {
        return 0u;
    }
    if (priority > OPENOS_X86_64_SCHED_PRIO_MAX) {
        priority = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
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
    for (uint32_t i = 0u; i < OPENOS_X86_64_SCHED_MAX_KTHREADS; ++i) {
        if (i == cur) continue;
        if (sched_slots[i].state == SCHED_SLOT_READY ||
            sched_slots[i].state == SCHED_SLOT_RUNNING) {
            return 1;
        }
    }
    /* Bootstrap (slot 0) is always considered ready as a fallback. */
    if (cur != 0u && sched_slots[0].state != SCHED_SLOT_EXITED) {
        return 1;
    }
    return 0;
}

uint32_t arch_x86_64_sched_on_tick(void) {
    sched_ensure_bootstrap_slot();
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
