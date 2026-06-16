#include "../include/early_console64.h"

#define EARLY_COM1_PORT 0x3F8u
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
        return;
    }
    early_fb_info = *info;
    early_fb_info.available = 1u;
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

void early_console64_init(void) {
    early_serial64_init();
    early_vga64_init();
}

void early_console64_putc(char c) {
    if (c == '\n') {
        early_serial64_putc('\r');
    }
    early_serial64_putc(c);
    early_vga64_putc(c);
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
