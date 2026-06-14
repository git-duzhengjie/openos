; ============================================================
; openos - 中断服务例程 (ISR) 汇编部分
; ============================================================

[bits 32]
[extern isr_handler]         ; C语言中断处理函数
[extern irq_handler]         ; C语言IRQ处理函数
[extern syscall_dispatch]  ; 系统调用分发函数

section .text

; ----------------------------------------------------------
; 通用ISR包装 (无错误码)
; ----------------------------------------------------------
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    cli
    push 0                   ; 错误码 = 0
    push %1                  ; 中断号
    jmp isr_common_stub
%endmacro

; ----------------------------------------------------------
; 通用ISR包装 (有错误码)
; ----------------------------------------------------------
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    cli
    push %1                  ; 中断号
    jmp isr_common_stub
%endmacro

; ----------------------------------------------------------
; 通用IRQ包装
; ----------------------------------------------------------
%macro IRQ 2
global irq%1
irq%1:
    cli
    push 0
    push %2                  ; 中断号 = IRQ号+32
    jmp irq_common_stub
%endmacro

; ========== 定义ISR (0-31) ==========
ISR_NOERRCODE 0     ; 除零错误
ISR_NOERRCODE 1     ; 调试异常
ISR_NOERRCODE 2     ; NMI
ISR_NOERRCODE 3     ; 断点
ISR_NOERRCODE 4     ; 溢出
ISR_NOERRCODE 5     ; 边界检查
ISR_NOERRCODE 6     ; 非法操作码
ISR_NOERRCODE 7     ; 设备不可用
ISR_ERRCODE   8     ; 双重错误
ISR_NOERRCODE 9     ; 协处理器段超限
ISR_ERRCODE   10    ; 无效TSS
ISR_ERRCODE   11    ; 段不存在
ISR_ERRCODE   12    ; 栈段错误
ISR_ERRCODE   13    ; 通用保护错误
ISR_ERRCODE   14    ; 页错误
ISR_NOERRCODE 15    ; 保留
ISR_NOERRCODE 16    ; 浮点错误
ISR_NOERRCODE 17    ; 对齐检查
ISR_NOERRCODE 18    ; 机器检查
ISR_NOERRCODE 19    ; SIMD浮点异常
ISR_NOERRCODE 20    ; 虚拟化异常
ISR_NOERRCODE 21    ; 保留
ISR_NOERRCODE 22    ; 保留
ISR_NOERRCODE 23    ; 保留
ISR_NOERRCODE 24    ; 保留
ISR_NOERRCODE 25    ; 保留
ISR_NOERRCODE 26    ; 保留
ISR_NOERRCODE 27    ; 保留
ISR_NOERRCODE 28    ; 保留
ISR_NOERRCODE 29    ; 保留
ISR_NOERRCODE 30    ; 安全异常
ISR_NOERRCODE 31    ; 保留

; ========== 定义IRQ (32-47) ==========
IRQ 0, 32   ; PIT定时器
IRQ 1, 33   ; 键盘
IRQ 2, 34   ; 级联
IRQ 3, 35   ; COM2
IRQ 4, 36   ; COM1
IRQ 5, 37   ; LPT2
IRQ 6, 38   ; 软盘
IRQ 7, 39   ; LPT1
IRQ 8, 40   ; CMOS实时钟
IRQ 9, 41   ; 保留
IRQ 10, 42  ; 保留
IRQ 11, 43  ; 保留
IRQ 12, 44  ; PS/2鼠标
IRQ 13, 45  ; FPU
IRQ 14, 46  ; 主ATA
IRQ 15, 47  ; 从ATA

; ============================================================
; 系统调用入口 (int 0x80)
; ============================================================
global isr128
isr128:
    cli                      ; 关闭中断（防止重入）
    push 0                   ; 无错误码，压栈 0
    push 128                 ; 中断号 = 0x80
    jmp syscall_common_stub


; ----------------------------------------------------------
; 通用ISR存根
; ----------------------------------------------------------
isr_common_stub:
    pusha                    ; 保存通用寄存器
    push ds                  ; 保存数据段
    push es
    push fs
    push gs

    mov ax, 0x10             ; 加载内核数据段
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                 ; 传入 registers_t 指针
    call isr_handler
    add esp, 4               ; 清理栈

    pop gs                   ; 恢复段寄存器
    pop fs
    pop es
    pop ds
    popa                     ; 恢复通用寄存器
    add esp, 8               ; 清理错误码和中断号
    ; 不要在 iret 前手动 sti。iret 会根据保存的 EFLAGS 恢复 IF。
    ; 若这里提前开启中断，新的 IRQ 可能嵌套在未返回完成的栈帧上，破坏返回现场。
    iret                     ; 中断返回

