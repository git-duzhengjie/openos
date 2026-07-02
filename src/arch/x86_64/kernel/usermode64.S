.section .text,"ax"
.code64

/* Must match OPENOS_X86_64_PERCPU_OFF_USER_DISPATCH in percpu64.h.
 * percpu64.c carries a _Static_assert tying the struct field offset
 * to that constant, so any drift here is caught at compile time. */
#define PERCPU_OFF_USER_DISPATCH 0x78

.global arch_x86_64_iretq_enter_user
.global arch_x86_64_iretq_enter_user_full
.global arch_x86_64_user_thread_trampoline
.global arch_x86_64_user_thread_trampoline_full
.global arch_x86_64_user_thread_sentinel_trampoline

/*
 * G.7e: arch_x86_64_user_thread_trampoline
 *
 * Invoked via context_switch when a USER slot is dispatched. The slot's
 * ctx.rsp points at a pre-built iretq frame (5 qwords: rip, cs, rflags,
 * user_rsp, ss); ctx.rip is &arch_x86_64_user_thread_trampoline; ctx.rflags
 * is 0 (IF=0) so the dispatch sequence (pushq %rax / popfq / jmp *rax) does
 * not re-enable interrupts before iretq. swapgs flips KERNEL_GS_BASE/GS_BASE
 * to the user-side mapping (currently same value, G.7c no-op), then iretq
 * atomically restores user CS:RIP, RFLAGS (IF=1 from the prebuilt frame),
 * and SS:RSP. Entry rsp already points exactly at the iretq frame; no
 * adjustment needed.
 */
arch_x86_64_user_thread_trampoline:
    /* G.7e-3: bump per-CPU dispatch counter before crossing to ring3.
     * %gs at this point is still kernel GS (KERNEL_GS_BASE was inactive
     * since we never SWAPGS'd in this dispatch path). swapgs below then
     * flips to user GS just before iretq. */
    incq %gs:PERCPU_OFF_USER_DISPATCH
    swapgs
    iretq


/*
 * G.7e-3: arch_x86_64_user_thread_sentinel_trampoline
 *
 * Diagnostic-only trampoline used by Stage 21 (smp_selftest) and other
 * scaffolding that wants to verify the spawn/dispatch path WITHOUT a real
 * ring3 transition. The dispatched USER slot's ctx.rip is set to here;
 * we bump the same per-CPU counter so the same observer (
 * arch_x86_64_percpu_user_dispatch_count) covers both real and sentinel
 * variants, then halt this CPU. The slot stays "running" forever on its
 * owner CPU; the BSP / other APs are unaffected.
 *
 * Interrupts are masked here (ctx.rflags=0 ensured by the spawn helper),
 * so the hlt loop is a hard freeze of this CPU only. The caller must
 * therefore pick a CPU it is willing to retire (typically a spare AP
 * picked by the test).
 */
arch_x86_64_user_thread_sentinel_trampoline:
    incq %gs:PERCPU_OFF_USER_DISPATCH
1:
    cli
    hlt
    jmp 1b


/*
 * gamma.3b-S2a Seg-4: arch_x86_64_user_thread_trampoline_full
 *
 * Fork-resume dispatch entry. Same contract as arch_x86_64_user_thread_
 * trampoline (context_switch lands here with %gs=kernel_gs, IF=0,
 * ctx.rsp already pointing at a caller-built frame), but the frame is
 * 11 qwords instead of 5:
 *
 *   [ ctx.rsp ] ->  0x00 rbx    (callee-saved to restore)
 *                   0x08 rbp
 *                   0x10 r12
 *                   0x18 r13
 *                   0x20 r14
 *                   0x28 r15
 *                   0x30 rip    (iretq consumes from here down)
 *                   0x38 cs
 *                   0x40 rflags
 *                   0x48 user_rsp
 *                   0x50 ss
 *
 * We pop the six callee-saved slots off into their registers, which
 * naturally advances rsp to the 5-qword iretq frame, then zero the
 * caller-saved GPRs so the child observes rax==0 (fork-returns-0),
 * bump the per-CPU dispatch counter, swapgs, iretq. Identical exit
 * sequence to iretq_enter_user_full -- this is just its
 * context_switch-callable twin. */
