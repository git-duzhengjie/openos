#include "../include/early_console64.h"
#include "font8x8_basic.h"

#define EARLY_COM1_PORT 0x3F8u

/* Framebuffer text console (fbcon) geometry: font8x8 glyphs, 1x scale. */
#define EARLY_FBCON_GLYPH_W 8u
#define EARLY_FBCON_GLYPH_H 8u
#define EARLY_FBCON_FG 0x00FFFFFFu /* white  (XRGB8888) */
#define EARLY_FBCON_BG 0x00000000u /* black  (XRGB8888) */
#define EARLY_VGA_WIDTH 80u
#define EARLY_VGA_HEIGHT 25u
#define EARLY_VGA_MEMORY 0xB8000ULL
#define EARLY_VGA_CRTC_ADDR 0x3D4u
#define EARLY_VGA_CRTC_DATA 0x3D5u
#define EARLY_VGA_CURSOR_LOW 0x0Fu
#define EARLY_VGA_CURSOR_HIGH 0x0Eu

static uint16_t *early_vga_mem = (uint16_t *)EARLY_VGA_MEMORY;
static uint32_t early_vga_x;
static uint32_t early_vga_y;
static uint8_t early_vga_color = 0x07u;
static uint8_t early_vga_ready;
static early_framebuffer64_info_t early_fb_info;

/* fbcon: cursor position in character cells, and grid dimensions. */
static uint32_t early_fbcon_cols;
static uint32_t early_fbcon_rows;
static uint32_t early_fbcon_cx;
static uint32_t early_fbcon_cy;
static uint8_t early_fbcon_ready;
static uint8_t early_fbcon_suppress; /* 1 = 桌面接管 framebuffer 后停止在屏幕上打印（串口仍输出） */

