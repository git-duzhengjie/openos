#include "../include/sched64.h"

#include <stddef.h>
#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/gdt64.h"
#include "../include/heap64.h"
#include "../include/percpu64.h"
#include "../include/smp64.h"  /* G.6.5c: OPENOS_X86_64_SMP_MAX_CPUS for spawn_on clamp */
#include "../include/usermode64.h"
#include "../include/address_space64.h"  /* γ.3: as_activate on slot dispatch */

/* γ.3b-S2a岔路5(方案B): forward decl for owner_proc back-pointer in sched_slot_t.
 * Full proc PCB layout is proc64.h; sched64 only stores an opaque pointer. */
struct x86_64_proc;

/* G.7e: trampoline implemented in usermode64.S. context_switch dispatches
 * a USER slot by loading ctx.rsp (-> iretq frame) and jumping to ctx.rip,
 * which is this label. */
extern void arch_x86_64_user_thread_trampoline(void);
extern void arch_x86_64_user_thread_trampoline_full(void);  /* γ.3b-S2a Seg-4 */
extern void arch_x86_64_user_thread_sentinel_trampoline(void);

/* γ.5-F1: dedicated per-CPU idle stacks.
 *
 * Before F1, sched_register_ap_idle() left slot[cpu_idx].ctx zeroed and the
 * AP's real idle loop ran on the AP boot stack (from ap_entry.S). That boot
 * stack gets clobbered by every subsequent kernel entry (IRQ, syscall,
 * fork parent path). When timer preempts idle for the first time, the ISR
 * saves the current RSP/RIP into slot[cpu_idx].ctx — but by then the boot
 * stack contents around that RSP have already been repeatedly overwritten,
 * and no dispatch has yet routed through slot[cpu_idx] as a *from* slot to
 * repopulate it either. So the first sched_exit_self() fallback on an AP
 * (nxt == cur → slot[cpu_idx] idle) restored a coherent-but-stale RSP that
 * pointed into freed frames → iretq to RIP=0 / hang. This precisely matches
 * the observed regression: waitpid section pid=5/7 on cpu2 never return
 * while wait-multi siblings (whose exit_self picks another READY sibling in
 * Pass 1) succeed.
 *
 * F1 gives each AP a clean, dedicated 4 KiB idle stack and a well-defined
 * arch_x86_64_ap_idle_entry(). ap_entry.S enters idle via
 * arch_x86_64_sched_enter_ap_idle(), which calls context_switch(NULL, &idle)
 * → the FIRST time the AP touches its idle loop it is already running on
 * the dedicated stack. Every subsequent save-into-idle-slot writes clean
 * frames onto that same private stack, and every subsequent restore-from-
 * idle-slot is guaranteed coherent.
 *
 * Stack size: 4 KiB is sufficient — the loop body is exactly `sti; hlt`,
 * no locals, no calls. IRQ handlers switch to their IST/TSS stack, so the
 * idle stack only holds the resume frame of the interrupted idle loop.
 *
 * Slot 0 is BSP and never uses this array; g_ap_idle_stacks[0] is unused. */
#define OPENOS_X86_64_AP_IDLE_STACK_BYTES 16384u
static __attribute__((aligned(16)))
uint8_t g_ap_idle_stacks[OPENOS_X86_64_SMP_MAX_CPUS_HINT]
                        [OPENOS_X86_64_AP_IDLE_STACK_BYTES];

