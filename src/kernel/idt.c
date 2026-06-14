/* ============================================================
 * openos - 中断描述符表 (IDT) 实现
 * 初始化IDT、设置中断门、处理中断
 * ============================================================ */

#include "include/idt.h"
#include "include/io.h"
#include "include/mouse.h"
#include "include/process.h"
#include "serial.h"

/* IDT表 (256个条目) */
static idt_entry_t idt[256];

/* IDT指针 */
static idt_ptr_t idt_ptr;

/* 中断处理函数数组 */
static isr_t interrupt_handlers[256] = {0};

/* 定时器中断专用处理函数（在 timer_isr.asm 中）*/
extern void timer_isr_entry(void);
extern uint8_t __kernel_end;

/* ============================================================
 * PIC (可编程中断控制器) 操作
 * ============================================================ */

/* 
 * 重新映射PIC
 * 将IRQ 0-15映射到中断号32-47
 * 避免与CPU异常(0-31)冲突
 */
static void pic_remap(int offset1, int offset2) {
	/* 发送ICW1: 开始初始化 */
	outb(0x20, 0x11);  /* 主PIC */
	outb(0xA0, 0x11);  /* 从PIC */

	/* 发送ICW2: 设置中断向量偏移 */
	outb(0x21, offset1);  /* 主PIC: IRQ 0-7 -> INT 32-39 */
	outb(0xA1, offset2);  /* 从PIC: IRQ 8-15 -> INT 40-47 */

	/* 发送ICW3: 设置主从级联 */
	outb(0x21, 0x04);  /* 主PIC: IR2连接从PIC */
	outb(0xA1, 0x02);  /* 从PIC: 对应主PIC的IR2 */

	/* 发送ICW4: 8086模式 */
	outb(0x21, 0x01);
	outb(0xA1, 0x01);

	/* 清除所有IRQ屏蔽 */
	outb(0x21, 0x0);
	outb(0xA1, 0x0);
}

/*
 * 初始化PIT (可编程间隔定时器)
 * 设置定时器频率为约100Hz (每10ms一次中断)
 */
static void pit_init(void) {
	/* 计算分频值: PIT频率 1193182 Hz / 目标频率 */
	uint32_t divisor = 1193182 / 100;  /* 约100Hz */
	
	/* 发送控制字:
	 * 二进制: 00110110
	 * - 通道0 (00)
	 * - 先低字节后高字节 (11)
	 * - 模式3: 方波发生器 (011)
	 * - 二进制计数 (0)
	 */
	outb(0x43, 0x36);
	
	/* 发送分频值 (先低字节后高字节) */
	outb(0x40, (uint8_t)(divisor & 0xFF));
	outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
	
	/* 确认写入 - 读取状态 */
	uint8_t status = inb(0x61);
	(void)status;  /* 避免未使用警告 */
}

/* ============================================================
 * IDT操作
 * ============================================================ */

/*
 * 设置单个IDT门
 * @param num     中断号
 * @param base    处理函数地址
 * @param sel     段选择子 (内核代码段 = 0x08)
 * @param flags   标志 (0x8E = 存在、DPL=0、32位中断门)
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
	idt[num].base_low = base & 0xFFFF;
	idt[num].base_high = (base >> 16) & 0xFFFF;
	idt[num].sel = sel;
	idt[num].always0 = 0;
	idt[num].flags = flags;
}

/*
 * 安装中断处理函数
 */
void isr_install_handler(uint8_t num, isr_t handler) {
	interrupt_handlers[num] = handler;
}

/*
 * 卸载中断处理函数
 */
void isr_uninstall_handler(uint8_t num) {
	interrupt_handlers[num] = 0;
}

/* ============================================================
 * 中断处理
 * ============================================================ */

/*
 * 异常消息
 */
static const char *exception_messages[] = {
	"Division By Zero",
	"Debug",
	"Non Maskable Interrupt",
	"Breakpoint",
	"Into Detected Overflow",
	"Out of Bounds",
	"Invalid Opcode",
	"No Coprocessor",
	"Double Fault",
	"Coprocessor Segment Overrun",
	"Bad TSS",
	"Segment Not Present",
	"Stack Fault",
	"General Protection Fault",
	"Page Fault",
	"Unknown Interrupt",
	"Coprocessor Fault",
	"Alignment Check",
	"Machine Check",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved"
};

