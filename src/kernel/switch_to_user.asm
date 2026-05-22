; ============================================================
; openos - 用户态切换 (修复 v14)
; 使用 iret 从内核态切换到用户态
; 修复: 不在 iret 前切换 ESP，让 iret 自己从帧中恢复
; ============================================================

[bits 32]

section .text

; ============================================================
; switch_to_user_asm(eip, esp)
; cdecl: [esp+4]=eip, [esp+8]=esp
; ============================================================
global switch_to_user_asm
switch_to_user_asm:
    mov eax, [esp + 4]    ; eip  (用户代码入口)
    mov ecx, [esp + 8]    ; esp  (用户栈指针)
    
    ; 构建 iret 帧【在内核栈上】
    push dword 0x23        ; SS  (用户数据段 RPL=3)
    push ecx               ; ESP (用户栈)
    push dword 0x0202      ; EFLAGS (IF=1, 允许中断)
    push dword 0x1B        ; CS  (用户代码段 RPL=3)
    push eax               ; EIP (用户入口)
    
    ; 加载用户数据段寄存器（可选，iret 后 CPU 会用 SS 对应的数据段）
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; 【不要】加载 SS
    ; 【不要】mov esp, ecx
    ; 让 iret 自己从帧中恢复 SS 和 ESP
    
    iret

; ============================================================
; 用户态测试代码（备用，当前未使用）
; ============================================================
global user_test_code
user_test_code:
    jmp short user_test_code