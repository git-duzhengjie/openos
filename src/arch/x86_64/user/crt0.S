/*
 * H.5a: SysV-style program entry stub.
 *
 * On entry the kernel arranges the user stack so that:
 *     (%rsp)                       = argc   (int, zero-extended to 64-bit)
 *     8(%rsp)                      = argv[0]
 *     8(%rsp) + (argc+1)*8         = envp[0]
 *
 * We need to compute envp = argv + argc + 1 and load it into %rdx,
 * the third SysV register, before tail-calling openos64_start.
 * argc/argv stay in %rdi/%rsi as before.
 *
 * The ABI mandates rsp % 16 == 0 immediately before the call; the
 * call pushes the 8-byte return address making rsp % 16 == 8 inside
 * openos64_start, which matches every other SysV C function we emit.
 */

.section .text,"ax"
.code64

.global _start
.extern openos64_start

_start:
    xorq %rbp, %rbp
    movq (%rsp), %rdi          /* argc                          */
    leaq 8(%rsp), %rsi         /* argv = &argv[0]              */
    /* envp = argv + argc + 1  (skip argv[argc] = NULL)         */
    movq %rdi, %rax
    incq %rax                  /* rax = argc + 1                */
    leaq (%rsi,%rax,8), %rdx   /* rdx = argv + (argc+1)*8       */
    andq $-16, %rsp
    call openos64_start
1:
    hlt
    jmp 1b
