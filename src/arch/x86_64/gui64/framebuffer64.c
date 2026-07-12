/* ============================================================
 * openos - x86_64 framebuffer backend (UEFI GOP)
 * Provides framebuffer.h API for gui.c / gui_user.c / font.c
 * Uses linear framebuffer set up by UEFI GOP (via early_console64).
 * gui.c never touches raw addr fields, only uses drawing API.
 * ============================================================ */

#include "framebuffer.h"

/* ------------------------------------------------------------
 * early_console64 GOP info —— 布局与 early_framebuffer64_info_t
 * 严格对齐（不能 include early_console64.h，因其拖入 <stdint.h>，
 * 会与 i386 types.h 的 size_t/int64_t typedef 冲突）。
 *
 * 真实结构 (early_console64.h):
 *   x86_64_phys_addr_t base;   // uint64_t, 偏移0, 8字节
 *   uint32_t width;            // 偏移8
 *   uint32_t height;           // 偏移12
 *   uint32_t pitch;            // 偏移16
 *   uint32_t bpp;              // 偏移20
 *   uint32_t format;           // 偏移24
 *   uint8_t  available;        // 偏移28
 * ------------------------------------------------------------ */
typedef struct {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    uint8_t  available;
} early_fb_info_t;

extern const early_fb_info_t *early_framebuffer64_get_info(void);

/* internal state */
static framebuffer_info_t g_fb_info;
static framebuffer_caps_t g_fb_caps;
static volatile uint32_t *g_fb_base = 0;
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static uint32_t g_fb_pitch_px = 0;
static int g_fb_ready = 0;

void framebuffer_init(void)
{
    const early_fb_info_t *fb = early_framebuffer64_get_info();
    if (!fb || fb->base == 0 || fb->width == 0 || fb->height == 0) {
        g_fb_ready = 0;
        return;
    }

    g_fb_base = (volatile uint32_t *)(uint64_t)fb->base;
    g_fb_width = fb->width;
    g_fb_height = fb->height;
    g_fb_pitch_px = fb->pitch ? (fb->pitch / 4) : fb->width;

    g_fb_caps.backend = FRAMEBUFFER_BACKEND_EFI_GOP;
    g_fb_caps.name = "UEFI GOP";
    g_fb_caps.flags = FRAMEBUFFER_CAP_LINEAR | FRAMEBUFFER_CAP_SOFTWARE_2D | FRAMEBUFFER_CAP_ALPHA_BLEND | FRAMEBUFFER_CAP_ROW_BLIT | FRAMEBUFFER_CAP_RECT_BLIT;
    g_fb_caps.max_width = fb->width;
    g_fb_caps.max_height = fb->height;
    g_fb_caps.preferred_bpp = fb->bpp ? fb->bpp : 32;

    g_fb_info.width = fb->width;
    g_fb_info.height = fb->height;
    g_fb_info.pitch = fb->pitch;
    g_fb_info.bpp = fb->bpp ? fb->bpp : 32;
    g_fb_info.bytes_per_pixel = (fb->bpp ? fb->bpp : 32) / 8;
    g_fb_info.phys_addr = (uint32_t)(fb->base & 0xFFFFFFFF);
    g_fb_info.virt_addr = (uint32_t)(fb->base & 0xFFFFFFFF);
    g_fb_info.size = fb->pitch * fb->height;
    g_fb_info.available = 1;
    g_fb_info.mode_set = 1;
    g_fb_info.driver_name = "UEFI GOP";
    g_fb_info.caps = g_fb_caps;

    g_fb_ready = 1;
}

int framebuffer_is_available(void)
{
    return g_fb_ready;
}

int framebuffer_set_mode(uint32_t width, uint32_t height, uint32_t bpp)
{
    (void)bpp;
    if (!g_fb_ready) {
        return -1;
    }
    /* UEFI GOP 分辨率由固件在引导阶段锁定，无法运行时重设。
     * gui_start() 会请求 1024x768，但实际 GOP 分辨率往往不同。
     * 这里不再因分辨率不匹配而失败：始终保留固件实际
     * 分辨率，gui 层会从 framebuffer_get_info() 读回 info->width/
     * height 并以实际值布局桌面，因此直接返回成功。 */
    (void)width;
    (void)height;
    return 0;
}

const framebuffer_info_t *framebuffer_get_info(void)
{
    return &g_fb_info;
}

const framebuffer_caps_t *framebuffer_get_caps(void)
{
    return &g_fb_caps;
}

const char *framebuffer_backend_name(framebuffer_backend_type_t backend)
{
    switch (backend) {
        case FRAMEBUFFER_BACKEND_NONE:       return "none";
        case FRAMEBUFFER_BACKEND_BGA:        return "BGA";
        case FRAMEBUFFER_BACKEND_VESA:       return "VESA";
        case FRAMEBUFFER_BACKEND_EFI_GOP:    return "UEFI GOP";
        case FRAMEBUFFER_BACKEND_VIRTIO_GPU: return "virtio-gpu";
        default:                             return "unknown";
    }
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_fb_ready || x >= g_fb_width || y >= g_fb_height) {
        return;
    }
    g_fb_base[(uint64_t)y * g_fb_pitch_px + x] = color;
}

