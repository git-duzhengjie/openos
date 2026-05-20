; ============================================================
; openos - 上下文切换 (Context Switch)
; 保存当前线程寄存器，从栈恢复目标线程
; ============================================================

[bits 32]

global context_switch
global save_context_and_switch

; ============================================================
; void context_switch(thread_t *from, thread_t *to)
; ============================================================
context_switch:
    push ebp
    mov ebp, esp

    pushad                     ; 保存所有通用寄存器

    ; 保存段寄存器
    mov eax, [ebp + 8]         ; from = 第一个参数
    mov ebx, [ebp + 12]       ; to   = 第二个参数

    ; 保存 ESP 到 from->kernel_esp
    mov [eax + 52], esp       ; offsetof(thread_t, kernel_esp)

    ; 恢复 to->kernel_esp 到 ESP
    mov esp, [ebx + 52]

    ; 恢复通用寄存器 (从 to 线程的栈布局)
    popad

    ; 恢复 EIP (跳转回调用点)
    ret

; ============================================================
; 保存当前上下文并切换
; (用于 schedule() 从中断返回时)
; ============================================================
save_context_and_switch:
    ; 保存当前线程状态
    push ebp
    mov ebp, esp
    pushad

    mov eax, [ebp + 8]        ; current thread
    mov ebx, [ebp + 12]       ; next thread

    ; 保存当前寄存器到 PCB
    mov [eax + 4], ebx         ; eax
    mov [eax + 8], ecx         ; ebx
    mov [eax + 12], edx        ; ecx
    ; ... (完整实现见 PCB 定义)

    ; 切换到 next 的栈
    mov esp, [ebx + 52]
    popad
    ret