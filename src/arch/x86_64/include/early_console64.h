#ifndef OPENOS_ARCH_X86_64_EARLY_CONSOLE64_H
#define OPENOS_ARCH_X86_64_EARLY_CONSOLE64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_EARLY_FB_FORMAT_XRGB8888 1u

typedef struct early_framebuffer64_info {
    x86_64_phys_addr_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    uint8_t available;
} early_framebuffer64_info_t;

void early_console64_init(void);
void early_console64_set_fbcon(int on);
void early_console64_putc(char c);
void early_console64_write(const char *text);
void early_console64_write_hex64(uint64_t value);

void early_serial64_init(void);
void early_serial64_putc(char c);
void early_serial64_write(const char *text);

void early_vga64_init(void);
void early_vga64_clear(void);
void early_vga64_putc(char c);
void early_vga64_write(const char *text);

void early_framebuffer64_init(const early_framebuffer64_info_t *info);
int early_framebuffer64_available(void);
const early_framebuffer64_info_t *early_framebuffer64_get_info(void);
void early_framebuffer64_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void early_framebuffer64_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

#endif /* OPENOS_ARCH_X86_64_EARLY_CONSOLE64_H */