/* VGA输出 */
static void terminal_write(const char *str) {
	volatile char *vga = (volatile char *)0xB8000;
	static int pos = 0;
	while (*str) {
		if (*str == '\n') {
			pos = (pos / 80 + 1) * 80;
		} else {
			vga[pos * 2] = *str;
			vga[pos * 2 + 1] = 0x0C; /* 红色 */
			pos++;
		}
		str++;
	}
}

static int is_user_mode_trap(registers_t *regs) {
	return regs && ((regs->cs & 0x3) == 0x3);
}

static void kill_current_user_process(registers_t *regs) {
	thread_t *cur = sched_get_current();
	if (!cur || cur->pid == 0) {
		return;
	}

	serial_write("[EXC] killing user process pid=");
	serial_write_hex(cur->pid);
	serial_write(" int=");
	serial_write_hex(regs->int_no);
	serial_write(" eip=");
	serial_write_hex(regs->eip);
	serial_write("\n");

	proc_mark_exit(cur->pid, -128 - (int)regs->int_no);
	cur->state = PROC_ZOMBIE;
	sched_yield();

	while (1) {
		__asm__ volatile ("hlt");
	}
}

/*
 * ISR处理函数 (由isr.asm调用)
 */
void isr_handler(registers_t *regs) {
	/* 调试：打印栈原始内容 */
	uint32_t *stk = (uint32_t*)regs;
	serial_write("isr_dbg: ");
	for (int i = 0; i < 16; i++) {
		serial_write_hex(stk[i]); serial_write(" ");
	}
	serial_write("\n");

	/* 调用已注册的处理函数 */
	if (interrupt_handlers[regs->int_no]) {
		isr_t handler = interrupt_handlers[regs->int_no];
		handler(regs);
	} else {
			/* 未注册处理函数，显示异常信息 */
		serial_write("\n!!! EXCEPTION: ");
		if (regs->int_no < 32) {
			serial_write(exception_messages[regs->int_no]);
		} else {
			serial_write("Unknown/Corrupt Interrupt Vector");
		}
		serial_write(" !!!\n");
		serial_write("ctx int="); serial_write_hex(regs->int_no);
		serial_write(" err="); serial_write_hex(regs->err_code);
		serial_write(" eip="); serial_write_hex(regs->eip);
		serial_write(" cs="); serial_write_hex(regs->cs);
		serial_write(" eflags="); serial_write_hex(regs->eflags);
		serial_write("\n");

		/* GPF: 打印错误码和 EIP 帮助调试 */
		if (regs->int_no == 13) {
			/* error_code 在栈上 (ISR_ERRCODE push 的) */
			uint32_t err = regs->err_code;
			serial_write("  [GPF] err=0x");
			/* 简单 hex 输出 */
			{ const char hex[] = "0123456789ABCDEF"; int i; for(i=28;i>=0;i-=4) { char tmp[2]; tmp[0]=hex[(err>>i)&0xF]; tmp[1]=0; serial_write(tmp); } }
			serial_write(" eip=0x");
			{ const char hex[] = "0123456789ABCDEF"; int i; for(i=28;i>=0;i-=4) { char tmp[2]; tmp[0]=hex[(regs->eip>>i)&0xF]; tmp[1]=0; serial_write(tmp); } }
			serial_write(" cs=0x");
			{ const char hex[] = "0123456789ABCDEF"; int i; for(i=28;i>=0;i-=4) { char tmp[2]; tmp[0]=hex[(regs->cs>>i)&0xF]; tmp[1]=0; serial_write(tmp); } }
			serial_write("\n");
		}

		/* 页错误打印错误地址和EIP */
		if (regs->int_no == 14) {
			uint32_t fault_addr;
			__asm__ volatile ("mov %%cr2, %0" : "=r"(fault_addr));
			serial_write("Page Fault Address: 0x");
			{ const char hex[] = "0123456789ABCDEF"; int i; for(i=28;i>=0;i-=4) { char tmp[2]; tmp[0]=hex[(fault_addr>>i)&0xF]; tmp[1]=0; serial_write(tmp); } }
			serial_write(" eip=0x");
			{ const char hex[] = "0123456789ABCDEF"; int i; for(i=28;i>=0;i-=4) { char tmp[2]; tmp[0]=hex[(regs->eip>>i)&0xF]; tmp[1]=0; serial_write(tmp); } }
			serial_write("\n");
		}

		if (regs->int_no < 32 && is_user_mode_trap(regs)) {
			kill_current_user_process(regs);
		}

		/* 严重内核错误，停止系统 */
		if (regs->int_no < 8 || regs->int_no == 14) {
			serial_write("System Halted.\n");
			__asm__ volatile ("cli; hlt");
		}
	}
}

