#ifndef OPENOS_ARCH_X86_64_SIGNAL64_H
#define OPENOS_ARCH_X86_64_SIGNAL64_H

/*
 * signal64 — per-process POSIX-style signal state for the x86_64 port.
 *
 * Scope (M4.2 kernel side):
 *   - Per-PCB pending / blocked bitmaps + disposition table.
 *   - sigaction()  : register / query a handler for a signal.
 *   - sigprocmask(): block / unblock signals.
 *   - send()       : set a pending bit (the actual work behind SYS_KILL).
 *   - next_pending(): pick the lowest-numbered deliverable signal.
 *   - default_action(): classic POSIX default disposition table.
 *
 * Out of scope for M4.2 (deferred to M4.2b when a ring3 foreground
 * process exists to test against): building the user-space signal
 * trampoline that actually reroutes ring3 %rip/%rsp into a registered
 * handler and restores context on sigreturn. The ABI here is designed
 * so that step is a drop-in: the handler pointer is already stored.
 *
 * This module keeps ZERO dependency on proc64 so it can be embedded in
 * the PCB (proc64.h includes this header, not the other way round).
 */

#include <stdint.h>
#include <stdbool.h>

/* Classic signal numbers (Linux/x86_64 compatible, 1..31 range). */
#define OPENOS_SIGHUP    1
#define OPENOS_SIGINT    2
#define OPENOS_SIGQUIT   3
#define OPENOS_SIGILL    4
#define OPENOS_SIGTRAP   5
#define OPENOS_SIGABRT   6
#define OPENOS_SIGBUS    7
#define OPENOS_SIGFPE    8
#define OPENOS_SIGKILL   9
#define OPENOS_SIGUSR1   10
#define OPENOS_SIGSEGV   11
#define OPENOS_SIGUSR2   12
#define OPENOS_SIGPIPE   13
#define OPENOS_SIGALRM   14
#define OPENOS_SIGTERM   15
#define OPENOS_SIGCHLD   17
#define OPENOS_SIGCONT   18
#define OPENOS_SIGSTOP   19
#define OPENOS_SIGTSTP   20

/* Highest signal we track. Bit i (1..31) of a bitmap == signal i;
 * bit 0 is unused so the "no pending signal" sentinel is 0. */
#define OPENOS_NSIG      32

/* Special handler values stored in openos_sigaction.handler. */
#define OPENOS_SIG_DFL   ((uint64_t)0)  /* default disposition */
#define OPENOS_SIG_IGN   ((uint64_t)1)  /* ignore the signal */

/* sigprocmask how values. */
#define OPENOS_SIG_BLOCK    0
#define OPENOS_SIG_UNBLOCK  1
#define OPENOS_SIG_SETMASK  2

/* Classic default dispositions returned by x86_64_signal_default_action(). */
typedef enum x86_64_sig_default {
    OPENOS_SIG_ACT_TERM = 0,  /* terminate the process */
    OPENOS_SIG_ACT_IGN  = 1,  /* ignore */
    OPENOS_SIG_ACT_CORE = 2,  /* terminate + (would) dump core */
    OPENOS_SIG_ACT_STOP = 3,  /* stop the process */
    OPENOS_SIG_ACT_CONT = 4,  /* continue if stopped */
} x86_64_sig_default_t;

/* Per-signal disposition (mirrors struct sigaction essentials). */
typedef struct openos_sigaction {
    uint64_t handler;  /* OPENOS_SIG_DFL / OPENOS_SIG_IGN / user fn ptr */
    uint64_t mask;     /* extra signals blocked while handler runs */
    uint64_t flags;    /* SA_* flags (stored, not yet interpreted) */
} openos_sigaction_t;

/* Per-process signal state, embedded in the PCB. */
typedef struct x86_64_sigstate {
    uint64_t           pending;              /* bitmap of raised signals */
    uint64_t           blocked;              /* bitmap of masked signals */
    openos_sigaction_t actions[OPENOS_NSIG]; /* index by signal number */
    /* M4.2b: user-handler delivery bookkeeping. When a handler is running
     * in ring3, in_handler != 0 holds the signal number currently being
     * serviced and saved_mask holds the blocked mask to restore on
     * sigreturn. Zero when no handler is active. */
    int                in_handler;           /* signo of in-flight handler, 0 = none */
    uint64_t           saved_mask;           /* blocked mask to restore on sigreturn */
} x86_64_sigstate_t;

/* ---- M4.2b user-space signal trampoline ------------------------------ *
 *
 * Classic Linux/x86_64 sigframe model. Before returning to ring3 the
 * kernel, on finding a pending signal with a user handler, saves the
 * interrupted ring3 context onto the user stack as an x86_64_sigcontext,
 * pushes a restorer return address below it, then reroutes the iretq
 * frame so ring3 resumes at handler(signo). The handler eventually rets
 * into the restorer trampoline which issues SYS_RT_SIGRETURN; the kernel
 * then restores the saved context and iretq's back to the original %rip.
 */

