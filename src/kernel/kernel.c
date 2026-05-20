/**
 * @file kernel.c
 * @brief OpenOS 内核主程序
 */

#include "types.h"
#include "vga.h"
#include "string.h"

void kernel_main() {
    // 初始化 VGA 缓冲区
    vga_init();
    vga_clear();
    
    // 输出蓝色欢迎语
    uint8_t welcome_color = vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK);
    vga_write_string("Welcome to openOS!", 0, 0, welcome_color);

    // 设置颜色（白底黑字）
    uint8_t color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    
    // 打印 Phase 2 标题
    vga_write_string("[ OpenOS ]", 2, 0, color);
    vga_write_string("Phase 2: Memory & Multitasking Framework", 3, 0, color);
    vga_write_string("========================================", 4, 0, color);
    
    // 简单的 OK 信息，表明我们进了 Phase 2
    vga_write_string("OK: Entered Phase 2", 6, 0, vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
    
    // 死循环，保持系统运行
    for (;;) {
        // Halt 让 CPU 休息，避免 100%
        __asm__ volatile ("hlt");
    }
}