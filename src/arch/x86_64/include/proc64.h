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
#include <stdbool.h>

#include "arch64_types.h"
#include "usermode64.h"  /* for x86_64_user_iretq_frame_t (saved_user_frame) */
#include "syscall64.h"   /* A2.P3-B: x86_64_int80_frame_t / x86_64_syscall_frame_t */
#include "signal64.h"    /* M4.2: per-process signal state (x86_64_sigstate_t) */

/* Forward decl: H.5b.1 wiring. The full definition lives in
 * address_space64.h; PCBs only need a pointer field today (NULL on every
 * existing path; populated by later H.5b.* sub-steps). */
struct x86_64_address_space;

#define OPENOS_X86_64_PROC_MAX 8u
#define OPENOS_X86_64_PROC_NAME_MAX 16u
#define OPENOS_X86_64_PROC_INVALID_INDEX ((uint16_t)0xFFFFu)

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
    /* H.5b.1: per-process address space pointer. Kept NULL on every
     * code path until H.5b.3 enables CR3 switching; carrying the slot
     * now lets the rest of the kernel be plumbed without further PCB
     * layout churn. Owned by proc64 (allocated in spawn_user, freed in
     * proc_exit). */
    struct x86_64_address_space *as;

    /* ------------------------------------------------------------------
     * A2.P1 — PCB-ize the cooperative ring3 longjmp core.
     *
     * usermode64.c used to hold these three pieces of per-thread state
     * as file-scope statics, which served fine as long as exactly one
     * ring3 thread could be alive. Real fork() needs multiple ring3
     * PCBs to coexist, so the state lives here now.
     *
     *   saved_user_frame   — IRETQ frame prepared by
     *                        arch_x86_64_usermode_prepare_iretq().
     *                        Read by the inline-asm trampoline in
     *                        arch_x86_64_usermode_run().
     *
     *   kernel_return_rsp  — kernel %rsp saved just before the IRETQ.
     *                        arch_x86_64_usermode_return_to_kernel()
     *                        does `mov %rsp,<this>; ret` to long-jump
     *                        back into usermode_run().
     *
     *   usermode_canary    — magic word planted at the bottom of the
     *                        kernel-side save area; checked after the
     *                        longjmp to detect stack corruption from
     *                        ring3.
     *
     * Only `current` is touched today; other slots keep these fields
     * zeroed until A2.P3 wires real fork(). */
    x86_64_user_iretq_frame_t saved_user_frame;
    uint64_t                  kernel_return_rsp;
    uint64_t                  usermode_canary;

    /* ------------------------------------------------------------------
     * A2.P3-B — vfork-semantics minimal fork.
     *
     * When ring3 issues SYS_FORK (220), the syscall wrapper short-circuits
     * BEFORE dispatch_common so that we still have the architectural
     * trapframe. The wrapper:
     *   1. snapshots the trapframe into one of saved_fork_frame_int80 /
     *      saved_fork_frame_sysc (depending on which entry path fired,
     *      indicated by fork_via_syscall=0|1),
     *   2. for the SYSCALL path it ALSO snapshots the user %rsp out of
     *      %gs:syscall_user_rsp into fork_user_rsp (the syscall trapframe
     *      doesn't carry user rsp, unlike int80),
     *   3. allocates/stores a minimal child_pid and sets fork_pending=1,
     *   4. returns that child_pid to the parent in %rax.
     *
     * After arch_x86_64_usermode_run() returns to the main loop, kernel64
     * checks fork_pending and re-enters ring3 from the saved frame with
     * %rax=0 — that becomes the child. The child shares the parent's
     * address space AND stack (vfork semantics): it MUST call _exit
     * before touching anything the parent will look at later. */
    bool                      fork_pending;
    uint8_t                   fork_via_syscall;   /* 0=int80, 1=syscall */
    uint64_t                  fork_user_rsp;      /* only valid when fork_via_syscall=1 */

    /* A2.P2: temporary single-child wait/waitpid bookkeeping for the
     * vfork-style child captured above. child_exited/exit_code are set
     * when the child side of the fork-resume path exits; wait_in_progress
     * guards the nested do_wait() to usermode_run() path. */
    uint32_t                  child_pid;
    bool                      child_exited;
    int32_t                   child_exit_code;
    bool                      wait_in_progress;

    /* γ.2.a — child now owns an independent PCB slot.
     *   parent.child_slot -> proc_table index of the queued child PCB
     *                        (OPENOS_X86_64_PROC_INVALID_INDEX when none).
     *   child.parent_slot -> proc_table index of the parent PCB. Used by
     *                        mark_exited() to publish exit status into
     *                        the parent's child_exited/child_exit_code
     *                        fields when the parent is blocked in wait().
     *
     * Address space is still shared with the parent in this sub-step
     * (γ.2.b/γ.3 will introduce as_clone + activate). Only the PCB and
     * the fork trapframe move to the child. */
    uint16_t                  child_slot;
    uint16_t                  parent_slot;

    /* γ.4 S2b — multi-child fork/wait support.
     *
     * children_head  : head of the parent's list of *live* (not yet reaped)
     *                  children, linked via child.sibling_next. When a
     *                  child exits it is removed from this list and pushed
     *                  onto zombie_head so wait() can drain it.
     * zombie_head    : head of the parent's list of *exited but not reaped*
     *                  children, linked via child.zombie_next. Populated by
     *                  do_exit() (on the child) and drained by do_wait()
     *                  (on the parent). While non-empty, child_exited=true.
     * sibling_next   : per-child pointer used by children_head list.
     * zombie_next    : per-child pointer used by zombie_head list.
     *
     * All fields carry OPENOS_X86_64_PROC_INVALID_INDEX for "NULL".
     * Legacy single-child fields child_slot/child_pid/child_exited/
     * child_exit_code stay in place: they now mirror the *most recent*
     * child event so any code path that hasn't been migrated yet keeps
     * seeing consistent (if stale) data. */
    uint16_t                  children_head;
    uint16_t                  zombie_head;
    uint16_t                  sibling_next;
    uint16_t                  zombie_next;

    /* gamma.3b-alpha: index of the PARKED sched_slot allocated at
     * fork_alloc_child time to hold the child's future USER dispatch
     * context. In alpha the slot is NEVER flipped to READY and is
     * released on reap; it exists purely to prove out the allocation
     * / lifecycle plumbing. Beta will flip PARKED -> READY at
     * fork(2)-return so the child enters the preemption path.
     *
     * 0 == OPENOS_X86_64_SCHED_INVALID_SLOT (idx 0 is reserved for
     * the bootstrap slot in sched64.c). */
    uint32_t                  fork_child_sched_slot;

    x86_64_int80_frame_t      saved_fork_frame_int80;
    x86_64_syscall_frame_t    saved_fork_frame_sysc;

    /* ------------------------------------------------------------------
     * M4.2 — per-process signal state.
     *
     * Holds the pending / blocked bitmaps and the disposition table for
     * this PCB. Initialised by arch_x86_64_proc_init() (slot 0) and
     * arch_x86_64_proc_spawn_user() (ring3 slots) via
     * x86_64_signal_state_init(). SYS_KILL / SYS_SIGACTION /
     * SYS_SIGPROCMASK operate on this block through the signal64 API.
     * The scheduler consults next_pending() to enact default actions;
     * the ring3 handler trampoline (M4.2b) will read actions[] here. */
    x86_64_sigstate_t         sig;

    /* ------------------------------------------------------------------
     * M4.4b — job control: POSIX process group + session ids.
     *
     * pgid : process-group id this PCB belongs to. A new ring3 proc starts
     *        in its own group (pgid == pid) unless setpgid() moves it.
     * sid  : session id. setsid() makes the caller a session+group leader
     *        (sid = pgid = pid) and detaches it from any controlling tty.
     *
     * The TTY layer's foreground pgrp (tty64_get/set_pgrp) is matched
     * against pgid to decide who receives ^C/^Z generated signals: only
     * PCBs whose pgid == tty foreground pgrp get the signal. Both fields
     * default to the pid at spawn and are inherited across fork. */
    uint32_t                  pgid;
    uint32_t                  sid;
} x86_64_proc_t;

