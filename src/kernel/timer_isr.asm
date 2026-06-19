; ============================================================
; openos - timer interrupt handler (preemptive scheduler)
; ============================================================

[bits 32]

%define T_KERNEL_SP  72

section .text

extern sched_tick
extern timer_schedule_handler
extern sched_get_current

global timer_isr_entry
timer_isr_entry:
    ; Save general purpose and segment registers.
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

    ; Save the interrupted thread kernel stack before scheduling.
    call sched_get_current
    test eax, eax
    jz .skip_save
    mov [eax + T_KERNEL_SP], esp
.skip_save:

    call sched_tick
    call timer_schedule_handler

    test eax, eax
    jz .no_switch

    ; Switch to the selected thread stack.  This instruction must be on its
    ; own line; anything after ';' is a NASM comment.
    mov esp, [eax + T_KERNEL_SP]

.no_switch:
    ; Send EOI to the PIC before returning from the interrupt.
    mov al, 0x20
    out 0x20, al

    ; Restore segment and general purpose registers.
    pop gs
    pop fs
    pop es
    pop ds
    popad

    iret
