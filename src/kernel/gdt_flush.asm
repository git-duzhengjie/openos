; ============================================================
; openos - GDT 加载汇编
; ============================================================

[bits 32]

global gdt_flush

gdt_flush:
    mov eax, [esp + 4]    ; 获取 GDTR 地址
    lgdt [eax]            ; 加载 GDT

    ; 重新加载所有段寄存器
    mov ax, 0x10           ; 内核数据段 (GDT_KERNEL_DATA)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 刷新 CS: 用 push + retf 实现远跳转（避免 NASM 标签地址计算问题）
    push 0x08              ; 目标 CS = 内核代码段
    push .reload_cs        ; 目标 EIP = .reload_cs
    retf                   ; 远返回，同时刷新 CS 和 EIP

.reload_cs:
    ret

; ============================================================
; 加载 TSS
; ============================================================
global tss_flush

tss_flush:
    mov ax, 0x28           ; TSS 选择子 (index=5, RPL=0)
    ltr ax                 ; 加载任务寄存器
    ret