/* Lifecycle ---------------------------------------------------------- */

void arch_x86_64_proc_init(void);
/* Allocate a ring3 PCB, rotate current to it. Returns pid (>0) or 0 on
 * failure. name may be NULL. */
uint32_t arch_x86_64_proc_spawn_user(const char *name);
/* Mark current as exited, rotate current back to the kernel proc. */
void arch_x86_64_proc_exit(int code);

/* Queries used by the syscall dispatcher ----------------------------- */

/* A2.P1: direct PCB accessor for usermode64.c. Returns the PCB of the
 * thread that the cooperative scheduler is currently running, or NULL
 * before arch_x86_64_proc_init() has registered slot 0. Never returns
 * a pointer into a FREE slot. */
x86_64_proc_t *arch_x86_64_proc_current(void);

uint32_t arch_x86_64_proc_current_pid(void);
uint32_t arch_x86_64_proc_current_tid(void);
uint32_t arch_x86_64_proc_current_ppid(void);
uint32_t arch_x86_64_proc_current_uid(void);
uint32_t arch_x86_64_proc_current_gid(void);
uint32_t arch_x86_64_proc_alloc_child_pid(x86_64_proc_t *parent);

/* γ.2.a — allocate an independent PCB slot for the fork child. Parent is
 * `parent_pcb` (usually arch_x86_64_proc_current()). Inherits ppid/uid/gid
 * and links parent.child_slot / child.parent_slot both ways. Does NOT
 * rotate current_index — the parent keeps running until wait() drives the
 * child via arch_x86_64_proc_switch_to() below. Returns NULL on failure. */
