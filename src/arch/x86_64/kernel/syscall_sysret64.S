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
     * G.7b: re-introduce the swapgs pair removed in D.2. By the time we
     * reach this stub, percpu_install_gs() has already wrmsr'd both
     * IA32_GS_BASE and IA32_KERNEL_GS_BASE to &g_percpu[cpu_idx]. That makes
     * the post-swapgs %gs:0 layout valid (it is the same percpu pointer),
     * which fixes the original D.2 hazard. Note we are still running on
     * the user stack here -- a proper per-CPU TSS RSP0 switch is Step G.7c.
     *
     * No conditional check needed: SYSCALL only ever traps from ring 3.
     */
    swapgs
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
    /* G.7b: pair swapgs back to user GS before sysretq. */
    swapgs
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