/* Saved ring3 register context (the sigframe body). Field order is the
 * canonical iretq-friendly layout; matched by proc64/usermode glue. */
typedef struct x86_64_sigcontext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t rip;      /* interrupted instruction pointer */
    uint64_t rsp;      /* interrupted stack pointer */
    uint64_t rflags;   /* interrupted RFLAGS */
    uint64_t signo;    /* signal being delivered (informational) */
    uint64_t saved_mask; /* blocked mask captured at delivery time */
} x86_64_sigcontext_t;

/* Register snapshot handed in/out of build/restore. Mirrors the syscall
 * entry's saved GPR set plus the iretq trio (rip/rsp/rflags). The glue
 * layer fills this from the trap frame and writes it back after restore. */
typedef struct x86_64_sigregs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t rip, rsp, rflags;
} x86_64_sigregs_t;

/* Validity check: 1..31 (SIGKILL/SIGSTOP included; callers decide catch). */
static inline bool x86_64_signal_valid(int sig) {
    return sig >= 1 && sig < OPENOS_NSIG;
}

/* SIGKILL / SIGSTOP can never be caught, blocked, or ignored. */
static inline bool x86_64_signal_uncatchable(int sig) {
    return sig == OPENOS_SIGKILL || sig == OPENOS_SIGSTOP;
}

/* ---- API (implemented in signal64.c) ---------------------------------- */

/* Reset a state block: no pending, nothing blocked, all dispositions
 * default. Call from proc init / spawn. */
void x86_64_signal_state_init(x86_64_sigstate_t *st);

/* Raise `sig` on `st`: set its pending bit. SIG_IGN dispositions are
 * dropped here (never become pending) EXCEPT uncatchable signals, which
 * are always recorded. Returns 0 on success, -1 for an invalid signal. */
int x86_64_signal_send(x86_64_sigstate_t *st, int sig);

/* Register / query a disposition. `act` may be NULL (query-only); `old`
 * may be NULL. Returns 0 on success, -1 for invalid signal or an attempt
 * to change an uncatchable signal's handler. */
int x86_64_signal_sigaction(x86_64_sigstate_t *st, int sig,
                            const openos_sigaction_t *act,
                            openos_sigaction_t *old);

/* Adjust the blocked mask. `how` is one of OPENOS_SIG_BLOCK/UNBLOCK/
 * SETMASK. `oldset` (may be NULL) receives the prior mask. SIGKILL/
 * SIGSTOP bits are silently stripped from the effective blocked set.
 * Returns 0 on success, -1 for a bad `how`. */
int x86_64_signal_procmask(x86_64_sigstate_t *st, int how,
                           uint64_t set, uint64_t *oldset);

/* Lowest-numbered pending-and-not-blocked signal, or 0 if none. Does NOT
 * clear the pending bit — the caller consumes it via _consume(). */
int x86_64_signal_next_pending(const x86_64_sigstate_t *st);

/* Clear the pending bit for `sig` (called once the signal is delivered
 * or its default action taken). No-op for invalid signals. */
void x86_64_signal_consume(x86_64_sigstate_t *st, int sig);

/* Classic default disposition for a signal (used when handler==SIG_DFL). */
x86_64_sig_default_t x86_64_signal_default_action(int sig);

/* ---- M4.2b: sigframe build / restore (pure logic) --------------------- *
 *
 * x86_64_signal_build_frame: given the interrupted ring3 register set
 * `regs`, the target signal `sig` and a `restorer` return address,
 * compute the new user stack layout for handler delivery. On success it
 *   - writes the x86_64_sigcontext to *frame_out (caller copies it to the
 *     user stack at the returned *sp_out),
 *   - sets *sp_out to the new ring3 %rsp (points at the restorer return
 *     address, 16-byte ABI alignment honoured),
 *   - rewrites `regs` so rip=handler, rsp=*sp_out, rdi=sig,
 * and updates `st` (in_handler=sig, saved_mask=blocked, blocks sig +
 * action mask). Returns 0 on success, -1 on bad args / no handler.
 *
 * `old_sp` is the interrupted ring3 %rsp; the frame is placed below it
 * (honouring the SysV 128-byte red zone). */
int x86_64_signal_build_frame(x86_64_sigstate_t *st, int sig,
                              uint64_t handler, uint64_t restorer,
                              x86_64_sigregs_t *regs,
                              x86_64_sigcontext_t *frame_out,
                              uint64_t *sp_out);

/* x86_64_signal_restore_frame: inverse of build. Given the saved
 * sigcontext read back from the user stack, restore `regs` (rip/rsp/
 * rflags + GPRs) and reinstate st->blocked = ctx->saved_mask, clearing
 * in_handler. Returns 0 on success, -1 if no handler was active. */
int x86_64_signal_restore_frame(x86_64_sigstate_t *st,
                                const x86_64_sigcontext_t *ctx,
                                x86_64_sigregs_t *regs);

#endif /* OPENOS_ARCH_X86_64_SIGNAL64_H */
