.section .text,"ax"
.code64

.global x86_64_syscall_entry
.global arch_x86_64_usermode_syscall_return_trampoline
.extern arch_x86_64_syscall_dispatch
.extern arch_x86_64_smap_on

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
    movq %rsp, %gs:0x68          /* stash user rsp in scratch slot */
    movq %gs:0x60, %rsp          /* load kernel rsp (== TSS.RSP0) */

    /*
     * L5 FIX: the per-CPU syscall_user_rsp slot (%gs:0x68) is a SINGLE
     * slot shared by all threads on this CPU. If this syscall blocks
     * (e.g. futex_wait) and cooperatively switches to another thread
     * that itself enters syscall, that thread overwrites %gs:0x68.
     * When we later resume and reach the epilogue, restoring rsp from
     * %gs:0x68 would load the OTHER thread's user rsp -> sysret onto the
     * wrong user stack -> crash. Move the user rsp onto THIS thread's
     * kernel stack (per-thread, immune to cross-thread clobber) so it
     * is preserved across any context switch.
     */
    pushq %gs:0x68              /* [saved user rsp on kernel stack] */

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
    /*
     * M6.4.4 SMAP: dispatch 会访问用户内存(用户指针参数)。开启 SMAP 后
     * 内核默认 AC=0 无法访问 user 页, 故在 dispatch 前 STAC 打开窗口、
     * 返回后 CLAC 关闭。rax 此时可自由破坏(syscall 号已在栈上)。
     * 由 arch_x86_64_smap_on 门控: SMAP 未启用时跳过以免 #UD。
     * STAC = 0f 01 cb。
     */
    movl arch_x86_64_smap_on(%rip), %eax
    testl %eax, %eax
    jz 2f
    .byte 0x0f,0x01,0xcb        /* stac */
2:
    call arch_x86_64_syscall_dispatch
    movq %rax, 0x00(%rsp)
    /* M6.4.4 SMAP: 关闭 user-access 窗口。CLAC = 0f 01 ca。
     * 返回值已存入 0x00(%rsp), 此处可自由破坏 rax, 随后 popq %rax 恢复。 */
    movl arch_x86_64_smap_on(%rip), %eax
    testl %eax, %eax
    jz 3f
    .byte 0x0f,0x01,0xca        /* clac */
3:

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
     * L5 FIX: restore the user rsp we pushed onto the kernel stack at
     * entry (see comment above). This is per-thread and cannot have
     * been clobbered by another thread's syscall, unlike the old
     * `movq %gs:0x68, %rsp` which read a shared per-CPU slot.
     */
    popq %rsp                   /* restore user rsp from kernel stack */
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
