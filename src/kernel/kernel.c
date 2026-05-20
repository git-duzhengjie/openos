// ============================================================
// openos - 内核主函数
// ============================================================

// VGA文本模式缓冲区 (80x25)
#define VGA_BUF      ((volatile char*)0xb8000)
#define VGA_WIDTH    80
#define VGA_HEIGHT   25

// 颜色定义
enum vga_color {
	VGA_BLACK       = 0,
	VGA_BLUE        = 1,
	VGA_GREEN       = 2,
	VGA_CYAN        = 3,
	VGA_RED         = 4,
	VGA_MAGENTA     = 5,
	VGA_BROWN       = 6,
	VGA_LIGHT_GREY  = 7,
	VGA_DARK_GREY   = 8,
	VGA_LIGHT_BLUE  = 9,
	VGA_LIGHT_GREEN = 10,
	VGA_LIGHT_CYAN  = 11,
	VGA_LIGHT_RED   = 12,
	VGA_LIGHT_MAGENTA = 13,
	VGA_YELLOW      = 14,
	VGA_WHITE       = 15,
};

// 当前光标位置
static int cursor_x = 0;
static int cursor_y = 0;

// 合成颜色属性字节
static inline char make_color(enum vga_color fg, enum vga_color bg) {
	return (char)(fg | bg << 4);
}

// 获取VGA字符条目
static inline unsigned short vga_entry(char ch, char color) {
	return (unsigned short)ch | (unsigned short)color << 8;
}

// ============================================================
// I/O 端口操作 (必须在 move_cursor 之前声明)
// ============================================================
static inline void outb(unsigned short port, unsigned char val) {
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
	unsigned char ret;
	__asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

// 移动硬件光标
static void move_cursor(int x, int y) {
	unsigned short pos = y * VGA_WIDTH + x;
	outb(0x3d4, 14);
	outb(0x3d5, (unsigned char)(pos >> 8));
	outb(0x3d4, 15);
	outb(0x3d5, (unsigned char)(pos));
}

// 禁用光标 (消除闪烁)
static void disable_cursor(void) {
	outb(0x3d4, 0x0A);
	outb(0x3d5, 0x20);  // 设置光标起始扫描线高位，禁用光标
}

// 默认颜色：黑底绿字 (高可读性)
#define COLOR_DEFAULT 0x0A  // 0x0A = 黑底(0) + 绿字(10)

// 清屏
void terminal_clear(void) {
	char color = COLOR_DEFAULT;
	volatile unsigned short* buf = (volatile unsigned short*)VGA_BUF;
	for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
		buf[i] = vga_entry(' ', color);
	}
	cursor_x = 0;
	cursor_y = 0;
	move_cursor(cursor_x, cursor_y);
}

// 滚动屏幕
static void terminal_scroll(void) {
	volatile unsigned short* buf = (volatile unsigned short*)VGA_BUF;
	char color = COLOR_DEFAULT;

	for (int y = 1; y < VGA_HEIGHT; y++) {
		for (int x = 0; x < VGA_WIDTH; x++) {
			buf[(y - 1) * VGA_WIDTH + x] = buf[y * VGA_WIDTH + x];
		}
	}
	for (int x = 0; x < VGA_WIDTH; x++) {
		buf[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', color);
	}
	cursor_y = VGA_HEIGHT - 1;
}

// 输出单个字符
void terminal_putchar(char c) {
	volatile unsigned short* buf = (volatile unsigned short*)VGA_BUF;
	char color = COLOR_DEFAULT;

	if (c == '\n') {
		cursor_x = 0;
		cursor_y++;
	} else if (c == '\r') {
		cursor_x = 0;
	} else if (c == '\t') {
		cursor_x = (cursor_x + 8) & ~7;
	} else {
		buf[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(c, color);
		cursor_x++;
	}

	if (cursor_x >= VGA_WIDTH) {
		cursor_x = 0;
		cursor_y++;
	}
	if (cursor_y >= VGA_HEIGHT) {
		terminal_scroll();
	}

	move_cursor(cursor_x, cursor_y);
}

// 输出字符串
void terminal_write(const char* str) {
	while (*str) {
		terminal_putchar(*str++);
	}
}

// 输出十六进制数 (调试用)
void terminal_write_hex(unsigned int num) {
	const char* hex_digits = "0123456789ABCDEF";
	terminal_write("0x");
	for (int i = 28; i >= 0; i -= 4) {
		terminal_putchar(hex_digits[(num >> i) & 0x0F]);
	}
}

// ============================================================
// 外部函数声明
// ============================================================
extern void idt_init(void);

// ============================================================
// 内核主函数
// ============================================================
void kernel_main(void) {
	// 强制清空 VGA 缓冲区（直接操作硬件）
	volatile unsigned char* vga = (volatile unsigned char*)0xb8000;
	int i;
	for (i = 0; i < 80 * 25 * 2; i += 2) {
		vga[i] = ' ';        // 字符：空格
		vga[i+1] = 0x0A;   // 颜色：黑底绿字
	}

	terminal_clear();
	disable_cursor();  // 禁止光标闪烁

	terminal_write("========================================\n");
	terminal_write("        openos v0.1 - 开源智能操作系统\n");
	terminal_write("========================================\n\n");

	terminal_write("[INFO] 内核已加载到内存\n");
	terminal_write("[INFO] 保护模式已启用\n");

	// 初始化IDT
	terminal_write("[INFO] 初始化中断描述符表...\n");
	idt_init();
	terminal_write("[INFO] IDT初始化完成\n");

	terminal_write("[INFO] 系统初始化中...\n\n");

	terminal_write("  ____                   \n");
	terminal_write(" / __ \\                  \n");
	terminal_write("| |  | |_ __   ___   ___ \n");
	terminal_write("| |  | | '_ \\ / _ \\ / _ \\\n");
	terminal_write("| |__| | |_) | (_) | (_) |\n");
	terminal_write(" \\____/| .__/ \\___/ \\___/ \n");
	terminal_write("       | |                \n");
	terminal_write("       |_|                \n\n");

	terminal_write("[INFO] 系统就绪!\n");
	terminal_write("[INFO] 内核地址: ");
	terminal_write_hex((unsigned int)&kernel_main);
	terminal_write("\n");
	terminal_write("[INFO] CPU模式: 32位保护模式\n");
	terminal_write("[INFO] 中断: 已启用\n");
	terminal_write("[INFO] 内存映射: VGA文本模式 0xB8000\n\n");

	terminal_write("openos$ ");

	while (1) {
		__asm__ volatile ("hlt");
	}
}