; ============================================================
; openos - 引导加载程序 (Bootloader)
; 功能：BIOS 引导，进入32位保护模式，加载内核
; ============================================================

[bits 16]                    ; 16位实模式
[org 0x7c00]                 ; BIOS加载地址

; ----------------------------------------------------------
; 第一阶段：实模式启动
; ----------------------------------------------------------
start:
    cli                      ; 关闭中断
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00           ; 设置栈指针
    sti                      ; 开启中断

    ; 打印启动信息 (实模式)
    mov si, boot_msg
    call print_string

    ; 加载内核到内存
    mov si, loading_msg
    call print_string

    ; 从磁盘读取内核 (LBA模式)
    ; 每次读取 64 扇区 = 32KB，共读取 17 次 = 1088 扇区 = 544KB。
    ; 当前 kernel.bin 已超过旧版 512 扇区加载上限，因此必须覆盖完整内核，
    ; 否则 .rodata/字符串常量等后半段内容不会进入内存。
    ; 最高加载到约 0x88000，仍低于保护模式栈 0x9F000。
    mov word [dap + 2], 0x40     ; sectors per chunk
    mov word [dap + 4], 0x8000   ; first buffer offset: 0000:8000
    mov word [dap + 6], 0x0000   ; first buffer segment
    mov dword [dap + 8], 1       ; first kernel LBA
    mov dword [dap + 12], 0
    mov cx, 17
.load_kernel_chunk:
    push cx
    mov ah, 0x42
    mov si, dap
    int 0x13
    jc disk_error
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
    lgdt [gdt_desc]          ; 加载GDT

    ; 开启A20地址线
    in al, 0x92
    or al, 0x02
    out 0x92, al

    mov eax, cr0
    or eax, 0x01             ; 设置PE位
    mov cr0, eax

    ; 跳转到32位代码
    jmp 0x08:protected_mode

; ----------------------------------------------------------
; 子程序：打印字符串 (实模式)
; ----------------------------------------------------------
print_string:
    lodsb                    ; 加载字符到al
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10                 ; BIOS中断 - 显示字符
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
boot_msg    db 'openos v0.1 - Starting...', 0x0d, 0x0a, 0
loading_msg db 'Loading kernel...', 0x0d, 0x0a, 0
ok_msg      db 'OK!', 0x0d, 0x0a, 0
error_msg   db 'ERROR: Disk read failed!', 0x0d, 0x0a, 0

; 磁盘地址包 (DAP)
dap:
    db 0x10                  ; DAP大小
    db 0                     ; 保留
    dw 0x40                  ; 扇区数 (64 扇区 = 32KB)
    dw 0x8000                ; 缓冲区偏移
    dw 0                     ; 缓冲区段
    dq 1                     ; 起始LBA扇区

; ----------------------------------------------------------
; GDT (全局描述符表)
; ----------------------------------------------------------
gdt_start:
    ; null描述符
    dq 0

    ; 代码段描述符 (ring 0)
    dw 0xffff                ; limit low
    dw 0                     ; base low
    db 0                     ; base middle
    db 0x9a                  ; access: present, ring0, code, executable, readable
    db 0xcf                  ; flags: 4KB granularity, 32-bit
    db 0                     ; base high

    ; 数据段描述符 (ring 0)
    dw 0xffff                ; limit low
    dw 0                     ; base low
    db 0                     ; base middle
    db 0x92                  ; access: present, ring0, data, writable
    db 0xcf                  ; flags: 4KB granularity, 32-bit
    db 0                     ; base high
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1   ; limit
    dd gdt_start                 ; base

; ----------------------------------------------------------
; 32位保护模式代码
; ----------------------------------------------------------
[bits 32]
protected_mode:
    ; 设置数据段
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
dw 0xaa55                    ; 引导签名
