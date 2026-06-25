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
