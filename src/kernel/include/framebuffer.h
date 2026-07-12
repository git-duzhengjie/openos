/* ============================================================
 * openos - Linear Framebuffer Driver
 * First backend: Bochs/QEMU BGA (VBE-compatible I/O ports)
 * ============================================================ */

#ifndef OPENOS_FRAMEBUFFER_H
#define OPENOS_FRAMEBUFFER_H

#include "types.h"

#define FB_DEFAULT_WIDTH   1024u
#define FB_DEFAULT_HEIGHT  768u
#define FB_DEFAULT_BPP     32u

#define FB_COLOR_BLACK     0x00000000u
#define FB_COLOR_WHITE     0x00FFFFFFu
#define FB_COLOR_RED       0x00FF0000u
#define FB_COLOR_GREEN     0x0000FF00u
#define FB_COLOR_BLUE      0x000000FFu
#define FB_COLOR_CYAN      0x0000FFFFu
#define FB_COLOR_MAGENTA   0x00FF00FFu
#define FB_COLOR_YELLOW    0x00FFFF00u
#define FB_COLOR_GRAY      0x00808080u

/* 面向对象风格接口：后续可接入 VESA/EFI/GOP/virtio-gpu 后端 */
typedef struct framebuffer_driver framebuffer_driver_t;

struct framebuffer_driver {
    const char *name;
    int (*probe)(framebuffer_driver_t *drv);
    int (*set_mode)(framebuffer_driver_t *drv, uint32_t width, uint32_t height, uint32_t bpp);
    void (*clear)(framebuffer_driver_t *drv, uint32_t color);
    void (*put_pixel)(framebuffer_driver_t *drv, uint32_t x, uint32_t y, uint32_t color);
    void (*fill_rect)(framebuffer_driver_t *drv, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*draw_line)(framebuffer_driver_t *drv, int x0, int y0, int x1, int y1, uint32_t color);
    void *state;
};

typedef enum framebuffer_backend_type {
    FRAMEBUFFER_BACKEND_NONE = 0,
    FRAMEBUFFER_BACKEND_BGA,
    FRAMEBUFFER_BACKEND_VESA,
    FRAMEBUFFER_BACKEND_EFI_GOP,
    FRAMEBUFFER_BACKEND_VIRTIO_GPU
} framebuffer_backend_type_t;

typedef struct framebuffer_caps {
    framebuffer_backend_type_t backend;
    const char *name;
    uint32_t flags;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t preferred_bpp;
} framebuffer_caps_t;

#define FRAMEBUFFER_CAP_LINEAR      0x00000001u
#define FRAMEBUFFER_CAP_MODESET     0x00000002u
#define FRAMEBUFFER_CAP_SOFTWARE_2D 0x00000004u
#define FRAMEBUFFER_CAP_ALPHA_BLEND 0x00000008u
#define FRAMEBUFFER_CAP_ROW_BLIT    0x00000010u  /* 支持整行 memcpy blit（32bpp linear 直存） */
#define FRAMEBUFFER_CAP_RECT_BLIT   0x00000020u  /* 支持矩形块 blit（单次调用多行，边界检查仅一次） */

typedef struct framebuffer_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t bytes_per_pixel;
    uint32_t phys_addr;
    uint32_t virt_addr;
    uint32_t size;
    int available;
    int mode_set;
    const char *driver_name;
    framebuffer_caps_t caps;
} framebuffer_info_t;

void framebuffer_init(void);
int framebuffer_is_available(void);
int framebuffer_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
const framebuffer_info_t *framebuffer_get_info(void);
const framebuffer_caps_t *framebuffer_get_caps(void);
const char *framebuffer_backend_name(framebuffer_backend_type_t backend);

void framebuffer_clear(uint32_t color);
void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t framebuffer_get_pixel(uint32_t x, uint32_t y);
uint32_t framebuffer_blend_color(uint32_t dst, uint32_t src, uint8_t alpha);
void framebuffer_put_pixel_alpha(uint32_t x, uint32_t y, uint32_t color, uint8_t alpha);
void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
/* M6.3 图形加速：整行 blit 原语。
 * 将 src[0..count) 连续 32bpp 像素写入 VRAM 行 (x,y) 起始处。
 * 32bpp linear 直存后端走单次 memcpy（消除逐像素函数调用+地址重算）；
 * 越界部分自动裁剪。返回实际写入的像素数。 */
uint32_t framebuffer_blit_row(uint32_t x, uint32_t y, const uint32_t *src, uint32_t count);
/* M6.3d 图形加速：矩形块 blit 原语。
 * 将源缓冲 src（行距 src_stride 像素）的 w×h 矩形块写入 VRAM (x,y)。
 * 与逐行调用 blit_row 相比，边界/能力检查仅执行一次，
 * 内层逐行 memcpy（编译器更易展开/向量化）；右/下越界自动裁剪。
 * 返回实际写入的行数（裁剪后）。 */
uint32_t framebuffer_blit_rect(uint32_t x, uint32_t y, const uint32_t *src, uint32_t src_stride, uint32_t w, uint32_t h);
void framebuffer_fill_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint8_t alpha);
void framebuffer_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void framebuffer_test_pattern(void);
void framebuffer_print_info(void);

#endif /* OPENOS_FRAMEBUFFER_H */
