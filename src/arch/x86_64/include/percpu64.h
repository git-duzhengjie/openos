#ifndef OPENOS_ARCH_X86_64_PERCPU64_H
#define OPENOS_ARCH_X86_64_PERCPU64_H

#include <stdint.h>
#include <stdbool.h>

/* G.6.2: per-CPU "current" structure, addressable via %gs:0..
 * - self      : copy of the pointer to this structure itself (for sanity
 *               checks; lets code read it via %gs:0 and verify it == &g_percpu[i])
 * - cpu_idx   : logical CPU index (BSP=0, AP=1..N-1)
 * - magic     : 'PCPU' (0x55504350) so a stray uninit GS_BASE is detected
 * - sched_current_idx / sched_quantum_left / sched_switch_count /
 *   sched_preempt_count : per-CPU scheduler cursors and counters (G.6.3).
 *   The sched_slots[] pool itself remains a single shared array.
 */
#define OPENOS_X86_64_PERCPU_MAGIC 0x55504350u  /* 'PCPU' little-endian */

typedef struct openos_x86_64_percpu {
    uint64_t self;                /* offset 0x00: pointer to self */
    uint32_t cpu_idx;             /* offset 0x08 */
    uint32_t magic;               /* offset 0x0C */
    /* G.6.3: per-CPU scheduler state. Previously module-static globals
     * inside sched64.c; pulled into the percpu struct so that each CPU
     * has its own current-thread index and quantum bookkeeping. The
     * sched_slots[] pool itself remains a single shared array for now
     * (G.6.3 only splits the *cursors*; slot ownership stays global). */
    uint32_t sched_current_idx;   /* offset 0x10 */
    uint32_t sched_quantum_left;  /* offset 0x14 */
    uint64_t sched_switch_count;  /* offset 0x18 */
    uint64_t sched_preempt_count; /* offset 0x20 */
    /* G.6.5a: per-CPU LAPIC-timer tick counter. Incremented by the AP
     * LAPIC-timer ISR; observed by smp_selftest to confirm the AP's
     * timer is actually firing. The BSP's slot stays at 0 because the
     * BSP still ticks off PIT (its sched_switch_count is the visible
     * proof of life there). */
    uint64_t lapic_timer_count;   /* offset 0x28 */
    /* G.6.5b: per-CPU counter incremented at the very entry of
     * arch_x86_64_sched_on_tick(). This is *independent* of whether a
     * context switch actually happened: it counts every IRQ-driven
     * scheduler entry on this CPU. On the BSP it bumps via PIT IRQ0;
     * on APs it bumps via the LAPIC-timer ISR (vector 0x40). This is
     * the direct proof that sched_on_tick is reachable from each CPU's
     * own interrupt path. */
    uint64_t sched_tick_calls;    /* offset 0x30 */
    /* G.6.6a: per-CPU reschedule-IPI delivery counter. Bumped by the
     * fixed-delivery LAPIC IPI handler at vector 0x41. Unlike the
     * timer counter which is strictly AP-only, BSP may also receive
     * reschedule IPIs (self-IPI or from APs), so BSP slot is allowed
     * to be non-zero here. This is the direct proof that BSP->AP IPI
     * delivery actually lands on the target core. */
    uint64_t resched_ipi_count;   /* offset 0x38 */
    /* G.6.7a: cross-CPU "please reschedule" signal flag.
     *
     * Set by either:
     *   - the local reschedule-IPI handler (after it bumps
     *     resched_ipi_count) so that the same handler can immediately
     *     dispatch a context switch on this CPU, OR
     *   - a remote CPU via sched_set_need_resched_remote() (future use
     *     for cross-CPU wakeups that don't have to wait for the next
     *     timer tick).
     *
     * Cleared by arch_x86_64_sched_check_and_dispatch() at ISR-tail.
     * Naturally aligned u32 -> single-instruction atomic load/store
     * on x86_64, no torn-read risk even across CPUs.
     *
     * BSP=0 contract: BSP currently never receives reschedule IPIs in
     * G.6.7a (smp_send_resched_ipi rejects BSP target). BSP slot of
     * need_resched must stay 0 throughout normal operation; a non-zero
     * BSP value would indicate stray IPI / mis-routed wakeup. */
    uint32_t need_resched;        /* offset 0x40 */
    uint32_t _resv_after_need_resched; /* offset 0x44 (alignment hole) */
    /* G.6.7a: counts the number of times check_and_dispatch() observed
     * need_resched=1 on entry and acted on it (i.e. cleared the flag,
     * invoked sched_yield). Independent of resched_ipi_count: the IPI
     * counter goes up on every IPI delivery, the dispatch counter goes
     * up on every successful tail-hook fire. They should be equal in
     * steady state when need_resched is only ever set by the local IPI
     * handler. Selftest uses this to prove the IPI -> dispatch path
     * runs *before* the next timer tick. */
    uint64_t resched_dispatch_count; /* offset 0x48 */
    /* G.6.7b: per-CPU preempt-disable nesting depth. Incremented by
     * arch_x86_64_preempt_disable(), decremented by
     * arch_x86_64_preempt_enable(). While depth>0, the ISR-tail
     * dispatch hook (sched_check_and_dispatch) MUST NOT context-switch;
     * it leaves need_resched=1 latched and returns 0. preempt_enable()
     * checks for a pending latch on the 1->0 edge and fires a deferred
     * dispatch right there, so no wakeup is ever lost. u32 width
     * accommodates absurd nesting; a non-zero value at scheduler
     * shutdown is a bug. */
    uint32_t preempt_disable_depth;  /* offset 0x50 */
    uint32_t _resv_after_pdd;        /* offset 0x54 */
    /* G.6.7b: counts the number of times preempt_enable() observed a
     * pending need_resched on its 1->0 edge and consequently fired a
     * deferred dispatch. Selftest stage 15 uses this to prove the
     * latch-during-critical-section -> immediate-dispatch-on-exit
     * property. */
    uint64_t preempt_deferred_count; /* offset 0x58 */
    /* G.7c: per-CPU syscall save-area for the ring3->ring0 stack swap.
     *
     * The native syscall instruction does NOT switch stacks for us: on
     * entry %rsp still points at the user stack, and we must move to a
     * trusted kernel stack BEFORE we push the GPR save frame (otherwise
     * a hostile user could supply an unmapped / kernel-aliasing %rsp
     * and #PF us on the very first push, mid-swapgs window).
     *
     * The protocol used by syscall_sysret64.S is:
     *   - on entry, right after swapgs:
     *       movq %rsp, %gs:syscall_user_rsp
     *       movq %gs:syscall_kernel_rsp, %rsp
     *   - on exit, right before the closing swapgs + sysretq:
     *       movq %gs:syscall_user_rsp, %rsp
     *
     * syscall_kernel_rsp is kept in sync with the TSS RSP0 field by
     * arch_x86_64_percpu_set_rsp0() so that the syscall path and the
     * hardware ring3->ring0 path (used by interrupts) always land on
     * the same physical kernel stack for a given CPU/thread. */
    uint64_t syscall_kernel_rsp;     /* offset 0x60 */
    uint64_t syscall_user_rsp;       /* offset 0x68 */
    /* G.7e: per-CPU baseline RSP0 -- the value of TSS.RSP0 that
     * was installed by percpu_setup() before any user thread ran.
     * The scheduler restores TSS.RSP0 to this snapshot whenever it
     * switches *from* a USER sched slot *back to* a KERNEL slot,
     * so kthreads/interrupts continue to land on the per-CPU
     * baseline kernel stack. Set exactly once, during the very
     * first arch_x86_64_percpu_set_rsp0() call on this CPU. */
    uint64_t baseline_rsp0;          /* offset 0x70 */
    /* G.7e: counter of user-thread dispatches on this CPU.
     * Incremented atomically by the trampoline (sentinel or real)
     * right before it transitions to ring3. Used by smp_selftest
     * Stage 21 to verify that a USER sched slot actually got
     * dispatched on its owning CPU. */
    uint64_t user_dispatch_count;    /* offset 0x78 */
    /* gamma.3b-S2a Seg-2 (岔路5方案B): current_proc_slot has been
     * REMOVED. The Seg-1 field lived here at 0x80/0x84 as the per-CPU
     * "who am I?" answer for proc_current*(). Seg-2 collapses this back
     * into sched: each sched_slot_t carries an owner_proc back-pointer,
     * and proc_current() reads sched_current_slot()->owner_proc. That
     * gives us a single source of truth for the running PCB per-CPU
     * without a percpu mirror to keep in sync.
     *
     * We keep the 8-byte hole here so all higher OFF_* offsets stay
     * fixed (asm and selftest bake several of them in). Do NOT reuse
     * without renaming the OFF_CURRENT_PROC macro below. */
    /* gamma.5-P1: LAPIC-timer preempt probe.
     *
     * 0x80/0x84 was previously current_proc_slot (removed in
     * gamma.3b-S2a Seg-2, see the block comment right above). Reused
     * here to hold two independent counters that tally where the timer
     * IRQ struck on this CPU:
     *   tick_hits_user   -- CS.RPL == 3 (ring3, i.e. user code)
     *   tick_hits_kernel -- CS.RPL == 0 (ring0, i.e. kernel code)
     * Their sum equals sched_tick_calls (which counts every entry to
     * sched_on_tick regardless of who was interrupted).
     *
     * Populated by arch_x86_64_lapic_timer_irq_handler(iret_cs) using
     * the CS field from the iret frame that the isr64.S stub forwards.
     * See src/arch/x86_64/kernel/isr64.S :: x86_64_irq_lapic_timer. */
    uint32_t tick_hits_user;         /* offset 0x80 */
    uint32_t tick_hits_kernel;       /* offset 0x84 */
} __attribute__((aligned(64))) arch_x86_64_percpu_t;

