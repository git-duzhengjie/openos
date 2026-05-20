; ============================================================
; openos - 上下文切换 (Context Switch)
; 保存当前线程寄存器，从目标线程栈恢复
; ============================================================

[bits 32]

global context_switch
global save_context_and_switch

; ============================================================
; void context_switch(thread_t *from, thread_t *to)
; 切换 from -> to
; ============================================================
context_switch:
    push ebp
    mov ebp, esp

    ; 保存所有通用寄存器 (pushad: EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI)
    pushad

    ; from = [ebp+8], to = [ebp+12]
    mov eax, [ebp + 8]
    mov ebx, [ebp + 12]

    ; 保存 ESP 到 from->kernel_esp (offset = 36)
    mov [eax + 36], esp

    ; 从 to->kernel_esp 恢复 ESP
    mov esp, [ebx + 36]

    ; 恢复通用寄存器 (popad 逆序: EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX)
    popad

    ; 现在栈已经是 to 线程的栈
    ; 栈顶是 thread_create 构建的帧: [EDI,ESI,EBX,EBP,EIP]
    ; ret 将弹出 EIP，跳转到 to 线程的入口
    ret

; ============================================================
; save_context_and_switch (保留接口，暂时等价于 context_switch)
; ============================================================
save_context_and_switch:
    jmp context_switch
