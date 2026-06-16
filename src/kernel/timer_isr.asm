; ============================================================
; openos - 定时器中断处理 (抢占式调度)
; ============================================================

[bits 32]

%define T_KERNEL_SP  72

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
    mov fs, ax
    mov es, ax
    mov gs, ax
    
    ; 在调度前保存当前线程的 kernel_sp
    ; (必须在 timer_schedule_handler 之前，因为 handler 会修改 sched.current)
    call sched_get_current
    test eax, eax
    jz .skip_save
    mov [eax + T_KERNEL_SP], esp
.skip_save:
    
    call sched_tick
    call timer_schedule_handler
    
    test eax, eax
    jz .no_switch
    
    ; 切换到新线程栈
    mov esp, [eax + T_KERNEL_SP]
    
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