; ============================================================
; openos - 内核入口 (Kernel Entry)
; 由 bootloader 跳转到此，设置C运行时环境
; ============================================================

[bits 32]

; 外部符号声明 (C函数，ELF32 格式不需要下划线前缀)
[extern kernel_main]
[extern __bss_start]
[extern __bss_end]

; ============================================================
section .entry.text
global _start

_start:
    ; 设置栈指针到低端内存安全区顶部，避开内核 .bss。
    mov esp, 0x9F000

    ; 清除完整 BSS 段，范围由 linker.ld 提供。
    mov edi, __bss_start
    mov ecx, __bss_end
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

