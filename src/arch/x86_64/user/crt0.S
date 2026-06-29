.section .text,"ax"
.code64

/*
 * H.4: SysV-style program entry stub.
 *
 * On entry the kernel arranges the user stack so that:
 *     (%rsp)   = argc   (int, zero-extended to 64-bit)
 *     8(%rsp)  = argv   (char **; the argv vector itself lives just above)
 *
 * We read argc/argv into the SysV first/second argument registers
 * (rdi, rsi) before aligning the stack and tail-calling into the C
 * runtime starter. The ABI mandates rsp % 16 == 0 *immediately before*
 * the call instruction; the call itself pushes the 8-byte return
 * address making rsp % 16 == 8 inside openos64_start, which matches
 * every other SysV C function we emit.
 */

.global _start
.extern openos64_start

_start:
    xorq %rbp, %rbp
    movq (%rsp), %rdi          /* argc */
    leaq 8(%rsp), %rsi         /* argv */
    andq $-16, %rsp
    call openos64_start
1:
    hlt
    jmp 1b
