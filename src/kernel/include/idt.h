/* ============================================================
 * openos - 中断描述符表 (IDT) 头文件
 * ============================================================ */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* IDT条目结构 (8字节) */
typedef struct {
    uint16_t base_low;      /* 中断处理函数低16位 */
    uint16_t sel;           /* 段选择子 (内核代码段) */
    uint8_t  always0;       /* 保留位 */
    uint8_t  flags;         /* 标志: 类型、DPL、存在位 */
    uint16_t base_high;     /* 中断处理函数高16位 */
} __attribute__((packed)) idt_entry_t;

/* IDT指针结构 (用于lidt指令) */
typedef struct {
    uint16_t limit;         /* IDT大小-1 */
    uint32_t base;          /* IDT基地址 */
} __attribute__((packed)) idt_ptr_t;

/* 中断帧 (保存寄存器状态) - 栈布局与 isr_common_stub 完全匹配 */
typedef struct {
    /* 段寄存器 (isr_common_stub 按 ds,es,fs,gs push，最终 ESP 指向 gs) */
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    /* pusha 之后最低地址到最高地址依次是 EDI,ESI,EBP,原ESP,EBX,EDX,ECX,EAX */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_skip;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    /* ISR/IRQ 宏压入：先 err_code 后 int_no，最终 int_no 在更低地址 */
    uint32_t int_no;        /* 中断号 */
    uint32_t err_code;      /* 错误码 (有错误码的异常) 或 0 */
    /* CPU 自动压入 */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;      /* 仅特权级切换时有 */
    uint32_t user_ss;       /* 仅特权级切换时有 */
} __attribute__((packed)) registers_t;

/* 中断处理函数类型 */
typedef void (*isr_t)(registers_t*);

/* 函数声明 */
void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void isr_install_handler(uint8_t num, isr_t handler);
void isr_uninstall_handler(uint8_t num);

/* 汇编函数声明 (在isr.asm中) */
extern void isr0(void);   /* 除零错误 */
extern void isr1(void);   /* 调试异常 */
extern void isr2(void);   /* NMI */
extern void isr3(void);   /* 断点 */
extern void isr4(void);   /* 溢出 */
extern void isr5(void);   /* 边界检查 */
extern void isr6(void);   /* 非法操作码 */
extern void isr7(void);   /* 设备不可用 */
extern void isr8(void);   /* 双重错误 */
extern void isr9(void);   /* 协处理器段超限 */
extern void isr10(void);  /* 无效TSS */
extern void isr11(void);  /* 段不存在 */
extern void isr12(void);  /* 栈段错误 */
extern void isr13(void);  /* 通用保护错误 */
extern void isr14(void);  /* 页错误 */
extern void isr15(void);  /* 保留 */
extern void isr16(void);  /* 浮点错误 */
extern void isr17(void);  /* 对齐检查 */
extern void isr18(void);  /* 机器检查 */
extern void isr19(void);  /* SIMD浮点异常 */
extern void isr20(void);  /* 虚拟化异常 */
extern void isr21(void);  /* 保留 */
extern void isr22(void);  /* 保留 */
extern void isr23(void);  /* 保留 */
extern void isr24(void);  /* 保留 */
extern void isr25(void);  /* 保留 */
extern void isr26(void);  /* 保留 */
extern void isr27(void);  /* 保留 */
extern void isr28(void);  /* 保留 */
extern void isr29(void);  /* 保留 */
extern void isr30(void);  /* 安全异常 */
extern void isr31(void);  /* 保留 */
extern void isr128(void);  /* int 0x80 - 系统调用 */


/* IRQ处理函数 (PIC已重映射到32-47) */
extern void irq0(void);   /* PIT定时器 */
extern void irq1(void);   /* 键盘 */
extern void irq2(void);   /* 级联 */
extern void irq3(void);   /* COM2 */
extern void irq4(void);   /* COM1 */
extern void irq5(void);   /* LPT2 */
extern void irq6(void);   /* 软盘 */
extern void irq7(void);   /* LPT1 */
extern void irq8(void);   /* CMOS实时钟 */
extern void irq9(void);   /* 保留 */
extern void irq10(void);  /* 保留 */
extern void irq11(void);  /* 保留 */
extern void irq12(void);  /* PS/2鼠标 */
extern void irq13(void);  /* FPU */
extern void irq14(void);  /* 主ATA */
extern void irq15(void);  /* 从ATA */

#endif /* IDT_H */