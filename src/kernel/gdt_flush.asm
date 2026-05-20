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

    ; 刷新 CS: 跳转到代码段
    jmp 0x08:.reload_cs    ; 0x08 = GDT_KERNEL_CODE

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