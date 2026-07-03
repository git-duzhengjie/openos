.section .text,"ax"
.extern arch_x86_64_exception_dispatch
.extern arch_x86_64_pit_irq0_handler
.extern arch_x86_64_lapic_timer_irq_handler
.extern arch_x86_64_lapic_resched_irq_handler

/*
 * G.7b — swapgs scaffolding.
 *
 * Goal: establish the swapgs entry/exit pairing on every ring-crossing path
 * (ISR common, IRQ stubs, syscall) without yet introducing a divergent
 * KERNEL_GS_BASE. percpu_install_gs() currently writes both IA32_GS_BASE and
 * IA32_KERNEL_GS_BASE to the same &g_percpu[cpu_idx] value, so each swapgs
 * here is semantically a no-op (the MSRs swap to identical contents). What
 * we gain is the discipline: when G.7c later flips IA32_KERNEL_GS_BASE to
 * the "user GS" (= 0) model, every entry/exit pair already exists and we
 * only have to change percpu_install_gs(), not retro-fit every stub.
 *
 * The SWAPGS_IF_FROM_USER macro inspects the saved CS selector on the stack
 * (low 2 bits = RPL) and only runs swapgs when CS came from ring 3. Each
 * call site must pass the byte offset of the saved CS relative to current
 * %rsp. We use byte offsets because the saved CS is always 8 bytes wide and
 * 8-byte-aligned in the long-mode IRET frame, so testb on the low byte is
 * sufficient to inspect RPL.
 *
 * NOTE: NMI/MCE deliberately do not get swapgs. Per Linux, those vectors
 * need the IST + GS_BASE-range comparison dance to handle the swapgs window
 * race, and our NMI path currently only increments a counter without touching
 * percpu data, so leaving it alone is the safe choice for now.
 */
.macro SWAPGS_IF_FROM_USER cs_off
    testb $3, \cs_off(%rsp)
    jz 1001f
    swapgs
1001:
.endm

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
    /* G.7b: swap to kernel GS if we interrupted ring3. Must happen before
     * any push so a later C handler that uses %gs:0 sees the percpu pointer.
     * Hardware iret frame is intact: CS sits at (%rsp+8). */
    SWAPGS_IF_FROM_USER 8
    /* Now save the SysV caller-saved set and dispatch to C. The C handler is
     * responsible for bumping g_pit_ticks and sending the PIC EOI.
     *
     * gamma.5-P1: forward the interrupted CS to arch_x86_64_pit_irq0_handler
     * so BSP can also record ring3-vs-ring0 preempt hits (same story as the
     * LAPIC timer stub above). CS lives at (%rsp + 9*8 + 8) = 80(%rsp)
     * after the 9 caller-saved pushes below. */
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
    movq 80(%rsp), %rdi
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
    /* G.7b: pair swapgs back before iretq. CS is at (%rsp+8) again because
     * the caller-saved frame has been fully unwound. */
    SWAPGS_IF_FROM_USER 8
    iretq

.section .text,"ax"

/* G.6.5a — LAPIC timer IRQ stub (vector OPENOS_X86_64_LAPIC_TIMER_VECTOR=0x40).
 * Per-CPU periodic timer driving the AP scheduler heartbeat. The hardware
 * does not push an error code (external interrupt). We save the SysV
 * caller-saved set, call the C handler, restore, and iretq. The C handler
 * is responsible for sending the LAPIC EOI.
 *
 * gamma.5-P1: forward the interrupted CS to the C handler in %rdi so it
 * can classify user (CS.RPL==3) vs kernel (CS.RPL==0) preempts and bump
 * the per-CPU tick_hits_user / tick_hits_kernel histogram. After the
 * SWAPGS + 9-reg caller-saved push, the iret frame's CS sits at
 * (%rsp + 9*8 + 8) = 80(%rsp). RIP is at 72(%rsp) but we don't need it. */
.global x86_64_irq_lapic_timer
x86_64_irq_lapic_timer:
    /* G.7b: see x86_64_irq0 for rationale. */
    SWAPGS_IF_FROM_USER 8
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
    /* gamma.5-P1: 1st arg = interrupted CS. */
    movq 80(%rsp), %rdi
    call arch_x86_64_lapic_timer_irq_handler
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rax
    SWAPGS_IF_FROM_USER 8
    iretq

/* G.6.6a — LAPIC reschedule-IPI stub (vector 0x41).
 * Cross-CPU "poke" signal sent via fixed-delivery IPI. Same caller-saved
 * frame and EOI-in-C contract as the timer stub. The handler bumps the
 * per-CPU resched_ipi_count and EOIs; the actual reschedule decision is
 * deferred to the next timer tick. */
.global x86_64_irq_lapic_resched
x86_64_irq_lapic_resched:
    /* G.7b: see x86_64_irq0 for rationale. */
    SWAPGS_IF_FROM_USER 8
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
    call arch_x86_64_lapic_resched_irq_handler
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rax
    SWAPGS_IF_FROM_USER 8
    iretq

.global x86_64_isr_common
x86_64_isr_common:
    /* G.7b: at this point the stack has err_code(0)/vector(8)/RIP(16)/CS(24).
     * If we crossed from ring3, swap %gs so kernel code sees the percpu GS.
     * (Currently a no-op because KERNEL_GS_BASE == GS_BASE; the path is
     * established for G.7c when the two MSRs will diverge.) */
    SWAPGS_IF_FROM_USER 24
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
    /* G.7b: same stack shape as entry (err_code(0)/vector(8)/RIP(16)/CS(24)).
     * Pair the swapgs back to user GS before iretq if returning to ring3. */
    SWAPGS_IF_FROM_USER 24
    addq $16, %rsp
    iretq