x86_64_proc_t *arch_x86_64_proc_fork_alloc_child(x86_64_proc_t *parent_pcb);

/* γ.2.a — current-slot management for the wait-driven child run. Returns
 * the previous current slot index so the caller can restore it after the
 * nested run. proc_table[slot] must be RUNNING. */
uint16_t arch_x86_64_proc_current_slot(void);
uint16_t arch_x86_64_proc_switch_to(uint16_t slot);

/* γ.2.a — direct PCB accessor by slot (used by wait-path to read the
 * child's exit status after it has been reaped from mark_exited()). Returns
 * NULL for FREE slots or out-of-range indices. */
x86_64_proc_t *arch_x86_64_proc_slot(uint16_t slot);

/* γ.4 S2b — reverse lookup: given a PCB pointer previously returned by
 * arch_x86_64_proc_slot()/arch_x86_64_proc_current()/fork_alloc_child(),
 * return its slot index. Returns OPENOS_X86_64_PROC_INVALID_INDEX if the
 * pointer is NULL or does not point into proc_table[]. Used by the
 * multi-child fork/wait path to identify "self" so children can unlink
 * themselves from parent->children_head. */
uint16_t arch_x86_64_proc_slot_of(const x86_64_proc_t *p);

/* γ.2.a — release the child PCB slot after its status has been consumed by
 * a wait(). Safe to call on FREE slots (no-op). */
void arch_x86_64_proc_release_slot(uint16_t slot);

/* H.5b.2: per-process address-space accessors. as_create() ownership
 * is transferred into the PCB by set_as(); proc_exit() will destroy it
 * if non-NULL. Returns NULL when no AS is bound (boot/legacy paths). */
struct x86_64_address_space *arch_x86_64_proc_current_get_as(void);
void arch_x86_64_proc_current_set_as(struct x86_64_address_space *as);

/* Cooperative yield. No other runnable thread exists yet, so this just
 * bumps the global yield counter and returns. Wired up so callers can
 * compile against the real syscall today; sched64 will plug in later. */
int arch_x86_64_proc_yield(void);

/* Diagnostics -------------------------------------------------------- */

uint64_t arch_x86_64_proc_yield_count(void);
uint64_t arch_x86_64_proc_spawn_count(void);
uint64_t arch_x86_64_proc_exit_count(void);
void     arch_x86_64_proc_print_status(void);

/* M4.2: signal delivery + disposition management, layered on the
 * per-PCB x86_64_sigstate_t. arch_x86_64_proc_signal() is the work behind
 * SYS_KILL: it locates the PCB whose pid matches, raises the pending bit
 * through x86_64_signal_send(), then immediately enacts the default action
 * for uncatchable/terminating signals (SIGKILL/SIGTERM -> EXITED with
 * 128+sig) so the scheduler reaps it. sig==0 is an existence probe.
 * Returns 0 on success, -1 if no such pid exists. */
int      arch_x86_64_proc_signal(uint32_t pid, int sig);