/* Per-field offsets (compile-time, for asm or sanity checks). */
#define OPENOS_X86_64_PERCPU_OFF_SELF            0x00
#define OPENOS_X86_64_PERCPU_OFF_CPU_IDX         0x08
#define OPENOS_X86_64_PERCPU_OFF_MAGIC           0x0C
#define OPENOS_X86_64_PERCPU_OFF_SCHED_CURRENT   0x10
#define OPENOS_X86_64_PERCPU_OFF_SCHED_QUANTUM   0x14
#define OPENOS_X86_64_PERCPU_OFF_SCHED_SWITCHES  0x18
#define OPENOS_X86_64_PERCPU_OFF_SCHED_PREEMPTS  0x20
#define OPENOS_X86_64_PERCPU_OFF_LAPIC_TIMER     0x28
#define OPENOS_X86_64_PERCPU_OFF_SCHED_TICKS     0x30
#define OPENOS_X86_64_PERCPU_OFF_RESCHED_IPI     0x38
#define OPENOS_X86_64_PERCPU_OFF_NEED_RESCHED    0x40
#define OPENOS_X86_64_PERCPU_OFF_RESCHED_DISPATCH 0x48
#define OPENOS_X86_64_PERCPU_OFF_PREEMPT_DEPTH    0x50
#define OPENOS_X86_64_PERCPU_OFF_PREEMPT_DEFERRED 0x58
#define OPENOS_X86_64_PERCPU_OFF_SYSCALL_KRSP     0x60
#define OPENOS_X86_64_PERCPU_OFF_SYSCALL_URSP     0x68
#define OPENOS_X86_64_PERCPU_OFF_BASELINE_RSP0    0x70
#define OPENOS_X86_64_PERCPU_OFF_USER_DISPATCH    0x78
/* 0x80 was OPENOS_X86_64_PERCPU_OFF_CURRENT_PROC. Removed in gamma.3b-S2a
 * Seg-2 (岔路5方案B). See percpu64.h struct comment: sched_slot.owner_proc
 * is now the sole per-CPU running-PCB source. Slot 0x80/0x84 kept as
 * reserved hole to preserve future offsets. */