static inline void outb64(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb64(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t early_vga_entry(char c) {
    return (uint16_t)(uint8_t)c | ((uint16_t)early_vga_color << 8);
}

static void early_vga_update_cursor(void) {
    uint16_t pos = (uint16_t)(early_vga_y * EARLY_VGA_WIDTH + early_vga_x);
    outb64(EARLY_VGA_CRTC_ADDR, EARLY_VGA_CURSOR_HIGH);
    outb64(EARLY_VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFFu));
    outb64(EARLY_VGA_CRTC_ADDR, EARLY_VGA_CURSOR_LOW);
    outb64(EARLY_VGA_CRTC_DATA, (uint8_t)(pos & 0xFFu));
}

static void early_vga_scroll(void) {
    uint32_t x;
    uint32_t y;

    if (early_vga_y < EARLY_VGA_HEIGHT) {
        return;
    }

    for (y = 0; y + 1u < EARLY_VGA_HEIGHT; ++y) {
        for (x = 0; x < EARLY_VGA_WIDTH; ++x) {
            early_vga_mem[y * EARLY_VGA_WIDTH + x] = early_vga_mem[(y + 1u) * EARLY_VGA_WIDTH + x];
        }
    }
    for (x = 0; x < EARLY_VGA_WIDTH; ++x) {
        early_vga_mem[(EARLY_VGA_HEIGHT - 1u) * EARLY_VGA_WIDTH + x] = early_vga_entry(' ');
    }
    early_vga_y = EARLY_VGA_HEIGHT - 1u;
}

void early_serial64_init(void) {
    outb64(EARLY_COM1_PORT + 1u, 0x00u);
    outb64(EARLY_COM1_PORT + 3u, 0x80u);
    outb64(EARLY_COM1_PORT + 0u, 0x01u);
    outb64(EARLY_COM1_PORT + 1u, 0x00u);
    outb64(EARLY_COM1_PORT + 3u, 0x03u);
    outb64(EARLY_COM1_PORT + 2u, 0xC7u);
    outb64(EARLY_COM1_PORT + 4u, 0x0Bu);
}

void early_serial64_putc(char c) {
    while ((inb64(EARLY_COM1_PORT + 5u) & 0x20u) == 0) {
        __asm__ __volatile__("pause");
    }
    outb64(EARLY_COM1_PORT, (uint8_t)c);
}

void early_serial64_write(const char *text) {
    if (!text) {
        return;
    }
    while (*text) {
        if (*text == '\n') {
            early_serial64_putc('\r');
        }
        early_serial64_putc(*text++);
    }
}

void early_serial64_write_hex64(uint64_t v) {
    static const char hx[] = "0123456789abcdef";
    early_serial64_putc('0');
    early_serial64_putc('x');
    for (int i = 60; i >= 0; i -= 4) {
        early_serial64_putc(hx[(v >> i) & 0xF]);
    }
}

void early_vga64_init(void) {
    early_vga_x = 0;
    early_vga_y = 0;
    early_vga_color = 0x07u;
    early_vga_ready = 1u;
    early_vga64_clear();
}

void early_vga64_clear(void) {
    uint32_t x;
    uint32_t y;

    for (y = 0; y < EARLY_VGA_HEIGHT; ++y) {
        for (x = 0; x < EARLY_VGA_WIDTH; ++x) {
            early_vga_mem[y * EARLY_VGA_WIDTH + x] = early_vga_entry(' ');
        }
    }
    early_vga_x = 0;
    early_vga_y = 0;
    early_vga_update_cursor();
}

void early_vga64_putc(char c) {
    if (!early_vga_ready) {
        return;
    }

    if (c == '\n') {
        early_vga_x = 0;
        ++early_vga_y;
    } else if (c == '\r') {
        early_vga_x = 0;
    } else if (c == '\t') {
        early_vga_x = (early_vga_x + 8u) & ~7u;
    } else if (c == '\b') {
        if (early_vga_x > 0) {
            --early_vga_x;
        }
    } else {
        early_vga_mem[early_vga_y * EARLY_VGA_WIDTH + early_vga_x] = early_vga_entry(c);
        ++early_vga_x;
    }

    if (early_vga_x >= EARLY_VGA_WIDTH) {
        early_vga_x = 0;
        ++early_vga_y;
    }
    early_vga_scroll();
    early_vga_update_cursor();
}

void early_vga64_write(const char *text) {
    if (!text) {
        return;
    }
    while (*text) {
        early_vga64_putc(*text++);
    }
}

void early_framebuffer64_init(const early_framebuffer64_info_t *info) {
    if (!info || info->base == 0 || info->width == 0 || info->height == 0 || info->pitch == 0) {
        early_fb_info.available = 0;
        early_fbcon_ready = 0;
        return;
    }
    early_fb_info = *info;
    early_fb_info.available = 1u;

    /* Enable the framebuffer text console only for the pixel format we can draw. */
    if (early_fb_info.bpp == 32u &&
        early_fb_info.format == OPENOS_X86_64_EARLY_FB_FORMAT_XRGB8888) {
        early_fbcon_cols = early_fb_info.width / EARLY_FBCON_GLYPH_W;
        early_fbcon_rows = early_fb_info.height / EARLY_FBCON_GLYPH_H;
        early_fbcon_cx = 0;
        early_fbcon_cy = 0;
        if (early_fbcon_cols != 0u && early_fbcon_rows != 0u) {
            early_fbcon_ready = 1u;
            early_framebuffer64_fill_rect(0, 0, early_fb_info.width, early_fb_info.height,
                                          EARLY_FBCON_BG);
        } else {
            early_fbcon_ready = 0;
        }
    } else {
        early_fbcon_ready = 0;
    }
}

int early_framebuffer64_available(void) {
    return early_fb_info.available != 0;
}

const early_framebuffer64_info_t *early_framebuffer64_get_info(void) {
    return &early_fb_info;
}

void early_framebuffer64_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint8_t *row;

    if (!early_framebuffer64_available() || x >= early_fb_info.width || y >= early_fb_info.height) {
        return;
    }
    if (early_fb_info.bpp != 32u || early_fb_info.format != OPENOS_X86_64_EARLY_FB_FORMAT_XRGB8888) {
        return;
    }

    row = (uint8_t *)(uintptr_t)(early_fb_info.base + (x86_64_size_t)y * early_fb_info.pitch);
    ((uint32_t *)row)[x] = color;
}

void early_framebuffer64_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t yy;
    uint32_t xx;

    for (yy = 0; yy < h; ++yy) {
        for (xx = 0; xx < w; ++xx) {
            early_framebuffer64_put_pixel(x + xx, y + yy, color);
        }
    }
}

