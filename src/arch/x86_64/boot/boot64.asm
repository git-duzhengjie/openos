; ============================================================
; OpenOS x86_64 BIOS long mode boot sector skeleton
;
; Stage 1 goal:
;   - start in 16-bit real mode
;   - enter 32-bit protected mode
;   - build PML4 / PDPT / PD identity map using 2 MiB pages
;   - enable PAE
;   - set EFER.LME
;   - enable paging
;   - far jump to 64-bit code segment
;   - emit an early serial log
;
; This boot sector is standalone for architecture bring-up and does not
; replace the stable i386 boot path yet.
; ============================================================

[bits 16]
[org 0x7C00]

%define CODE32_SEL 0x08
%define DATA_SEL   0x10
%define CODE64_SEL 0x18

%define PML4_ADDR  0x1000
%define PDPT_ADDR  0x2000
%define PD_ADDR    0x3000
%define EFER_MSR   0xC0000080

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    in al, 0x92
    or al, 0x02
    out 0x92, al

    lgdt [gdt32_desc]

    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp CODE32_SEL:protected32

[bits 32]
protected32:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    call setup_page_tables

    mov eax, PML4_ADDR
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5                 ; CR4.PAE
    mov cr4, eax

    mov ecx, EFER_MSR
    rdmsr
    or eax, 1 << 8                 ; EFER.LME
    wrmsr

    mov eax, cr0
    or eax, 1 << 31                ; CR0.PG
    mov cr0, eax

    jmp CODE64_SEL:long_mode64

setup_page_tables:
    push edi
    push ecx
    push eax

    mov edi, PML4_ADDR
    xor eax, eax
    mov ecx, 4096 * 3 / 4
    rep stosd

    mov dword [PML4_ADDR], PDPT_ADDR | 0x003
    mov dword [PML4_ADDR + 4], 0

    mov dword [PDPT_ADDR], PD_ADDR | 0x003
    mov dword [PDPT_ADDR + 4], 0

    ; Identity map first 1 GiB with 2 MiB pages.
    mov edi, PD_ADDR
    mov ecx, 512
    xor eax, eax
.map_pd:
    mov edx, eax
    or edx, 0x083                  ; present | writable | huge page
    mov [edi], edx
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .map_pd

    pop eax
    pop ecx
    pop edi
    ret

[bits 64]
long_mode64:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x90000

    call serial64_init
    lea rsi, [rel long_mode_msg]
    call serial64_write

.halt:
    hlt
    jmp .halt

serial64_init:
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 0x03
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    ret

serial64_write:
    lodsb
    test al, al
    jz .done
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    mov al, [rsi - 1]
    mov dx, 0x3F8
    out dx, al
    jmp serial64_write
.done:
    ret

long_mode_msg db '[x86_64] BIOS path entered long mode', 13, 10, 0

align 8
gdt32_start:
    dq 0
    ; 32-bit kernel code
    dw 0xFFFF
    dw 0
    db 0
    db 0x9A
    db 0xCF
    db 0
    ; kernel data
    dw 0xFFFF
    dw 0
    db 0
    db 0x92
    db 0xCF
    db 0
    ; 64-bit kernel code
    dw 0
    dw 0
    db 0
    db 0x9A
    db 0x20
    db 0
gdt32_end:

gdt32_desc:
    dw gdt32_end - gdt32_start - 1
    dd gdt32_start

times 510 - ($ - $$) db 0
dw 0xAA55
