#include "../include/sched64.h"

#include <stddef.h>
#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/gdt64.h"
#include "../include/heap64.h"
#include "../include/percpu64.h"
#include "../include/smp64.h"  /* G.6.5c: OPENOS_X86_64_SMP_MAX_CPUS for spawn_on clamp */
#include "../include/usermode64.h"

/* G.7e: trampoline implemented in usermode64.S. context_switch dispatches
 * a USER slot by loading ctx.rsp (-> iretq frame) and jumping to ctx.rip,
 * which is this label. */
extern void arch_x86_64_user_thread_trampoline(void);

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
    /* G.7d: thread kind tagging --------------------------------------
     * KERNEL = ring0-only; USER = scheduled to ring3 (G.7e+).
     *
     * `kernel_stack_top` is the saved-stack-top value that would be
     * loaded into TSS.RSP0 each time a USER slot gets dispatched. For
     * KERNEL slots it is informational (equals stack_base + KSTACK_BYTES)
     * and is NOT applied to TSS.RSP0 (those use the per-CPU shared RSP0).
     * For slot 0 (BSP bootstrap) both fields stay zero -- BSP uses the
     * per-CPU RSP0 baked into TSS at percpu_setup. */
    uint32_t            kind;
    uintptr_t           kernel_stack_top;
} sched_slot_t;

static sched_slot_t  sched_slots[OPENOS_X86_64_SCHED_MAX_KTHREADS];
/* G.6.3: sched_current_idx, sched_switch_count, sched_quantum_left,
 * sched_preempt_count moved into arch_x86_64_percpu_t (this_cpu()->...). */

static void sched_apply_rsp0_for_next(uint32_t nxt) {
    /* G.7e: maintain TSS.RSP0 across slot transitions.
     *
     *   - USER slot:    write the slot's kernel_stack_top into TSS.RSP0
     *                   so future syscalls/IRQs from ring3 land on this
     *                   thread's private kernel stack.
     *   - KERNEL slot:  restore TSS.RSP0 to the per-CPU baseline that
     *                   percpu_setup() installed (kthreads use the
     *                   per-CPU shared RSP0; their ctx.rsp is a
     *                   separate switch stack, not RSP0).
     *
     * Slot 0 (BSP bootstrap) is KERNEL with kernel_stack_top=0 -- it
     * uses the BSP's percpu baseline RSP0, which is exactly what the
     * baseline-restore branch installs. */
    uint32_t cpu = arch_x86_64_this_cpu_ptr()->cpu_idx;
    if (sched_slots[nxt].kind == OPENOS_X86_64_SCHED_KIND_USER) {
        uintptr_t kstk = sched_slots[nxt].kernel_stack_top;
        if (kstk != 0u) {
            (void)arch_x86_64_percpu_set_rsp0(cpu, (x86_64_stack_ptr_t)kstk);
        }
    } else {
        uint64_t baseline = arch_x86_64_percpu_baseline_rsp0(cpu);
        if (baseline != 0u) {
            (void)arch_x86_64_percpu_set_rsp0(cpu, (x86_64_stack_ptr_t)baseline);
        }
    }
}

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
    /* G.7d: BSP bootstrap is a kernel thread; it never owns a per-thread
     * kernel stack (it runs on whatever stack the boot path set up and
     * then on the BSP's per-CPU RSP0 once percpu_setup(0) ran). Leave
     * kernel_stack_top=0 to signal "do not touch TSS.RSP0 for this slot". */
    sched_slots[0].kind             = OPENOS_X86_64_SCHED_KIND_KERNEL;
    sched_slots[0].kernel_stack_top = 0u;
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
            /* G.7d: reset kind back to KERNEL on slot reuse; spawn_kthread
             * will (re-)set kind + kernel_stack_top with the fresh stack. */
            sched_slots[i].kind             = OPENOS_X86_64_SCHED_KIND_KERNEL;
            sched_slots[i].kernel_stack_top = 0u;
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
    /* G.7d: tag the new slot as KERNEL and record the stack top.
     * For KERNEL slots kernel_stack_top is informational only -- the
     * dispatcher leaves TSS.RSP0 alone for them (KERNEL stays on the
     * per-CPU shared RSP0). G.7e introduces spawn_uthread which sets
     * kind=USER and the same kernel_stack_top is then applied to
     * TSS.RSP0 at every dispatch of that slot. */
    sched_slots[idx].kind             = OPENOS_X86_64_SCHED_KIND_KERNEL;
    sched_slots[idx].kernel_stack_top = stack_top;
    return idx;
}

uint32_t arch_x86_64_sched_spawn_uthread(uintptr_t user_entry,
                                         uintptr_t user_rsp,
                                         uint32_t priority,
                                         uint32_t target_cpu) {
    if (user_entry == 0u) {
        return 0u;
    }
    if (priority > OPENOS_X86_64_SCHED_PRIO_MAX) {
        priority = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    }
    if (target_cpu >= OPENOS_X86_64_SMP_MAX_CPUS) {
        target_cpu = arch_x86_64_this_cpu_ptr()->cpu_idx;
    }

    /* If the caller did not supply an explicit user_rsp, fall back to the
     * embedded usermode blob's reserved bootstrap stack. usermode64.c
     * keeps it identity-mapped user-writable, so it is safe to reuse from
     * any CPU as long as only one user thread is live at a time (Stage 21
     * spawns exactly one). */
    if (user_rsp == 0u) {
        x86_64_user_iretq_frame_t tmp;
        const x86_64_user_iretq_frame_t *prepared =
            arch_x86_64_usermode_get_prepared_frame();
        tmp = *prepared;
        if (tmp.rsp == 0u) {
            return 0u; /* usermode subsystem not initialized yet */
        }
        user_rsp = (uintptr_t)tmp.rsp;
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

    uintptr_t kstack_top = (uintptr_t)stack + OPENOS_X86_64_SCHED_KSTACK_BYTES;

    /* G.7e: build the iretq frame at the very top of the kernel stack so
     * that ctx.rsp points exactly at it. context_switch's restore path uses
     * `pushq %rax + popfq` which temporarily writes rsp-8, so the iretq
     * frame must live strictly at and above the value loaded into rsp.
     *
     * Layout (low -> high): rip, cs, rflags, rsp, ss
     */
    uintptr_t frame_base = kstack_top - (5u * sizeof(uint64_t));
    /* Align the iretq RSP slot (the kernel rsp loaded into the context) to
     * a 16-byte boundary. iretq itself does not require 16B alignment of
     * the kernel rsp, but keeping it aligned avoids surprises in any
     * future entry-stub spill. */
    frame_base &= ~((uintptr_t)0xFu);
    uint64_t *frame = (uint64_t *)frame_base;
    frame[0] = (uint64_t)user_entry;                          /* RIP */
    frame[1] = (uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);  /* CS */
    frame[2] = 0x202ULL;                                      /* RFLAGS: IF=1 */
    frame[3] = (uint64_t)user_rsp;                            /* RSP */
    frame[4] = (uint64_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);  /* SS */

    /* Hand-craft the slot context. context_switch64.S restore sequence:
     *   mov ctx.rsp,    %rsp      ; rsp = frame_base
     *   mov ctx.rflags, %rax      ; rax = 0 (IF=0)
     *   pushq %rax                ; uses rsp-8 (below frame, scratch)
     *   popfq                     ; rflags loaded; rsp back to frame_base
     *   mov ctx.rip,    %rax      ; rax = trampoline addr
     *   jmp *%rax                 ; -> trampoline; rsp == frame_base
     * Trampoline then does swapgs + iretq, popping the 5-qword frame and
     * atomically restoring user CS:RIP, RFLAGS (IF=1), and SS:RSP. */
    x86_64_context_t ctx;
    ctx.rsp    = (x86_64_stack_ptr_t)frame_base;
    ctx.rip    = (x86_64_entry_t)&arch_x86_64_user_thread_trampoline;
    ctx.rflags = 0u; /* IF cleared until iretq atomically restores user IF */
    ctx.rbx    = 0u;
    ctx.rbp    = 0u;
    ctx.r12    = 0u;
    ctx.r13    = 0u;
    ctx.r14    = 0u;
    ctx.r15    = 0u;
    ctx.r8     = 0u;
    ctx.r9     = 0u;
    ctx.r10    = 0u;
    ctx.r11    = 0u;

    sched_slots[idx].ctx              = ctx;
    sched_slots[idx].state            = SCHED_SLOT_READY;
    sched_slots[idx].id               = idx;
    sched_slots[idx].priority         = priority;
    sched_slots[idx].owner_cpu        = target_cpu;
    sched_slots[idx].is_idle          = 0u;
    sched_slots[idx].kind             = OPENOS_X86_64_SCHED_KIND_USER;
    sched_slots[idx].kernel_stack_top = kstack_top;
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
    sched_apply_rsp0_for_next(nxt);
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
    sched_apply_rsp0_for_next(nxt);
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
    /* G.2: re-arm quantum from the current (about-to-leave) thread's
     * priority. The post-switch re-arm happens in check_and_dispatch()
     * after sched_yield(), see the symmetric block there. */
    sched_pc_set_quantum(arch_x86_64_sched_quantum_for_priority(
        sched_slots[sched_pc_current()].priority));

    if (!sched_has_other_ready(sched_pc_current())) {
        return 0u;
    }

    /* G.6.7c: timer-tick path is now unified with the resched-IPI path.
     * Instead of yielding inline (which bypassed preempt_disable and
     * left us with two independent dispatch sites), we set the per-CPU
     * need_resched latch and let the ISR-tail dispatch hook decide
     * whether to actually yield.
     *
     * This achieves three things:
     *   1. preempt_disable() now uniformly defers timer-driven
     *      preemption as well as IPI-driven preemption.
     *   2. dispatch accounting (resched_dispatch_count) is incremented
     *      in exactly one place -- the ISR-tail hook -- regardless of
     *      whether the wakeup came from a timer or an IPI.
     *   3. The historical "preempts" counter is no longer bumped here;
     *      it would double-count with resched_dispatch_count. The new
     *      single source of truth is resched_dispatch_count, which is
     *      what selftests (Stage 14/15/16) read.
     *
     * We do NOT call check_and_dispatch() here: that is the ISR-tail's
     * job and runs *after* EOI, in a context where the IRQ stub has
     * saved caller-saved regs and is ready to context-switch via
     * iretq. Calling it here, in the middle of timer handling, would
     * re-enter sched_yield while the IRQ is not yet acknowledged. */
    arch_x86_64_sched_set_need_resched();
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
    /* G.7d: AP idle slots are KERNEL; they ride the per-CPU RSP0 that
     * was baked into the AP's TSS by percpu_setup(cpu) on AP bringup,
     * so kernel_stack_top stays 0 ("don't touch TSS.RSP0"). */
    sched_slots[cpu].kind             = OPENOS_X86_64_SCHED_KIND_KERNEL;
    sched_slots[cpu].kernel_stack_top = 0u;
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

/* G.7d observers ------------------------------------------------------
 * Reads from the static slot array; safe from any CPU (no locking, u32
 * naturally aligned). Return 0xFFFFFFFFu / 0 for FREE/out-of-bounds so
 * Stage 20 can sanity-check what was just spawned. */
uint32_t arch_x86_64_sched_slot_kind(uint32_t slot_idx) {
    if (slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) return 0xFFFFFFFFu;
    sched_slot_t *s = &sched_slots[slot_idx];
    if (s->state == SCHED_SLOT_FREE) return 0xFFFFFFFFu;
    return s->kind;
}

uintptr_t arch_x86_64_sched_slot_kstack_top(uint32_t slot_idx) {
    if (slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) return 0u;
    sched_slot_t *s = &sched_slots[slot_idx];
    if (s->state == SCHED_SLOT_FREE) return 0u;
    return s->kernel_stack_top;
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

/* ====================================================================
 * G.6.7a: preemption tail-hook plumbing
 *
 * Design notes (also see sched64.h):
 *  - need_resched is a per-CPU latch (u32, naturally aligned at offset
 *    0x40). Writers store 1, the local CPU reads-and-clears it at
 *    ISR-tail.
 *  - On x86_64 aligned u32 stores are atomic w.r.t. aligned u32 loads,
 *    so a remote CPU writing 1 while the local CPU reads-and-clears
 *    cannot produce a torn value. Worst case is the remote write loses
 *    a race with the clear, in which case the next IPI / next tick will
 *    re-arm the latch -- acceptable for a wakeup hint.
 *  - Dispatch is *not* recursive: we sched_yield only when not already
 *    inside another yield (a future preempt_count gate would live here
 *    and replace this simple check). Today the only callers are ISR
 *    tails which never re-enter, so we keep it simple.
 * ==================================================================== */

void arch_x86_64_sched_set_need_resched(void) {
    /* Local-CPU latch. Going through %gs avoids touching the global
     * percpu array when the only thing we want is "this CPU". */
    arch_x86_64_percpu_t *p = arch_x86_64_this_cpu_ptr();
    if (!p) return;
    p->need_resched = 1u;
}

void arch_x86_64_sched_set_need_resched_remote(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) return;
    arch_x86_64_percpu_t *p = arch_x86_64_percpu_slot(cpu_idx);
    if (!p) return;
    /* Plain store: aligned u32 -> single instruction, no torn-write
     * risk on the remote reader. No memory barrier needed because the
     * accompanying IPI (which is what the caller will send next) acts
     * as a serializing event on the remote CPU's interrupt boundary. */
    p->need_resched = 1u;
}

uint32_t arch_x86_64_sched_drain_need_resched(uint32_t cpu_idx) {
    /* G.6.7c: read-and-clear the need_resched latch WITHOUT going
     * through the dispatch gate. The gate (check_and_dispatch) is
     * the normal way to consume the latch, but it short-circuits
     * when preempt_disable_depth>0 -- it returns 0 with the latch
     * STILL SET so a later preempt_enable() can pick it up. That
     * deferred-fire behaviour is exactly what we want during normal
     * operation, but selftests sometimes need to drain a coincidental
     * latch (e.g. a PIT tick happening to fire inside a measurement
     * window) without triggering deferred-dispatch on the way out.
     *
     * Returns the old value so callers can log/check it. Safe to call
     * cross-CPU as long as the caller can tolerate the inherent race
     * with the remote CPU's own check_and_dispatch (the canonical use
     * is to call it for the current CPU while the gate is held, which
     * is race-free by construction). */
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) return 0u;
    arch_x86_64_percpu_t *p = arch_x86_64_percpu_slot(cpu_idx);
    if (!p) return 0u;
    uint32_t old = p->need_resched;
    p->need_resched = 0u;
    return old;
}

uint32_t arch_x86_64_sched_check_and_dispatch(void) {
    arch_x86_64_percpu_t *p = arch_x86_64_this_cpu_ptr();
    if (!p) return 0u;

    /* G.6.7b: critical-section gate. While preempt_disable_depth>0,
     * we are inside a non-preemptible region. Leave the latch alone
     * so that preempt_enable() can pick it up on the 1->0 edge and
     * fire a deferred dispatch. This is the *load-bearing* behavior
     * of the new gate: we observe the latch but refuse to act on it. */
    if (p->preempt_disable_depth != 0u) {
        return 0u;
    }

    /* Read-and-clear. If the latch wasn't set, fast path: no yield. */
    uint32_t pending = p->need_resched;
    if (!pending) return 0u;
    p->need_resched = 0u;

    /* Account *before* yielding -- once we yield, our stack is parked
     * and the count we want to bump belongs to the CPU we're leaving.
     * Note: dispatch_count is tied to the CPU, not the thread, so it
     * stays consistent across the upcoming context switch. */
    p->resched_dispatch_count++;

    /* Perform the actual reschedule. sched_yield will pick the next
     * runnable slot on this CPU (or fall back to the per-CPU idle if
     * nothing else is ready). It returns through context_switch which
     * restores the next thread's rflags -- the restored thread will
     * therefore observe IF=1 even though we entered with IF=0. This is
     * the same EOI-last + iretq-restores-IF discipline used by the
     * timer tick path; see F.3 commit for the full proof. */
    arch_x86_64_sched_yield();

    /* G.6.7c: post-switch quantum re-arm. After yielding, sched_pc_current()
     * now refers to the NEW thread on this CPU. Re-arm its quantum from
     * its own priority so HIGH threads get their full slice on resume.
     * This was previously done inside sched_on_tick; now that the timer
     * path no longer yields inline, the only place where a yield triggered
     * by tick-expiry actually executes is right here. Re-arming here keeps
     * the per-priority slice contract intact for both timer- and IPI-driven
     * dispatches. (For IPI-driven dispatches there is no quantum-expiry
     * implied, but giving the incoming thread a fresh slice is still the
     * right call: the previous thread voluntarily lost the CPU.) */
    sched_pc_set_quantum(arch_x86_64_sched_quantum_for_priority(
        sched_slots[sched_pc_current()].priority));
    sched_pc_inc_preempts();
    return 1u;
}

uint64_t arch_x86_64_sched_dispatch_count_for_cpu(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) return 0ull;
    arch_x86_64_percpu_t *p = arch_x86_64_percpu_slot(cpu_idx);
    if (!p) return 0ull;
    return p->resched_dispatch_count;
}

uint32_t arch_x86_64_sched_need_resched_for_cpu(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) return 0u;
    arch_x86_64_percpu_t *p = arch_x86_64_percpu_slot(cpu_idx);
    if (!p) return 0u;
    return p->need_resched;
}

/* ------------------------------------------------------------------
 * G.6.7b: preempt-disable / preempt-enable.
 *
 * Both functions operate on the *current* CPU's percpu slot via %gs.
 * They do NOT touch interrupt-flag state (IF). Disabling preemption
 * is a softer guarantee than CLI: interrupts still fire, ISRs still
 * run, but the ISR tail will NOT yield. This is the standard Linux
 * preempt_count discipline and is the right granularity for the
 * majority of kernel critical sections that need to be atomic w.r.t.
 * other threads on the same CPU but tolerate interrupt nesting. */
void arch_x86_64_preempt_disable(void) {
    arch_x86_64_percpu_t *p = arch_x86_64_this_cpu_ptr();
    if (!p) return;
    /* Single-CPU local counter; no atomics required because only
     * this CPU writes it. Interrupt nesting on this CPU is also fine:
     * a disable inside an ISR raises the count, the matching enable
     * lowers it, and the outer context sees a balanced delta of 0. */
    p->preempt_disable_depth++;
}

void arch_x86_64_preempt_enable(void) {
    arch_x86_64_percpu_t *p = arch_x86_64_this_cpu_ptr();
    if (!p) return;

    /* Underflow guard: a depth of 0 here means somebody called
     * preempt_enable() without a matching preempt_disable(). That is
     * a hard bug; do not decrement (would wrap u32 and permanently
     * disable preemption on this CPU). */
    if (p->preempt_disable_depth == 0u) {
        return;
    }

    uint32_t new_depth = --p->preempt_disable_depth;

    /* The deferred-dispatch edge: only fires on the 1->0 transition,
     * not on nested 2->1 decrements. */
    if (new_depth == 0u && p->need_resched != 0u) {
        /* Account before yielding -- once we yield, our stack parks
         * and any further increments would land on the wrong slot. */
        p->preempt_deferred_count++;

        /* Hand off to check_and_dispatch. By this point depth==0 so
         * the gate inside it is open; it will read-and-clear the latch
         * and call sched_yield. We could also call sched_yield directly
         * here, but going through check_and_dispatch keeps the latch
         * clearing logic and the dispatch_count bump in a single place. */
        (void)arch_x86_64_sched_check_and_dispatch();
    }
}

uint32_t arch_x86_64_preempt_depth_for_cpu(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) return 0u;
    arch_x86_64_percpu_t *p = arch_x86_64_percpu_slot(cpu_idx);
    if (!p) return 0u;
    return p->preempt_disable_depth;
}

uint64_t arch_x86_64_preempt_deferred_count_for_cpu(uint32_t cpu_idx) {
    if (cpu_idx >= OPENOS_X86_64_PERCPU_MAX_CPUS) return 0ull;
    arch_x86_64_percpu_t *p = arch_x86_64_percpu_slot(cpu_idx);
    if (!p) return 0ull;
    return p->preempt_deferred_count;
}
