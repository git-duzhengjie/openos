/* vga.c - VGA Text Mode Driver Implementation */

#include "../include/vga.h"
#include "../include/io.h"

/* VGA 初始化标志 - 防止未初始化访问 */
static int vga_ok = 0;

/* VGA 状态 */
static uint16_t *vga_mem = (uint16_t *)VGA_MEMORY;
static int vga_x = 0;
static int vga_y = 0;
static uint8_t vga_color = 0x07;  /* 亮灰 on 黑 */

/* 组合字符和属性 */
static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

/* 滚动屏幕 */
static void vga_scroll(void) {
    if (vga_y >= VGA_HEIGHT) {
        /* 上移一行 */
        for (int y = 0; y < VGA_HEIGHT - 1; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                vga_mem[y * VGA_WIDTH + x] = vga_mem[(y + 1) * VGA_WIDTH + x];
            }
        }
        
        /* 清空最后一行 */
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_mem[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', vga_color);
        }
        
        vga_y = VGA_HEIGHT - 1;
    }
}

/* 获取当前 VGA 光标位置 */
void vga_get_xy(int *x, int *y) {
    if (x) *x = vga_x;
    if (y) *y = vga_y;
}

/* 更新光标位置 */
void vga_update_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;
    
    outb(VGA_CRTC_ADDR, VGA_CURSOR_LOC_HIGH);
    outb(VGA_CRTC_DATA, (pos >> 8) & 0xFF);
    outb(VGA_CRTC_ADDR, VGA_CURSOR_LOC_LOW);
    outb(VGA_CRTC_DATA, pos & 0xFF);
}

/* 设置光标位置（同时更新内部坐标） */
void vga_set_xy(int x, int y) {
    vga_x = x;
    vga_y = y;
    vga_update_cursor(x, y);
}

/* 初始化 VGA 控制台 */
void vga_init(void) {
    vga_x = 0;
    vga_y = 0;
    vga_color = 0x07;  /* 亮灰 on 黑 */
    vga_clear();
    vga_enable_cursor(0, 15);
    vga_ok = 1;
}


/* 清屏 */
void vga_clear(void) {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_mem[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
        }
    }
    vga_x = 0;
    vga_y = 0;
    vga_update_cursor(vga_x, vga_y);
}

/* 设置颜色 */
void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (bg << 4) | fg;
}

/* 输出单个字符 */
void vga_putc(char c) {
    /* 防止未初始化访问 */
    if (!vga_ok) return;

    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\b') {
        if (vga_x > 0) {
            vga_x--;
        } else if (vga_y > 0) {
            vga_y--;
            vga_x = VGA_WIDTH - 1;
        }
    } else if (c == '\r') {
        vga_x = 0;
    } else if (c == '\t') {
        vga_x = (vga_x + 8) & ~7;
    } else {
        vga_mem[vga_y * VGA_WIDTH + vga_x] = vga_entry(c, vga_color);
        vga_x++;
    }
    
    /* 换行 */
    if (vga_x >= VGA_WIDTH) {
        vga_x = 0;
        vga_y++;
    }
    
    /* 滚动 */
    vga_scroll();
    
    /* 更新光标 */
    vga_update_cursor(vga_x, vga_y);
}

/* 输出字符串 */
void vga_write(const char *str) {
    /* 防止未初始化访问 */
    if (!vga_ok) return;

    while (*str) {
        vga_putc(*str++);
    }
}

/* 启用光标 */
void vga_enable_cursor(uint8_t start, uint8_t end) {
    outb(VGA_CRTC_ADDR, 0x0A);
    outb(VGA_CRTC_DATA, (inb(VGA_CRTC_DATA) & 0xC0) | start);
    
    outb(VGA_CRTC_ADDR, 0x0B);
    outb(VGA_CRTC_DATA, (inb(VGA_CRTC_DATA) & 0xE0) | end);
}

/* 禁用光标 */
void vga_disable_cursor(void) {
    outb(VGA_CRTC_ADDR, 0x0A);
    outb(VGA_CRTC_DATA, 0x20);
}