/* M4.2: sigaction / sigprocmask acting on the CURRENT process. Thin
 * wrappers that forward to the signal64 API against
 * arch_x86_64_proc_current()->sig, so the syscall dispatcher need not
 * reach into the PCB layout. Return 0 on success, -1 on error (invalid
 * signal, immutable disposition, bad `how`, or no current PCB). */
int      arch_x86_64_proc_sigaction(int sig,
                                    const openos_sigaction_t *act,
                                    openos_sigaction_t *old);
int      arch_x86_64_proc_sigprocmask(int how, uint64_t set, uint64_t *oldset);

/* M4.2: pump pending signals for the CURRENT process, enacting default
 * actions (TERM/CORE -> proc_exit(128+sig)). Called on the syscall return
 * path. Returns the signal whose default action was taken (>0), or 0 if
 * nothing was pending/deliverable. Signals with a registered user handler
 * are left pending for the M4.2b ring3 trampoline. */
int      arch_x86_64_proc_signal_pump(void);

/* M4.2b: user-mode signal handler trampoline.
 *
 * deliver_user() takes the interrupted ring3 register set (as captured on
 * the syscall/interrupt entry), builds a signal frame on the user stack for
 * the CURRENT process's next deliverable handler-registered signal, and
 * reroutes `regs` so that the eventual return to ring3 lands in the handler
 * (rdi=signo, rsp pointing just below a sigcontext, a `restorer` return
 * address on top so the handler's `ret` re-enters the kernel via sigreturn).
 * The sigcontext body is copied to the user stack via the supplied
 * user-memory writer, so this stays free of any specific paging layout.
 *
 *   regs      - in/out: interrupted context, rerouted to the handler on hit.
 *   uwrite    - callback: copy `n` bytes from `src` to user VA `dst`.
 *   uctx      - opaque cookie forwarded to uwrite (e.g. target PCB/CR3).
 *
 * Returns the signal delivered (>0) if a handler frame was set up, 0 if no
 * handler-registered signal was pending, or -1 on error (bad frame build /
 * user-stack write failure).
 *
 * sigreturn() undoes one deliver_user(): it reads the sigcontext back from
 * the user stack (top of `regs->rsp`, per the layout deliver_user built) via
 * the supplied reader, restores the interrupted register set into `regs`,
 * and reinstates the pre-delivery signal mask. Returns 0 on success, -1 if
 * no handler was active or the user read failed. */
int      arch_x86_64_proc_signal_deliver_user(
             x86_64_sigregs_t *regs,
             int (*uwrite)(void *uctx, uint64_t dst, const void *src,
                           uint64_t n),
             void *uctx);
int      arch_x86_64_proc_sigreturn(
             x86_64_sigregs_t *regs,
             int (*uread)(void *uctx, void *dst, uint64_t src, uint64_t n),
             void *uctx);

/* M4.4b: job control — process group / session management on the CURRENT
 * process (or an explicit pid for setpgid). All operate on the proc_table.
 *
 *   getpgid(pid)         -> pgid of `pid` (0 means "caller"). -1 if no such
 *                           pid. getpgid alias for the current proc when
 *                           pid==0.
 *   setpgid(pid, pgid)   -> put process `pid` (0=caller) into group `pgid`
 *                           (0 means "use pid as the new group"). Returns 0
 *                           / -1. Cannot move a session leader.
 *   getsid(pid)          -> session id of `pid` (0=caller). -1 if none.
 *   setsid()             -> caller becomes session+group leader: sid=pgid=
 *                           pid, controlling tty detached. Returns the new
 *                           sid (>0), or -1 if the caller is already a
 *                           group leader.
 *   signal_group(pgid,s) -> deliver signal `s` to every PCB whose pgid ==
 *                           `pgid` (the work behind kill(-pgid, s) and the
 *                           TTY ^C -> foreground-group broadcast). Returns
 *                           the number of processes signalled (>=0). */
uint32_t arch_x86_64_proc_getpgid(uint32_t pid);
int      arch_x86_64_proc_setpgid(uint32_t pid, uint32_t pgid);
uint32_t arch_x86_64_proc_getsid(uint32_t pid);
int      arch_x86_64_proc_setsid(void);
int      arch_x86_64_proc_signal_group(uint32_t pgid, int sig);

#endif /* OPENOS_ARCH_X86_64_PROC64_H */
