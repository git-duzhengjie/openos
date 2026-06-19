; ============================================================
; openos - switch to user mode
; ============================================================
;
; switch_to_user_asm(eip, esp)
; cdecl: [esp+4]=user eip, [esp+8]=user esp
;
; Build a ring-3 iret frame on the current kernel stack and let iret
; restore SS:ESP, EFLAGS and CS:EIP atomically.  Do not move ESP to the
; user stack before iret.
;
; User selectors:
;   0x1B = user code selector, RPL=3
;   0x23 = user data selector, RPL=3
;

[bits 32]

section .text

global switch_to_user_asm
switch_to_user_asm:
    mov eax, [esp + 4]        ; user eip
    mov ecx, [esp + 8]        ; user esp

    ; Build iret frame on the kernel stack.
    push dword 0x23           ; SS
    push ecx                  ; ESP
    push dword 0x0202         ; EFLAGS, IF=1
    push dword 0x1B           ; CS
    push eax                  ; EIP

    ; Load user data selectors before returning to ring 3.
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    iret

; Spare user-mode test loop, currently unused.
global user_test_code
user_test_code:
    jmp short user_test_code