/* Draw one 8x8 glyph at character cell (cx, cy) using font8x8_basic. */
static void early_fbcon_draw_glyph(uint32_t cx, uint32_t cy, char c) {
    uint32_t px;
    uint32_t py;
    uint32_t row;
    uint8_t bits;
    const unsigned char *glyph;
    unsigned char uc = (unsigned char)c;

    if (uc >= 128u) {
        uc = (unsigned char)'?';
    }
    glyph = (const unsigned char *)font8x8_basic[uc];

    px = cx * EARLY_FBCON_GLYPH_W;
    py = cy * EARLY_FBCON_GLYPH_H;
    for (row = 0; row < EARLY_FBCON_GLYPH_H; ++row) {
        uint32_t col;
        bits = glyph[row];
        for (col = 0; col < EARLY_FBCON_GLYPH_W; ++col) {
            uint32_t color = ((bits >> col) & 1u) ? EARLY_FBCON_FG : EARLY_FBCON_BG;
            early_framebuffer64_put_pixel(px + col, py + row, color);
        }
    }
}

/* Scroll the framebuffer console up by one text row. */
static void early_fbcon_scroll(void) {
    uint32_t row_bytes;
    uint32_t scroll_h;
    uint8_t *base;
    uint32_t y;

    if (!early_fbcon_ready) {
        return;
    }

    base = (uint8_t *)(uintptr_t)early_fb_info.base;
    row_bytes = early_fb_info.pitch;
    scroll_h = EARLY_FBCON_GLYPH_H;

    /* Move rows [scroll_h .. height) up by scroll_h pixels. */
    for (y = 0; y + scroll_h < early_fb_info.height; ++y) {
        uint8_t *dst = base + (x86_64_size_t)y * row_bytes;
        const uint8_t *src = base + (x86_64_size_t)(y + scroll_h) * row_bytes;
        uint32_t i;
        for (i = 0; i < row_bytes; ++i) {
            dst[i] = src[i];
        }
    }
    /* Clear the last text row. */
    early_framebuffer64_fill_rect(0, (early_fbcon_rows - 1u) * EARLY_FBCON_GLYPH_H,
                                  early_fb_info.width, EARLY_FBCON_GLYPH_H, EARLY_FBCON_BG);
}

static void early_fbcon_putc(char c) {
    if (!early_fbcon_ready || early_fbcon_suppress) {
        return;
    }

    if (c == '\n') {
        early_fbcon_cx = 0;
        ++early_fbcon_cy;
    } else if (c == '\r') {
        early_fbcon_cx = 0;
    } else if (c == '\t') {
        early_fbcon_cx = (early_fbcon_cx + 8u) & ~7u;
    } else if (c == '\b') {
        if (early_fbcon_cx > 0) {
            --early_fbcon_cx;
        }
    } else {
        early_fbcon_draw_glyph(early_fbcon_cx, early_fbcon_cy, c);
        ++early_fbcon_cx;
    }

    if (early_fbcon_cx >= early_fbcon_cols) {
        early_fbcon_cx = 0;
        ++early_fbcon_cy;
    }
    while (early_fbcon_cy >= early_fbcon_rows) {
        early_fbcon_scroll();
        --early_fbcon_cy;
    }
}

void early_console64_init(void) {
    early_serial64_init();
    early_vga64_init();
}

/* 桌面拉起后调用 early_console64_set_fbcon(0)：停止向 framebuffer
 * 贴图字（避免盖住桌面），串口日志不受影响。 */
void early_console64_set_fbcon(int on) {
    early_fbcon_suppress = on ? 0u : 1u;
}

void early_console64_putc(char c) {
    if (c == '\n') {
        early_serial64_putc('\r');
    }
    early_serial64_putc(c);
    early_vga64_putc(c);
    early_fbcon_putc(c);
}

void early_console64_write(const char *text) {
    if (!text) {
        return;
    }
    while (*text) {
        early_console64_putc(*text++);
    }
}

void early_console64_write_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    early_console64_write("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        early_console64_putc(digits[(value >> (uint32_t)shift) & 0xFu]);
    }
}