static int irq_handler_pointer_is_safe(isr_t handler) {
	uint32_t addr = (uint32_t)handler;
	uint32_t kernel_end = (uint32_t)&__kernel_end;

	/* OpenOS 当前内核链接在 0x8000，合法 IRQ handler 必须落在内核镜像内。
	 * 这既允许 keyboard_handler 这类低于 1MB 的合法函数，
	 * 又能继续拦截 0x00000003 这类明显坏函数指针。
	 */
	return addr >= 0x00008000u && addr < kernel_end;
}

/*
 * IRQ处理函数 (由isr.asm调用)
 */
void irq_handler(registers_t *regs) {
	/* 鼠标 IRQ12 固定直连，绕开 interrupt_handlers[] 函数指针表 */
	if (regs->int_no == 44) {
		mouse_irq_handle();
	} else if (regs->int_no < 256 && interrupt_handlers[regs->int_no]) {
		isr_t handler = interrupt_handlers[regs->int_no];
		if (irq_handler_pointer_is_safe(handler)) {
			handler(regs);
		} else {
			serial_write("[IRQ] ignored bad handler pointer int=");
			serial_write_hex(regs->int_no);
			serial_write(" handler=");
			serial_write_hex((uint32_t)handler);
			serial_write("\n");
		}
	}

	/* 发送EOI (End of Interrupt) */
	if (regs->int_no >= 40) {
		/* 从PIC的IRQ，需要发送两个EOI */
		outb(0xA0, 0x20);  /* 从PIC */
	}
	outb(0x20, 0x20);  /* 主PIC */
}

/* ============================================================
 * IDT初始化
 * ============================================================ */

/*
 * 初始化IDT
 */
void idt_init(void) {
	/* 初始化IDT指针 */
	idt_ptr.limit = sizeof(idt) - 1;
	idt_ptr.base = (uint32_t)&idt;

	/* 重新映射PIC */
	pic_remap(0x20, 0x28);

	/* 设置ISR门 (0-31) */
	idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
	idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
	idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);
	idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
	idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);
	idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
	idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);
	idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
	idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);
	idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
	idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
	idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
	idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
	idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
	idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
	idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
	idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
	idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
	idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
	idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
	idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
	idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
	idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
	idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
	idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
	idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
	idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
	idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
	idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
	idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
	idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
	idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

	/* 设置IRQ门 (32-47) */
	/* INT 32 使用专用定时器处理函数 */
	idt_set_gate(32, (uint32_t)timer_isr_entry, 0x08, 0x8E);
	
	/* 其他IRQ */
	idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);
	idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);
	idt_set_gate(35, (uint32_t)irq3, 0x08, 0x8E);
	idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);
	idt_set_gate(37, (uint32_t)irq5, 0x08, 0x8E);
	idt_set_gate(38, (uint32_t)irq6, 0x08, 0x8E);
	idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);
	idt_set_gate(40, (uint32_t)irq8, 0x08, 0x8E);
	idt_set_gate(41, (uint32_t)irq9, 0x08, 0x8E);
	idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
	idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
	idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
	idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
	idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
	idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

	/* 系统调用 (int 0x80) - 用户态可调用 */
	idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE);

	/* 加载IDT */
	__asm__ volatile ("lidt %0" : : "m"(idt_ptr));
	
	/* 注意：不在这里开启中断，由 kernel.c 在 sched_init() 后开启 */
}

/* 导出 pit_init 供 kernel.c 调用 */
void pit_start(void) {
	pit_init();
}
