.section .text,"ax"
.code64

.global x86_64_syscall_entry
.extern arch_x86_64_syscall_dispatch

/*
 * syscall/sysret fast path skeleton.
 * CPU enters with user RIP in rcx and user RFLAGS in r11.  The ABI keeps the
 * syscall number in rax and arguments in rdi/rsi/rdx/r10/r8/r9.
 */
x86_64_syscall_entry:
    swapgs
    pushq %rax
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %rcx
    pushq %r11

    movq %rsp, %rdi
    cld
    call arch_x86_64_syscall_dispatch
    movq %rax, 0x40(%rsp)

    popq %r11
    popq %rcx
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rax
    swapgs
    sysretq
