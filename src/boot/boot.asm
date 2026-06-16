; ============================================================
; openos - 引导加载程序 (Bootloader)
; 功能：BIOS 引导，进入32位保护模式，加载内核
; ============================================================

[bits 16]
[org 0x7c00]

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
    mov [boot_drive], dl

    mov si, boot_msg
    call print_string

    call check_int13_extensions

    mov si, loading_msg
    call print_string

    ; 从磁盘读取内核 (LBA模式)
    ; 每次读取 64 扇区 = 32KB，共读取 19 次 = 1216 扇区 = 608KB。
    ; 保存 BIOS 启动盘号，每块失败自动复位磁盘并重试。
    mov word [dap + 2], 0x40
    mov word [dap + 4], 0x8000
    mov word [dap + 6], 0x0000
    mov dword [dap + 8], 1
    mov dword [dap + 12], 0
    mov cx, 19
.load_kernel_chunk:
    push cx
    mov di, 3
.read_retry:
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jnc .read_ok
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec di
    jnz .read_retry
    jmp disk_error
.read_ok:
    mov al, '.'
    call print_char

    add dword [dap + 8], 64
    adc dword [dap + 12], 0

    ; First chunk uses 0000:8000. Later chunks use segment:0000 and advance
    ; by 0x0800 paragraphs = 32KB each time.
    cmp word [dap + 4], 0x8000
    jne .advance_kernel_segment
    mov word [dap + 4], 0
    mov word [dap + 6], 0x1000
    jmp .kernel_buffer_ready
.advance_kernel_segment:
    add word [dap + 6], 0x0800
.kernel_buffer_ready:
    pop cx
    loop .load_kernel_chunk

    mov si, ok_msg
    call print_string

    ; 进入保护模式
    cli
    lgdt [gdt_desc]

    ; 开启A20地址线
    in al, 0x92
    or al, 0x02
    out 0x92, al

    mov eax, cr0
    or eax, 0x01
    mov cr0, eax

    jmp 0x08:protected_mode

; ----------------------------------------------------------
; 子程序：检查 BIOS 扩展读盘支持
; ----------------------------------------------------------
check_int13_extensions:
    mov ah, 0x41
    mov bx, 0x55aa
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    cmp bx, 0xaa55
    jne disk_error
    test cx, 1
    jz disk_error
    ret

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

print_char:
    mov ah, 0x0e
    int 0x10
    ret

; ----------------------------------------------------------
; 错误处理
; ----------------------------------------------------------
disk_error:
    mov si, error_msg
    call print_string
    hlt
    jmp $

; ----------------------------------------------------------
; 数据
; ----------------------------------------------------------
boot_drive  db 0
boot_msg    db 'openos boot', 0x0d, 0x0a, 0
loading_msg db 'load kernel', 0x0d, 0x0a, 0
ok_msg      db ' ok', 0x0d, 0x0a, 0
error_msg   db 'disk err', 0x0d, 0x0a, 0

; 磁盘地址包 (DAP)
dap:
    db 0x10
    db 0
    dw 0x40
    dw 0x8000
    dw 0
    dq 1

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
; 32位保护模式代码
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

    ; 跳转到内核入口
    jmp 0x08:0x8000

; ----------------------------------------------------------
; 填充到512字节 (主引导记录)
; ----------------------------------------------------------
times 510-($-$$) db 0
dw 0xaa55
