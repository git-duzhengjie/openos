#include "openos.h"

#define SKIA_DEMO_W 192
#define SKIA_DEMO_H 112

static unsigned int framebuffer[SKIA_DEMO_W * SKIA_DEMO_H];

static unsigned int blend(unsigned int a, unsigned int b, unsigned int t)
{
    unsigned int ar = (a >> 16) & 0xffu;
    unsigned int ag = (a >> 8) & 0xffu;
    unsigned int ab = a & 0xffu;
    unsigned int br = (b >> 16) & 0xffu;
    unsigned int bg = (b >> 8) & 0xffu;
    unsigned int bb = b & 0xffu;
    unsigned int r = (ar * (255u - t) + br * t) / 255u;
    unsigned int g = (ag * (255u - t) + bg * t) / 255u;
    unsigned int c = (ab * (255u - t) + bb * t) / 255u;
    return 0xff000000u | (r << 16) | (g << 8) | c;
}

static void clear_gradient(void)
{
    int x;
    int y;
    for (y = 0; y < SKIA_DEMO_H; ++y) {
        for (x = 0; x < SKIA_DEMO_W; ++x) {
            unsigned int t = (unsigned int)((x * 255) / (SKIA_DEMO_W - 1));
            unsigned int shade = (unsigned int)((y * 48) / (SKIA_DEMO_H - 1));
            framebuffer[y * SKIA_DEMO_W + x] = blend(0xff102040u + shade, 0xff54a6ffu, t);
        }
    }
}

static void put_pixel(int x, int y, unsigned int color)
{
    if (x < 0 || y < 0 || x >= SKIA_DEMO_W || y >= SKIA_DEMO_H) {
        return;
    }
    framebuffer[y * SKIA_DEMO_W + x] = color;
}

static void fill_rect_sw(int x, int y, int w, int h, unsigned int color)
{
    int ix;
    int iy;
    for (iy = 0; iy < h; ++iy) {
        for (ix = 0; ix < w; ++ix) {
            put_pixel(x + ix, y + iy, color);
        }
    }
}

static void stroke_rect_sw(int x, int y, int w, int h, unsigned int color)
{
    int i;
    for (i = 0; i < w; ++i) {
        put_pixel(x + i, y, color);
        put_pixel(x + i, y + h - 1, color);
    }
    for (i = 0; i < h; ++i) {
        put_pixel(x, y + i, color);
        put_pixel(x + w - 1, y + i, color);
    }
}

static void draw_demo_picture(int x, int y)
{
    static const unsigned int icon[16] = {
        0xffff5252u, 0xffffc107u, 0xff4caf50u, 0xff2196f3u,
        0xffff7043u, 0xffffffffu, 0xffffffffu, 0xff5c6bc0u,
        0xffab47bcu, 0xffffffffu, 0xffffffffu, 0xff26a69au,
        0xffec407au, 0xffffee58u, 0xff66bb6au, 0xff42a5f5u
    };
    int ix;
    int iy;
    for (iy = 0; iy < 4; ++iy) {
        for (ix = 0; ix < 4; ++ix) {
            fill_rect_sw(x + ix * 10, y + iy * 10, 8, 8, icon[iy * 4 + ix]);
        }
    }
    stroke_rect_sw(x - 2, y - 2, 42, 42, 0xffffffffu);
}

int main(void)
{
    int win;
    int ok;
    openos_font_query_t font;

    win = openos_gui_create_window("Skia Demo", 96, 96, 260, 180);
    if (win < 0) {
        printf("skia_demo: failed to create window\n");
        return 1;
    }

    clear_gradient();
    fill_rect_sw(12, 14, 76, 32, 0xff1de9b6u);
    fill_rect_sw(24, 26, 96, 40, 0xcc0d47a1u);
    stroke_rect_sw(10, 12, 112, 58, 0xffffffffu);
    fill_rect_sw(135, 16, 40, 40, 0xff263238u);
    draw_demo_picture(136, 17);
    fill_rect_sw(18, 82, 152, 12, 0xfffff176u);
    stroke_rect_sw(16, 80, 156, 16, 0xff202124u);

    ok = 0;
    if (openos_gui_fill_rect(win, 0, 0, 260, 180, 0xff202124u) == 0) ok++;
    if (openos_gui_blit_rgba32(win, 18, 42, SKIA_DEMO_W, SKIA_DEMO_H, framebuffer, SKIA_DEMO_W) == 0) ok++;
    if (openos_gui_draw_text(win, 18, 18, "Skia software raster demo", 0xffffffffu) == 0) ok++;
    if (openos_gui_draw_text(win, 24, 126, "rect + text + image", 0xff202124u) == 0) ok++;

    memset(&font, 0, sizeof(font));
    strncpy(font.text, "Skia", sizeof(font.text) - 1);
    if (openos_font_query(&font) == 0) {
        char metrics[96];
        snprintf(metrics, sizeof(metrics), "font %ux%u line %u", font.text_width, font.text_height, font.line_height);
        openos_gui_draw_text(win, 18, 158, metrics, 0xffbbdefbu);
    }

    if (openos_gui_present(win) == 0) ok++;
    printf("skia_demo: window=%d software-raster=%dx%d checks=%d/5\n", win, SKIA_DEMO_W, SKIA_DEMO_H, ok);
    return ok == 5 ? 0 : 1;
}
