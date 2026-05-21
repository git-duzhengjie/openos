; ============================================================
; openos - 上下文切换
; ============================================================

[bits 32]

section .text

global context_switch

; void context_switch(thread_t *from, thread_t *to)
;
; 栈布局（函数入口）：
;   [esp+0]  = 返回地址
;   [esp+4]  = from 参数
;   [esp+8]  = to 参数
;
; thread_t.kernel_esp 偏移 = 36
;
context_switch:
    ; 保存返回地址到 eax
    mov eax, [esp]          ; 返回地址

    ; 保存所有寄存器到当前线程栈
    pushad                  ; 8个通用寄存器
    pushfd                  ; EFLAGS

    ; 此时栈：
    ;   [esp+0]  = EFLAGS
    ;   [esp+4] = EDI
    ;   ...
    ;   [esp+32] = EAX
    ;   [esp+36] = 返回地址
    ;   [esp+40] = from
    ;   [esp+44] = to

    ; 保存 ESP 到 from->kernel_esp
    mov ebx, [esp + 40]     ; from
    mov [ebx + 36], esp     ; from->kernel_esp = esp

    ; 切换到 to 的栈
    mov ecx, [esp + 44]     ; to
    mov esp, [ecx + 36]     ; esp = to->kernel_esp

    ; 恢复 to 的寄存器
    popfd                   ; 恢复 EFLAGS
    popad                   ; 恢复通用寄存器

    ; 返回到 to 线程
    ret
