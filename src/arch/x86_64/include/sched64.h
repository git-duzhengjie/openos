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
#define OPENOS_X86_64_SCHED_MAX_KTHREADS 32u
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

/* G.6.5c: spawn a kthread pinned to a specific CPU (owner_cpu = target_cpu).
 * Used to distribute work across CPUs without changing the default
 * spawn_kthread / spawn_kthread_prio behavior (which still pins to the
 * spawning CPU).
 *
 * target_cpu must be a valid online CPU index; out-of-range values are
 * clamped to the spawning CPU. priority semantics identical to _prio.
 *
 * Returns the slot id on success, 0 on failure (entry==NULL, no free slot,
 * or kmalloc failure for the stack). */
uint32_t arch_x86_64_sched_spawn_kthread_prio_on(x86_64_thread_entry_t entry,
                                                 void *arg,
                                                 uint32_t priority,
                                                 uint32_t target_cpu);

/*
 * G.7e: spawn a ring-3 user thread on a specific CPU.
 *
 *   user_entry  : ring-3 RIP (must be mapped user-executable; for the
 *                 self-test we point this at the embedded usermode blob).
 *   user_rsp    : initial user-mode RSP (must be 16-aligned, mapped
 *                 user-writable). Pass 0 to default to the embedded blob's
 *                 reserved user stack slot.
 *   priority    : same semantics as kthread variants.
 *   target_cpu  : 0..ncpu-1 (ncpu => use any).
 *
 * The slot's kernel stack is allocated internally (OPENOS_X86_64_SCHED_KSTACK_BYTES).
 * sched_slot.kind is set to SCHED_KIND_USER and kernel_stack_top is recorded
 * so sched_apply_rsp0_for_next can program TSS.RSP0 on dispatch. The first
 * dispatch lands at arch_x86_64_user_thread_trampoline, which swapgs + iretq
 * into ring 3.
 *
 * Returns slot id (>=1) on success, 0 on failure. */
uint32_t arch_x86_64_sched_spawn_uthread(uintptr_t user_entry,
                                         uintptr_t user_rsp,
                                         uint32_t priority,
                                         uint32_t target_cpu);

/* G.7e-3: diagnostic spawn variant. Allocates a USER slot exactly like
 * the real spawn, but ctx.rip is set to a sentinel trampoline that bumps
 * the per-CPU user_dispatch_count and then cli;hlt's forever. No ring3
 * transition happens, so user page-tables and user code are NOT required;
 * the test verifies only the spawn -> schedule -> context_switch ->
 * trampoline pipeline. Picks the indicated AP as a sacrifice CPU.
 *
 * Returns slot id (>=1) on success, 0 on failure. */
uint32_t arch_x86_64_sched_spawn_uthread_sentinel(uint32_t priority,
                                                  uint32_t target_cpu);

uint32_t arch_x86_64_sched_set_priority(uint32_t slot, uint32_t priority);
uint32_t arch_x86_64_sched_get_priority(uint32_t slot);
uint32_t arch_x86_64_sched_quantum_for_priority(uint32_t priority);

/* G.6.5b: per-CPU sched_on_tick entry counter. Reads the value from
 * the CPU identified by cpu_idx, NOT necessarily the caller's CPU.
 * Implemented by walking the percpu array directly (no %gs needed).
 * Returns 0 for unknown cpu_idx. Used by smp_selftest stage 10 to
 * prove sched_on_tick fires on each CPU's own IRQ path. */
uint64_t arch_x86_64_sched_tick_calls_for_cpu(uint32_t cpu_idx);

/* G.6.5c: per-CPU switch counter. Reads sched_switch_count from the
 * percpu slot identified by cpu_idx, NOT %gs. Used by smp_selftest
 * stage 11 to prove that distributed kthreads cause real context
 * switches on each AP. Returns 0 for unknown cpu_idx. */
uint64_t arch_x86_64_sched_switch_count_for_cpu(uint32_t cpu_idx);