uint32_t framebuffer_blit_row(uint32_t x, uint32_t y, const uint32_t *src, uint32_t count)
{
    if (!g_fb_ready || !src || y >= g_fb_height || x >= g_fb_width) {
        return 0;
    }
    /* 水平裁剪：不超出可见宽度 */
    uint32_t max = g_fb_width - x;
    if (count > max) {
        count = max;
    }
    /* 32bpp linear 直存：目标行起始地址 + 单调叠拷（消除逐像素函数调用）。
     * VRAM 为 volatile，不能直接 memcpy，逐字写入；编译器仍可向量化/展开。 */
    volatile uint32_t *dst = g_fb_base + (uint64_t)y * g_fb_pitch_px + x;
    for (uint32_t i = 0; i < count; i++) {
        dst[i] = src[i];
    }
    return count;
}

uint32_t framebuffer_blit_rect(uint32_t x, uint32_t y, const uint32_t *src, uint32_t src_stride, uint32_t w, uint32_t h)
{
    if (!g_fb_ready || !src || y >= g_fb_height || x >= g_fb_width || w == 0 || h == 0) {
        return 0;
    }
    /* 边界检查仅一次：右/下越界自动裁剪 */
    uint32_t maxw = g_fb_width - x;
    if (w > maxw) {
        w = maxw;
    }
    uint32_t maxh = g_fb_height - y;
    if (h > maxh) {
        h = maxh;
    }
    /* 逐行直存：目标行步进 g_fb_pitch_px，源行步进 src_stride。
     * 与逐行调用 blit_row 相比，开销、能力位、边界校验均只做一次。 */
    for (uint32_t ry = 0; ry < h; ry++) {
        volatile uint32_t *dst = g_fb_base + (uint64_t)(y + ry) * g_fb_pitch_px + x;
        const uint32_t *srow = src + (uint64_t)ry * src_stride;
        for (uint32_t rx = 0; rx < w; rx++) {
            dst[rx] = srow[rx];
        }
    }
    return h;
}

uint32_t framebuffer_get_pixel(uint32_t x, uint32_t y)
{
    if (!g_fb_ready || x >= g_fb_width || y >= g_fb_height) {
        return 0;
    }
    return g_fb_base[(uint64_t)y * g_fb_pitch_px + x];
}

uint32_t framebuffer_blend_color(uint32_t dst, uint32_t src, uint8_t alpha)
{
    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >> 8) & 0xFF;
    uint32_t sb = src & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = dst & 0xFF;

    uint32_t r = (sr * alpha + dr * (255 - alpha)) / 255;
    uint32_t g = (sg * alpha + dg * (255 - alpha)) / 255;
    uint32_t b = (sb * alpha + db * (255 - alpha)) / 255;

    return (r << 16) | (g << 8) | b;
}

void framebuffer_put_pixel_alpha(uint32_t x, uint32_t y, uint32_t color, uint8_t alpha)
{
    if (!g_fb_ready || x >= g_fb_width || y >= g_fb_height) {
        return;
    }
    if (alpha == 255) {
        framebuffer_put_pixel(x, y, color);
        return;
    }
    if (alpha == 0) {
        return;
    }
    uint32_t dst = framebuffer_get_pixel(x, y);
    framebuffer_put_pixel(x, y, framebuffer_blend_color(dst, color, alpha));
}

void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!g_fb_ready) {
        return;
    }
    uint32_t x1 = x + w;
    uint32_t y1 = y + h;
    if (x1 > g_fb_width) {
        x1 = g_fb_width;
    }
    if (y1 > g_fb_height) {
        y1 = g_fb_height;
    }
    for (uint32_t py = y; py < y1; py++) {
        volatile uint32_t *row = &g_fb_base[(uint64_t)py * g_fb_pitch_px];
        for (uint32_t px = x; px < x1; px++) {
            row[px] = color;
        }
    }
}

void framebuffer_fill_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint8_t alpha)
{
    if (!g_fb_ready) {
        return;
    }
    if (alpha == 255) {
        framebuffer_fill_rect(x, y, w, h, color);
        return;
    }
    if (alpha == 0) {
        return;
    }
    uint32_t x1 = x + w;
    uint32_t y1 = y + h;
    if (x1 > g_fb_width) {
        x1 = g_fb_width;
    }
    if (y1 > g_fb_height) {
        y1 = g_fb_height;
    }
    for (uint32_t py = y; py < y1; py++) {
        for (uint32_t px = x; px < x1; px++) {
            framebuffer_put_pixel_alpha(px, py, color, alpha);
        }
    }
}

void framebuffer_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = adx - ady;

    for (;;) {
        if (x0 >= 0 && y0 >= 0) {
            framebuffer_put_pixel((uint32_t)x0, (uint32_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 > -ady) {
            err -= ady;
            x0 += sx;
        }
        if (e2 < adx) {
            err += adx;
            y0 += sy;
        }
    }
}

void framebuffer_clear(uint32_t color)
{
    if (!g_fb_ready) {
        return;
    }
    framebuffer_fill_rect(0, 0, g_fb_width, g_fb_height, color);
}

void framebuffer_test_pattern(void)
{
    if (!g_fb_ready) {
        return;
    }
    for (uint32_t y = 0; y < g_fb_height; y++) {
        for (uint32_t x = 0; x < g_fb_width; x++) {
            uint8_t r = (uint8_t)(x * 255 / g_fb_width);
            uint8_t g = (uint8_t)(y * 255 / g_fb_height);
            framebuffer_put_pixel(x, y, ((uint32_t)r << 16) | ((uint32_t)g << 8) | 0x40);
        }
    }
}

void framebuffer_print_info(void)
{
    /* optional serial dump; keep minimal */
}
