;; openos - 简单用户态测试程序
;; 编译: nasm -f bin src/user/hello.asm -o target/hello.bin
;; 然后写入 ramfs 或直接加载

[BITS 32]

;; 系统调用号
SYS_WRITE  equ 3
SYS_EXIT   equ 6

section .text
global _start

_start:
    ; sys_write(1, msg, len)
    mov eax, SYS_WRITE
    mov ebx, 1          ; fd = stdout
    mov ecx, msg        ; buffer
    mov edx, msg.len    ; length
    int 0x80

    ; sys_exit(0)
    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x80

section .rodata
msg: db "Hello from user program!", 10
.len equ $ - msg