; ============================================================
; openos - 内核入口 (Kernel Entry)
; 由 bootloader 跳转到此，设置C运行时环境
; ============================================================

[bits 32]

; 外部符号声明 (C函数，ELF32 格式不需要下划线前缀)
[extern kernel_main]

; ============================================================
section .entry.text
global _start

_start:
    ; 设置栈指针 (0x90000 = 576KB，足够用)
    mov esp, 0x90000

    ; 清除BSS段
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    ; ============================================================
    ; 调用内核主函数 kernel_main() (永不返回)
    ; ============================================================
    call kernel_main

    ; 正常情况下不会执行到这里
    cli
    hlt
    jmp $

; ============================================================
section .bss
global bss_start
bss_start:
    resb 0x10000             ; 64KB BSS
bss_end:
