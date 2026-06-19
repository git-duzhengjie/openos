; ============================================================
; openos - 引导加载程序 (Bootloader)
; 功能：BIOS 引导，进入32位保护模式，在 1MiB 加载内核
; ============================================================

[bits 16]
[org 0x7c00]

KERNEL_LOAD_ADDR equ 0x00100000
KERNEL_LBA       equ 1
KERNEL_SECTORS   equ 2816

; ----------------------------------------------------------
; 第一阶段：实模式启动
; ----------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov si, boot_msg
    call print_string

    ; 开启 A20 地址线，允许访问 1MiB 以上内存。
    in al, 0x92
    or al, 0x02
    out 0x92, al

    ; 进入保护模式。后续用 ATA PIO 直接把内核读到高地址，
    ; 避免 BIOS 实模式低端内存窗口和 VGA 保留区限制。
    cli
    lgdt [gdt_desc]

    mov eax, cr0
    or eax, 0x01
    mov cr0, eax

    jmp 0x08:protected_mode

; ----------------------------------------------------------
; 子程序：打印字符串 (实模式)
; ----------------------------------------------------------
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print_string
.done:
    ret

; ----------------------------------------------------------
; 数据
; ----------------------------------------------------------
boot_msg db 'openos highmem boot', 0x0d, 0x0a, 0

; ----------------------------------------------------------
; GDT (全局描述符表)
; ----------------------------------------------------------
gdt_start:
    dq 0

    ; 代码段描述符 (ring 0)
    dw 0xffff
    dw 0
    db 0
    db 0x9a
    db 0xcf
    db 0

    ; 数据段描述符 (ring 0)
    dw 0xffff
    dw 0
    db 0
    db 0x92
    db 0xcf
    db 0
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ----------------------------------------------------------
; 第二阶段：32位保护模式
; ----------------------------------------------------------
[bits 32]
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000
    cld

    ; 从 primary IDE master 读取 kernel.bin 到 1MiB。
    mov edi, KERNEL_LOAD_ADDR
    mov ebx, KERNEL_LBA
    mov esi, KERNEL_SECTORS
.load_sector:
    test esi, esi
    jz .kernel_loaded

    call ata_wait_ready

    mov dx, 0x1F2
    mov al, 1
    out dx, al              ; sector count

    inc dx
    mov eax, ebx
    out dx, al              ; LBA low

    inc dx
    mov eax, ebx
    shr eax, 8
    out dx, al              ; LBA mid

    inc dx
    mov eax, ebx
    shr eax, 16
    out dx, al              ; LBA high

    inc dx
    mov eax, ebx
    shr eax, 24
    and al, 0x0F
    or al, 0xE0
    out dx, al              ; drive/head, LBA mode, master

    inc dx
    mov al, 0x20
    out dx, al              ; READ SECTORS

    call ata_wait_drq

    mov dx, 0x1F0
    mov ecx, 256
    rep insw

    inc ebx
    dec esi
    jmp .load_sector

.kernel_loaded:
    jmp 0x08:KERNEL_LOAD_ADDR

ata_wait_ready:
    mov dx, 0x1F7
.ready_loop:
    in al, dx
    test al, 0x80           ; BSY
    jnz .ready_loop
    test al, 0x40           ; DRDY
    jz .ready_loop
    ret

ata_wait_drq:
    mov dx, 0x1F7
.drq_loop:
    in al, dx
    test al, 0x01           ; ERR
    jnz boot_halt
    test al, 0x08           ; DRQ
    jz .drq_loop
    ret

boot_halt:
    hlt
    jmp boot_halt

; ----------------------------------------------------------
; 填充到512字节 (主引导记录)
; ----------------------------------------------------------
times 510-($-$$) db 0
dw 0xaa55
