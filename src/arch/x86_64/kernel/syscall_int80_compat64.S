.section .text,"ax"
.code64

.global x86_64_int80_compat_entry
.extern arch_x86_64_int80_dispatch

/*
 * Compatibility int 0x80 entry for the x86_64 bring-up phase.
 * Saves the volatile and extended register set, calls the C dispatcher with
 * x86_64_int80_frame_t *, returns dispatcher result in rax, then iretq.
 */
x86_64_int80_compat_entry:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    cld
    call arch_x86_64_int80_dispatch
    movq %rax, 0x70(%rsp)

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq
