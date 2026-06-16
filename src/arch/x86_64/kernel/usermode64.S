.section .text,"ax"
.code64

.global arch_x86_64_iretq_enter_user

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
    iretq

1:
    hlt
    jmp 1b
