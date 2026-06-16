; ============================================================
; openos - 上下文切换
; 栈帧格式必须与 timer_isr.asm 和 thread_create 一致：
;   [GS][FS][ES][DS][EDI][ESI][EBP][ESP_skip][EBX][EDX][ECX][EAX][EIP][CS][EFLAGS]
; ============================================================

[bits 32]

section .text

global context_switch

; void context_switch(thread_t *from, thread_t *to)
; thread_t.kernel_esp 偏移 = 72
context_switch:
    ; 保存通用寄存器和段寄存器，匹配 timer_isr/thread_create 栈帧
    pushad
    push ds
    push es
    push fs
    push gs

    ; 参数在原始栈上: from=[esp+36+4], to=[esp+36+8]
    ; pushad(32) + ds/es/fs/gs(16) + ret(4) = 52
    mov ebx, [esp + 52]     ; from
    mov ecx, [esp + 56]     ; to

    ; 保存 ESP 到 from->kernel_esp
    mov [ebx + 72], esp

    ; 切换到 to 的栈
    mov esp, [ecx + 72]     ; esp = to->kernel_esp

    ; 恢复段寄存器和通用寄存器，匹配 timer_isr pop 顺序
    pop gs
    pop fs
    pop es
    pop ds
    popad

    ; EIP 在栈顶(由 thread_create 放置)或 ret addr(由 timer_isr 放置)
    ; 对于 thread_create 创建的帧: 栈顶是 EIP，用 ret 返回
    ret