/* IA32_GS_BASE MSR */
#define OPENOS_X86_64_MSR_GS_BASE        0xC0000101u
#define OPENOS_X86_64_MSR_KERNEL_GS_BASE 0xC0000102u

/* Install GS_BASE for the *current* CPU to &g_percpu[cpu_idx], after
 * filling the struct. Safe to call from BSP (cpu_idx=0) and from each AP. */
void arch_x86_64_percpu_install_gs(uint32_t cpu_idx);

/* Read the current CPU's percpu struct via %gs:0 (the "self" slot). */
static inline arch_x86_64_percpu_t *arch_x86_64_this_cpu_ptr(void) {
    arch_x86_64_percpu_t *p;
    __asm__ volatile ("movq %%gs:0, %0" : "=r"(p));
    return p;
}

static inline uint32_t arch_x86_64_this_cpu_idx(void) {
    uint32_t idx;
    __asm__ volatile ("movl %%gs:8, %0" : "=r"(idx));
    return idx;
}

static inline uint32_t arch_x86_64_this_cpu_magic(void) {
    uint32_t mg;
    __asm__ volatile ("movl %%gs:12, %0" : "=r"(mg));
    return mg;
}

/* Returns true iff %gs:0 points to a properly initialized percpu struct
 * whose self-pointer is self-consistent and magic == 'PCPU'. */
bool arch_x86_64_percpu_gs_ok(void);

/* Direct accessor for the BSP's percpu (used by the selftest). */
arch_x86_64_percpu_t *arch_x86_64_percpu_slot(uint32_t cpu_idx);

/*
 * Step G.5-gdt-tss: per-CPU GDT + TSS infrastructure.
 *
 * Each CPU needs its own TSS because RSP0 (the stack pointer hardware
 * loads on ring3 -> ring0 transitions) is per-CPU state. The TSS
 * descriptor lives inside the GDT, and the TSS *base* field encodes
 * a 64-bit virtual address, so the GDT itself must also be per-CPU.
 *
 * This module owns a fixed-size array of GDT+TSS+stack tuples indexed
 * by cpu_idx (0 = BSP, 1..N = APs). Each AP calls percpu_setup() to
 * fill in its slot, then percpu_load() to commit lgdt + ltr.
 */

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_PERCPU_MAX_CPUS    4u
#define OPENOS_X86_64_PERCPU_RSP0_SIZE   16384u   /* 16 KiB ring0 stack */
#define OPENOS_X86_64_PERCPU_IST_COUNT   2u       /* IST1 = NMI, IST2 = #DF */
#define OPENOS_X86_64_PERCPU_IST_SIZE    8192u    /* 8 KiB each */

/* Build the GDT + TSS for cpu_idx in this module's BSS arrays. */
void arch_x86_64_percpu_setup(uint32_t cpu_idx);

/* Execute lgdt + ltr against cpu_idx's tables on the current CPU.
 * Must be called from the CPU that will use these tables. */
void arch_x86_64_percpu_load(uint32_t cpu_idx);

/* Helpers / accessors. */
uint32_t           arch_x86_64_percpu_max(void);
x86_64_stack_ptr_t arch_x86_64_percpu_rsp0(uint32_t cpu_idx);
x86_64_virt_addr_t arch_x86_64_percpu_tss_base(uint32_t cpu_idx);

/* G.7a: update the RSP0 field of cpu_idx's TSS at runtime. Returns the
 * previous value, or 0 if cpu_idx is out of range. Required by the
 * upcoming syscall / ring3-thread paths so that each thread can install
 * its own kernel stack on entry; the value is reloaded by hardware on
 * every ring3->ring0 transition. Does NOT touch the underlying per-CPU
 * RSP0 backing-stack buffer in this module — callers are responsible
 * for keeping the new pointer alive. */
x86_64_stack_ptr_t arch_x86_64_percpu_set_rsp0(uint32_t cpu_idx,
                                                x86_64_stack_ptr_t new_rsp0);

/* G.7e: read back the baseline RSP0 latched on this CPU during the
 * very first percpu_install_gs() / percpu_setup() pass. The scheduler
 * uses this snapshot to restore TSS.RSP0 when context-switching out
 * of a USER sched slot back to a KERNEL slot. Returns 0 for an
 * out-of-range cpu_idx or before this CPU's percpu install. */
uint64_t arch_x86_64_percpu_baseline_rsp0(uint32_t cpu_idx);
uint64_t arch_x86_64_percpu_user_dispatch_count(uint32_t cpu_idx);

/* G.7f: read cpu_idx's LAPIC-timer count (bumped at the head of the
 * LAPIC timer ISR, in lapic64.c). Used by stage 30 to prove that the
 * timer IRQ actually preempts a ring3 busy loop on the target CPU. */
uint64_t arch_x86_64_percpu_lapic_timer_count(uint32_t cpu_idx);

/* gamma.5-P1: read cpu_idx's timer-hit histogram. tick_hits_user +
 * tick_hits_kernel == sched_tick_calls if the accounting is correct.
 * Non-zero tick_hits_user on any CPU proves the timer IRQ actually
 * preempts ring3 code (i.e. the missing piece before gamma.5-P2
 * cooperative -> preemptive scheduling of two ring3 tasks). */
uint32_t arch_x86_64_percpu_tick_hits_user(uint32_t cpu_idx);
uint32_t arch_x86_64_percpu_tick_hits_kernel(uint32_t cpu_idx);

/* G.7a: read a 1-based IST entry (1..OPENOS_X86_64_PERCPU_IST_COUNT)
 * from cpu_idx's TSS. Returns 0 for out-of-range cpu or ist index. */
x86_64_stack_ptr_t arch_x86_64_percpu_ist(uint32_t cpu_idx, uint32_t ist_index);

/* G.7b: read the current CPU's IA32_GS_BASE / IA32_KERNEL_GS_BASE pair.
 * Either out pointer may be NULL. Used by Stage 18 to verify that both
 * MSRs are populated and reference the same percpu slot (precondition
 * for the no-op swapgs scaffolding installed in this commit). */
void arch_x86_64_percpu_read_gs_pair(uint64_t *out_gs_base,
                                     uint64_t *out_kernel_gs_base);

#endif /* OPENOS_ARCH_X86_64_PERCPU64_H */
