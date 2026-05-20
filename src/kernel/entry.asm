; ============================================================
; openos - 内核入口 (Kernel Entry)
; 由 bootloader 跳转到此，设置C运行时环境
; ============================================================

[bits 32]
[extern kernel_main]         ; C语言主函数

section .entry.text           ; 特殊段，确保最先链接
global _start
_start:
    ; 设置栈指针 (已由bootloader设置，但重新确认)
    mov esp, 0x90000

    ; 清除BSS段
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    ; 调用内核主函数 (永不返回)
    call kernel_main

    ; 正常情况下不会执行到这里
    cli
    hlt
    jmp $

section .bss
global bss_start
bss_start:
    resb 0x10000             ; 64KB BSS
bss_end:
