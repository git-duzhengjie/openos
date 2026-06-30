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
     * which fixes the original D.2 hazard.
     *
     * No conditional check needed: SYSCALL only ever traps from ring 3.
     */
    swapgs

    /*
     * G.7c: swap from the (untrusted) user stack to this CPU's kernel
     * stack BEFORE we touch memory. We park the user %rsp in the
     * percpu save-slot so the sysretq exit path can restore it.
     *
     * Hardware does NOT do this for us on `syscall` (unlike int/iretq,
     * which consults TSS.RSP0). Doing it manually here is what lets
     * us drop the old "keep using the user stack" hack and start
     * routing syscall through a trusted per-CPU stack -- the same
     * stack TSS.RSP0 will deliver interrupts onto, so an IRQ taken
     * mid-syscall is now safe.
     *
     * The displacements 0x68 (syscall_user_rsp) and 0x60
     * (syscall_kernel_rsp) are pinned by _Static_assert in percpu64.c.
     */
    movq %rsp, %gs:0x68          /* save user rsp */
    movq %gs:0x60, %rsp          /* load kernel rsp (== TSS.RSP0) */

    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbp
    pushq %rbx
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
    popq %rbx
    popq %rbp
    popq %r12
    popq %r13
    popq %r14
    popq %r15

    /*
     * G.7c: restore user stack pointer that we saved on entry, then
     * pair swapgs back to user GS and sysretq.
     */
    movq %gs:0x68, %rsp
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
