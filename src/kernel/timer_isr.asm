; ============================================================
; openos - 定时器中断处理 (抢占式调度)
; ============================================================

[bits 32]

%define T_KERNEL_ESP  36

section .text

extern sched_tick
extern timer_schedule_handler
extern sched_get_current

global timer_isr_entry
timer_isr_entry:
    ; 保存所有寄存器
    pushad
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; ESP 现在指向保存的上下文顶部
    ; 保存当前线程的 ESP（在调用任何 C 函数之前）
    ; 先获取当前线程指针
    call sched_get_current
    test eax, eax
    jz .skip_save_esp
    mov [eax + T_KERNEL_ESP], esp    ; 保存 ISR 上下文的 ESP
.skip_save_esp:
    
    call sched_tick
    call timer_schedule_handler
    
    ; EAX = 新线程指针 (NULL = 不切换)
    test eax, eax
    jz .no_switch
    
    ; 切换到新线程栈
    mov esp, [eax + T_KERNEL_ESP]
    
.no_switch:
    ; 发送 EOI
    mov al, 0x20
    out 0x20, al
    
    ; 恢复寄存器
    pop gs
    pop fs
    pop es
    pop ds
    popad
    
    iret