static void __attribute__((noreturn)) arch_x86_64_ap_idle_entry(void)
{
    /* AP idle loop, running on its dedicated idle stack.
     * `sti` is explicit for defence-in-depth in case F1 is ever entered
     * before rflags-restore paths (F.3) are fully wired. */
    __asm__ volatile ("sti");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

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

/* NOTE: sched_slot_state_t moved to sched64.h so external selftests
 * (and future proc-layer integration in gamma.3b-S2+) can inspect
 * slot_state() return values by name. */

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
    /* γ.3: address space to activate on dispatch. NULL = keep current CR3
     * (kernel-only slot rides the boot AS or whatever CR3 is loaded). */
    struct x86_64_address_space *as;
    /* γ.3b-S2a岔路5(方案B): owner PCB back-pointer. Non-NULL for USER
     * slots owned by a proc; NULL for KERNEL / idle / bootstrap slots.
     * proc_current() reads this via sched_current_slot() to derive the
     * running PCB per-CPU without a separate percpu.current_proc_slot
     * field. Set by fork_alloc_child right before slot_wakeup; cleared
     * on slot_release / EXITED reap. */
    struct x86_64_proc          *owner_proc;
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

    /* γ.3: activate the target slot's address space (CR3 switch).
     * NULL as = keep current CR3 (kthread selftest slots ride the boot AS). */
    if (sched_slots[nxt].as != NULL) {
        arch_x86_64_as_activate(sched_slots[nxt].as);
    }

    /* gamma.3b-S2a Patch B removed: disp_dbg debug telemetry stripped. */
}

