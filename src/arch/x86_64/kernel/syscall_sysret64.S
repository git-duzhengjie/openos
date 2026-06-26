.section .text,"ax"
.code64

.global x86_64_syscall_entry
.global arch_x86_64_usermode_syscall_return_trampoline
.extern arch_x86_64_syscall_dispatch

/*
 * syscall/sysret fast path skeleton.
 * CPU enters with user RIP in rcx and user RFLAGS in r11.  The ABI keeps the
 * syscall number in rax and arguments in rdi/rsi/rdx/r10/r8/r9.
 */
x86_64_syscall_entry:
    /*
     * Step D.2: removed swapgs pair. We had no %gs: accesses anywhere in the
     * kernel and KERNEL_GS_BASE was never wrmsr'd, so the swapgs/swapgs
     * dance only loaded a random GS_BASE during dispatch -- and, more
     * importantly, on the very first syscall the CPU's IA32_KERNEL_GS_BASE
     * was still zero, so the post-swapgs %gs:0 layout was garbage. We
     * currently run on the user stack throughout the syscall (single-stack
     * model); push/pop happens on the user-stack page mapped U=1, which is
     * safe for ring0 because CPL trumps U on supervisor accesses. A proper
     * per-CPU TSS RSP0 switch is tracked as Step D.3.
     */
    pushq %r11
    pushq %rcx
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rax

    movq %rsp, %rdi
    cld
    call arch_x86_64_syscall_dispatch
    movq %rax, 0x00(%rsp)

    popq %rax
    popq %rdx
    popq %rsi
    popq %rdi
    popq %r8
    popq %r9
    popq %r10
    popq %rcx
    popq %r11
    sysretq

/*
 * User crt0 switches its return address to this stub before calling main().
 * If main returns without an explicit openos64_exit(), this trampoline issues
 * one final syscall with the return value still in rax.
 */
arch_x86_64_usermode_syscall_return_trampoline:
    syscall
1:
    hlt
    jmp 1b
