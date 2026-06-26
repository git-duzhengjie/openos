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
    mov 0x08(%rsi), %rax
    jmp *%rax

2:
    ret