/* G.6.6b: thread migration primitive.
 * Atomically reassigns a slot's owner_cpu to target_cpu and pings the
 * target via a reschedule IPI so it picks the new work on its next
 * tick. Returns 0 on success; non-zero on error (bad slot, bad target,
 * idle slot, slot currently RUNNING on another CPU, etc.).
 *
 * Safety contract: only slots in SCHED_SLOT_READY may be migrated.
 * Slots in SCHED_SLOT_RUNNING are owned by another CPU's register file
 * and would race the context save -- callers must yield first if they
 * want to migrate self, which is currently unsupported. */
uint32_t arch_x86_64_sched_migrate(uint32_t slot_idx, uint32_t target_cpu);

/* G.6.6b: read a slot's current owner_cpu without going through %gs.
 * Returns 0xFFFFFFFFu if slot_idx is out of bounds or the slot is FREE.
 * Used by smp_selftest stage 13 to prove a migration took effect. */
uint32_t arch_x86_64_sched_slot_owner(uint32_t slot_idx);

/* ---- G.7d: thread kind tagging --------------------------------------
 *
 * Every sched slot is tagged with a `kind`:
 *   KERNEL = ring0-only thread (kmain bootstrap, kthreads, AP idle).
 *            Uses the per-CPU shared RSP0 baked into TSS at percpu_setup.
 *   USER   = thread that will eventually iretq to ring3 (G.7e+).
 *            Owns a per-thread kernel stack; TSS.RSP0 must be repointed
 *            to this slot's kernel_stack_top whenever it gets dispatched
 *            (so that mid-userland IRQ/syscall lands on the right stack).
 *
 * In G.7d this is pure scaffolding: every slot is KERNEL, no RSP0
 * switching happens at context_switch time, no runtime behaviour changes.
 * G.7e flips on the RSP0-apply hook and introduces the first USER slot. */
#define OPENOS_X86_64_SCHED_KIND_KERNEL 0u
#define OPENOS_X86_64_SCHED_KIND_USER   1u

/* Observer: returns the slot's kind, or 0xFFFFFFFFu if slot_idx is
 * out of bounds or the slot is FREE. Read directly from the static
 * slot array (no %gs deref); safe from any CPU. Used by Stage 20. */
uint32_t arch_x86_64_sched_slot_kind(uint32_t slot_idx);

/* Observer: returns the slot's kernel_stack_top (the value that would
 * be loaded into TSS.RSP0 if this slot were dispatched as USER).
 * Returns 0 for slot 0 (bootstrap; uses per-CPU shared RSP0) and for
 * FREE/out-of-bounds slots. Used by Stage 20 to prove the stack-top
 * equals stack_base + KSTACK_BYTES for every spawned kthread. */
uintptr_t arch_x86_64_sched_slot_kstack_top(uint32_t slot_idx);

/* γ.3: bind an address space to a slot. The AS will be activated
 * (CR3 loaded) every time this slot gets dispatched via sched_yield.
 * Pass NULL to detach (keeps whatever CR3 is loaded on dispatch).
 * Returns 0 on success, 0xFFFFFFFFu on invalid slot. */
struct x86_64_address_space;
uint32_t arch_x86_64_sched_slot_set_as(uint32_t slot_idx,
                                       struct x86_64_address_space *as);
struct x86_64_address_space *arch_x86_64_sched_slot_get_as(uint32_t slot_idx);

/* γ.3b-S2a岔路5(方案B): bind a PCB back-pointer to a slot so that
 * proc_current() can reverse-map the running CPU's sched_current_slot()
 * to its owning proc without a separate percpu.current_proc_slot mirror.
 * Set NULL to detach (kernel / idle / bootstrap slots).
 *
 * Contract:
 *   - Must be called by fork_alloc_child *after* sched_slot_alloc and
 *     *before* sched_slot_wakeup, otherwise the target CPU may dispatch
 *     the slot and see a stale/NULL owner on its first syscall.
 *   - Cleared to NULL by do_exit's sched-USER branch before the slot is
 *     reaped, so a subsequent alloc of the same idx starts clean.
 *
 * Returns 0 on success, 0xFFFFFFFFu on OOB / FREE slot. */
struct x86_64_proc;
uint32_t arch_x86_64_sched_slot_set_owner_proc(uint32_t slot_idx,
                                                struct x86_64_proc *owner);
struct x86_64_proc *arch_x86_64_sched_slot_get_owner_proc(uint32_t slot_idx);

/* ---- G.6.7a: preemption tail-hook primitives -----------------------
 *
 * Background: historically the only place we ever made a scheduling
 * decision was inside the timer-tick ISR (sched_on_tick -> sched_yield
 * when the local quantum hits 0). That works for time-slicing but it
 * means any cross-CPU wakeup has to wait up to one full tick (~166ms
 * on AP LAPIC timer @ ~6Hz) before the target CPU actually picks up
 * the new runnable thread, even though the resched-IPI itself reaches
 * the target in microseconds.
 *
 * G.6.7a fixes that by introducing a per-CPU "please reschedule"
 * latch, plus a generic ISR-tail dispatcher:
 *
 *   - sched_set_need_resched()      sets the latch on caller's CPU.
 *   - sched_set_need_resched_remote() sets it on a remote CPU's slot
 *                                     (used by future cross-CPU
 *                                     wakeup paths; safe today because
 *                                     u32 stores on x86_64 are atomic
 *                                     w.r.t. aligned naturally-sized
 *                                     loads).
 *   - sched_check_and_dispatch()    reads-and-clears the latch and, if
 *                                     it was set, performs a sched_yield
 *                                     on the caller's CPU. Returns the
 *                                     number of yields actually issued
 *                                     (0 or 1). MUST be called from
 *                                     ISR-tail context, after EOI has
 *                                     been written, with IF=0 still in
 *                                     effect; sched_yield itself goes
 *                                     through context_switch and the
 *                                     restored thread's rflags will
 *                                     re-enable IF.
 *
 * Counters:
 *   - resched_dispatch_count goes up once per successful tail-fire on
 *     each CPU. Selftest stage 14 uses it to *prove* a remote IPI
 *     triggered an immediate context switch on the target CPU rather
 *     than waiting for the next timer tick.
 *
 * BSP=0 contract: BSP must never see need_resched=1 in G.6.7a (BSP is
 * not a valid resched-IPI target). A non-zero BSP value is an error. */
void arch_x86_64_sched_set_need_resched(void);
void arch_x86_64_sched_set_need_resched_remote(uint32_t cpu_idx);
uint32_t arch_x86_64_sched_check_and_dispatch(void);

/* G.6.7c: read-and-clear the need_resched latch on a CPU WITHOUT
 * going through the dispatch gate (preempt_disable_depth check).
 * Returns the old latch value. Intended for selftest cleanup paths
 * that need to discard a coincidental latch (e.g. a PIT tick caught
 * inside a measurement window) without triggering the deferred-
 * dispatch behaviour that preempt_enable() would otherwise produce
 * on the 1->0 edge. */
uint32_t arch_x86_64_sched_drain_need_resched(uint32_t cpu_idx);

/* Observer for selftests. Returns the per-CPU resched_dispatch_count
 * read directly from the percpu slot (not via %gs). Returns 0 for
 * unknown cpu_idx. */
uint64_t arch_x86_64_sched_dispatch_count_for_cpu(uint32_t cpu_idx);
uint32_t arch_x86_64_sched_need_resched_for_cpu(uint32_t cpu_idx);

