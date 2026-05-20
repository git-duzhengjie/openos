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
    mov ah, 0x42             ; INT 13h AH=42h - 扩展读
    mov si, dap               ; 磁盘地址包
    int 0x13
    jc disk_error

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
    dw 0x40                  ; 扇区数 (32KB / 512字节)
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
    mov esp, 0x90000

    ; 跳转到内核入口
    jmp 0x08:0x8000

; ----------------------------------------------------------
; 填充到512字节 (主引导记录)
; ----------------------------------------------------------
times 510-($-$$) db 0
dw 0xaa55                    ; 引导签名