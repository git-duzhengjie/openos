#include "../include/usermode64.h"

#include <stddef.h>

#include "../include/address_space64.h"
#include "../include/early_console64.h"
#include "../include/gdt64.h"
#include "../include/idt64.h"
#include "../include/pmm64.h"
#include "../include/percpu64.h"
#include "../include/proc64.h"
#include "../include/elf64_loader.h"
#include "../include/initrd64.h"
#include "../include/heap64.h"
#include "../../../kernel/core/fs/vfs.h" /* RAMFS-backed vfs_open/read/stat */

extern void arch_x86_64_iretq_enter_user(const x86_64_user_iretq_frame_t *frame);
extern void arch_x86_64_iretq_enter_user_full(const x86_64_user_iretq_frame_t *frame);

/*
 * Step G.x: ring3-drop sentry state.
 *
 * usermode_canary is set to:
 *   0 before arch_x86_64_usermode_run() ever runs,
 *   1 right after the kernel stashes its longjmp context ("entered"),
 *   2 right after the inline-asm return path falls through `1:` and pops
 *     the callee-saved frame cleanly ("return path executed end-to-end").
 *
 * If a regression of commit 0b14358 reintroduces the misaligned saved-rsp
 * bug, the `ret` from arch_x86_64_usermode_return_to_kernel() lands at
 * something other than label 1 — the canary will stay at 1 (or change in
 * an unexpected way), and the selftest below panics.
 *
 * Paired with arch_x86_64_idt_kernel_fault_count(): even if the canary
 * somehow gets to 2 by accident, a #UD/#GP/#PF on the way there bumps the
 * kfault counter and the selftest still catches it.
 */
static volatile uint64_t usermode_kfault_before = 0;
static volatile uint64_t usermode_kfault_after = 0;

/*
 * Step D.2: the bootstrap user stack must live in memory that ring3 can
 * actually access through the active page tables.  Putting it in kernel .bss
 * lands it in the higher-half mapping, whose boot-time PD entries are
 * kernel-only -- ring3 takes a #PF on the very first 'and rsp / call' pair
 * even though the user code page itself is mapped U=1.
 *
 * Allocate a low-memory page from PMM and use it as the bootstrap stack.
 * PMM returns identity-mapped 4 KiB pages within the early identity range
 * (0..4 GiB in our boot tables), which is U-readable/writable.
 */
#define OPENOS_X86_64_USER_STACK_PAGES 4U
#define OPENOS_X86_64_USER_STACK_SIZE (OPENOS_X86_64_USER_STACK_PAGES * 0x1000U)

static uint8_t usermode_ready;
static uint8_t usermode_running;
static uint8_t usermode_exited;
static int usermode_last_exit_code;
static uint64_t usermode_run_count;
static uint64_t usermode_exit_count;

/*
 * A2.P1: the three pieces of per-thread state that the cooperative
 * longjmp core used to keep as file-scope statics now live in the PCB.
 * The macros below expand to lvalue references on the current thread's
 * PCB so the rest of this file can keep reading/writing them with
 * one-line expressions and one-operand inline-asm constraints. After
 * arch_x86_64_proc_init() runs (slot 0 = kernel PCB), the accessor
 * never returns NULL, and the cooperative scheduler only ever calls
 * into usermode64.c from kernel context, so the indirection is safe.
 */
#define PREPARED_USER_FRAME (arch_x86_64_proc_current()->saved_user_frame)
#define USERMODE_RETURN_RSP (arch_x86_64_proc_current()->kernel_return_rsp)
#define USERMODE_CANARY     (arch_x86_64_proc_current()->usermode_canary)
/*
 * Global backup of the kernel return RSP for the *outermost* usermode_run
 * frame. arch_x86_64_usermode_mark_exited() tears down the ring3 PCB via
 * proc_exit(), which flips `current` back to the kernel proc — after that
 * USERMODE_RETURN_RSP (a per-proc field) reads 0 and the exit trampoline
 * would fall through into a hlt loop instead of unwinding usermode_run().
 * We snapshot the RSP here so return_to_kernel() can always unwind. */
static uint64_t g_usermode_return_rsp_backup;
static x86_64_phys_addr_t bootstrap_user_stack_base;  /* phys == virt (identity) */

/*
 * H.3 execve trampoline state.
 *
 * On SYS_EXEC the dispatcher loads the new ELF, then calls
 * arch_x86_64_usermode_mark_exec(new_entry). That sets exited=1 +
 * pending_exec=1 + pending_exec_entry=new_entry and longjmps back to
 * the saved kernel context (same path SYS_EXIT uses). The outer kernel
 * driver inspects has_pending_exec() after usermode_run() returns and,
 * if set, calls usermode_run(take_pending_exec_entry()) to reenter ring3
 * on the freshly loaded image. The pid stays the same -- mark_exec does
 * NOT call arch_x86_64_proc_exit().
 */
static volatile uint8_t usermode_pending_exec;
static volatile x86_64_entry_t usermode_pending_exec_entry;
static volatile uint64_t usermode_exec_count;
static volatile uint64_t usermode_exec_fail_count;

static uintptr_t bootstrap_user_stack_top(void) {
    if (bootstrap_user_stack_base == 0) {
        x86_64_phys_addr_t p = 0;
        for (unsigned i = 0; i < OPENOS_X86_64_USER_STACK_PAGES; ++i) {
            x86_64_phys_addr_t one = arch_x86_64_pmm_alloc_page();
            if (one == 0) {
                early_console64_write("[x86_64][usermode] PMM out of pages for user stack\n");
                return 0;
            }
            if (i == 0) {
                p = one;
            }
            /* PMM is bump-allocator; pages come back in order, so contiguous. */
        }
        bootstrap_user_stack_base = p;
    }
    return (uintptr_t)(bootstrap_user_stack_base + OPENOS_X86_64_USER_STACK_SIZE);
}