/* ------------------------------------------------------------------
 * G.6.7b: preempt-disable / preempt-enable critical-section gating.
 *
 * Semantics (Linux-style preempt_count, single-CPU local nesting):
 *
 *   preempt_disable():
 *     this_cpu->preempt_disable_depth++
 *     -- caller is now in a non-preemptible region. ISR-tail
 *        dispatch (check_and_dispatch) will see depth>0 and refuse
 *        to switch. need_resched can still be latched (by IPI, by
 *        timer-tick-elect, by manual set), but it stays latched.
 *
 *   preempt_enable():
 *     d = --this_cpu->preempt_disable_depth
 *     if d == 0 and need_resched == 1:
 *         preempt_deferred_count++
 *         check_and_dispatch()   <-- deferred wakeup fires HERE
 *
 * Invariants:
 *   - disable/enable are strictly paired per CPU (mismatched calls are
 *     a bug; the depth would drift).
 *   - depth is per-CPU, NOT per-thread. It is part of CPU state, like
 *     interrupt-disable. A thread that yields with depth>0 carries
 *     no debt to the next thread; that's why callers must always
 *     enable before yielding/blocking.
 *   - Re-entry from interrupts is safe: ISR handlers run on the
 *     same CPU; if they call disable/enable in a balanced pair, the
 *     outer count is preserved.
 *   - At depth==0, behavior is identical to G.6.7a -- the gate is
 *     transparent. This means no regression for stages 12/13/14.
 *
 * BSP=0 contract carry-over: BSP never receives reschedule IPIs in
 * the current design, so BSP's preempt_deferred_count must stay 0
 * under normal operation. Stage 15 manually drives the BSP gate from
 * the BSP itself to exercise the latch path. */
void arch_x86_64_preempt_disable(void);
void arch_x86_64_preempt_enable(void);

/* Observers for selftests (read percpu slot directly, not via %gs). */
uint32_t arch_x86_64_preempt_depth_for_cpu(uint32_t cpu_idx);
uint64_t arch_x86_64_preempt_deferred_count_for_cpu(uint32_t cpu_idx);

/* ------------------------------------------------------------------
 * gamma.3b-alpha: PARKED USER slot preparation for fork(2).
 *
 * A slot that has been fully built (kernel stack allocated, iretq
 * frame written, context_switch trampoline ready, AS bound via
 * sched_slot_set_as) but is NOT yet on the ready-list is in state
 * SCHED_SLOT_PARKED. pick_next / has_other_ready must skip it, so
 * from the dispatcher's point of view a PARKED slot is invisible.
 *
 * This is pure scaffolding for gamma.3b-beta: fork_alloc_child will
 * allocate a PARKED slot to hold the child's future USER dispatch
 * context, and beta will flip it to READY at fork(2)-return time so
 * the child actually enters the preemption path.
 *
 * gamma.3b-alpha itself does NOT wire fork_alloc_child to use these
 * APIs -- the point of alpha is *structure only*, zero behaviour
 * change on the hello_fork happy path. The child is still resumed
 * via the legacy fork_pending / saved_fork_frame handoff on the BSP
 * main loop; the PARKED slot is allocated, sanity-checked at
 * reap time, and freed. If beta is deferred, alpha still passes
 * every stage 30 selftest.
 *
 * spawn_uthread_parked: identical layout to arch_x86_64_sched_spawn_uthread
 *   but the resulting slot's state is SCHED_SLOT_PARKED, not
 *   SCHED_SLOT_READY. Caller is expected to bind an address space
 *   via arch_x86_64_sched_slot_set_as before making it READY.
 *   Returns the slot index, or 0xFFFFFFFFu on allocation failure.
 *
 * slot_release: tear down a PARKED (or EXITED) slot: frees the
 *   kernel stack, clears the AS binding (but does NOT tear down the
 *   AS itself -- ownership stays with the caller / proc PCB), and
 *   returns the slot to SCHED_SLOT_FREE. Rejects (returns non-zero)
 *   if the slot is RUNNING or READY -- callers must yield/drain first.
 */
uint32_t arch_x86_64_sched_spawn_uthread_parked(uint64_t user_rip,
                                                uint64_t user_rsp,
                                                uint64_t user_rflags,
                                                uint16_t user_cs,
                                                uint16_t user_ss,
                                                uint32_t priority,
                                                uint32_t target_cpu);
