; ============================================================
; openos - kernel thread entry trampoline
; ============================================================
;
; A freshly-created kernel thread is first entered when sched_start,
; sched_yield or timer_isr restores its synthetic interrupt frame and
; returns here with iret.  C functions expect a regular cdecl call frame,
; so this tiny trampoline calls entry(arg) explicitly.
;
; Stack state on entry after iret:
;   [esp + 0] = entry
;   [esp + 4] = arg
;

[bits 32]

section .text

global kernel_thread_entry_trampoline
extern serial_write
extern serial_write_hex

section .rodata
bad_entry_msg db "[KTRAP] bad kernel thread entry esp=", 0
entry_msg db " entry=", 0
arg_msg db " arg=", 0
nl_msg db 10, 0

section .text
kernel_thread_entry_trampoline:
    ; Keep interrupts enabled.  The first scheduled thread may sleep or
    ; halt while waiting for timer interrupts, so entering here with
    ; interrupts disabled can freeze the boot path.
    ; The synthetic frame restores EAX=entry and EDX=arg before iret.
    ; Keep [esp]/[esp+4] as a fallback copy for older frames, but do not
    ; require post-iret ESP to be perfect before making the first C call.
    cmp eax, 0x00100000
    jae .call_entry

    mov eax, [esp]
    mov edx, [esp + 4]
    cmp eax, 0x00100000
    jae .call_entry

    ; A valid kernel C entry point must never be in the low memory/null page.
    ; If this fires, the synthetic thread frame is being consumed with the
    ; wrong stack/register state, and printing here preserves the real bad
    ; value instead of letting CPU jump to 0x00000003 and report Invalid Opcode.
    push edx
    push eax
    mov ebx, esp

    push bad_entry_msg
    call serial_write
    add esp, 4

    push ebx
    call serial_write_hex
    add esp, 4

    push entry_msg
    call serial_write
    add esp, 4

    push eax
    call serial_write_hex
    add esp, 4

    push arg_msg
    call serial_write
    add esp, 4

    push edx
    call serial_write_hex
    add esp, 4

    push nl_msg
    call serial_write
    add esp, 4

    pop eax
    pop edx
    jmp .hang

.call_entry:
    push edx
    call eax
    add esp, 4

.hang:
    sti
    hlt
    jmp .hang