; ----------------------------------------------------------
; 通用IRQ存根
; ----------------------------------------------------------
irq_common_stub:
    pusha                    ; 保存通用寄存器
    push ds                  ; 保存数据段
    push es
    push fs
    push gs

    mov ax, 0x10             ; 加载内核数据段
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                 ; 传入 registers_t 指针
    call irq_handler
    add esp, 4               ; 清理栈

    pop gs                   ; 恢复段寄存器
    pop fs
    pop es
    pop ds
    popa                     ; 恢复通用寄存器
    add esp, 8               ; 清理错误码和中断号
    ; 不要在 iret 前手动 sti。iret 会根据保存的 EFLAGS 恢复 IF。
    ; 若这里提前开启中断，新的 IRQ 可能嵌套在未返回完成的栈帧上，破坏返回现场。
    iret                     ; 中断返回
; ----------------------------------------------------------
; 系统调用通用存根 (int 0x80)
; ----------------------------------------------------------
syscall_common_stub:
    ; 此时栈布局：
    ; ESP+0   = 中断号 (128)
    ; ESP+4   = 错误码 (0)
    ; ESP+8   = 用户 EIP
    ; ESP+12  = 用户 CS
    ; ESP+16  = 用户 EFLAGS
    
    ; 保存用户寄存器
    pusha                    ; 保存 EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    push ds
    push es
    push fs
    push gs
    
    ; 设置内核数据段
    mov ax, 0x10            ; 内核数据段选择子
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; 此时栈布局：
    ; ESP+0   = GS
    ; ESP+4   = FS
    ; ESP+8   = ES
    ; ESP+12  = DS
    ; ESP+16  = EDI (用户 EDI, arg5)
    ; ESP+20  = ESI (用户 ESI, arg4)
    ; ESP+24  = EBP
    ; ESP+28  = ESP (pusha 之前的)
    ; ESP+32  = EBX (用户 EBX, arg1)
    ; ESP+36  = EDX (用户 EDX, arg3)
    ; ESP+40  = ECX (用户 ECX, arg2)
    ; ESP+44  = EAX (用户 EAX, 系统调用号)
    ; ESP+48  = 中断号 (128)
    ; ESP+52  = 错误码 (0)
    ; ESP+56  = 用户 EIP
    ; ESP+60  = 用户 CS
    ; ESP+64  = 用户 EFLAGS
    
    ; 提取参数
    mov eax, [esp + 44]     ; EAX = 系统调用号
    mov ebx, [esp + 32]     ; EBX = arg1
    mov ecx, [esp + 40]     ; ECX = arg2
    mov edx, [esp + 36]     ; EDX = arg3
    mov esi, [esp + 20]     ; ESI = arg4
    mov edi, [esp + 16]     ; EDI = arg5
    
    ; 调用 syscall_dispatch(num, a, b, c, d, e)
    ; C calling convention: 参数从右到左压栈
    push edi                 ; arg5 = e
    push esi                 ; arg4 = d
    push edx                 ; arg3 = c
    push ecx                 ; arg2 = b
    push ebx                 ; arg1 = a
    push eax                 ; num
    
    call syscall_dispatch
    add esp, 24             ; 清理栈 (6 args * 4 bytes)
    
    ; 返回值在 EAX 中，需要保存到栈中保存的用户 EAX 位置
    ; 注意：add esp, 24 之后，ESP 恢复到 pusha 之前的位置
    ; 所以 [esp + 44] 仍然是保存的用户 EAX
    mov [esp + 44], eax     ; 修改保存的 EAX 值
    
    ; 恢复寄存器
    pop gs
    pop fs
    pop es
    pop ds
    popa                     ; 恢复 EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
                              ; 此时 EAX = 系统调用返回值
    
    add esp, 8              ; 清理错误码和中断号
    ; Do not sti here. iret restores the user EFLAGS.IF saved by the CPU.
    iret                     ; 返回用户态
