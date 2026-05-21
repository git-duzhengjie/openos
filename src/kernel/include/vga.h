/* vga.h - VGA Text Mode Driver Header */

#ifndef VGA_H
#define VGA_H

#include <stdint.h>

/* VGA 文本模式常量 */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  0xB8000

/* VGA 颜色定义 */
#define VGA_BLACK         0
#define VGA_BLUE          1
#define VGA_GREEN         2
#define VGA_CYAN          3
#define VGA_RED           4
#define VGA_MAGENTA       5
#define VGA_BROWN         6
#define VGA_LIGHT_GREY   7
#define VGA_DARK_GREY     8
#define VGA_LIGHT_BLUE    9
#define VGA_LIGHT_GREEN   10
#define VGA_LIGHT_CYAN    11
#define VGA_LIGHT_RED     12
#define VGA_LIGHT_MAGENTA 13
#define VGA_LIGHT_BROWN   14
#define VGA_WHITE         15

/* VGA 端口 */
#define VGA_CRTC_ADDR 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_CURSOR_LOC_LOW  0x0F
#define VGA_CURSOR_LOC_HIGH 0x0E

/* 函数声明 */
void vga_init(void);
void vga_putc(char c);
void vga_write(const char *str);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_clear(void);
void vga_enable_cursor(uint8_t start, uint8_t end);
void vga_disable_cursor(void);
void vga_update_cursor(int x, int y);

#endif /* VGA_H */
