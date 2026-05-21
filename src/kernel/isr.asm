; ============================================================
; openos - 中断服务例程 (ISR) 汇编部分
; ============================================================

[bits 32]
[extern isr_handler]         ; C语言中断处理函数
[extern irq_handler]         ; C语言IRQ处理函数

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

; ----------------------------------------------------------
; 通用ISR存根
; ----------------------------------------------------------
isr_common_stub:
    pusha                    ; 保存通用寄存器
    push ds                  ; 保存数据段

    mov ax, 0x10             ; 加载内核数据段
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                 ; 传入 registers_t 指针
    call isr_handler
    add esp, 4               ; 清理栈

    pop ds                   ; 恢复数据段
    popa                     ; 恢复通用寄存器
    add esp, 8               ; 清理错误码和中断号
    sti                      ; 开启中断
    iret                     ; 中断返回

; ----------------------------------------------------------
; 通用IRQ存根
; ----------------------------------------------------------
irq_common_stub:
    pusha                    ; 保存通用寄存器
    push ds                  ; 保存数据段

    mov ax, 0x10             ; 加载内核数据段
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                 ; 传入 registers_t 指针
    call irq_handler
    add esp, 4               ; 清理栈

    pop ds                   ; 恢复数据段
    popa                     ; 恢复通用寄存器
    add esp, 8               ; 清理错误码和中断号
    sti                      ; 开启中断
    iret                     ; 中断返回