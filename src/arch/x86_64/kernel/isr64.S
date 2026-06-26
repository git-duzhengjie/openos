.section .text,"ax"
.extern arch_x86_64_exception_dispatch
.extern arch_x86_64_pit_irq0_handler

.macro ISR_NOERR vector
.global x86_64_isr\vector
x86_64_isr\vector:
    pushq $0
    pushq $\vector
    jmp x86_64_isr_common
.endm

.macro ISR_ERR vector
.global x86_64_isr\vector
x86_64_isr\vector:
    pushq $\vector
    jmp x86_64_isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

/* Step F.2 — IRQ0 / vector 0x20 dedicated stub.
 * Hardware does not push an error code for external interrupts, and we do
 * NOT route this through the common exception path (no frame layout, no
 * panic). We save the SysV caller-saved set, call the C handler, restore,
 * and iretq. The PIC EOI is sent inside the C handler. */
.global x86_64_irq0
x86_64_irq0:
    /* Diagnostic: snapshot the hardware iret frame to a fixed memory region
     * so we can examine it post-mortem if anything goes wrong. The stack
     * shape on entry is (top-down):
     *  [%rsp+0]   hw-pushed RIP
     *  [%rsp+8]   hw-pushed CS
     *  [%rsp+16]  hw-pushed RFLAGS
     *  [%rsp+24]  hw-pushed RSP
     *  [%rsp+32]  hw-pushed SS
     */
    pushq %rax
    pushq %rbx
    leaq irq0_iret_dump(%rip), %rbx
    movq 16(%rsp), %rax
    movq %rax, 0(%rbx)    /* RIP    */
    movq 24(%rsp), %rax
    movq %rax, 8(%rbx)    /* CS     */
    movq 32(%rsp), %rax
    movq %rax, 16(%rbx)   /* RFLAGS */
    movq 40(%rsp), %rax
    movq %rax, 24(%rbx)   /* RSP    */
    movq 48(%rsp), %rax
    movq %rax, 32(%rbx)   /* SS     */
    popq %rbx
    popq %rax

    /* Now save the SysV caller-saved set and dispatch to C. The C handler is
     * responsible for bumping g_pit_ticks and sending the PIC EOI. */
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    cld
    call arch_x86_64_pit_irq0_handler
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rax
    iretq

.section .bss,"aw",@nobits
.global irq0_iret_dump
.balign 16
irq0_iret_dump:
    .skip 64
.section .text,"ax"

.global x86_64_isr_common
x86_64_isr_common:
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
    call arch_x86_64_exception_dispatch

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
    addq $16, %rsp
    iretq