uint32_t arch_x86_64_sched_slot_release(uint32_t slot_idx);

/* ------------------------------------------------------------------
 * gamma.3b-S2a Seg-4: spawn_uthread_parked_full
 *
 * Fork-resume variant of spawn_uthread_parked. Same allocation and
 * bookkeeping, but the child's first dispatch materializes an 11-qword
 * frame carrying the parent's six callee-saved GPRs (rbx/rbp/r12-r15)
 * on top of the usual iretq frame. Dispatch entry is
 * arch_x86_64_user_thread_trampoline_full, which pops the six
 * callee-saved slots into their registers, zeros all caller-saved
 * (so rax==0 == fork()==0), then iretq's into user mode.
 *
 * The five caller-saved GPRs the parent had at the SYSCALL boundary
 * (rcx, rdx, rsi, rdi, r8/r9/r10/r11) are intentionally discarded --
 * the SYSCALL ABI already treats them as clobbered, so the child
 * observing them zero is spec-conformant and matches Linux behavior.
 * r11 in particular is rflags on SYSCALL entry; the fork_alloc_child
 * caller has already stashed it into user_rflags for us.
 *
 * All alpha lifecycle invariants of spawn_uthread_parked hold:
 *   - Returns SCHED_SLOT_PARKED (must set_as + wakeup to run).
 *   - Returns 0xFFFFFFFFu on slot / kstack allocation failure.
 *   - The 11-qword frame lives at the top of the kernel stack;
 *     context_switch loads ctx.rsp = frame_base = &rbx_slot. */
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
                                                     uint64_t init_r15);

/* ------------------------------------------------------------------
 * gamma.3b-S1: slot state enum exposed to selftests.
 *
 * Kept in sync with the internal typedef in sched64.c. slot_state()
 * returns these values as uint32_t. Do NOT reorder or renumber -- the
 * dispatcher checks state == SCHED_SLOT_READY directly.
 */
typedef enum {
    SCHED_SLOT_FREE    = 0,
    SCHED_SLOT_READY   = 1,
    SCHED_SLOT_RUNNING = 2,
    SCHED_SLOT_EXITED  = 3,
    SCHED_SLOT_PARKED  = 4,
} sched_slot_state_t;

/* ------------------------------------------------------------------
 * gamma.3b-S1: PARKED -> READY lifecycle.
 *
 * slot_wakeup: flips a PARKED slot to READY so pick_next / has_other_ready
 *   can pick it up. Also sends a reschedule IPI to the slot's owner_cpu
 *   so the target actually preempts and dispatches promptly (without the
 *   IPI, dispatch would be deferred until the next natural tick on the
 *   owner CPU, which on TCG is ~1.3s -- unacceptable for fork(2)-return
 *   latency).
 *
 *   Returns 0 on success, non-zero on refusal:
 *     1 = bad slot_idx
 *     2 = slot is FREE (never allocated / already released)
 *     3 = slot is not PARKED (already READY/RUNNING/EXITED)
 *
 *   Precondition: caller must have bound the slot's AS via
 *   arch_x86_64_sched_slot_set_as() *before* wakeup, otherwise the
 *   dispatcher will activate a NULL AS on first dispatch. Alpha's
 *   fork_alloc_child hook already does this.
 *
 * slot_state: read-only observer of the slot's SCHED_SLOT_* state. Used
 *   by selftests to prove PARKED slots are invisible to the dispatcher
 *   until wakeup, and to detect EXITED transitions after ring3 exit.
 *   Returns SCHED_SLOT_FREE for out-of-range idx.
 */
uint32_t arch_x86_64_sched_slot_wakeup(uint32_t slot_idx);
uint32_t arch_x86_64_sched_slot_state(uint32_t slot_idx);

#endif /* OPENOS_ARCH_X86_64_SCHED64_H */
