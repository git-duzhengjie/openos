.section .text,"ax"
.code64

.global _start
.extern openos64_start

_start:
    xorq %rbp, %rbp
    andq $-16, %rsp
    call openos64_start
1:
    hlt
    jmp 1b
