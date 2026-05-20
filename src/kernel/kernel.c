// ============================================================
// openos - 内核主函数
// Phase 2: GDT / PMM / VMM / Scheduler / Syscall
// ============================================================

// 内核模块头文件
#include "include/gdt.h"
#include "include/pmm.h"
#include "include/vmm.h"
#include "include/process.h"
#include "include/syscall.h"

// ============================================================
// VGA 输出
// ============================================================
static int cur_x = 0;
static int cur_y = 2;

static inline void outb(unsigned short port, unsigned char val) {
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void move_cursor(int x, int y) {
	unsigned short pos = y * 80 + x;
	outb(0x3d4, 14);
	outb(0x3d5, (unsigned char)(pos >> 8));
	outb(0x3d4, 15);
	outb(0x3d5, (unsigned char)pos);
}

void terminal_clear(void) {
	volatile unsigned short *buf = (volatile unsigned short *)0xB8000;
	for (int i = 0; i < 80 * 25; i++)
		buf[i] = 0x0A00 | ' ';
	cur_x = 0; cur_y = 2;
	move_cursor(cur_x, cur_y);
}

void terminal_putchar(char c) {
	volatile unsigned short *buf = (volatile unsigned short *)0xB8000;
	if (c == '\n') { cur_x = 0; cur_y++; }
	else if (c == '\r') { cur_x = 0; }
	else if (c == '\t') { cur_x = (cur_x + 8) & ~7; }
	else { buf[cur_y * 80 + cur_x] = (0x0A << 8) | (unsigned char)c; cur_x++; }
	if (cur_x >= 80) { cur_x = 0; cur_y++; }
	if (cur_y >= 25) {
		for (int y = 1; y < 25; y++)
			for (int x = 0; x < 80; x++)
				buf[(y-1)*80+x] = buf[y*80+x];
		for (int x = 0; x < 80; x++)
			buf[24*80+x] = (0x0A<<8)|' ';
		cur_y = 24;
	}
	move_cursor(cur_x, cur_y);
}

void terminal_write(const char *str) {
	while (*str) terminal_putchar(*str++);
}

void terminal_write_hex(unsigned int num) {
	const char *hex = "0123456789ABCDEF";
	terminal_write("0x");
	for (int i = 28; i >= 0; i -= 4)
		terminal_putchar(hex[(num >> i) & 0x0F]);
}

// ============================================================
// 内核主函数
// ============================================================
void kernel_main(void)
{
	/* 强制清空 VGA 缓冲区 */
	volatile unsigned char *vga = (volatile unsigned char *)0xB8000;
	for (int i = 0; i < 80 * 25 * 2; i += 2) {
		vga[i] = ' ';
		vga[i+1] = 0x0A;
	}

	terminal_clear();

	terminal_write("========================================\n");
	terminal_write("        openos v0.2 - Phase 2\n");
	terminal_write("        GDT / PMM / VMM / Sched\n");
	terminal_write("========================================\n\n");

	/* ---- Phase 2 模块初始化顺序 ---- */
	terminal_write("[1] Initializing GDT... ");
	gdt_init();
	terminal_write("[OK]\n");

	terminal_write("[2] Initializing IDT... ");
	idt_init();
	terminal_write("[OK]\n");

	terminal_write("[3] Initializing PMM (Physical Memory)...\n");
	pmm_init(0x9000 + 0x10000);
	terminal_write("      PMM ready\n");

	terminal_write("[4] Initializing VMM (Virtual Memory)...\n");
	vmm_init();
	terminal_write("      VMM CR3 loaded\n");

	terminal_write("[5] Initializing Scheduler... ");
	sched_init();
	terminal_write("[OK]\n");

	terminal_write("[6] Initializing Syscall... ");
	syscall_init();
	terminal_write("[OK]\n");

	terminal_write("\n========================================\n");
	terminal_write("   _  _     _             \n");
	terminal_write("  | || |___| |___ _ _     \n");
	terminal_write("  | __ / -_|   / -_) ' \\   \n");
	terminal_write("  |_||_\\___|_\\___|_||_|   \n");
	terminal_write("                           \n");
	terminal_write("  Phase 2: Kernel Core Ready!\n");
	terminal_write("========================================\n\n");

	terminal_write("[INFO] Paging enabled (4GB identity map)\n");
	terminal_write("[INFO] Scheduler: MLFQ (8 queues)\n");
	terminal_write("[INFO] Syscall: INT 0x80 ready\n");
	terminal_write("\nopenos$ ");

	while (1) {
		__asm__ volatile("hlt");
	}
}