static void sched_ensure_bootstrap_slot(void) {
    if (sched_slots[0].state != SCHED_SLOT_FREE) {
        return;
    }
    sched_slots[0].state     = SCHED_SLOT_RUNNING;
    sched_slots[0].id        = 0u;
    sched_slots[0].priority  = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    sched_slots[0].owner_cpu = 0u;   /* G.6.4: BSP owns slot 0 */
    sched_slots[0].as        = NULL; /* γ.3: bootstrap rides boot AS */
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
    sched_slots[0].owner_proc       = NULL;  /* γ.3b-S2a Seg-2: bootstrap has no user PCB */
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
            sched_slots[i].as               = NULL;  /* γ.3: no AS by default */
            sched_slots[i].owner_proc       = NULL;  /* γ.3b-S2a Seg-2: no owner PCB */
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

/* gamma.3b-alpha: parked USER slot for fork(2) child preparation.
 *
 * Identical layout to arch_x86_64_sched_spawn_uthread but:
 *   - accepts full iretq state (rip/rsp/rflags/cs/ss) so the child can
 *     resume with parent-saved r11/rcx/user_rsp and ring3 selectors;
 *   - final state is SCHED_SLOT_PARKED, not READY, so pick_next skips
 *     it until the caller flips it to READY;
 *   - the slot's AS binding is NOT touched here -- caller is expected
 *     to invoke arch_x86_64_sched_slot_set_as() before READY-flip so
 *     dispatcher's as_activate lands on the child's cloned tree.
 *
 * On failure returns 0xFFFFFFFFu (all slot indices are <= 0xFFFF).
 *
 * Rationale: fork_alloc_child needs to hand the child a real iretq
 * frame carrying (a) rax=0 return code, (b) parent's SYSCALL-time
 * user CS/SS/RSP/RFLAGS, (c) the syscall-return RIP. Beta will wire
 * those in; alpha allocates the slot in PARKED state, sanity-checks
 * bookkeeping, then releases it on reap. Zero behaviour change. */
uint32_t arch_x86_64_sched_spawn_uthread_parked(uint64_t user_rip,
                                                uint64_t user_rsp,
                                                uint64_t user_rflags,
                                                uint16_t user_cs,
                                                uint16_t user_ss,
                                                uint32_t priority,
                                                uint32_t target_cpu) {
    if (user_rip == 0u || user_rsp == 0u) {
        return 0xFFFFFFFFu;
    }
    if (priority > OPENOS_X86_64_SCHED_PRIO_MAX) {
        priority = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    }
    if (target_cpu >= OPENOS_X86_64_SMP_MAX_CPUS) {
        target_cpu = arch_x86_64_this_cpu_ptr()->cpu_idx;
    }
    /* IF must be set on iretq (child must resume interruptible). Force
     * it in defensively even if caller passed a raw r11 that had it. */
    user_rflags |= 0x200ULL;

    uint32_t idx = sched_alloc_slot();
    if (idx == 0u) {
        return 0xFFFFFFFFu;
    }

    void *stack = arch_x86_64_kmalloc(OPENOS_X86_64_SCHED_KSTACK_BYTES);
    if (stack == NULL) {
        sched_slots[idx].state = SCHED_SLOT_FREE;
        return 0xFFFFFFFFu;
    }
    sched_slots[idx].stack_base = stack;

    uintptr_t kstack_top = (uintptr_t)stack + OPENOS_X86_64_SCHED_KSTACK_BYTES;
    uintptr_t frame_base = kstack_top - (5u * sizeof(uint64_t));
    frame_base &= ~((uintptr_t)0xFu);
    uint64_t *frame = (uint64_t *)frame_base;
    frame[0] = user_rip;
    frame[1] = (uint64_t)user_cs;
    frame[2] = user_rflags;
    frame[3] = user_rsp;
    frame[4] = (uint64_t)user_ss;

    x86_64_context_t ctx;
    ctx.rsp    = (x86_64_stack_ptr_t)frame_base;
    ctx.rip    = (x86_64_entry_t)&arch_x86_64_user_thread_trampoline;
    ctx.rflags = 0u;
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
    sched_slots[idx].state            = SCHED_SLOT_PARKED;
    sched_slots[idx].id               = idx;
    sched_slots[idx].priority         = priority;
    sched_slots[idx].owner_cpu        = target_cpu;
    sched_slots[idx].is_idle          = 0u;
    sched_slots[idx].kind             = OPENOS_X86_64_SCHED_KIND_USER;
    sched_slots[idx].kernel_stack_top = kstack_top;
    sched_slots[idx].as               = NULL;
    return idx;
}

/* gamma.3b-S2a Seg-4: fork-resume variant.
 *
 * See spawn_uthread_parked above for the baseline. The only differences:
 *   1. Frame is 11 qwords instead of 5. The extra 6 qwords sit BELOW
 *      the iretq frame and hold callee-saved GPRs at the offsets that
 *      arch_x86_64_user_thread_trampoline_full expects to pop from:
 *          frame[0] = rbx        (popped first)
 *          frame[1] = rbp
 *          frame[2] = r12
 *          frame[3] = r13
 *          frame[4] = r14
 *          frame[5] = r15
 *          frame[6] = rip        (iretq base)
 *          frame[7] = cs
 *          frame[8] = rflags
 *          frame[9] = user_rsp
 *          frame[10]= ss
 *   2. Dispatch RIP goes to arch_x86_64_user_thread_trampoline_full
 *      rather than the vanilla trampoline.
 *
 * Caller-saved GPRs are NOT plumbed through: the SYSCALL ABI already
 * treats them as clobbered on kernel entry, so the child observing
 * rax==0 / rcx=r11=0 / rdx=rsi=rdi=r8..r11=0 is spec-conformant.
 * (rax==0 is the fork()==0 contract; rest fall out of the trampoline's
 * xorq sequence.)
 *
 * Alignment: SysV ABI requires 16-byte alignment at function entry,
 * which for iretq means the user_rsp we deliver must itself be 16-aligned.
 * We do not touch user_rsp here; the caller (fork_alloc_child) inherits
 * parent's syscall-saved rsp, which the parent set up correctly for its
 * own syscall return. The 11-qword kernel frame is 8-byte aligned by
 * construction; we mask frame_base down to 16 for defence in depth just
 * like the 5-qword variant. */
uint32_t arch_x86_64_sched_spawn_uthread_parked_full(uint64_t user_rip,
                                                     uint64_t user_rsp,
                                                     uint64_t user_rflags,
                                                     uint16_t user_cs,
                                                     uint16_t user_ss,
                                                     uint32_t priority,
                                                     uint32_t target_cpu,
                                                     uint64_t init_rbx,
                                                     uint64_t init_rbp,
                                                     uint64_t init_r12,
                                                     uint64_t init_r13,
                                                     uint64_t init_r14,
                                                     uint64_t init_r15) {
    if (user_rip == 0u || user_rsp == 0u) {
        return 0xFFFFFFFFu;
    }
    if (priority > OPENOS_X86_64_SCHED_PRIO_MAX) {
        priority = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    }
    if (target_cpu >= OPENOS_X86_64_SMP_MAX_CPUS) {
        target_cpu = arch_x86_64_this_cpu_ptr()->cpu_idx;
    }
    user_rflags |= 0x200ULL;  /* enforce IF=1 in child on iretq */

    uint32_t idx = sched_alloc_slot();
    if (idx == 0u) {
        return 0xFFFFFFFFu;
    }

    void *stack = arch_x86_64_kmalloc(OPENOS_X86_64_SCHED_KSTACK_BYTES);
    if (stack == NULL) {
        sched_slots[idx].state = SCHED_SLOT_FREE;
        return 0xFFFFFFFFu;
    }
    sched_slots[idx].stack_base = stack;

    uintptr_t kstack_top = (uintptr_t)stack + OPENOS_X86_64_SCHED_KSTACK_BYTES;
    uintptr_t frame_base = kstack_top - (11u * sizeof(uint64_t));
    frame_base &= ~((uintptr_t)0xFu);
    uint64_t *frame = (uint64_t *)frame_base;
    frame[0]  = init_rbx;
    frame[1]  = init_rbp;
    frame[2]  = init_r12;
    frame[3]  = init_r13;
    frame[4]  = init_r14;
    frame[5]  = init_r15;
    frame[6]  = user_rip;
    frame[7]  = (uint64_t)user_cs;
    frame[8]  = user_rflags;
    frame[9]  = user_rsp;
    frame[10] = (uint64_t)user_ss;

    x86_64_context_t ctx;
    ctx.rsp    = (x86_64_stack_ptr_t)frame_base;
    ctx.rip    = (x86_64_entry_t)&arch_x86_64_user_thread_trampoline_full;
    ctx.rflags = 0u;
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
    sched_slots[idx].state            = SCHED_SLOT_PARKED;
    sched_slots[idx].id               = idx;
    sched_slots[idx].priority         = priority;
    sched_slots[idx].owner_cpu        = target_cpu;
    sched_slots[idx].is_idle          = 0u;
    sched_slots[idx].kind             = OPENOS_X86_64_SCHED_KIND_USER;
    sched_slots[idx].kernel_stack_top = kstack_top;
    sched_slots[idx].as               = NULL;  /* caller does slot_set_as before wakeup */
    sched_slots[idx].owner_proc       = NULL;  /* caller does slot_set_owner_proc before wakeup */
    return idx;
}

/* gamma.3b-alpha: release a PARKED or EXITED slot allocated via
 * spawn_uthread_parked. Frees the kernel stack, clears AS binding
 * (does NOT free the AS -- ownership stays with proc PCB), and
 * flips the slot to SCHED_SLOT_FREE. Returns 0 on success, non-zero
 * on refusal (bad idx / RUNNING / READY -- caller must drain first).
 *
 * Alpha never enters READY, so the RUNNING/READY guard is defense
 * in depth for beta callers that mis-order state transitions. */
uint32_t arch_x86_64_sched_slot_release(uint32_t slot_idx) {
    if (slot_idx == 0u || slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) {
        return 1u;
    }
    sched_slot_t *s = &sched_slots[slot_idx];
    if (s->state == SCHED_SLOT_FREE) {
        return 2u; /* double free */
    }
    if (s->state == SCHED_SLOT_RUNNING || s->state == SCHED_SLOT_READY) {
        return 3u; /* still dispatchable, refuse */
    }
    /* PARKED or EXITED -- safe to reclaim. */
    if (s->stack_base != NULL) {
        arch_x86_64_kfree(s->stack_base);
        s->stack_base = NULL;
    }
    s->kernel_stack_top = 0u;
    s->as               = NULL;
    s->owner_proc       = NULL;  /* gamma.3b-S2a Seg-2: drop owner PCB back-ptr on reap */
    s->kind             = OPENOS_X86_64_SCHED_KIND_KERNEL;
    s->owner_cpu        = 0u;
    s->priority         = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    s->is_idle          = 0u;
    s->state            = SCHED_SLOT_FREE;
    return 0u;
}

/* ------------------------------------------------------------------
 * gamma.3b-S1: slot_wakeup / slot_state
 *
 * These two functions form the minimum-viable dispatcher entry-point
 * for parked slots.
 *
 * slot_wakeup(idx):
 *   - Validates idx and current state (must be PARKED).
 *   - Flips state to READY under the same guards the migrate path
 *     uses (is_idle / RUNNING / EXITED refused).
 *   - Sends a resched IPI to owner_cpu if it is an AP; BSP will
 *     naturally re-enter check_and_dispatch on its next PIT tick, so
 *     no IPI is needed there. (BSP self-kick would also just re-arm
 *     what a PIT tick already does.)
 *
 *   Ordering with set_as: the caller MUST have called set_as() before
 *   wakeup(), because as soon as READY is visible the target CPU can
 *   race in and pick this slot up on its next tick. We do not enforce
 *   that here (set_as is idempotent and a NULL AS is caught at
 *   dispatch time by as_activate), but the alpha lifecycle in
 *   fork_alloc_child already sequences it correctly.
 *
 * slot_state(idx):
 *   - Pure read of s->state, no locking. Callers that need snapshot
 *     consistency across multiple observations should sample twice.
 *   - Out-of-range idx returns SCHED_SLOT_FREE (safe default: caller
 *     treats "nothing to see" == "free").
 */
uint32_t arch_x86_64_sched_slot_wakeup(uint32_t slot_idx) {
    if (slot_idx == 0u || slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) {
        return 1u;
    }
    sched_slot_t *s = &sched_slots[slot_idx];
    if (s->state == SCHED_SLOT_FREE) {
        return 2u;
    }
    if (s->state != SCHED_SLOT_PARKED) {
        return 3u; /* already READY / RUNNING / EXITED */
    }

    /* Flip PARKED -> READY. Once this store is visible on the target
     * CPU, pick_next / has_other_ready will start considering this
     * slot. On x86_64, aligned u32 stores are observed in program
     * order by other CPUs, so no explicit barrier is needed before
     * the IPI. */
    s->state = SCHED_SLOT_READY;

    /* Kick the owner CPU. BSP polls sched itself on every PIT tick so
     * we skip the IPI. APs might be idle-halted or running an
     * arbitrarily long user thread; the IPI forces re-entry into
     * check_and_dispatch which will see the new READY slot. */
    if (s->owner_cpu != 0u) {
        (void)arch_x86_64_smp_send_resched_ipi(s->owner_cpu);
    }
    return 0u;
}

uint32_t arch_x86_64_sched_slot_state(uint32_t slot_idx) {
    if (slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) {
        return SCHED_SLOT_FREE;
    }
    return (uint32_t)sched_slots[slot_idx].state;
}

uint32_t arch_x86_64_sched_spawn_uthread_sentinel(uint32_t priority,
                                                 uint32_t target_cpu) {
    /* G.7e-3: spawn a USER slot whose dispatch lands on the sentinel
     * trampoline. The trampoline bumps the per-CPU user_dispatch_count
     * via the same offset the real trampoline uses, then halts the CPU.
     * Used by Stage 21 to prove that:
     *   (a) sched_alloc_slot + scheduler picks up a USER slot pinned to
     *       target_cpu,
     *   (b) sched_apply_rsp0_for_next correctly writes TSS.RSP0 =
     *       slot.kernel_stack_top (visible indirectly via the trampoline
     *       not faulting),
     *   (c) context_switch64.S restore path successfully jumps to the
     *       trampoline with ctx.rflags=0 (IF off across the gs touch).
     *
     * No real ring3 transition happens, so no user page-tables are
     * required and sys_exit semantics do not matter. The owner CPU is
     * retired (cli;hlt loop); pick a spare AP. */
    if (priority > OPENOS_X86_64_SCHED_PRIO_MAX) {
        priority = OPENOS_X86_64_SCHED_PRIO_DEFAULT;
    }
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

    uintptr_t kstack_top = (uintptr_t)stack + OPENOS_X86_64_SCHED_KSTACK_BYTES;

    /* The sentinel trampoline never returns and never executes iretq, so
     * we do not need to pre-build a 5-qword frame. We still must give
     * ctx.rsp a 16-byte-aligned slot inside the allocated stack so that
     * context_switch's `pushq %rax + popfq` scratch write (rsp-8) lands
     * inside our own buffer. */
    uintptr_t rsp = (kstack_top - 16u) & ~((uintptr_t)0xFu);

    x86_64_context_t ctx;
    ctx.rsp    = (x86_64_stack_ptr_t)rsp;
    ctx.rip    = (x86_64_entry_t)&arch_x86_64_user_thread_sentinel_trampoline;
    ctx.rflags = 0u; /* IF=0; trampoline does cli;hlt loop, no iretq */
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

    /* gamma.5-P1: every ~64 ticks (aggregated across all CPUs) dump
     * the preempt-hit histogram of every CPU so we can observe whether
     * the timer IRQ ever caught ring3 code. The classification itself
     * is done inside arch_x86_64_lapic_timer_irq_handler / the PIT stub,
     * using the iret-frame CS.RPL. Any CPU may drive the dump — this is
     * important because during hello_fork the BSP blocks in wait() and
     * its PIT ticks slow way down, so we'd otherwise miss the very
     * window we're trying to observe. */
    {
        static volatile uint32_t p1_dump_gate = 0u;
        uint32_t g = __atomic_add_fetch(&p1_dump_gate, 1u, __ATOMIC_RELAXED);
        if ((g & 0x0Fu) == 0u) {
            early_console64_write("[gamma5-P1] tick_hits per-CPU:");
            for (uint32_t c = 0u; c < OPENOS_X86_64_PERCPU_MAX_CPUS; ++c) {
                uint32_t u = arch_x86_64_percpu_tick_hits_user(c);
                uint32_t k = arch_x86_64_percpu_tick_hits_kernel(c);
                if ((u | k) == 0u) continue;
                early_console64_write(" cpu");
                early_console64_write_hex64((uint64_t)c);
                early_console64_write("=u:");
                early_console64_write_hex64((uint64_t)u);
                early_console64_write(",k:");
                early_console64_write_hex64((uint64_t)k);
            }
            early_console64_write("\n");
        }
    }
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
    sched_slots[cpu].stack_base = NULL;  /* AP idle stack is static (g_ap_idle_stacks), not heap */
    /* G.7d: AP idle slots are KERNEL; they ride the per-CPU RSP0 that
     * was baked into the AP's TSS by percpu_setup(cpu) on AP bringup,
     * so kernel_stack_top stays 0 ("don't touch TSS.RSP0"). */
    sched_slots[cpu].kind             = OPENOS_X86_64_SCHED_KIND_KERNEL;
    sched_slots[cpu].kernel_stack_top = 0u;
    sched_slots[cpu].owner_proc       = NULL;  /* γ.3b-S2a Seg-2: AP idle has no user PCB */

    /* γ.5-F1: pre-populate the idle context so that any restore
     * (via sched_exit_self fallback, or the very first enter via
     * sched_enter_ap_idle) lands on a coherent RSP/RIP with IF=1.
     * Uses this AP's dedicated static idle stack. */
    uintptr_t idle_stack_top =
        (uintptr_t)&g_ap_idle_stacks[cpu][OPENOS_X86_64_AP_IDLE_STACK_BYTES];
    idle_stack_top &= ~(uintptr_t)0xFu;  /* 16-byte align */
    x86_64_context_t *ictx = &sched_slots[cpu].ctx;
    ictx->rsp    = (x86_64_stack_ptr_t)idle_stack_top;
    ictx->rip    = (x86_64_entry_t)(uintptr_t)arch_x86_64_ap_idle_entry;
    ictx->rflags = OPENOS_X86_64_CONTEXT_RFLAGS_IF;
    ictx->rbx = 0; ictx->rbp = 0;
    ictx->r12 = 0; ictx->r13 = 0;
    ictx->r14 = 0; ictx->r15 = 0;
    ictx->r8  = 0; ictx->r9  = 0;
    ictx->r10 = 0; ictx->r11 = 0;

    sched_pc_set_current(cpu);
    return cpu;
}

/* γ.5-F1: enter the AP idle loop via context_switch. Never returns.
 *
 * Called at the end of ap_entry (replacing raw `sti; for(;;) hlt;`).
 * Rationale documented at g_ap_idle_stacks[] above. Passing NULL as
 * `from` tells context_switch64.S to skip the save half and land
 * directly into the pre-populated idle context (dedicated stack,
 * arch_x86_64_ap_idle_entry as rip, IF=1 in rflags). */
void arch_x86_64_sched_enter_ap_idle(void) {
    uint32_t cpu = arch_x86_64_this_cpu_ptr()->cpu_idx;
    /* Defensive: if register_ap_idle wasn't called or slot got clobbered,
     * fall back to the historical inline idle loop rather than jumping to
     * a zero ctx. This branch should never trigger in practice. */
    if (cpu == 0u || cpu >= OPENOS_X86_64_SMP_MAX_CPUS_HINT ||
        sched_slots[cpu].is_idle != 1u ||
        sched_slots[cpu].ctx.rip == 0) {
        __asm__ volatile ("sti");
        for (;;) { __asm__ volatile ("hlt"); }
    }
    arch_x86_64_context_switch(NULL, &sched_slots[cpu].ctx);
    /* Unreachable. */
    __builtin_unreachable();
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

/* γ.3: bind an address space to a slot. Activated on every dispatch. */
uint32_t arch_x86_64_sched_slot_set_as(uint32_t slot_idx,
                                       struct x86_64_address_space *as) {
    if (slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) return 0xFFFFFFFFu;
    sched_slot_t *s = &sched_slots[slot_idx];
    if (s->state == SCHED_SLOT_FREE) return 0xFFFFFFFFu;
    s->as = as;
    return 0u;
}

struct x86_64_address_space *arch_x86_64_sched_slot_get_as(uint32_t slot_idx) {
    if (slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) return NULL;
    return sched_slots[slot_idx].as;
}

/* γ.3b-S2a岔路5(方案B): owner PCB back-pointer setter/getter.
 *
 * Set by fork_alloc_child before slot_wakeup so a USER slot dispatched on
 * any CPU can be reverse-mapped to its PCB via sched_current_slot(). This
 * is the sole source of truth for "which proc is running here" -- there
 * is no separate percpu.current_proc_slot mirror anymore. Clear (NULL)
 * for kernel / idle / bootstrap slots. Fully collected on slot reap.
 *
 * Returns 0 on success, 0xFFFFFFFF on OOB / FREE slot. Getter returns
 * NULL on OOB or when the slot has no owning PCB. */
uint32_t arch_x86_64_sched_slot_set_owner_proc(uint32_t slot_idx,
                                                struct x86_64_proc *owner) {
    if (slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) return 0xFFFFFFFFu;
    sched_slot_t *s = &sched_slots[slot_idx];
    if (s->state == SCHED_SLOT_FREE) return 0xFFFFFFFFu;
    s->owner_proc = owner;
    return 0u;
}

struct x86_64_proc *arch_x86_64_sched_slot_get_owner_proc(uint32_t slot_idx) {
    if (slot_idx >= OPENOS_X86_64_SCHED_MAX_KTHREADS) return NULL;
    return sched_slots[slot_idx].owner_proc;
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