arch_x86_64_user_thread_trampoline_full:
    popq %rbx
    popq %rbp
    popq %r12
    popq %r13
    popq %r14
    popq %r15

    /* Zero caller-saved so child sees fork()==0 and clean scratch regs.
     * We match the ordering / set used by iretq_enter_user_full above
     * so any future audit sees one canonical child-entry state. */
    xorq %rax, %rax
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10
    xorq %r11, %r11

    incq %gs:PERCPU_OFF_USER_DISPATCH
    swapgs
    iretq


/*
 * void arch_x86_64_iretq_enter_user(const x86_64_user_iretq_frame_t *frame)
 *
 * Frame layout:
 *   0x00 rip
 *   0x08 cs
 *   0x10 rflags
 *   0x18 rsp
 *   0x20 ss
 *
 * iretq consumes stack entries in this order: rip, cs, rflags, rsp, ss.
 */
arch_x86_64_iretq_enter_user:
    test %rdi, %rdi
    jz 1f

    movq 0x20(%rdi), %rax
    movw %ax, %ds
    movw %ax, %es

    pushq 0x20(%rdi)
    pushq 0x18(%rdi)
    pushq 0x10(%rdi)
    pushq 0x08(%rdi)
    pushq 0x00(%rdi)

    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rbp, %rbp
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15
    /*
     * G.7b: swap to "user GS" (currently still == percpu, because
     * percpu_install_gs() programs KERNEL_GS_BASE to the same value). The
     * kernel-side counterpart on every ring0 entry (ISR_NOERR, ISR_ERR,
     * irq*, syscall) will swapgs back. Establishing the path here lets G.7c
     * later flip KERNEL_GS_BASE to 0 without touching this stub.
     */
    swapgs
    iretq

1:
    hlt
    jmp 1b


/*
 * void arch_x86_64_iretq_enter_user_full(const x86_64_user_iretq_frame_t *frame)
 *
 * A2.P3-B-β: fork-resume variant. Identical to arch_x86_64_iretq_enter_user
 * for the iretq portion, but additionally restores callee-saved registers
 * from the extended frame slots before crossing to ring3. Caller-saved
 * registers (rax/rcx/rdx/rsi/rdi/r8-r11) are still zeroed so the child
 * observes rax == 0 (the fork-returns-0-in-child convention).
 *
 * Frame layout:
 *   0x00 rip
 *   0x08 cs
 *   0x10 rflags
 *   0x18 rsp
 *   0x20 ss
 *   0x28 rbx
 *   0x30 rbp
 *   0x38 r12
 *   0x40 r13
 *   0x48 r14
 *   0x50 r15
 */
arch_x86_64_iretq_enter_user_full:
    test %rdi, %rdi
    jz 1f

    movq 0x20(%rdi), %rax
    movw %ax, %ds
    movw %ax, %es

    pushq 0x20(%rdi)
    pushq 0x18(%rdi)
    pushq 0x10(%rdi)
    pushq 0x08(%rdi)
    pushq 0x00(%rdi)

    /* Restore callee-saved from the extended frame slots. */
    movq 0x28(%rdi), %rbx
    movq 0x30(%rdi), %rbp
    movq 0x38(%rdi), %r12
    movq 0x40(%rdi), %r13
    movq 0x48(%rdi), %r14
    movq 0x50(%rdi), %r15

    /* Zero caller-saved so child sees fork()==0 and clean scratch regs. */
    xorq %rax, %rax
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    xorq %r11, %r11

    swapgs
    iretq

1:
    hlt
    jmp 1b
