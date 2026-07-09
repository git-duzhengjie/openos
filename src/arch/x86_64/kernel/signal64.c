/*
 * signal64.c — per-process signal bookkeeping for the x86_64 port (M4.2).
 *
 * This is the kernel-side half of POSIX signals:
 *   - pending / blocked bitmaps and a per-signal disposition table,
 *   - sigaction / sigprocmask / send / next_pending / consume,
 *   - the classic default-disposition table.
 *
 * It deliberately does NOT touch scheduling, ring3 stacks, or the PCB
 * layout — proc64 owns those and calls into here. That keeps the signal
 * semantics unit-testable in isolation and lets the user-space handler
 * trampoline (M4.2b) slot in later without reshaping this module.
 */

#include "../include/signal64.h"

/* Bit helpers. Bit `sig` (1..31) of a uint64_t bitmap. */
static inline uint64_t sig_bit(int sig) {
    return (uint64_t)1 << (unsigned)sig;
}

void x86_64_signal_state_init(x86_64_sigstate_t *st) {
    if (st == 0) {
        return;
    }
    st->pending = 0;
    st->blocked = 0;
    st->in_handler = 0;
    st->saved_mask = 0;
    for (int i = 0; i < OPENOS_NSIG; i++) {
        st->actions[i].handler = OPENOS_SIG_DFL;
        st->actions[i].mask    = 0;
        st->actions[i].flags   = 0;
    }
}

int x86_64_signal_send(x86_64_sigstate_t *st, int sig) {
    if (st == 0 || !x86_64_signal_valid(sig)) {
        return -1;
    }
    /* Uncatchable signals are always recorded, no matter the disposition. */
    if (!x86_64_signal_uncatchable(sig)) {
        /* An explicit SIG_IGN disposition drops the signal on the floor:
         * it never becomes pending, matching POSIX semantics. */
        if (st->actions[sig].handler == OPENOS_SIG_IGN) {
            return 0;
        }
    }
    st->pending |= sig_bit(sig);
    return 0;
}

int x86_64_signal_sigaction(x86_64_sigstate_t *st, int sig,
                            const openos_sigaction_t *act,
                            openos_sigaction_t *old) {
    if (st == 0 || !x86_64_signal_valid(sig)) {
        return -1;
    }
    /* SIGKILL / SIGSTOP dispositions are immutable. A pure query
     * (act == NULL) is still allowed so callers can read them back. */
    if (act != 0 && x86_64_signal_uncatchable(sig)) {
        return -1;
    }
    if (old != 0) {
        *old = st->actions[sig];
    }
    if (act != 0) {
        st->actions[sig] = *act;
        /* If the new disposition ignores an already-pending signal,
         * discard the pending bit (again, unless uncatchable). */
        if (act->handler == OPENOS_SIG_IGN &&
            !x86_64_signal_uncatchable(sig)) {
            st->pending &= ~sig_bit(sig);
        }
    }
    return 0;
}

int x86_64_signal_procmask(x86_64_sigstate_t *st, int how,
                           uint64_t set, uint64_t *oldset) {
    if (st == 0) {
        return -1;
    }
    if (oldset != 0) {
        *oldset = st->blocked;
    }
    switch (how) {
        case OPENOS_SIG_BLOCK:
            st->blocked |= set;
            break;
        case OPENOS_SIG_UNBLOCK:
            st->blocked &= ~set;
            break;
        case OPENOS_SIG_SETMASK:
            st->blocked = set;
            break;
        default:
            return -1;
    }
    /* SIGKILL / SIGSTOP can never be blocked — strip them unconditionally. */
    st->blocked &= ~sig_bit(OPENOS_SIGKILL);
    st->blocked &= ~sig_bit(OPENOS_SIGSTOP);
    return 0;
}

int x86_64_signal_next_pending(const x86_64_sigstate_t *st) {
    if (st == 0) {
        return 0;
    }
    uint64_t deliverable = st->pending & ~st->blocked;
    if (deliverable == 0) {
        return 0;
    }
    /* Lowest-numbered signal wins (POSIX leaves order unspecified, but a
     * deterministic low-first order keeps the self-test reproducible). */
    for (int sig = 1; sig < OPENOS_NSIG; sig++) {
        if (deliverable & sig_bit(sig)) {
            return sig;
        }
    }
    return 0;
}

void x86_64_signal_consume(x86_64_sigstate_t *st, int sig) {
    if (st == 0 || !x86_64_signal_valid(sig)) {
        return;
    }
    st->pending &= ~sig_bit(sig);
}

x86_64_sig_default_t x86_64_signal_default_action(int sig) {
    switch (sig) {
        /* Ignored by default. */
        case OPENOS_SIGCHLD:
        case OPENOS_SIGCONT:  /* CONT's "resume" effect handled separately */
            return (sig == OPENOS_SIGCONT) ? OPENOS_SIG_ACT_CONT
                                           : OPENOS_SIG_ACT_IGN;
        /* Stop the process. */
        case OPENOS_SIGSTOP:
        case OPENOS_SIGTSTP:
            return OPENOS_SIG_ACT_STOP;
        /* Core-dumping terminators. */
        case OPENOS_SIGQUIT:
        case OPENOS_SIGILL:
        case OPENOS_SIGTRAP:
        case OPENOS_SIGABRT:
        case OPENOS_SIGBUS:
        case OPENOS_SIGFPE:
        case OPENOS_SIGSEGV:
            return OPENOS_SIG_ACT_CORE;
        /* Everything else (HUP/INT/KILL/USR1/USR2/PIPE/ALRM/TERM/...)
         * terminates the process. */
        default:
            return OPENOS_SIG_ACT_TERM;
    }
}