void arch_x86_64_usermode_init(void) {
    usermode_ready = 1;
    usermode_running = 0;
    usermode_exited = 0;
    usermode_last_exit_code = 0;
    usermode_run_count = 0;
    usermode_exit_count = 0;
    USERMODE_RETURN_RSP = 0;
    usermode_pending_exec = 0;
    usermode_pending_exec_entry = 0;
    usermode_exec_count = 0;
    usermode_exec_fail_count = 0;
    PREPARED_USER_FRAME.rip = 0;
    PREPARED_USER_FRAME.cs = (uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
    PREPARED_USER_FRAME.rflags = 0x202ULL;
    PREPARED_USER_FRAME.rsp = (uint64_t)bootstrap_user_stack_top();
    PREPARED_USER_FRAME.ss = (uint64_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
}

void arch_x86_64_usermode_prepare_iretq(x86_64_user_iretq_frame_t *frame,
                                         x86_64_entry_t entry,
                                         x86_64_virt_addr_t stack_top) {
    if (frame == NULL) {
        return;
    }
    frame->rip = (uint64_t)entry;
    frame->cs = (uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
    frame->rflags = 0x202ULL;
    frame->rsp = (uint64_t)stack_top;
    frame->ss = (uint64_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
    /* A2.P3-B-β: fresh user thread starts with zeroed callee-saved.
     * iretq_enter_user_full will load these into rbx/rbp/r12-r15. */
    frame->rbx = 0;
    frame->rbp = 0;
    frame->r12 = 0;
    frame->r13 = 0;
    frame->r14 = 0;
    frame->r15 = 0;
}

uint8_t arch_x86_64_usermode_validate_frame(const x86_64_user_iretq_frame_t *frame) {
    if (frame == NULL) {
        return 0;
    }
    if (frame->rip == 0 || frame->rsp == 0) {
        return 0;
    }
    if ((frame->cs & 3u) != 3u || (frame->ss & 3u) != 3u) {
        return 0;
    }
    return 1;
}

const x86_64_user_iretq_frame_t *arch_x86_64_usermode_get_prepared_frame(void) {
    return &PREPARED_USER_FRAME;
}

uint8_t arch_x86_64_usermode_is_running(void) {
    return usermode_running;
}

uint8_t arch_x86_64_usermode_has_exited(void) {
    return usermode_exited;
}

int arch_x86_64_usermode_exit_code(void) {
    return usermode_last_exit_code;
}

int arch_x86_64_usermode_run(x86_64_entry_t entry) {
    /*
     * A2.P3-B-alpha: detect fork-resume path. When PCB.fork_pending is
     * set, the SYS_FORK wrapper has stashed the parent's trapframe into
     * PCB and the caller (main loop) wants us to drop into ring3 *as the
     * child*: same address space, same user stack, rax=0, rip=next-after-
     * syscall. We skip seed_user_stack / stack remap / prepare_iretq and
     * fill PREPARED_USER_FRAME directly from the saved trapframe. Every-
     * thing else (longjmp anchor, kfault snapshot, iretq, post-cleanup) is
     * reused verbatim, which is the whole point of routing through
     * usermode_run() instead of a parallel resume path -- it gives the
     * child the same SYS_EXIT-via-longjmp landing zone the parent has.
     */
    x86_64_proc_t *p_fork = arch_x86_64_proc_current();
    int fork_resume = (p_fork != NULL && p_fork->fork_pending);

    if (!fork_resume) {
        if (!usermode_ready || entry == 0) {
            return -1;
        }
    } else {
        if (!usermode_ready) {
            return -1;
        }
        /* entry is ignored on the fork-resume path; the rip comes from
         * the stashed trapframe. */
    }

    x86_64_virt_addr_t user_rsp = 0;

    if (!fork_resume) {
    /*
     * H.4: seed the user stack with the SysV program-startup frame so
     * that ring3 _start can `(rsp)` -> argc, `8(rsp)` -> argv. Any args
     * queued by arch_x86_64_usermode_set_args() are consumed here; when
     * none are queued seed_user_stack still lays out the minimal
     * argc=0/argv={NULL} frame to satisfy the ABI.
     */
    /*
     * H.4 + P4.c.1: seed the user stack with the SysV program-startup
     * frame. Kernel-side writes still go through the low identity alias
     * (bootstrap_user_stack_top() is a low VA == phys), but every
     * pointer value the ring3 _start will dereference (argv[i], envp[i])
     * and the returned RSP are biased by OPENOS_X86_64_USER_VBASE so
     * ring3 sees the high-half alias. This is the precondition for
     * dropping the U bit on PML4[0]: ring3 must never need to dereference
     * a low-identity VA.
     *
     * The high-half alias mapping itself is installed in the block
     * immediately below; the seed write order is fine because ring3
     * doesn't run until iretq, by which point the alias is live.
     */
    user_rsp = arch_x86_64_usermode_seed_user_stack_ex(
        (x86_64_virt_addr_t)bootstrap_user_stack_top(),
        (x86_64_virt_addr_t)OPENOS_X86_64_USER_VBASE);
    if (user_rsp == 0) {
        return -3;
    }

    /*
     * H.5b.2 step B: ring3 now executes from PML4[1] (USER_VBASE). The
     * bootstrap stack is still allocated in low identity-mapped memory
     * (so seed_user_stack can write argv/envp via the low alias), but the
     * VA handed to ring3 must live in the high half so that after we flip
     * CR3 to the per-PCB AS the stack is still reachable.
     *
     * Map the 4 stack pages into the current task's AS at
     * va = USER_VBASE + phys, then rewrite user_rsp to its high-half
     * alias.
     */
    {
        x86_64_address_space_t *as = arch_x86_64_proc_current_get_as();
        if (as != NULL) {
            uintptr_t stk_base_phys = (uintptr_t)bootstrap_user_stack_base;
            for (uintptr_t off = 0; off < OPENOS_X86_64_USER_STACK_SIZE;
                 off += OPENOS_X86_64_VMM_PAGE_SIZE) {
                x86_64_virt_addr_t hi =
                    (x86_64_virt_addr_t)(OPENOS_X86_64_USER_VBASE + stk_base_phys + off);
                x86_64_phys_addr_t ph =
                    (x86_64_phys_addr_t)(stk_base_phys + off);
                (void)arch_x86_64_as_map_user(as, hi, ph,
                    OPENOS_X86_64_VMM_PAGE_SIZE,
                    OPENOS_X86_64_AS_FLAG_RW |
                    OPENOS_X86_64_AS_FLAG_BORROWED);
            }
            /* P4.c.1: user_rsp is already biased by USER_VBASE inside
             * seed_user_stack_ex(); no further translation needed here. */
        }
    }

    arch_x86_64_usermode_prepare_iretq(&PREPARED_USER_FRAME,
                                       entry,
                                       user_rsp);
    } else {
        /* fork_resume path: synthesize PREPARED_USER_FRAME from PCB's
         * stashed trapframe. cs/ss are ring3 selectors; rip/rsp/rflags
         * come from the SYS_FORK call site so the child returns to the
         * exact instruction right after the syscall, on the same shared
         * user stack. arch_x86_64_iretq_enter_user (invoked by the
         * inline asm below) will then zero every GPR before the iretq,
         * which gives the child rax==0 -- the fork-returns-0 ABI. */
        PREPARED_USER_FRAME.cs =
            (uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
        PREPARED_USER_FRAME.ss =
            (uint64_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
        if (p_fork->fork_via_syscall) {
            PREPARED_USER_FRAME.rip    = p_fork->saved_fork_frame_sysc.rcx;
            PREPARED_USER_FRAME.rflags = p_fork->saved_fork_frame_sysc.r11;
            PREPARED_USER_FRAME.rsp    = p_fork->fork_user_rsp;
            /* A2.P3-B-β: restore callee-saved from the syscall save area
             * so the child resumes with the exact same rbx/rbp/r12-r15
             * the parent had at the SYS_FORK call site. Without this the
             * child crashed on the first `call *%rbx` after the branch. */
            PREPARED_USER_FRAME.rbx = p_fork->saved_fork_frame_sysc.rbx;
            PREPARED_USER_FRAME.rbp = p_fork->saved_fork_frame_sysc.rbp;
            PREPARED_USER_FRAME.r12 = p_fork->saved_fork_frame_sysc.r12;
            PREPARED_USER_FRAME.r13 = p_fork->saved_fork_frame_sysc.r13;
            PREPARED_USER_FRAME.r14 = p_fork->saved_fork_frame_sysc.r14;
            PREPARED_USER_FRAME.r15 = p_fork->saved_fork_frame_sysc.r15;
        } else {
            PREPARED_USER_FRAME.rip    = p_fork->saved_fork_frame_int80.rip;
            PREPARED_USER_FRAME.rflags = p_fork->saved_fork_frame_int80.rflags;
            PREPARED_USER_FRAME.rsp    = p_fork->saved_fork_frame_int80.rsp;
            PREPARED_USER_FRAME.rbx = p_fork->saved_fork_frame_int80.rbx;
            PREPARED_USER_FRAME.rbp = p_fork->saved_fork_frame_int80.rbp;
            PREPARED_USER_FRAME.r12 = p_fork->saved_fork_frame_int80.r12;
            PREPARED_USER_FRAME.r13 = p_fork->saved_fork_frame_int80.r13;
            PREPARED_USER_FRAME.r14 = p_fork->saved_fork_frame_int80.r14;
            PREPARED_USER_FRAME.r15 = p_fork->saved_fork_frame_int80.r15;
        }
        /* One-shot: clear pending before the no-return iretq so a future
         * re-entry sees a clean state. */
        p_fork->fork_pending = 0;
        early_console64_write("[fork:resume] entering child rip=");
        early_console64_write_hex64(PREPARED_USER_FRAME.rip);
        early_console64_write(" rsp=");
        early_console64_write_hex64(PREPARED_USER_FRAME.rsp);
        early_console64_write(" rfl=");
        early_console64_write_hex64(PREPARED_USER_FRAME.rflags);
        early_console64_write(" cs=");
        early_console64_write_hex64(PREPARED_USER_FRAME.cs);
        early_console64_write("\n");
    }
    if (!arch_x86_64_usermode_validate_frame(&PREPARED_USER_FRAME)) {
        return -2;
    }

    usermode_exited = 0;
    usermode_last_exit_code = 0;
    usermode_running = 1;
    ++usermode_run_count;

    /* Step G.x: take a snapshot of the ring0 fault counters before the
     * iretq drop. The kernel between here and the matching post-iretq
     * point below must observe zero ring0 exceptions — SYS_EXIT goes
     * through the syscall path, not the IDT, so the sentry's "total"
     * counter must not advance for a healthy run. */
    usermode_kfault_before = arch_x86_64_idt_kernel_fault_count();
    USERMODE_CANARY = 1;

    /*
     * Step D.3: save a real longjmp-style return context.
     *
     * Earlier we did `movq %rsp, saved; ...; iretq_enter_user(...)` and
     * relied on the syscall path to `mov saved, rsp; ret` back. But that
     * `saved` rsp pointed at the *current* frame's locals (the inline-asm
     * was emitted in the middle of the function), so the `ret` popped
     * a stack slot that was never a return address -- ring3 looked like
     * it kept running and #GP'd on the post-syscall `hlt`.
     *
     * Fix: stash rbx/rbp/r12-r15 + a real RIP label ("1:") on the stack,
     * record that exact rsp, then enter ring3. When ring3 SYS_EXIT calls
     * arch_x86_64_usermode_return_to_kernel(), we restore rsp and `ret`
     * straight to label 1, which falls through to the function epilogue.
     */
    int exited_local = 0;
    int code_local = 0;
    /*
     * H.5b.2 step B: flip CR3 onto the per-PCB address space right before
     * the iretq drop. PML4[0] is the shared boot identity mirror, so kernel
     * text/data/stack/IDT/GDT remain reachable both before and after the
     * load. On SYS_EXIT we restore CR3 to the boot PML4 so the rest of
     * the kernel code path returns to a known table.
     */
    {
        x86_64_address_space_t *as = arch_x86_64_proc_current_get_as();
        if (as != NULL) {
            arch_x86_64_as_activate(as);
        }
    }
    /*
     * Step D.4: stack layout (low -> high) right before iretq_enter_user:
     *   [rsp+ 0] = label-1 RIP   <-- saved_rsp points here
     *   [rsp+ 8] = r15
     *   [rsp+16] = r14
     *   [rsp+24] = r13
     *   [rsp+32] = r12
     *   [rsp+40] = rbx
     *   [rsp+48] = rbp
     * arch_x86_64_usermode_return_to_kernel does `mov saved,%rsp; ret`,
     * which pops the RIP slot and lands at label 1. Then we pop the six
     * callee-saved registers in reverse-push order and fall through to
     * the function epilogue. Previously saved_rsp pointed at the r15 slot
     * so `ret` popped r15's value as RIP -> #UD.
     */
    __asm__ __volatile__ (
        "pushq %%rbp\n\t"
        "pushq %%rbx\n\t"
        "pushq %%r12\n\t"
        "pushq %%r13\n\t"
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"             /* RIP slot on top -- ret target */
        "movq %%rsp, %0\n\t"          /* publish kernel return rsp */
        "movq %%rsp, g_usermode_return_rsp_backup(%%rip)\n\t" /* + global backup */
        "movq %3, %%rdi\n\t"
        "call arch_x86_64_iretq_enter_user_full\n\t"
        /* Should never fall through here -- iretq goes to ring3. */
        "ud2\n\t"
        "1:\n\t"                       /* return target from SYS_EXIT */
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        "popq %%r13\n\t"
        "popq %%r12\n\t"
        "popq %%rbx\n\t"
        "popq %%rbp\n\t"
        : "=m"(USERMODE_RETURN_RSP),
          "=m"(exited_local),
          "=m"(code_local)
        : "r"(&PREPARED_USER_FRAME)
        : "rax", "rcx", "rdx", "rsi", "rdi",
          "r8", "r9", "r10", "r11",
          "memory", "cc"
    );
    /* snapshot the just-published return RSP so the exit trampoline can
     * still unwind even after mark_exited() flipped `current` to the
     * kernel proc (which zeroes the per-proc kernel_return_rsp field). */
    g_usermode_return_rsp_backup = USERMODE_RETURN_RSP;
    (void)exited_local; (void)code_local;

    /*
     * H.5b.2 step B: ring3 has returned (via SYS_EXIT trampoline). Drop
     * back onto the boot PML4 so subsequent kernel paths -- selftest,
     * exec respawn, idle, smp -- see the same CR3 they entered with.
     */
    arch_x86_64_as_activate_boot();

    /* Step G.x: end of the ring3 round-trip. Mark canary=2 and sample the
     * kfault counter again. The exported helpers below let the selftest
     * verify (after_total - before_total) == 0. */
    USERMODE_CANARY = 2;
    usermode_kfault_after = arch_x86_64_idt_kernel_fault_count();

    usermode_running = 0;
    return usermode_exited ? usermode_last_exit_code : -3;
}

int arch_x86_64_usermode_run_pending_child_for_wait(void) {
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == NULL) return -1;
    if (p->wait_in_progress) return -2;
    if (p->child_pid == 0) return -10;
    if (p->child_exited) return p->child_exit_code;
    if (p->child_slot == OPENOS_X86_64_PROC_INVALID_INDEX) return -10;

    x86_64_proc_t *child = arch_x86_64_proc_slot(p->child_slot);
    if (child == NULL) return -10;
    if (!child->fork_pending) return -10;

    /* Preserve the parent's outer SYS_EXIT landing zone. The nested child
     * run will publish its own USERMODE_RETURN_RSP, then return here after
     * child SYS_EXIT. Parent must keep its original landing zone for its own
     * later SYS_EXIT after wait has returned to ring3. */
    uint64_t saved_return_rsp = USERMODE_RETURN_RSP;
    int saved_running = usermode_running;
    int saved_exited = usermode_exited;
    int saved_last_exit_code = usermode_last_exit_code;

    p->wait_in_progress = true;

    /* γ.2.a: temporarily promote the child to `current` for the nested
     * usermode_run(). fork_resume path inside usermode_run reads
     * `current()->saved_fork_frame_*` + `current()->fork_pending`, and
     * child SYS_EXIT will hit arch_x86_64_usermode_mark_exited() while
     * current == child, so publishing exit_code into child's own PCB is
     * natural. Parent slot is restored right after the nested run. */
    uint16_t parent_slot = arch_x86_64_proc_current_slot();
    uint16_t child_slot  = p->child_slot;
    (void)arch_x86_64_proc_switch_to(child_slot);

    arch_x86_64_percpu_t *pc = arch_x86_64_this_cpu_ptr();
    /* A2.P2-fix: keep these reads/writes 8-byte and prevent the compiler
     * from fusing the adjacent kernel_rsp(@0x60) + user_rsp(@0x68) loads
     * into a 16-byte SSE movdqa/movaps pair -- the per-CPU struct is only
     * 8-byte aligned, so a fused movaps to/from %rsp triggers #GP. */
    volatile uint64_t saved_syscall_kernel_rsp = pc ? pc->syscall_kernel_rsp : 0;
    asm volatile("" ::: "memory");
    volatile uint64_t saved_syscall_user_rsp   = pc ? pc->syscall_user_rsp   : 0;
    asm volatile("" ::: "memory");
    if (pc != NULL) {
        /* A2.P2: the parent is blocked inside a syscall handler on the
         * per-CPU syscall stack. A nested usermode_run publishes its return
         * frame on that same stack; if child SYS_EXIT reused the original
         * syscall_kernel_rsp, its entry frame would overwrite the nested
         * return frame. Put the child syscall frame well below the whole
         * wait() call chain while this synchronous wait-driven run is active,
         * then restore it below. One page is not enough on debug-ish builds:
         * the child SYS_EXIT frame can still clobber the inner return slot. */
        pc->syscall_kernel_rsp = saved_syscall_kernel_rsp - 0x4000ULL;
    }
    int code = arch_x86_64_usermode_run(0);

    /* Restore parent as current before any parent-facing bookkeeping. */
    (void)arch_x86_64_proc_switch_to(parent_slot);
    /*
     * A2.P2-fix (CR3-restore): usermode_run() unconditionally ends with
     * arch_x86_64_as_activate_boot(), which loads boot PML4 into CR3. On
     * the outer/top-level ring3 round-trip that's fine (kernel keeps
     * running on boot PML4), but for a *nested* run driven from inside
     * the parent's wait() syscall handler, dropping to boot PML4 means
     * that when do_wait_common eventually returns and sysretq's back to
     * ring3, CR3 no longer has PML4[1]/USER_VBASE mapped -> the parent's
     * next user instruction #PF's with err=0x14 (user, ifetch, P=0) at
     * some 0x00000080_00xxxxxx address.
     *
     * Since γ.2.b, parent and child each own an independent AS (child
     * = as_clone(parent.as)). usermode_run() ends with activate_boot(),
     * so CR3 == boot PML4 here. Re-activate the parent's AS so that when
     * do_wait_common sysretq's back to ring3, CR3 has parent's PML4[1]
     * remapped. release_slot() will then as_destroy() the child AS
     * safely (CR3 is parent.as, not child.as).
     */
    {
        x86_64_address_space_t *as = arch_x86_64_proc_current_get_as();
        if (as != NULL) {
            arch_x86_64_as_activate(as);
        }
    }
    if (pc != NULL) {
        asm volatile("" ::: "memory");
        pc->syscall_kernel_rsp = saved_syscall_kernel_rsp;
        asm volatile("" ::: "memory");
        /* A2.P2-fix: child's SYS_EXIT entry overwrote %gs:syscall_user_rsp
         * with the child's user RSP. When the parent's wait() syscall
         * eventually sysretq's, it reloads %rsp from this slot -- so we
         * MUST restore the parent's user RSP here, otherwise sysret hands
         * the parent the child's user stack and an immediate #PF follows. */
        pc->syscall_user_rsp   = saved_syscall_user_rsp;
    }
    p->wait_in_progress = false;

    USERMODE_RETURN_RSP = saved_return_rsp;
    usermode_running = saved_running;
    usermode_exited = saved_exited;
    usermode_last_exit_code = saved_last_exit_code;

    if (p->child_exited) {
        /* Reap the child slot now that its status has been consumed. */
        arch_x86_64_proc_release_slot(child_slot);
        p->child_slot = OPENOS_X86_64_PROC_INVALID_INDEX;
        return p->child_exit_code;
    }
    return code;
}

void arch_x86_64_usermode_mark_exited(int code) {
    usermode_last_exit_code = code;
    usermode_exited = 1;
    usermode_running = 0;
    ++usermode_exit_count;

    /* A2.P2 / γ.2.a: wait()/waitpid() can synchronously drive the pending
     * fork child while the parent is blocked inside the wait syscall. As of
     * γ.2.a the child owns its own PCB slot, so `current` here is the
     * CHILD; the parent is reachable via child.parent_slot. Record the
     * child's exit into the parent's child_exited/child_exit_code fields
     * and return to the nested usermode_run() — do_wait() will then reap
     * the child slot after usermode_run_pending_child_for_wait() returns.
     * Freeing the child slot here would rip out the stack the SYS_EXIT is
     * still running on. */
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p != NULL && p->parent_slot != OPENOS_X86_64_PROC_INVALID_INDEX) {
        x86_64_proc_t *parent = arch_x86_64_proc_slot(p->parent_slot);
        if (parent != NULL && parent->wait_in_progress) {
            /* Publish exit status on BOTH sides:
             *   - parent.child_exit_code   → what wait() returns
             *   - child.exit_code / exited → record on the child PCB too
             *                                (kept for future waitpid()). */
            parent->child_exited    = true;
            parent->child_exit_code = code;
            p->exit_code = code;
            p->fork_pending = 0;
            return;
        }
    }

    /* Step E.1: tear down the ring3 PCB so SYS_GETPID after exit reports
     * the kernel proc again. Safe even if the user program never spawned
     * (proc_exit is a no-op on the kernel slot). */
    arch_x86_64_proc_exit(code);
}

void arch_x86_64_usermode_mark_exec(x86_64_entry_t entry) {
    /*
     * H.3 execve: stash the new entry, mark pending_exec, and reuse the
     * SYS_EXIT trampoline back to the kernel context inside usermode_run().
     * Crucially we do NOT call arch_x86_64_proc_exit() -- the process
     * lives on with the same pid, just running a different image.
     */
    usermode_pending_exec_entry = entry;
    usermode_pending_exec = 1;
    usermode_last_exit_code = 0;
    usermode_exited = 1;
    usermode_running = 0;
    ++usermode_exec_count;
}

uint8_t arch_x86_64_usermode_has_pending_exec(void) {
    return usermode_pending_exec;
}

x86_64_entry_t arch_x86_64_usermode_take_pending_exec(void) {
    x86_64_entry_t e = usermode_pending_exec_entry;
    usermode_pending_exec = 0;
    usermode_pending_exec_entry = 0;
    return e;
}

uint64_t arch_x86_64_usermode_exec_count(void) {
    return usermode_exec_count;
}

uint64_t arch_x86_64_usermode_exec_fail_count(void) {
    return usermode_exec_fail_count;
}

void arch_x86_64_usermode_note_exec_fail(void) {
    ++usermode_exec_fail_count;
}

void arch_x86_64_usermode_snapshot_return_rsp(void) {
    /* Capture the kernel return RSP while `current` still points at the
     * exiting user proc. mark_exited()->proc_exit() will flip current to
     * the kernel proc whose kernel_return_rsp is 0, so we must stash it
     * here first for return_to_kernel() to unwind on. */
    uint64_t rsp = USERMODE_RETURN_RSP;
    if (rsp != 0) {
        g_usermode_return_rsp_backup = rsp;
    }
}

void arch_x86_64_usermode_return_to_kernel(void) {
    uint64_t rsp = USERMODE_RETURN_RSP;
    if (rsp == 0) {
        /* current proc may already be the kernel proc (mark_exited ran
         * proc_exit); fall back to the outermost usermode_run snapshot. */
        rsp = g_usermode_return_rsp_backup;
    }
    if (rsp != 0) {
        __asm__ __volatile__("movq %0, %%rsp\n\tret" : : "r"(rsp) : "memory");
    }
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void arch_x86_64_usermode_print_status(void) {
    early_console64_write("[x86_64][usermode] ready=");
    early_console64_write_hex64(usermode_ready);
    early_console64_write(" running=");
    early_console64_write_hex64(usermode_running);
    early_console64_write(" exited=");
    early_console64_write_hex64(usermode_exited);
    early_console64_write(" exit_code=");
    early_console64_write_hex64((uint64_t)(uint32_t)usermode_last_exit_code);
    early_console64_write(" runs=");
    early_console64_write_hex64(usermode_run_count);
    early_console64_write(" exits=");
    early_console64_write_hex64(usermode_exit_count);
    early_console64_write(" frame_rip=");
    early_console64_write_hex64(PREPARED_USER_FRAME.rip);
    early_console64_write(" frame_rsp=");
    early_console64_write_hex64(PREPARED_USER_FRAME.rsp);
    early_console64_write(" canary=");
    early_console64_write_hex64(USERMODE_CANARY);
    early_console64_write(" kfault_delta=");
    early_console64_write_hex64(usermode_kfault_after - usermode_kfault_before);
    early_console64_write(" exec_count=");
    early_console64_write_hex64(usermode_exec_count);
    early_console64_write(" exec_fail=");
    early_console64_write_hex64(usermode_exec_fail_count);
    early_console64_write(" pending_exec=");
    early_console64_write_hex64(usermode_pending_exec);
    early_console64_write("\n");
}

uint64_t arch_x86_64_usermode_canary(void) {
    return USERMODE_CANARY;
}

uint64_t arch_x86_64_usermode_kfault_delta(void) {
    return usermode_kfault_after - usermode_kfault_before;
}

/* ===================================================================
 * H.4: argv plumbing.
 * ===================================================================
 *
 * The kernel keeps an N-slot scratch table of argv strings owned by the
 * kernel data segment. The lifetime of these strings is independent of
 * the ring3 image, which is critical because:
 *   1. SYS_EXEC may receive an argv pointer that lives inside the
 *      current ring3 image's .rodata at the very VA elf64_load_image()
 *      is about to overwrite (H.3 path-lifetime hazard).
 *   2. The newly loaded ring3 image has no idea what arguments were
 *      passed; the kernel must inject them by writing onto the user
 *      stack of the *new* program before iretq.
 *
 * The table is single-threaded by construction: the kernel is BSP-only
 * during initial spawn (H.3 baseline) and SYS_EXEC runs under the
 * current task's syscall stack with interrupts disabled by the trap
 * gate. No locking is needed at this stage; when SMP scheduling lands
 * the table will become per-task in the PCB.
 */

static char     usermode_arg_storage[X86_64_USER_ARGV_MAX][X86_64_USER_ARG_MAX];
static int      usermode_arg_count = 0;

/* H.5a: envp snapshot table, same lifetime rationale as argv. */
static char     usermode_env_storage[X86_64_USER_ENVP_MAX][X86_64_USER_ENV_MAX];
static int      usermode_env_count = 0;

static x86_64_size_t k_strlen_u(const char *s) {
    x86_64_size_t n = 0;
    if (!s) return 0;
    while (s[n] != '\0') ++n;
    return n;
}

void arch_x86_64_usermode_clear_args(void) {
    usermode_arg_count = 0;
    for (unsigned i = 0; i < X86_64_USER_ARGV_MAX; ++i) {
        usermode_arg_storage[i][0] = '\0';
    }
}

int arch_x86_64_usermode_pending_argc(void) {
    return usermode_arg_count;
}

void arch_x86_64_usermode_set_args(int argc, const char *const *argv) {
    arch_x86_64_usermode_clear_args();
    if (argc <= 0 || argv == NULL) {
        return;
    }
    int cap = (int)X86_64_USER_ARGV_MAX;
    int n = argc < cap ? argc : cap;
    for (int i = 0; i < n; ++i) {
        const char *src = argv[i];
        if (src == NULL) {
            usermode_arg_storage[i][0] = '\0';
            continue;
        }
        x86_64_size_t len = k_strlen_u(src);
        if (len >= X86_64_USER_ARG_MAX) {
            len = X86_64_USER_ARG_MAX - 1;
        }
        for (x86_64_size_t j = 0; j < len; ++j) {
            usermode_arg_storage[i][j] = src[j];
        }
        usermode_arg_storage[i][len] = '\0';
    }
    usermode_arg_count = n;
}

/*
 * H.5a envp snapshot helpers, mirror of set_args/clear_args/pending_argc.
 *
 * Same single-threaded-by-construction rationale: today only the BSP
 * touches this on the spawn path or while executing SYS_EXEC under a
 * trap gate. When SMP scheduling lands these tables will migrate into
 * the PCB along with argv.
 */
void arch_x86_64_usermode_clear_envs(void) {
    usermode_env_count = 0;
    for (unsigned i = 0; i < X86_64_USER_ENVP_MAX; ++i) {
        usermode_env_storage[i][0] = '\0';
    }
}

int arch_x86_64_usermode_pending_envc(void) {
    return usermode_env_count;
}

void arch_x86_64_usermode_set_envs(int envc, const char *const *envp) {
    arch_x86_64_usermode_clear_envs();
    if (envc <= 0 || envp == NULL) {
        return;
    }
    int cap = (int)X86_64_USER_ENVP_MAX;
    int n = envc < cap ? envc : cap;
    for (int i = 0; i < n; ++i) {
        const char *src = envp[i];
        if (src == NULL) {
            usermode_env_storage[i][0] = '\0';
            continue;
        }
        x86_64_size_t len = k_strlen_u(src);
        if (len >= X86_64_USER_ENV_MAX) {
            len = X86_64_USER_ENV_MAX - 1;
        }
        for (x86_64_size_t j = 0; j < len; ++j) {
            usermode_env_storage[i][j] = src[j];
        }
        usermode_env_storage[i][len] = '\0';
    }
    usermode_env_count = n;
}

/*
 * Compose the SysV program-startup frame at the top of the user stack.
 *
 * Stack image (high address at top):
 *   ...untouched user-stack reserve...
 *   stack_top_in -> +---------------------+
 *                   | argv strings (NUL)  |  copied top-down, packed
 *                   +---------------------+
 *                   | 8-byte zero pad     |  (alignment scratch)
 *                   +---------------------+
 *                   | argv[N] = NULL      |
 *                   | argv[N-1]           |
 *                   | ...                 |
 *                   | argv[0]             |
 *                   +---------------------+
 *                   | argc (64-bit)       |  <- returned RSP, % 16 == 0
 *                   +---------------------+
 *
 * We round the final RSP down to a 16-byte boundary so that crt0.S's
 * `andq $-16, %rsp` is a no-op and `call openos64_start` lands inside
 * the C ABI's usual rsp % 16 == 8 contract.
 */
x86_64_virt_addr_t arch_x86_64_usermode_seed_user_stack(x86_64_virt_addr_t stack_top_in) {
    return arch_x86_64_usermode_seed_user_stack_ex(stack_top_in, 0);
}

x86_64_virt_addr_t arch_x86_64_usermode_seed_user_stack_ex(
    x86_64_virt_addr_t stack_top_in,
    x86_64_virt_addr_t pointer_va_bias) {
    if (stack_top_in == 0) {
        return 0;
    }

    int argc = usermode_arg_count;
    if (argc < 0) argc = 0;
    if (argc > (int)X86_64_USER_ARGV_MAX) argc = (int)X86_64_USER_ARGV_MAX;

    int envc = usermode_env_count;
    if (envc < 0) envc = 0;
    if (envc > (int)X86_64_USER_ENVP_MAX) envc = (int)X86_64_USER_ENVP_MAX;

    /* 1) Copy env strings to the very top of the user stack first, then
     *    argv strings below them. Both grow downward. */
    uint8_t *sp = (uint8_t *)(uintptr_t)stack_top_in;

    uintptr_t env_va[X86_64_USER_ENVP_MAX];
    for (int i = envc - 1; i >= 0; --i) {
        const char *s = usermode_env_storage[i];
        x86_64_size_t len = k_strlen_u(s) + 1; /* incl. NUL */
        sp -= len;
        for (x86_64_size_t j = 0; j < len; ++j) {
            sp[j] = (uint8_t)s[j];
        }
        env_va[i] = (uintptr_t)sp;
    }

    uintptr_t arg_va[X86_64_USER_ARGV_MAX];
    for (int i = argc - 1; i >= 0; --i) {
        const char *s = usermode_arg_storage[i];
        x86_64_size_t len = k_strlen_u(s) + 1; /* incl. NUL */
        sp -= len;
        for (x86_64_size_t j = 0; j < len; ++j) {
            sp[j] = (uint8_t)s[j];
        }
        arg_va[i] = (uintptr_t)sp;
    }

    /* 2) 16-byte alignment scratch so the final RSP lands on a 16-byte
     *    boundary regardless of how many slots / bytes we pushed.
     *
     *    H.5a math: after the base align we push, top-down:
     *      envp NULL terminator   : 1 slot
     *      envp[envc-1 .. 0]      : envc slots
     *      argv NULL terminator   : 1 slot
     *      argv[argc-1 .. 0]      : argc slots
     *      argc                   : 1 slot
     *    Total = 8 * (argc + envc + 3) bytes. For 16-alignment we need
     *    (argc + envc + 3) to be even, i.e. (argc + envc) odd. When the
     *    sum is even we prepend an 8-byte zero pad here (lives above
     *    the envp NULL terminator, doesn't affect any pointer slot). */
    uintptr_t cursor = (uintptr_t)sp;
    cursor &= ~((uintptr_t)15);
    if (((argc + envc) & 1) == 0) {
        cursor -= 8;
        *(uint64_t *)cursor = 0;
    }

    /* 3) envp[envc] = NULL terminator, then envp[envc-1..0]. */
    cursor -= 8;
    *(uint64_t *)cursor = 0;
    for (int i = envc - 1; i >= 0; --i) {
        cursor -= 8;
        /* P4.c.1: expose env_va[i] to ring3 through pointer_va_bias. The
         * kernel-side write target (cursor) stays unbiased. */
        *(uint64_t *)cursor = (uint64_t)(env_va[i] + (uintptr_t)pointer_va_bias);
    }
    uintptr_t envp_base = cursor; /* envp[0] (or terminator when envc==0) */

    /* 4) argv[argc] = NULL terminator, then argv[argc-1..0]. */
    cursor -= 8;
    *(uint64_t *)cursor = 0;
    for (int i = argc - 1; i >= 0; --i) {
        cursor -= 8;
        /* P4.c.1: expose arg_va[i] to ring3 through pointer_va_bias. */
        *(uint64_t *)cursor = (uint64_t)(arg_va[i] + (uintptr_t)pointer_va_bias);
    }
    uintptr_t argv_base = cursor; /* argv[0] */

    /* 5) push argc; (rsp) reads it in crt0.S. Final SysV layout for crt0:
     *      0(rsp)                       = argc
     *      8(rsp)                       = argv[0]
     *      8 + (argc+1)*8 (rsp)         = envp[0]
     */
    cursor -= 8;
    *(uint64_t *)cursor = (uint64_t)(uint32_t)argc;

    /* sanity: argv_base == cursor + 8, envp_base == argv_base + (argc+1)*8. */
    (void)argv_base;
    (void)envp_base;

    /* 6) final alignment check: SysV mandates rsp % 16 == 0 *at* _start. */
    if ((cursor & 15ULL) != 0ULL) {
        /* Should never happen given the design but bail gracefully. */
        return 0;
    }

    early_console64_write("[x86_64][usermode] seed_user_stack argc=");
    early_console64_write_hex64((uint64_t)(uint32_t)argc);
    early_console64_write(" envc=");
    early_console64_write_hex64((uint64_t)(uint32_t)envc);
    early_console64_write(" rsp=");
    early_console64_write_hex64((uint64_t)cursor);
    early_console64_write(" bias=");
    early_console64_write_hex64((uint64_t)pointer_va_bias);
    early_console64_write("\n");

    /* H.5a: arg+env tables are one-shot per usermode_run/exec round. Clear
     * them here so a subsequent run/exec that forgets to set them sees a
     * clean argc=0 envc=0 frame. */
    arch_x86_64_usermode_clear_args();
    arch_x86_64_usermode_clear_envs();

    /* P4.c.1: return the ring3-visible RSP (kernel did all writes via the
     * unbiased cursor, but ring3 enters with rsp = cursor + bias). */
    return (x86_64_virt_addr_t)(cursor + (uintptr_t)pointer_va_bias);
}

/* =====================================================================
 * H.7: reusable program launcher (shell-facing entry point)
 * ---------------------------------------------------------------------
 * arch_x86_64_usermode_launch_path() wraps the full "load an ELF, build
 * a fresh address space, spawn a user PCB, drop to ring3, and drive the
 * execve/fork rounds until the program tree exits" flow that the boot
 * path (kernel64.c) hand-rolls for /bin/launcher. Factoring it here lets
 * the interactive shell start arbitrary programs by path.
 *
 * Image resolution mirrors do_exec(): VFS/RAMFS first (disk-backed,
 * user-droppable), then the compiled-in initrd as a fallback.
 *
 * Returns the program's exit code (>=0), or -1 on load/spawn failure.
 * ===================================================================== */
int arch_x86_64_usermode_launch_path(const char *path,
                                     int argc, const char **argv,
                                     int envc, const char **envp)
{
    if (path == ((const char *)0) || path[0] == '\0') {
        return -1;
    }

    /* --- resolve image: VFS first, initrd fallback --- */
    const uint8_t *img_data = (const uint8_t *)0;
    uint64_t       img_size = 0;
    void          *vfs_buf  = (void *)0;
    {
        inode_t st;
        if (vfs_stat(path, &st) == 0 && (st.mode & FS_FILE) && st.size > 0) {
            void *buf = arch_x86_64_kmalloc((x86_64_size_t)st.size);
            if (buf != (void *)0) {
                int fd = vfs_open(path, 0, 0);
                if (fd >= 0) {
                    int got = vfs_read(fd, buf, (uint32_t)st.size);
                    vfs_close(fd);
                    if (got == (int)st.size) {
                        vfs_buf  = buf;
                        img_data = (const uint8_t *)buf;
                        img_size = (uint64_t)st.size;
                    } else {
                        arch_x86_64_kfree(buf);
                    }
                } else {
                    arch_x86_64_kfree(buf);
                }
            }
        }
    }
    if (img_data == (const uint8_t *)0) {
        const x86_64_initrd_file_t *file = arch_x86_64_initrd_find(path);
        if (file == ((const x86_64_initrd_file_t *)0)) {
            early_console64_write("[x86_64][launch] ENOENT path=");
            early_console64_write(path);
            early_console64_write("\n");
            return -1;
        }
        img_data = file->data;
        img_size = file->size;
    }

    /* --- build fresh AS + load ELF --- */
    x86_64_address_space_t *as = arch_x86_64_as_create();
    elf64_load_result_t lr =
        arch_x86_64_elf64_load_image_into(img_data, img_size, as);
    if (vfs_buf != (void *)0) {
        arch_x86_64_kfree(vfs_buf);
        vfs_buf = (void *)0;
    }
    if (lr.status != ELF64_LOADER_OK) {
        early_console64_write("[x86_64][launch] elf-load-failed path=");
        early_console64_write(path);
        early_console64_write("\n");
        if (as != ((x86_64_address_space_t *)0)) {
            arch_x86_64_as_destroy(as);
        }
        return -1;
    }

    /* --- spawn PCB + bind AS --- */
    (void)arch_x86_64_proc_spawn_user(path);
    if (as != ((x86_64_address_space_t *)0)) {
        arch_x86_64_proc_current_set_as(as);
    }

    /* --- seed argv/envp (default argv[0]=path when caller passes none) --- */
    if (argc > 0 && argv != ((const char **)0)) {
        arch_x86_64_usermode_set_args(argc, argv);
    } else {
        const char *default_argv[2] = { path, (const char *)0 };
        arch_x86_64_usermode_set_args(1, default_argv);
    }
    if (envc > 0 && envp != ((const char **)0)) {
        arch_x86_64_usermode_set_envs(envc, envp);
    }

    /* --- drive ring3 with execve/fork rounds (mirrors boot loop) --- */
    x86_64_entry_t next_entry = lr.entry;
    const int kExecRoundCap = 8;
    int round = 0;
    for (;;) {
        (void)arch_x86_64_usermode_run(next_entry);
        if (arch_x86_64_usermode_has_pending_exec()) {
            next_entry = arch_x86_64_usermode_take_pending_exec();
            if (++round >= kExecRoundCap) {
                early_console64_write("[x86_64][launch] exec-round cap hit\n");
                break;
            }
            continue;
        }
        break;
    }

    int code = 0;
    if (arch_x86_64_usermode_has_exited()) {
        code = arch_x86_64_usermode_exit_code();
    }
    return code;
}
