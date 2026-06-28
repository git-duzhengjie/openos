.section .text,"ax"
.code64

.global arch_x86_64_iretq_enter_user
.global arch_x86_64_user_thread_trampoline

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
