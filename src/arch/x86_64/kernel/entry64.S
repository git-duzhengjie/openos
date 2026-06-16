.section .text.entry,"ax"
.global _start64
.extern kernel_main64

_start64:
    cli
    lea kernel64_stack_top(%rip), %rsp
    andq $-16, %rsp
    call kernel_main64

1:
    hlt
    jmp 1b