/* ---- M4.2b: sigframe build / restore ---------------------------------- */

/* Align `v` down to a 16-byte boundary (SysV AMD64 stack ABI). */
static inline uint64_t align16_down(uint64_t v) {
    return v & ~(uint64_t)0xF;
}

int x86_64_signal_build_frame(x86_64_sigstate_t *st, int sig,
                              uint64_t handler, uint64_t restorer,
                              x86_64_sigregs_t *regs,
                              x86_64_sigcontext_t *frame_out,
                              uint64_t *sp_out) {
    if (st == 0 || regs == 0 || frame_out == 0 || sp_out == 0) {
        return -1;
    }
    if (!x86_64_signal_valid(sig)) {
        return -1;
    }
    /* A user handler must be installed: not DFL, not IGN. */
    uint64_t disp = st->actions[sig].handler;
    if (disp == OPENOS_SIG_DFL || disp == OPENOS_SIG_IGN) {
        return -1;
    }
    if (handler == 0) {
        return -1;
    }

    /* Snapshot the interrupted context into the sigframe body. */
    frame_out->r15 = regs->r15; frame_out->r14 = regs->r14;
    frame_out->r13 = regs->r13; frame_out->r12 = regs->r12;
    frame_out->r11 = regs->r11; frame_out->r10 = regs->r10;
    frame_out->r9  = regs->r9;  frame_out->r8  = regs->r8;
    frame_out->rdi = regs->rdi; frame_out->rsi = regs->rsi;
    frame_out->rbp = regs->rbp; frame_out->rbx = regs->rbx;
    frame_out->rdx = regs->rdx; frame_out->rcx = regs->rcx;
    frame_out->rax = regs->rax;
    frame_out->rip = regs->rip;
    frame_out->rsp = regs->rsp;
    frame_out->rflags = regs->rflags;
    frame_out->signo = (uint64_t)sig;
    frame_out->saved_mask = st->blocked;

    /* Stack layout, growing down from the interrupted %rsp:
     *
     *   [ old rsp ]                         <- honour 128B red zone below
     *        - 128 (red zone)
     *   [ sigcontext (sizeof, 16B aligned) ] <- ctx_addr
     *   [ restorer return address (8B)     ] <- new %rsp (ret pops this)
     *
     * After the handler's `ret`, %rsp points just above the sigcontext,
     * which is exactly where sigreturn reads it back from. */
    uint64_t sp = regs->rsp;
    sp -= 128;                                    /* red zone */
    sp -= sizeof(x86_64_sigcontext_t);
    sp = align16_down(sp);                        /* ctx 16B aligned */
    uint64_t ctx_addr = sp;
    sp -= 8;                                      /* restorer return addr */
    /* SysV ABI: at handler entry %rsp+8 must be 16B aligned, i.e. %rsp is
     * 16B-aligned minus 8. ctx_addr is 16B aligned so sp = ctx_addr-8
     * already satisfies this. */

    *sp_out = sp;

    /* Reroute the interrupted context to the handler. The glue layer
     * copies *frame_out to ctx_addr and writes `restorer` at sp. */
    regs->rip = handler;
    regs->rsp = sp;
    regs->rdi = (uint64_t)sig;   /* SysV first arg: signo */
    (void)ctx_addr;
    (void)restorer;

    /* Mask this signal (and the action's mask) for the handler's duration,
     * remembering the mask to reinstate on sigreturn. */
    st->saved_mask = st->blocked;
    st->blocked |= st->actions[sig].mask;
    if (!x86_64_signal_uncatchable(sig)) {
        st->blocked |= sig_bit(sig);
    }
    /* KILL/STOP can never stay blocked. */
    st->blocked &= ~sig_bit(OPENOS_SIGKILL);
    st->blocked &= ~sig_bit(OPENOS_SIGSTOP);

    st->in_handler = sig;
    /* The delivered signal is no longer pending. */
    st->pending &= ~sig_bit(sig);
    return 0;
}

int x86_64_signal_restore_frame(x86_64_sigstate_t *st,
                                const x86_64_sigcontext_t *ctx,
                                x86_64_sigregs_t *regs) {
    if (st == 0 || ctx == 0 || regs == 0) {
        return -1;
    }
    if (st->in_handler == 0) {
        return -1;   /* no handler was active */
    }

    /* Restore the interrupted register set from the saved context. */
    regs->r15 = ctx->r15; regs->r14 = ctx->r14;
    regs->r13 = ctx->r13; regs->r12 = ctx->r12;
    regs->r11 = ctx->r11; regs->r10 = ctx->r10;
    regs->r9  = ctx->r9;  regs->r8  = ctx->r8;
    regs->rdi = ctx->rdi; regs->rsi = ctx->rsi;
    regs->rbp = ctx->rbp; regs->rbx = ctx->rbx;
    regs->rdx = ctx->rdx; regs->rcx = ctx->rcx;
    regs->rax = ctx->rax;
    regs->rip = ctx->rip;
    regs->rsp = ctx->rsp;
    regs->rflags = ctx->rflags;

    /* Reinstate the pre-delivery blocked mask; KILL/STOP always unblocked. */
    st->blocked = ctx->saved_mask;
    st->blocked &= ~sig_bit(OPENOS_SIGKILL);
    st->blocked &= ~sig_bit(OPENOS_SIGSTOP);
    st->saved_mask = 0;
    st->in_handler = 0;
    return 0;
}
