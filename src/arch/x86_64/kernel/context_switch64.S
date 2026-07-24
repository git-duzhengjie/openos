.section .text
.code64

.global arch_x86_64_context_switch
.extern arch_x86_64_sched_note_switch

/*
 * void arch_x86_64_context_switch(x86_64_context_t *from, x86_64_context_t *to)
 *
 * x86_64_context_t layout:
 *   0x00 rsp
 *   0x08 rip
 *   0x10 rflags
 *   0x18 rbx
 *   0x20 rbp
 *   0x28 r12
 *   0x30 r13
 *   0x38 r14
 *   0x40 r15
 *   0x48 r8
 *   0x50 r9
 *   0x58 r10
 *   0x60 r11
 *   0x68 swapgs_needed  ; γ.5 P3: 1=swapgs already executed, 0=not executed
 *
 * γ.5 P3 root cause: When an IRQ fires from user mode, the ISR entry does swapgs to
 * switch to kernel GS. If the ISR handler triggers preemption and context_switch
 * is called, the outgoing thread's swapgs state is NOT saved. When the thread is
 * later resumed, the ISR exit path checks the *original* CS in the iretq frame,
 * but GS may be in the wrong state (because another thread may have modified it).
 *
 * Solution: Save/restore swapgs state across context_switch. The ISR entry path
 * records whether swapgs was executed into the per-CPU slot. Before context_switch,
 * we save this state into the outgoing thread's context. After switching to the
 * incoming thread, we restore its swapgs state.
 *
 * Note: swapgs is only meaningful for user threads; kernel threads always run
 * with kernel GS, so their swapgs_needed is always 0.
 */
arch_x86_64_context_switch:
    test %rdi, %rdi
    jz 1f

    mov %rsp, 0x00(%rdi)
    leaq 2f(%rip), %rax
    mov %rax, 0x08(%rdi)
    pushfq
    popq %rax
    /* Step F.3: force IF=1 in the *saved* rflags. When an IRQ handler
     * (CPL=0, IF=0) calls into the scheduler, the live rflags has IF=0;
     * if we stored that as-is the preempted thread would resume with
     * interrupts disabled and never be preempted again. Always store
     * IF=1 so every resume reopens the preemption window. */
    orq  $0x200, %rax
    mov %rax, 0x10(%rdi)

    mov %rbx, 0x18(%rdi)
    mov %rbp, 0x20(%rdi)
    mov %r12, 0x28(%rdi)
    mov %r13, 0x30(%rdi)
    mov %r14, 0x38(%rdi)
    mov %r15, 0x40(%rdi)
    mov %r8,  0x48(%rdi)
    mov %r9,  0x50(%rdi)
    mov %r10, 0x58(%rdi)
    mov %r11, 0x60(%rdi)

    /* γ.5 P3: Save swapgs_needed state from per-CPU slot.
     * The ISR entry path sets this flag when swapgs is executed.
     * We store it in the outgoing thread's context so that when
     * the thread is later resumed, we know whether to expect
     * swapgs to have been done for its ISR exit path. */
    pushq %rsi
    mov %gs:0, %rax          /* rax = arch_x86_64_this_cpu_ptr() */
    mov 0x68(%rdi), %rax     /* swapgs_needed = percpu->swapgs_state */
    popq %rsi

1:
    pushq %rsi
    mov %rsi, %rdi
    call arch_x86_64_sched_note_switch
    popq %rsi

    mov 0x18(%rsi), %rbx
    mov 0x20(%rsi), %rbp
    mov 0x28(%rsi), %r12
    mov 0x30(%rsi), %r13
    mov 0x38(%rsi), %r14
    mov 0x40(%rsi), %r15
    mov 0x48(%rsi), %r8
    mov 0x50(%rsi), %r9
    mov 0x58(%rsi), %r10
    mov 0x60(%rsi), %r11

    mov 0x00(%rsi), %rsp
    mov 0x10(%rsi), %rax
    pushq %rax
    popfq

    /* γ.5 P3: Restore swapgs_needed state to per-CPU slot.
     * When the incoming thread is resumed, its ISR exit path
     * needs to know whether swapgs was done at entry time.
     * Note: For kernel threads and threads not in ISR, this is 0.
     * Also, if the current swapgs state doesn't match the incoming
     * thread's state, we need to execute swapgs here to synchronize.
     *
     * However: GS_BASE is the percpu pointer itself. If we swapgs
     * here, the next %gs: access would read user GS which is NOT
     * a valid percpu pointer. So we only restore the *flag* into
     * the percpu slot for the ISR exit path to check; the actual
     * GS state is handled by the paired ISR entry/exit swapgs.
     */
    mov 0x68(%rsi), %rax
    mov %rax, %gs:8           /* percpu->swapgs_state = incoming swapgs_needed */

    mov 0x08(%rsi), %rax
    jmp *%rax

2:
    ret
