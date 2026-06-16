.section .text,"ax"
.code64

.global _start
.extern efi_main

_start:
    subq $0x28, %rsp
    call efi_main
    addq $0x28, %rsp
    ret
