/* ============================================================
 * openos - Minimal GUI / Window System
 *
 * 鏀寔锛欸UI 缁堢銆丳S/2 榧犳爣鍏夋爣銆佷簨浠堕槦鍒椼€佹寜閽偣鍑伙拷?
 *       绐楀彛鎷栧姩/缃《/鍏抽棴/鏈€灏忓寲銆佸弻缂撳啿娓叉煋锟?
 * ============================================================ */

#include "gui.h"
#include "framebuffer.h"
#include "mouse.h"
#include "usb_tablet.h"
#include "font.h"
#include "serial.h"
#include "string.h"
#include "heap.h"
#include "input_buffer.h"
#include "fs/vfs.h"

static void gui_desktop_run_action(uint32_t action);
static void gui_file_preview_open(void);
static void gui_file_preview_render_list(void);
static void gui_file_preview_render_view(void);
static void gui_file_preview_rebuild(void);

static gui_system_t g_gui;
static gui_accel_info_t g_gui_accel;

#ifndef GUI_DEBUG_LOG
#define GUI_DEBUG_LOG 0
#endif

#ifndef GUI_TERMINAL_START_SHELL
#define GUI_TERMINAL_START_SHELL 1
#endif

#define GUI_TERMINAL_OUTPUT_QUEUE_SIZE 4096u
#define GUI_TERMINAL_OUTPUT_DRAIN_LIMIT 128u
#define GUI_DESKTOP_ACTION_TERMINAL 1u
#define GUI_DESKTOP_ACTION_ABOUT    2u
#define GUI_DESKTOP_ACTION_MENU     3u
#define GUI_DESKTOP_ACTION_DEMO     4u
#define GUI_DESKTOP_ACTION_FILES    5u
static volatile uint32_t g_terminal_out_head = 0;
static volatile uint32_t g_terminal_out_tail = 0;
static char g_terminal_out_queue[GUI_TERMINAL_OUTPUT_QUEUE_SIZE];

static int gui_compositor_active(void) {
    return g_gui.initialized && g_gui.compositor_enabled &&
           g_gui.double_buffered && g_gui.backbuffer;
}

int gui_accel_is_enabled(void) {
    return g_gui_accel.enabled;
}

void gui_get_accel_info(gui_accel_info_t *info) {
    if (!info) return;
    *info = g_gui_accel;
}

int gui_get_desktop_info(gui_desktop_info_t *info) {
    if (!info) return -1;
    info->enabled = g_gui.desktop_enabled;
    info->start_menu_open = g_gui.desktop_start_menu_open;
    info->icon_count = g_gui.desktop_icon_count;
    info->taskbar_rect = g_gui.desktop_taskbar_rect;
    info->start_button_rect = g_gui.desktop_start_button_rect;
    info->start_menu_rect = g_gui.desktop_start_menu_rect;
    return 0;
}

int gui_get_launcher_info(gui_launcher_info_t *info) {
    if (!info) return -1;
    info->enabled = g_gui.launcher_enabled;
    info->app_count = g_gui.launcher_app_count;
    info->menu_rect = g_gui.desktop_start_menu_rect;
    return 0;
}

int gui_launcher_launch(uint32_t index) {
    if (!g_gui.launcher_enabled || index >= g_gui.launcher_app_count) return -1;
    if (!g_gui.launcher_entries[index].used) return -1;
    gui_desktop_run_action(g_gui.launcher_entries[index].action);
    return 0;
}

static int gui_rect_contains(const gui_rect_t *r, int x, int y);
static int gui_rect_intersect(const gui_rect_t *a, const gui_rect_t *b, gui_rect_t *out);
static gui_window_t *gui_top_window(void);
static void gui_set_hovered_widget(gui_widget_t *wg);
static gui_app_t *gui_app_for_window(gui_window_t *window);
static void gui_refresh_active_app(void);
static void gui_demo_button(gui_widget_t *widget, void *user_data);
static void gui_terminal_invalidate_cursor(void);
static void gui_terminal_invalidate_body(void);
static void gui_terminal_drain_output_queue(void);
static void gui_desktop_init(void);
static void gui_desktop_draw(void);
static int gui_desktop_handle_click(int x, int y);
static void gui_launcher_init(void);
static int gui_terminal_point_to_cell(int x, int y, uint32_t *col, uint32_t *row);
static void gui_terminal_update_selection(uint32_t col, uint32_t row);
static int gui_terminal_cell_selected(uint32_t col, uint32_t row);
void gui_terminal_set_input_focus(int focused);

static uint32_t gui_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t gui_mix_rgb(uint32_t a, uint32_t b, uint32_t amount) {
    uint32_t ar = (a >> 16) & 0xffu;
    uint32_t ag = (a >> 8) & 0xffu;
    uint32_t ab = a & 0xffu;
    uint32_t br = (b >> 16) & 0xffu;
    uint32_t bg = (b >> 8) & 0xffu;
    uint32_t bb = b & 0xffu;
    uint32_t inv;

    if (amount > 255u) amount = 255u;
    inv = 255u - amount;
    return gui_rgb((uint8_t)((ar * inv + br * amount) / 255u),
                   (uint8_t)((ag * inv + bg * amount) / 255u),
                   (uint8_t)((ab * inv + bb * amount) / 255u));
}

static void gui_write_dec(uint32_t value) {
    char buf[11];
    int i = 0;
    if (value == 0) {
        serial_write("0");
        return;
    }
    while (value > 0 && i < 10) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

static void gui_copy_text(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void gui_put_pixel_unclipped(int x, int y, uint32_t color) {
    if (!g_gui.initialized) return;
    if (x < 0 || y < 0 || x >= (int)g_gui.width || y >= (int)g_gui.height) return;
    if (gui_compositor_active()) {
        g_gui.backbuffer[(uint32_t)y * g_gui.width + (uint32_t)x] = color;
    } else {
        framebuffer_put_pixel((uint32_t)x, (uint32_t)y, color);
    }
}

static void gui_raw_put_pixel(int x, int y, uint32_t color) {
    if (g_gui.clip_enabled && !gui_rect_contains(&g_gui.clip_rect, x, y)) return;
    gui_put_pixel_unclipped(x, y, color);
}

static void gui_raw_fill_rect(int x, int y, int w, int h, uint32_t color) {
    int yy, xx;
    gui_rect_t rect;
    gui_rect_t clipped;
    uint32_t pixels;
    if (w <= 0 || h <= 0) return;
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    clipped.x = 0;
    clipped.y = 0;
    clipped.w = (int)g_gui.width;
    clipped.h = (int)g_gui.height;
    if (!gui_rect_intersect(&rect, &clipped, &rect)) return;
    if (g_gui.clip_enabled) {
        if (!gui_rect_intersect(&rect, &g_gui.clip_rect, &clipped)) return;
        rect = clipped;
    }
    pixels = (uint32_t)rect.w * (uint32_t)rect.h;
    g_gui_accel.rect_fills++;
    g_gui_accel.rect_fill_pixels += pixels;
    if (gui_compositor_active()) {
        for (yy = rect.y; yy < rect.y + rect.h; yy++) {
            uint32_t *dst = &g_gui.backbuffer[(uint32_t)yy * g_gui.width + (uint32_t)rect.x];
            for (xx = 0; xx < rect.w; xx++) {
                dst[xx] = color;
            }
        }
        g_gui_accel.backbuffer_fast_fills++;
        return;
    }
    framebuffer_fill_rect((uint32_t)rect.x, (uint32_t)rect.y, (uint32_t)rect.w, (uint32_t)rect.h, color);
    g_gui_accel.framebuffer_fast_fills++;
}

static void gui_raw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        gui_raw_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static int gui_rect_intersect(const gui_rect_t *a, const gui_rect_t *b, gui_rect_t *out) {
    int x1, y1, x2, y2;
    if (!a || !b || !out) return 0;
    x1 = a->x > b->x ? a->x : b->x;
    y1 = a->y > b->y ? a->y : b->y;
    x2 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    y2 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    if (x2 <= x1 || y2 <= y1) return 0;
    out->x = x1; out->y = y1; out->w = x2 - x1; out->h = y2 - y1;
    return 1;
}

static void gui_apply_clip_rect(const gui_rect_t *rect) {
    gui_rect_t screen;
    gui_rect_t clipped;
    gui_rect_t effective;
    if (!rect) {
        if (g_gui.render_clip_enabled) {
            g_gui.clip_rect = g_gui.render_clip_rect;
            g_gui.clip_enabled = 1;
        } else {
            g_gui.clip_enabled = 0;
        }
        return;
    }
    screen.x = 0; screen.y = 0; screen.w = (int)g_gui.width; screen.h = (int)g_gui.height;
    if (!gui_rect_intersect(rect, &screen, &clipped)) {
        g_gui.clip_rect.x = 0; g_gui.clip_rect.y = 0; g_gui.clip_rect.w = 0; g_gui.clip_rect.h = 0;
        g_gui.clip_enabled = 1;
        return;
    }
    if (g_gui.render_clip_enabled) {
        if (!gui_rect_intersect(&clipped, &g_gui.render_clip_rect, &effective)) {
            g_gui.clip_rect.x = 0; g_gui.clip_rect.y = 0; g_gui.clip_rect.w = 0; g_gui.clip_rect.h = 0;
            g_gui.clip_enabled = 1;
            return;
        }
        clipped = effective;
    }
    g_gui.clip_rect = clipped;
    g_gui.clip_enabled = 1;
}

static void gui_set_clip_rect(const gui_rect_t *rect) {
    gui_apply_clip_rect(rect);
}

static void gui_clear_clip_rect(void) {
    gui_apply_clip_rect(0);
}

static void gui_push_render_clip(const gui_rect_t *rect) {
    if (rect && rect->w > 0 && rect->h > 0) {
        g_gui.render_clip_rect = *rect;
        g_gui.render_clip_enabled = 1;
        gui_apply_clip_rect(0);
    }
}

static void gui_pop_render_clip(void) {
    g_gui.render_clip_enabled = 0;
    gui_apply_clip_rect(0);
}

static void gui_font_put_pixel(void *ctx, int x, int y, uint32_t color) {
    (void)ctx;
    gui_raw_put_pixel(x, y, color);
}

void gui_draw_char(int x, int y, char ch, uint32_t color) {
    font_draw_char(font_get_default(), gui_font_put_pixel, 0, x, y, ch, color);
}

void gui_draw_text(int x, int y, const char *text, uint32_t color) {
    font_draw_text(font_get_default(), gui_font_put_pixel, 0, x, y, text, color);
}

static void gui_title_rect_px(int x, int y, int w, int h, uint32_t color, const gui_rect_t *clip);

static uint8_t gui_glyph5x7_row(char ch, int row) {
    if (ch >= 'a' && ch <= 'z') {
        switch (ch) {
            case 'a': { static const uint8_t r[7] = {0,0,14,1,15,17,15}; return r[row]; }
            case 'b': { static const uint8_t r[7] = {16,16,30,17,17,17,30}; return r[row]; }
            case 'c': { static const uint8_t r[7] = {0,0,14,17,16,17,14}; return r[row]; }
            case 'd': { static const uint8_t r[7] = {1,1,15,17,17,17,15}; return r[row]; }
            case 'e': { static const uint8_t r[7] = {0,0,14,17,31,16,14}; return r[row]; }
            case 'f': { static const uint8_t r[7] = {6,9,8,28,8,8,8}; return r[row]; }
            case 'g': { static const uint8_t r[7] = {0,0,15,17,17,15,1}; return r[row]; }
            case 'h': { static const uint8_t r[7] = {16,16,30,17,17,17,17}; return r[row]; }
            case 'i': { static const uint8_t r[7] = {4,0,12,4,4,4,14}; return r[row]; }
            case 'j': { static const uint8_t r[7] = {2,0,6,2,2,18,12}; return r[row]; }
            case 'k': { static const uint8_t r[7] = {16,16,18,20,24,20,18}; return r[row]; }
            case 'l': { static const uint8_t r[7] = {12,4,4,4,4,4,14}; return r[row]; }
            case 'm': { static const uint8_t r[7] = {0,0,26,21,21,17,17}; return r[row]; }
            case 'n': { static const uint8_t r[7] = {0,0,30,17,17,17,17}; return r[row]; }
            case 'o': { static const uint8_t r[7] = {0,0,14,17,17,17,14}; return r[row]; }
            case 'p': { static const uint8_t r[7] = {0,0,30,17,17,30,16}; return r[row]; }
            case 'q': { static const uint8_t r[7] = {0,0,15,17,17,15,1}; return r[row]; }
            case 'r': { static const uint8_t r[7] = {0,0,22,25,16,16,16}; return r[row]; }
            case 's': { static const uint8_t r[7] = {0,0,15,16,14,1,30}; return r[row]; }
            case 't': { static const uint8_t r[7] = {8,8,28,8,8,9,6}; return r[row]; }
            case 'u': { static const uint8_t r[7] = {0,0,17,17,17,17,15}; return r[row]; }
            case 'v': { static const uint8_t r[7] = {0,0,17,17,17,10,4}; return r[row]; }
            case 'w': { static const uint8_t r[7] = {0,0,17,17,21,21,10}; return r[row]; }
            case 'x': { static const uint8_t r[7] = {0,0,17,10,4,10,17}; return r[row]; }
            case 'y': { static const uint8_t r[7] = {0,0,17,17,17,15,1}; return r[row]; }
            case 'z': { static const uint8_t r[7] = {0,0,31,2,4,8,31}; return r[row]; }
        }
    }
    switch (ch) {
        case 'A': case 'a': { static const uint8_t r[7] = {14,17,17,31,17,17,17}; return r[row]; }
        case 'B': case 'b': { static const uint8_t r[7] = {30,17,17,30,17,17,30}; return r[row]; }
        case 'C': case 'c': { static const uint8_t r[7] = {14,17,16,16,16,17,14}; return r[row]; }
        case 'D': case 'd': { static const uint8_t r[7] = {30,17,17,17,17,17,30}; return r[row]; }
        case 'E': case 'e': { static const uint8_t r[7] = {31,16,16,30,16,16,31}; return r[row]; }
        case 'F': case 'f': { static const uint8_t r[7] = {31,16,16,30,16,16,16}; return r[row]; }
        case 'G': case 'g': { static const uint8_t r[7] = {14,17,16,23,17,17,14}; return r[row]; }
        case 'H': case 'h': { static const uint8_t r[7] = {17,17,17,31,17,17,17}; return r[row]; }
        case 'I': case 'i': { static const uint8_t r[7] = {31,4,4,4,4,4,31}; return r[row]; }
        case 'J': case 'j': { static const uint8_t r[7] = {7,2,2,2,18,18,12}; return r[row]; }
        case 'K': case 'k': { static const uint8_t r[7] = {17,18,20,24,20,18,17}; return r[row]; }
        case 'L': case 'l': { static const uint8_t r[7] = {16,16,16,16,16,16,31}; return r[row]; }
        case 'M': case 'm': { static const uint8_t r[7] = {17,27,21,21,17,17,17}; return r[row]; }
        case 'N': case 'n': { static const uint8_t r[7] = {17,25,21,19,17,17,17}; return r[row]; }
        case 'O': case 'o': { static const uint8_t r[7] = {14,17,17,17,17,17,14}; return r[row]; }
        case 'P': case 'p': { static const uint8_t r[7] = {30,17,17,30,16,16,16}; return r[row]; }
        case 'Q': case 'q': { static const uint8_t r[7] = {14,17,17,17,21,18,13}; return r[row]; }
        case 'R': case 'r': { static const uint8_t r[7] = {30,17,17,30,20,18,17}; return r[row]; }
        case 'S': case 's': { static const uint8_t r[7] = {15,16,16,14,1,1,30}; return r[row]; }
        case 'T': case 't': { static const uint8_t r[7] = {31,4,4,4,4,4,4}; return r[row]; }
        case 'U': case 'u': { static const uint8_t r[7] = {17,17,17,17,17,17,14}; return r[row]; }
        case 'V': case 'v': { static const uint8_t r[7] = {17,17,17,17,17,10,4}; return r[row]; }
        case 'W': case 'w': { static const uint8_t r[7] = {17,17,17,21,21,21,10}; return r[row]; }
        case 'X': case 'x': { static const uint8_t r[7] = {17,17,10,4,10,17,17}; return r[row]; }
        case 'Y': case 'y': { static const uint8_t r[7] = {17,17,10,4,4,4,4}; return r[row]; }
        case 'Z': case 'z': { static const uint8_t r[7] = {31,1,2,4,8,16,31}; return r[row]; }
        case '0': { static const uint8_t r[7] = {14,17,19,21,25,17,14}; return r[row]; }
        case '1': { static const uint8_t r[7] = {4,12,4,4,4,4,14}; return r[row]; }
        case '2': { static const uint8_t r[7] = {14,17,1,2,4,8,31}; return r[row]; }
        case '3': { static const uint8_t r[7] = {30,1,1,14,1,1,30}; return r[row]; }
        case '4': { static const uint8_t r[7] = {2,6,10,18,31,2,2}; return r[row]; }
        case '5': { static const uint8_t r[7] = {31,16,16,30,1,1,30}; return r[row]; }
        case '6': { static const uint8_t r[7] = {14,16,16,30,17,17,14}; return r[row]; }
        case '7': { static const uint8_t r[7] = {31,1,2,4,8,8,8}; return r[row]; }
        case '8': { static const uint8_t r[7] = {14,17,17,14,17,17,14}; return r[row]; }
        case '9': { static const uint8_t r[7] = {14,17,17,15,1,1,14}; return r[row]; }
        case '!': { static const uint8_t r[7] = {4,4,4,4,4,0,4}; return r[row]; }
        case '"': { static const uint8_t r[7] = {10,10,10,0,0,0,0}; return r[row]; }
        case '#': { static const uint8_t r[7] = {10,10,31,10,31,10,10}; return r[row]; }
        case '$': { static const uint8_t r[7] = {4,15,20,14,5,30,4}; return r[row]; }
        case '%': { static const uint8_t r[7] = {24,25,2,4,8,19,3}; return r[row]; }
        case '&': { static const uint8_t r[7] = {12,18,20,8,21,18,13}; return r[row]; }
        case '\'': { static const uint8_t r[7] = {4,4,8,0,0,0,0}; return r[row]; }
        case '(': { static const uint8_t r[7] = {2,4,8,8,8,4,2}; return r[row]; }
        case ')': { static const uint8_t r[7] = {8,4,2,2,2,4,8}; return r[row]; }
        case '*': { static const uint8_t r[7] = {0,10,4,31,4,10,0}; return r[row]; }
        case '+': { static const uint8_t r[7] = {0,4,4,31,4,4,0}; return r[row]; }
        case ',': { static const uint8_t r[7] = {0,0,0,0,0,4,8}; return r[row]; }
        case '-': { static const uint8_t r[7] = {0,0,0,14,0,0,0}; return r[row]; }
        case '_': { static const uint8_t r[7] = {0,0,0,0,0,0,31}; return r[row]; }
        case '.': { static const uint8_t r[7] = {0,0,0,0,0,12,12}; return r[row]; }
        case ':': { static const uint8_t r[7] = {0,12,12,0,12,12,0}; return r[row]; }
        case ';': { static const uint8_t r[7] = {0,4,4,0,0,4,8}; return r[row]; }
        case '<': { static const uint8_t r[7] = {2,4,8,16,8,4,2}; return r[row]; }
        case '=': { static const uint8_t r[7] = {0,0,31,0,31,0,0}; return r[row]; }
        case '>': { static const uint8_t r[7] = {8,4,2,1,2,4,8}; return r[row]; }
        case '?': { static const uint8_t r[7] = {14,17,1,2,4,0,4}; return r[row]; }
        case '@': { static const uint8_t r[7] = {14,17,23,21,23,16,14}; return r[row]; }
        case '[': { static const uint8_t r[7] = {14,8,8,8,8,8,14}; return r[row]; }
        case '\\': { static const uint8_t r[7] = {16,8,8,4,2,2,1}; return r[row]; }
        case ']': { static const uint8_t r[7] = {14,2,2,2,2,2,14}; return r[row]; }
        case '^': { static const uint8_t r[7] = {4,10,17,0,0,0,0}; return r[row]; }
        case '`': { static const uint8_t r[7] = {8,4,2,0,0,0,0}; return r[row]; }
        case '{': { static const uint8_t r[7] = {6,8,8,16,8,8,6}; return r[row]; }
        case '|': { static const uint8_t r[7] = {4,4,4,0,4,4,4}; return r[row]; }
        case '}': { static const uint8_t r[7] = {12,2,2,1,2,2,12}; return r[row]; }
        case '~': { static const uint8_t r[7] = {0,0,13,18,0,0,0}; return r[row]; }
        case '/': { static const uint8_t r[7] = {1,2,2,4,8,8,16}; return r[row]; }
        case ' ': return 0;
        default: { static const uint8_t r[7] = {14,17,1,2,4,0,4}; return r[row]; }
    }
}

static void gui_draw_rect_char5x7(int x, int y, char ch, uint32_t color, const gui_rect_t *clip) {
    int row;
    int col;
    if (!clip || clip->w <= 0 || clip->h <= 0) return;
    for (row = 0; row < 7; row++) {
        uint8_t bits = gui_glyph5x7_row(ch, row);
        for (col = 0; col < 5; col++) {
            if (bits & (uint8_t)(1u << (4 - col))) {
                gui_title_rect_px(x + col, y + row, 1, 1, color, clip);
            }
        }
    }
}

static void gui_draw_text_clipped_direct(int x, int y, const char *text, uint32_t color, const gui_rect_t *clip) {
    uint32_t i;
    int cx;
    if (!clip || clip->w <= 0 || clip->h <= 0 || !text) return;
    cx = x;
    for (i = 0; i < 63u; i++) {
        char ch = text[i];
        if (ch == '\0') break;
        if (cx > clip->x + clip->w - 1) break;
        gui_draw_rect_char5x7(cx, y, ch, color, clip);
        cx += 6;
    }
}

static void gui_title_rect_px(int x, int y, int w, int h, uint32_t color, const gui_rect_t *clip) {
    int x2;
    int y2;
    if (!clip || w <= 0 || h <= 0) return;
    if (x < clip->x) {
        w -= clip->x - x;
        x = clip->x;
    }
    if (y < clip->y) {
        h -= clip->y - y;
        y = clip->y;
    }
    x2 = clip->x + clip->w;
    y2 = clip->y + clip->h;
    if (x + w > x2) w = x2 - x;
    if (y + h > y2) h = y2 - y;
    if (w <= 0 || h <= 0) return;
    gui_raw_fill_rect(x, y, w, h, color);
}

static void gui_draw_window_title_text(int x, int y, const char *text, uint32_t color, const gui_rect_t *clip) {
    gui_draw_text_clipped_direct(x, y, text, color, clip);
}
static int gui_rect_contains(const gui_rect_t *r, int x, int y) {
    return r && x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static gui_rect_t gui_title_rect(gui_window_t *w) {
    gui_rect_t r;
    r.x = w->rect.x;
    r.y = w->rect.y;
    r.w = w->rect.w;
    r.h = GUI_TITLE_HEIGHT;
    return r;
}

static gui_rect_t gui_close_rect(gui_window_t *w) {
    gui_rect_t r;
    r.x = w->rect.x + w->rect.w - GUI_TITLE_HEIGHT + 3;
    r.y = w->rect.y + 4;
    r.w = GUI_TITLE_HEIGHT - 8;
    r.h = GUI_TITLE_HEIGHT - 8;
    return r;
}

static gui_rect_t gui_min_rect(gui_window_t *w) {
    gui_rect_t r;
    r.x = w->rect.x + w->rect.w - GUI_TITLE_HEIGHT * 2 + 5;
    r.y = w->rect.y + 4;
    r.w = GUI_TITLE_HEIGHT - 8;
    r.h = GUI_TITLE_HEIGHT - 8;
    return r;
}

static int gui_window_index(gui_window_t *window) {
    uint32_t idx;
    if (!window) return -1;
    if (window < &g_gui.windows[0] || window >= &g_gui.windows[GUI_MAX_WINDOWS]) return -1;
    idx = (uint32_t)(window - g_gui.windows);
    if (!g_gui.windows[idx].used) return -1;
    return (int)idx;
}

static gui_window_t *gui_top_window(void) {
    int i;
    for (i = (int)g_gui.window_count - 1; i >= 0; i--) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!w->used || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) continue;
        return w;
    }
    return 0;
}

static gui_window_t *gui_window_at(int x, int y) {
    int i;
    for (i = (int)g_gui.window_count - 1; i >= 0; i--) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!w->used || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) continue;
        if (gui_rect_contains(&w->rect, x, y)) return w;
    }
    return 0;
}

gui_window_t *gui_get_window_at(int x, int y) { return gui_window_at(x, y); }

void gui_cycle_windows(void) {
    uint32_t i;
    if (!g_gui.window_count) return;
    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[g_gui.window_count - 1 - i];
        gui_window_t *w;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (w != g_gui.active_window && w->used && w->visible && !(w->flags & GUI_WINDOW_FLAG_MINIMIZED)) {
            gui_set_active_window(w);
            return;
        }
    }
}

static gui_widget_t *gui_widget_at(gui_window_t *w, int sx, int sy) {
    uint32_t i;
    if (!w) return 0;
    for (i = 0; i < w->widget_count; i++) {
        gui_widget_t *wg = &w->widgets[i];
        if (!wg->visible || !wg->enabled) continue;
        if (gui_rect_contains(&wg->rect, sx, sy)) return wg;
    }
    return 0;
}

static int gui_widget_can_focus(gui_widget_t *wg) {
    return wg && wg->visible && wg->enabled &&
           (wg->type == GUI_WIDGET_TEXTBOX || wg->type == GUI_WIDGET_BUTTON);
}

static int gui_widget_is_clickable(gui_widget_t *wg) {
    return wg && wg->visible && wg->enabled && wg->type == GUI_WIDGET_BUTTON;
}

static void gui_button_activate(gui_widget_t *wg) {
    gui_event_t ev;
    if (!gui_widget_is_clickable(wg)) return;
    memset(&ev, 0, sizeof(ev));
    ev.type = GUI_EVENT_BUTTON_CLICK;
    ev.window = wg->owner;
    ev.widget = wg;
    gui_event_push(ev);
}

static void gui_set_hovered_widget(gui_widget_t *wg) {
    if (wg && !gui_widget_is_clickable(wg)) wg = 0;
    if (g_gui.hovered_widget == wg) return;
    if (g_gui.hovered_widget) g_gui.hovered_widget->hovered = 0;
    g_gui.hovered_widget = wg;
    if (g_gui.hovered_widget) g_gui.hovered_widget->hovered = 1;
    gui_invalidate_all();
}

static gui_widget_t *gui_widget_at_screen(int x, int y) {
    gui_window_t *w = gui_window_at(x, y);
    if (!w) return 0;
    return gui_widget_at(w, x - w->rect.x - GUI_BORDER_SIZE, y - w->rect.y - GUI_TITLE_HEIGHT);
}

static void gui_set_focused_widget(gui_widget_t *wg) {
    if (g_gui.focused_widget == wg) return;
    if (g_gui.focused_widget) g_gui.focused_widget->focused = 0;
    g_gui.focused_widget = 0;
    if (gui_widget_can_focus(wg)) {
        if (wg->type == GUI_WIDGET_TEXTBOX) {
            uint32_t len = (uint32_t)strlen(wg->text);
            if (wg->cursor > len) wg->cursor = len;
        }
        wg->focused = 1;
        g_gui.focused_widget = wg;
        gui_terminal_set_input_focus(0);
    }
    gui_invalidate_all();
}

static void gui_focus_first_widget(gui_window_t *window) {
    uint32_t i;
    if (!window || !window->used || !window->visible || (window->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;
    for (i = 0; i < window->widget_count; i++) {
        if (gui_widget_can_focus(&window->widgets[i])) {
            gui_set_focused_widget(&window->widgets[i]);
            return;
        }
    }
    if (g_gui.focused_widget && g_gui.focused_widget->owner == window) gui_set_focused_widget(0);
}

static void gui_focus_active_window_default(void) {
    gui_window_t *w = g_gui.active_window ? g_gui.active_window : gui_top_window();
    if (!w || w == g_gui.terminal.window || (w->flags & GUI_WINDOW_FLAG_TERMINAL)) {
        gui_set_focused_widget(0);
        return;
    }
    gui_focus_first_widget(w);
}

static void gui_focus_next_widget(void) {
    gui_window_t *w;
    int start = -1;
    uint32_t i;
    uint32_t count;
    if (g_gui.focused_widget && g_gui.focused_widget->owner) {
        w = g_gui.focused_widget->owner;
    } else {
        w = g_gui.active_window ? g_gui.active_window : gui_top_window();
    }
    if (!w || !w->used || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;
    count = w->widget_count;
    if (count == 0) return;
    for (i = 0; i < count; i++) {
        if (&w->widgets[i] == g_gui.focused_widget) {
            start = (int)i;
            break;
        }
    }
    for (i = 1; i <= count; i++) {
        uint32_t idx = (uint32_t)((start + (int)i) % (int)count);
        if (gui_widget_can_focus(&w->widgets[idx])) {
            gui_set_focused_widget(&w->widgets[idx]);
            return;
        }
    }
}

static void gui_textbox_on_key(gui_widget_t *wg, int key) {
    uint32_t len;
    uint32_t i;
    int changed = 1;
    if (!gui_widget_can_focus(wg)) return;
    len = (uint32_t)strlen(wg->text);
    if (wg->cursor > len) wg->cursor = len;

    if (key == GUI_KEY_BACKSPACE) {
        if (wg->cursor == 0 || len == 0) changed = 0;
        else {
            for (i = wg->cursor - 1; i < len; i++) wg->text[i] = wg->text[i + 1];
            wg->cursor--;
        }
    } else if (key == GUI_KEY_DELETE) {
        if (wg->cursor >= len) changed = 0;
        else {
            for (i = wg->cursor; i < len; i++) wg->text[i] = wg->text[i + 1];
        }
    } else if (key == GUI_KEY_LEFT) {
        if (wg->cursor > 0) wg->cursor--;
        else changed = 0;
    } else if (key == GUI_KEY_RIGHT) {
        if (wg->cursor < len) wg->cursor++;
        else changed = 0;
    } else if (key == GUI_KEY_HOME) {
        if (wg->cursor != 0) wg->cursor = 0;
        else changed = 0;
    } else if (key == GUI_KEY_END) {
        if (wg->cursor != len) wg->cursor = len;
        else changed = 0;
    } else if (key == GUI_KEY_ENTER || key == GUI_KEY_TAB) {
        changed = 0;
    } else if (key >= 32 && key <= 126 && len + 1 < sizeof(wg->text)) {
        for (i = len + 1; i > wg->cursor; i--) wg->text[i] = wg->text[i - 1];
        wg->text[wg->cursor++] = (char)key;
    } else {
        changed = 0;
    }
    if (changed && wg->owner) gui_invalidate_rect(wg->owner->rect.x, wg->owner->rect.y, wg->owner->rect.w, wg->owner->rect.h);
}

static void gui_cursor_restore_fb(void) {
    int x, y, sx, sy, ex, ey;
    if (!g_gui.cursor_drawn || !g_gui.backbuffer) return;
    sx = g_gui.cursor_fb_x - 2;
    sy = g_gui.cursor_fb_y - 2;
    ex = g_gui.cursor_fb_x + 14;
    ey = g_gui.cursor_fb_y + 18;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (ex > (int)g_gui.width) ex = (int)g_gui.width;
    if (ey > (int)g_gui.height) ey = (int)g_gui.height;
    for (y = sy; y < ey; y++) {
        for (x = sx; x < ex; x++) {
            framebuffer_put_pixel((uint32_t)x, (uint32_t)y, g_gui.backbuffer[y * (int)g_gui.width + x]);
        }
    }
    g_gui.cursor_drawn = 0;
}

static void gui_cursor_draw_fb(void) {
    int x = g_gui.mouse_x;
    int y = g_gui.mouse_y;
    uint32_t c = gui_rgb(255, 255, 255);
    uint32_t b = gui_rgb(0, 0, 0);
    if (!g_gui.cursor_visible) return;
    framebuffer_draw_line(x, y, x, y + 15, b);
    framebuffer_draw_line(x, y, x + 10, y + 10, b);
    framebuffer_draw_line(x, y + 15, x + 4, y + 11, b);
    framebuffer_draw_line(x + 4, y + 11, x + 10, y + 10, b);
    framebuffer_draw_line(x + 1, y + 2, x + 1, y + 12, c);
    framebuffer_draw_line(x + 1, y + 2, x + 8, y + 9, c);
    framebuffer_draw_line(x + 2, y + 12, x + 4, y + 10, c);
    g_gui.cursor_drawn = 1;
    g_gui.cursor_fb_x = x;
    g_gui.cursor_fb_y = y;
}

static void gui_cursor_present_fast(void) {
    if (!gui_compositor_active()) return;
    gui_cursor_restore_fb();
    gui_cursor_draw_fb();
}

static void gui_rect_union_inplace(gui_rect_t *dst, const gui_rect_t *src) {
    int x1, y1, x2, y2;
    if (!dst || !src) return;
    x1 = dst->x < src->x ? dst->x : src->x;
    y1 = dst->y < src->y ? dst->y : src->y;
    x2 = (dst->x + dst->w) > (src->x + src->w) ? (dst->x + dst->w) : (src->x + src->w);
    y2 = (dst->y + dst->h) > (src->y + src->h) ? (dst->y + dst->h) : (src->y + src->h);
    dst->x = x1; dst->y = y1; dst->w = x2 - x1; dst->h = y2 - y1;
}

static int gui_rects_touch_or_overlap(const gui_rect_t *a, const gui_rect_t *b) {
    if (!a || !b) return 0;
    return !(a->x + a->w < b->x || b->x + b->w < a->x ||
             a->y + a->h < b->y || b->y + b->h < a->y);
}

void gui_invalidate_rect(int x, int y, int w, int h) {
    gui_rect_t new_rect;
    gui_rect_t *r;
    uint32_t i;
    if (!g_gui.initialized) return;
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)g_gui.width || y >= (int)g_gui.height) return;
    if (x + w > (int)g_gui.width) w = (int)g_gui.width - x;
    if (y + h > (int)g_gui.height) h = (int)g_gui.height - y;
    if (w <= 0 || h <= 0) return;

    new_rect.x = x; new_rect.y = y; new_rect.w = w; new_rect.h = h;
    for (i = 0; i < g_gui.dirty_count; i++) {
        if (gui_rects_touch_or_overlap(&g_gui.dirty_rects[i], &new_rect)) {
            gui_rect_union_inplace(&g_gui.dirty_rects[i], &new_rect);
            return;
        }
    }
    if (g_gui.dirty_count >= GUI_MAX_DIRTY_RECTS) {
        g_gui.full_dirty = 1;
        return;
    }
    r = &g_gui.dirty_rects[g_gui.dirty_count++];
    *r = new_rect;
}

void gui_invalidate_all(void) {
    if (!g_gui.initialized) return;
    g_gui.full_dirty = 1;
    g_gui.dirty_count = 0;
}

int gui_blit_rgba32(int x, int y, int w, int h, const uint32_t *pixels, uint32_t src_stride) {
    int row;
    int col;
    int src_skip_x = 0;
    int src_skip_y = 0;
    gui_rect_t rect;
    gui_rect_t screen;
    gui_rect_t clipped;
    uint32_t copied;

    if (!g_gui.initialized || !pixels || w <= 0 || h <= 0) return -1;
    if (src_stride == 0) src_stride = (uint32_t)w;
    if (src_stride < (uint32_t)w) return -1;

    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    screen.x = 0; screen.y = 0; screen.w = (int)g_gui.width; screen.h = (int)g_gui.height;
    if (!gui_rect_intersect(&rect, &screen, &clipped)) return 0;
    if (g_gui.clip_enabled) {
        gui_rect_t clip2;
        if (!gui_rect_intersect(&clipped, &g_gui.clip_rect, &clip2)) return 0;
        clipped = clip2;
    }

    src_skip_x = clipped.x - x;
    src_skip_y = clipped.y - y;
    copied = (uint32_t)clipped.w * (uint32_t)clipped.h;
    g_gui_accel.blits++;
    g_gui_accel.blit_pixels += copied;

    if (gui_compositor_active()) {
        for (row = 0; row < clipped.h; row++) {
            const uint32_t *src = pixels + ((uint32_t)(src_skip_y + row) * src_stride) + (uint32_t)src_skip_x;
            uint32_t *dst = &g_gui.backbuffer[(uint32_t)(clipped.y + row) * g_gui.width + (uint32_t)clipped.x];
            memcpy(dst, src, (uint32_t)clipped.w * sizeof(uint32_t));
        }
        g_gui_accel.backbuffer_fast_blits++;
        gui_invalidate_rect(clipped.x, clipped.y, clipped.w, clipped.h);
        return (int)copied;
    }

    for (row = 0; row < clipped.h; row++) {
        const uint32_t *src = pixels + ((uint32_t)(src_skip_y + row) * src_stride) + (uint32_t)src_skip_x;
        for (col = 0; col < clipped.w; col++) {
            framebuffer_put_pixel((uint32_t)(clipped.x + col), (uint32_t)(clipped.y + row), src[col]);
        }
    }
    g_gui_accel.framebuffer_fast_blits++;
    return (int)copied;
}

int gui_copy_rect(int dst_x, int dst_y, int src_x, int src_y, int w, int h) {
    int row;
    int sy;
    int dy;
    int src_adj_x = src_x;
    int src_adj_y = src_y;
    gui_rect_t dst;
    gui_rect_t screen;
    gui_rect_t clipped;
    uint32_t copied;

    if (!g_gui.initialized || !gui_compositor_active() || w <= 0 || h <= 0) return -1;
    dst.x = dst_x; dst.y = dst_y; dst.w = w; dst.h = h;
    screen.x = 0; screen.y = 0; screen.w = (int)g_gui.width; screen.h = (int)g_gui.height;
    if (!gui_rect_intersect(&dst, &screen, &clipped)) return 0;
    if (g_gui.clip_enabled) {
        gui_rect_t clip2;
        if (!gui_rect_intersect(&clipped, &g_gui.clip_rect, &clip2)) return 0;
        clipped = clip2;
    }

    src_adj_x += clipped.x - dst_x;
    src_adj_y += clipped.y - dst_y;
    if (src_adj_x < 0 || src_adj_y < 0) return -1;
    if (src_adj_x + clipped.w > (int)g_gui.width) return -1;
    if (src_adj_y + clipped.h > (int)g_gui.height) return -1;

    copied = (uint32_t)clipped.w * (uint32_t)clipped.h;
    g_gui_accel.rect_copies++;
    g_gui_accel.rect_copy_pixels += copied;

    if (src_adj_y < clipped.y || (src_adj_y == clipped.y && src_adj_x < clipped.x)) {
        for (row = clipped.h - 1; row >= 0; row--) {
            sy = src_adj_y + row;
            dy = clipped.y + row;
            memmove(&g_gui.backbuffer[(uint32_t)dy * g_gui.width + (uint32_t)clipped.x],
                    &g_gui.backbuffer[(uint32_t)sy * g_gui.width + (uint32_t)src_adj_x],
                    (uint32_t)clipped.w * sizeof(uint32_t));
        }
    } else {
        for (row = 0; row < clipped.h; row++) {
            sy = src_adj_y + row;
            dy = clipped.y + row;
            memmove(&g_gui.backbuffer[(uint32_t)dy * g_gui.width + (uint32_t)clipped.x],
                    &g_gui.backbuffer[(uint32_t)sy * g_gui.width + (uint32_t)src_adj_x],
                    (uint32_t)clipped.w * sizeof(uint32_t));
        }
    }
    gui_invalidate_rect(clipped.x, clipped.y, clipped.w, clipped.h);
    return (int)copied;
}

static void gui_flush_rect(const gui_rect_t *r) {
    int x, y;
    if (!r || !g_gui.backbuffer) return;
    g_gui_accel.flush_rects++;
    g_gui_accel.flush_pixels += (uint32_t)r->w * (uint32_t)r->h;
    for (y = r->y; y < r->y + r->h; y++) {
        const uint32_t *src = &g_gui.backbuffer[(uint32_t)y * g_gui.width + (uint32_t)r->x];
        for (x = 0; x < r->w; x++) {
            framebuffer_put_pixel((uint32_t)(r->x + x), (uint32_t)y, src[x]);
        }
        g_gui_accel.flush_rows++;
    }
}

static void gui_flush_backbuffer(void) {
    uint32_t i;
    gui_rect_t all;
    if (!gui_compositor_active()) return;

    if (g_gui.full_dirty) {
        all.x = 0;
        all.y = 0;
        all.w = (int)g_gui.width;
        all.h = (int)g_gui.height;
        gui_flush_rect(&all);
    } else {
        for (i = 0; i < g_gui.dirty_count; i++) {
            gui_flush_rect(&g_gui.dirty_rects[i]);
        }
    }

    g_gui.full_dirty = 0;
    g_gui.dirty_count = 0;
}

static void gui_draw_widget(gui_widget_t *wg) {
    uint32_t bg, fg;
    if (!wg || !wg->visible || !wg->owner) return;
    int ax = wg->owner->rect.x + GUI_BORDER_SIZE + wg->rect.x;
    int ay = wg->owner->rect.y + GUI_TITLE_HEIGHT + wg->rect.y;

    if (wg->type == GUI_WIDGET_LABEL) {
        gui_draw_text(ax, ay + 3, wg->text, wg->fg_color ? wg->fg_color : g_gui.colors.text_fg);
    } else if (wg->type == GUI_WIDGET_BUTTON) {
        uint32_t light = g_gui.colors.button_border;
        uint32_t shadow = gui_rgb(20, 20, 20);
        int text_dx = wg->pressed ? 9 : 8;
        int text_dy = wg->pressed ? 1 : 0;
        if (!wg->enabled) {
            bg = gui_rgb(170, 174, 184);
            fg = gui_rgb(95, 98, 106);
            light = gui_rgb(198, 202, 210);
            shadow = gui_rgb(125, 128, 136);
        } else if (wg->pressed) {
            bg = g_gui.colors.accent;
            fg = gui_rgb(255, 255, 255);
            light = gui_rgb(20, 20, 20);
            shadow = g_gui.colors.button_border;
        } else if (wg->hovered) {
            bg = gui_rgb(235, 240, 250);
            fg = wg->fg_color ? wg->fg_color : g_gui.colors.button_fg;
        } else {
            bg = wg->bg_color ? wg->bg_color : g_gui.colors.button_bg;
            fg = wg->fg_color ? wg->fg_color : g_gui.colors.button_fg;
        }
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, light);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, light);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, shadow);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, shadow);
        if (wg->focused && wg->enabled) {
            uint32_t focus = gui_rgb(255, 255, 255);
            gui_raw_line(ax + 3, ay + 3, ax + wg->rect.w - 4, ay + 3, focus);
            gui_raw_line(ax + 3, ay + wg->rect.h - 4, ax + wg->rect.w - 4, ay + wg->rect.h - 4, focus);
            gui_raw_line(ax + 3, ay + 3, ax + 3, ay + wg->rect.h - 4, focus);
            gui_raw_line(ax + wg->rect.w - 4, ay + 3, ax + wg->rect.w - 4, ay + wg->rect.h - 4, focus);
        }
        gui_draw_text(ax + text_dx, ay + (wg->rect.h - GUI_CHAR_H) / 2 + text_dy, wg->text, fg);
    } else if (wg->type == GUI_WIDGET_PANEL) {
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, wg->bg_color);
    } else if (wg->type == GUI_WIDGET_TEXTBOX) {
        uint32_t border = wg->focused ? g_gui.colors.accent : g_gui.colors.button_border;
        uint32_t text_x = (uint32_t)(ax + 4);
        uint32_t text_y = (uint32_t)(ay + (wg->rect.h - GUI_CHAR_H) / 2);
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, wg->bg_color ? wg->bg_color : gui_rgb(250, 250, 250));
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_draw_text((int)text_x, (int)text_y, wg->text, wg->fg_color ? wg->fg_color : gui_rgb(20, 20, 20));
        if (wg->focused) {
            int cx = ax + 4 + (int)(wg->cursor * GUI_CHAR_W);
            if (cx < ax + wg->rect.w - 3) gui_raw_line(cx, ay + 4, cx, ay + wg->rect.h - 5, gui_rgb(20, 20, 20));
        }
    }
}

static void gui_draw_window(gui_window_t *w) {
    uint32_t i;
    uint32_t border;
    uint32_t title;
    if (!w || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;

    border = w->active ? g_gui.colors.accent : g_gui.colors.window_border;
    title = w->active ? g_gui.colors.title_bg : gui_rgb(50, 55, 68);

    gui_raw_fill_rect(w->rect.x, w->rect.y, w->rect.w, w->rect.h, border);
    gui_raw_fill_rect(w->rect.x + GUI_BORDER_SIZE, w->rect.y + GUI_BORDER_SIZE,
                      w->rect.w - GUI_BORDER_SIZE * 2, GUI_TITLE_HEIGHT - GUI_BORDER_SIZE,
                      title);
    gui_raw_fill_rect(w->rect.x + GUI_BORDER_SIZE, w->rect.y + GUI_TITLE_HEIGHT,
                      w->rect.w - GUI_BORDER_SIZE * 2, w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE,
                      w->bg_color ? w->bg_color : g_gui.colors.window_bg);

    if (w->flags & GUI_WINDOW_FLAG_CLOSABLE) {
        gui_rect_t c = gui_close_rect(w);
        gui_raw_fill_rect(c.x, c.y, c.w, c.h, gui_rgb(160, 50, 55));
        gui_raw_line(c.x + 3, c.y + 3, c.x + c.w - 4, c.y + c.h - 4, gui_rgb(255,255,255));
        gui_raw_line(c.x + c.w - 4, c.y + 3, c.x + 3, c.y + c.h - 4, gui_rgb(255,255,255));
    }

    if (w->flags & GUI_WINDOW_FLAG_MINIMIZABLE) {
        gui_rect_t m = gui_min_rect(w);
        gui_raw_fill_rect(m.x, m.y, m.w, m.h, gui_rgb(80, 90, 105));
        gui_raw_line(m.x + 3, m.y + m.h - 4, m.x + m.w - 4, m.y + m.h - 4, gui_rgb(255,255,255));
    }

    {
        gui_rect_t title_clip;
        int title_x = w->rect.x + 8;
        int title_y = w->rect.y + 7;
        int title_right = w->rect.x + w->rect.w - GUI_BORDER_SIZE - 6;
        if (w->flags & GUI_WINDOW_FLAG_CLOSABLE) {
            gui_rect_t c = gui_close_rect(w);
            title_right = c.x - 6;
        }
        if (w->flags & GUI_WINDOW_FLAG_MINIMIZABLE) {
            gui_rect_t m = gui_min_rect(w);
            if (m.x - 6 < title_right) title_right = m.x - 6;
        }
        title_clip.x = title_x;
        title_clip.y = w->rect.y + GUI_BORDER_SIZE;
        title_clip.w = title_right - title_x;
        title_clip.h = GUI_TITLE_HEIGHT - GUI_BORDER_SIZE;
        if (title_clip.w > 0 && title_clip.h > 0) {
            gui_draw_window_title_text(title_x, title_y, w->title, gui_rgb(235, 242, 255), &title_clip);
        }
    }

    {
        gui_rect_t client;
        client.x = w->rect.x + GUI_BORDER_SIZE;
        client.y = w->rect.y + GUI_TITLE_HEIGHT;
        client.w = w->rect.w - GUI_BORDER_SIZE * 2;
        client.h = w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE;
        gui_set_clip_rect(&client);
        for (i = 0; i < w->widget_count; i++) {
            gui_draw_widget(&w->widgets[i]);
        }
        gui_clear_clip_rect();
    }
}

void gui_event_push(gui_event_t event) {
    if (event.type == GUI_EVENT_MOUSE_MOVE && g_gui.event_count > 0) {
        uint32_t last = (g_gui.event_tail + GUI_EVENT_QUEUE_SIZE - 1) % GUI_EVENT_QUEUE_SIZE;
        if (g_gui.events[last].type == GUI_EVENT_MOUSE_MOVE) {
            /* 鍚堝苟杩炵画 MOVE锛岄伩鍏嶇Щ鍔ㄤ簨浠跺埛婊￠槦鍒楀鑷寸偣鍑?鎷栧姩浜嬩欢涓㈠け銆?*/
            g_gui.events[last] = event;
            return;
        }
    }

    if (g_gui.event_count >= GUI_EVENT_QUEUE_SIZE) {
        /* 闃熷垪婊℃椂涓㈠純鏈€鑰佷簨浠讹紝淇濊瘉 DOWN/UP 绛夊叧閿簨浠跺彲浠ュ叆闃熴€?*/
        if (event.type == GUI_EVENT_MOUSE_MOVE) {
            return;
        }
        g_gui.event_head = (g_gui.event_head + 1) % GUI_EVENT_QUEUE_SIZE;
        g_gui.event_count--;
    }
    g_gui.events[g_gui.event_tail] = event;
    g_gui.event_tail = (g_gui.event_tail + 1) % GUI_EVENT_QUEUE_SIZE;
    g_gui.event_count++;
}

int gui_event_pop(gui_event_t *event) {
    if (!event || g_gui.event_count == 0) return 0;
    *event = g_gui.events[g_gui.event_head];
    g_gui.event_head = (g_gui.event_head + 1) % GUI_EVENT_QUEUE_SIZE;
    g_gui.event_count--;
    return 1;
}

static void gui_refresh_window_refs(void) {
    uint32_t i;
    g_gui.terminal.window = 0;
    for (i = 0; i < GUI_MAX_WINDOWS; i++) {
        uint32_t j;
        if (!g_gui.windows[i].used) continue;
        for (j = 0; j < g_gui.windows[i].widget_count; j++) {
            g_gui.windows[i].widgets[j].owner = &g_gui.windows[i];
        }
        if (g_gui.windows[i].flags & GUI_WINDOW_FLAG_TERMINAL) {
            g_gui.terminal.window = &g_gui.windows[i];
        }
    }
}

void gui_bring_to_front(gui_window_t *window) {
    int idx;
    uint32_t pos;
    uint32_t i;
    if (!window || g_gui.window_count < 2) return;
    idx = gui_window_index(window);
    if (idx < 0) return;
    pos = GUI_MAX_WINDOWS;
    for (i = 0; i < g_gui.window_count; i++) {
        if (g_gui.z_order[i] == (uint32_t)idx) { pos = i; break; }
    }
    if (pos == GUI_MAX_WINDOWS || pos == g_gui.window_count - 1) return;
    for (i = pos; i + 1 < g_gui.window_count; i++) {
        g_gui.z_order[i] = g_gui.z_order[i + 1];
    }
    g_gui.z_order[g_gui.window_count - 1] = (uint32_t)idx;
    g_gui.active_window = window;
    gui_refresh_active_app();
}

void gui_set_active_window(gui_window_t *window) {
    uint32_t i;
    for (i = 0; i < GUI_MAX_WINDOWS; i++) g_gui.windows[i].active = 0;
    if (window && gui_window_index(window) >= 0 && window->visible && !(window->flags & GUI_WINDOW_FLAG_MINIMIZED)) {
        gui_bring_to_front(window);
        g_gui.active_window = window;
        g_gui.active_window->active = 1;
        if (!g_gui.focused_widget || g_gui.focused_widget->owner != window) {
            gui_focus_active_window_default();
        }
    } else {
        g_gui.active_window = 0;
        gui_set_focused_widget(0);
    }
    gui_refresh_active_app();
    gui_invalidate_all();
}

void gui_destroy_window(gui_window_t *window) {
    int idx;
    uint32_t i;
    uint32_t pos = GUI_MAX_WINDOWS;
    if (!window) return;
    idx = gui_window_index(window);
    if (idx < 0) return;

    for (i = 0; i < g_gui.window_count; i++) {
        if (g_gui.z_order[i] == (uint32_t)idx) { pos = i; break; }
    }
    if (pos < GUI_MAX_WINDOWS) {
        for (i = pos; i + 1 < g_gui.window_count; i++) g_gui.z_order[i] = g_gui.z_order[i + 1];
    }
    if (g_gui.window_count > 0) g_gui.window_count--;

    if (window == g_gui.terminal.window) gui_terminal_set_input_focus(0);
    if (g_gui.active_window == window) g_gui.active_window = 0;
    if (g_gui.drag_window == window) g_gui.drag_window = 0;
    if (g_gui.pressed_widget && g_gui.pressed_widget->owner == window) g_gui.pressed_widget = 0;
    if (g_gui.hovered_widget && g_gui.hovered_widget->owner == window) g_gui.hovered_widget = 0;
    if (g_gui.focused_widget && g_gui.focused_widget->owner == window) g_gui.focused_widget = 0;
    if (window->owner_app) {
        gui_app_t *app = window->owner_app;
        if (app->window_count > 0) app->window_count--;
        if (app->main_window == window) app->main_window = 0;
        if (app->window_count == 0) app->running = 0;
    }
    memset(window, 0, sizeof(gui_window_t));

    gui_refresh_window_refs();
    g_gui.active_window = gui_top_window();
    if (g_gui.active_window) {
        g_gui.active_window->active = 1;
        gui_focus_active_window_default();
    } else {
        gui_set_focused_widget(0);
    }
    gui_invalidate_all();
}

void gui_minimize_window(gui_window_t *window) {
    if (!window || !window->used) return;

    window->flags |= GUI_WINDOW_FLAG_MINIMIZED;
    window->visible = 1;
    window->active = 0;
    window->dragging = 0;

    if (window == g_gui.terminal.window) gui_terminal_set_input_focus(0);
    if (g_gui.active_window == window) g_gui.active_window = 0;
    if (g_gui.drag_window == window) g_gui.drag_window = 0;
    if (g_gui.pressed_widget && g_gui.pressed_widget->owner == window) g_gui.pressed_widget = 0;
    if (g_gui.hovered_widget && g_gui.hovered_widget->owner == window) g_gui.hovered_widget = 0;
    if (g_gui.focused_widget && g_gui.focused_widget->owner == window) gui_set_focused_widget(0);

    g_gui.active_window = gui_top_window();
    if (g_gui.active_window) {
        g_gui.active_window->active = 1;
        gui_focus_active_window_default();
    } else {
        gui_set_focused_widget(0);
    }

    gui_refresh_active_app();
    gui_invalidate_all();
}

void gui_restore_window(gui_window_t *window) {
    if (!window) return;
    window->flags &= ~GUI_WINDOW_FLAG_MINIMIZED;
    window->visible = 1;
    gui_set_active_window(window);
    gui_invalidate_all();
}

void gui_show_window(gui_window_t *window) {
    if (!window) return;
    window->visible = 1;
    if (!(window->flags & GUI_WINDOW_FLAG_MINIMIZED)) gui_set_active_window(window);
    else gui_invalidate_all();
}

void gui_hide_window(gui_window_t *window) {
    if (!window) return;
    window->visible = 0;
    window->active = 0;
    if (window == g_gui.terminal.window) gui_terminal_set_input_focus(0);
    if (g_gui.drag_window == window) g_gui.drag_window = 0;
    if (g_gui.pressed_widget && g_gui.pressed_widget->owner == window) g_gui.pressed_widget = 0;
    if (g_gui.hovered_widget && g_gui.hovered_widget->owner == window) g_gui.hovered_widget = 0;
    if (g_gui.focused_widget && g_gui.focused_widget->owner == window) gui_set_focused_widget(0);
    if (g_gui.active_window == window) {
        g_gui.active_window = gui_top_window();
        if (g_gui.active_window) g_gui.active_window->active = 1;
        gui_focus_active_window_default();
    }
    gui_invalidate_all();
}

typedef struct gui_taskbar_layout {
    gui_rect_t bar;
    gui_rect_t start_button;
    gui_rect_t terminal_button;
    int first_window_x;
    int item_y;
    int item_h;
} gui_taskbar_layout_t;

static int gui_taskbar_button_width(gui_window_t *window) {
    (void)window;
    return GUI_TASKBAR_ICON_BUTTON_W;
}

static int gui_taskbar_content_width(void) {
    uint32_t i;
    int width = GUI_TASKBAR_START_W + 6 + GUI_TASKBAR_START_W;
    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!w->used || !w->visible) continue;
        if (w->flags & GUI_WINDOW_FLAG_TERMINAL) continue;
        width += 6 + gui_taskbar_button_width(w);
    }
    return width;
}

static void gui_taskbar_get_layout(gui_taskbar_layout_t *layout) {
    int y;
    int margin = 12;
    int padding = 8;
    int content_w;
    int max_w;
    int bar_w;
    if (!layout) return;
    y = (int)g_gui.height - GUI_TASKBAR_HEIGHT;
    content_w = gui_taskbar_content_width();
    max_w = (int)g_gui.width - margin * 2;
    if (max_w < 0) max_w = (int)g_gui.width;
    bar_w = content_w + padding * 2;
    if (bar_w > max_w) bar_w = max_w;
    if (bar_w < GUI_TASKBAR_START_W * 2 + 6 + padding * 2) bar_w = GUI_TASKBAR_START_W * 2 + 6 + padding * 2;
    if (bar_w > (int)g_gui.width) bar_w = (int)g_gui.width;

    layout->bar.x = ((int)g_gui.width - bar_w) / 2;
    layout->bar.y = y;
    layout->bar.w = bar_w;
    layout->bar.h = GUI_TASKBAR_HEIGHT;
    layout->start_button.x = layout->bar.x + padding;
    layout->start_button.y = y + 3;
    layout->start_button.w = GUI_TASKBAR_START_W;
    layout->start_button.h = GUI_TASKBAR_HEIGHT - 6;
    layout->terminal_button.x = layout->start_button.x + layout->start_button.w + 6;
    layout->terminal_button.y = y + 3;
    layout->terminal_button.w = GUI_TASKBAR_START_W;
    layout->terminal_button.h = GUI_TASKBAR_HEIGHT - 6;
    layout->first_window_x = layout->terminal_button.x + layout->terminal_button.w + 6;
    layout->item_y = y + 3;
    layout->item_h = GUI_TASKBAR_HEIGHT - 6;
}

static int gui_taskbar_terminal_button_at(int x, int y) {
    gui_taskbar_layout_t layout;
    gui_taskbar_get_layout(&layout);
    return gui_rect_contains(&layout.terminal_button, x, y);
}

static int gui_is_taskbar_at(int x, int y) {
    gui_taskbar_layout_t layout;
    gui_taskbar_get_layout(&layout);
    return gui_rect_contains(&layout.bar, x, y);
}

static gui_window_t *gui_taskbar_window_at(int x, int y) {
    uint32_t i;
    gui_taskbar_layout_t layout;
    int bx;
    gui_taskbar_get_layout(&layout);
    if (!gui_rect_contains(&layout.bar, x, y)) return 0;
    bx = layout.first_window_x;
    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        gui_rect_t button;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!w->used || !w->visible) continue;
        if (w->flags & GUI_WINDOW_FLAG_TERMINAL) continue;
        button.x = bx;
        button.y = layout.item_y;
        button.w = gui_taskbar_button_width(w);
        button.h = layout.item_h;
        if (button.x + button.w > layout.bar.x + layout.bar.w - 8) button.w = layout.bar.x + layout.bar.w - 8 - button.x;
        if (button.w > 0 && gui_rect_contains(&button, x, y)) return w;
        bx += gui_taskbar_button_width(w) + 6;
    }
    return 0;
}

static int gui_taskbar_icon_hovered(gui_rect_t rect) {
    return gui_rect_contains(&rect, g_gui.mouse_x, g_gui.mouse_y);
}

static int gui_taskbar_icon_hover_lift(gui_rect_t rect) {
    return gui_taskbar_icon_hovered(rect) ? 2 : 0;
}

static void gui_taskbar_draw_icon_shadow(int x, int y, int w, int h) {
    gui_raw_fill_rect(x + 2, y + 4, w, h, gui_rgb(10, 14, 22));
}

static void gui_taskbar_invalidate_icon_hover_change(gui_rect_t rect,
                                                     int old_x,
                                                     int old_y,
                                                     int new_x,
                                                     int new_y) {
    if (rect.w <= 0 || rect.h <= 0) return;
    if (gui_rect_contains(&rect, old_x, old_y) != gui_rect_contains(&rect, new_x, new_y)) {
        gui_invalidate_rect(rect.x - 3, rect.y - 5, rect.w + 6, rect.h + 8);
    }
}

static void gui_taskbar_invalidate_hover_changes(int old_x, int old_y, int new_x, int new_y) {
    uint32_t i;
    gui_taskbar_layout_t layout;
    int bx;
    gui_taskbar_get_layout(&layout);
    gui_taskbar_invalidate_icon_hover_change(layout.start_button, old_x, old_y, new_x, new_y);
    gui_taskbar_invalidate_icon_hover_change(layout.terminal_button, old_x, old_y, new_x, new_y);
    bx = layout.first_window_x;
    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        gui_rect_t button;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!w->used || !w->visible) continue;
        if (w->flags & GUI_WINDOW_FLAG_TERMINAL) continue;
        button.x = bx;
        button.y = layout.item_y;
        button.w = gui_taskbar_button_width(w);
        button.h = layout.item_h;
        if (button.x + button.w > layout.bar.x + layout.bar.w - 8) {
            button.w = layout.bar.x + layout.bar.w - 8 - button.x;
        }
        if (button.w <= 0) break;
        gui_taskbar_invalidate_icon_hover_change(button, old_x, old_y, new_x, new_y);
        bx += gui_taskbar_button_width(w) + 6;
    }
}

void gui_post_key_code(int key) {
    gui_event_t ev;
    if (!g_gui.initialized || !key) return;
    memset(&ev, 0, sizeof(ev));
    ev.type = GUI_EVENT_KEY_DOWN;
    ev.key = key;
    ev.window = g_gui.active_window;
    gui_event_push(ev);
}

void gui_post_key(char ch) {
    gui_post_key_code((int)(unsigned char)ch);
}

static void gui_terminal_invalidate_body(void) {
    gui_window_t *w = g_gui.terminal.window;
    if (!g_gui.initialized || !g_gui.terminal.enabled || !w ||
        !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) {
        return;
    }
    gui_invalidate_rect(w->rect.x + GUI_BORDER_SIZE,
                        w->rect.y + GUI_TITLE_HEIGHT,
                        w->rect.w - GUI_BORDER_SIZE * 2,
                        w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE);
}

static void gui_terminal_invalidate_cell(uint32_t col, uint32_t row) {
    gui_window_t *w = g_gui.terminal.window;
    int x, y;
    if (!g_gui.initialized || !g_gui.terminal.enabled || !w ||
        !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) {
        return;
    }
    if (col >= g_gui.terminal.cols || row >= g_gui.terminal.rows) return;
    x = w->rect.x + 6 + (int)col * GUI_CHAR_W;
    y = w->rect.y + GUI_TITLE_HEIGHT + 5 + (int)row * (GUI_CHAR_H + 1);
    gui_invalidate_rect(x - 1, y - 1, GUI_CHAR_W + 2, GUI_CHAR_H + 3);
}

static void gui_terminal_invalidate_cursor_at(uint32_t col, uint32_t row) {
    gui_window_t *w = g_gui.terminal.window;
    int x, y;
    if (!g_gui.initialized || !g_gui.terminal.enabled || !w ||
        !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) {
        return;
    }
    if (row >= g_gui.terminal.rows) return;
    if (col >= g_gui.terminal.cols) col = g_gui.terminal.cols - 1;
    x = w->rect.x + 6 + (int)col * GUI_CHAR_W;
    y = w->rect.y + GUI_TITLE_HEIGHT + 5 + (int)row * (GUI_CHAR_H + 1) + GUI_CHAR_H;
    gui_invalidate_rect(x - 1, y - 1, GUI_CHAR_W + 2, 3);
}

static int gui_terminal_point_to_cell(int x, int y, uint32_t *col, uint32_t *row) {
    gui_window_t *w = g_gui.terminal.window;
    int ox, oy;
    int rel_x, rel_y;
    uint32_t c, r;
    if (!w || !g_gui.terminal.enabled || !col || !row) return 0;
    if (!w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return 0;
    ox = w->rect.x + 6;
    oy = w->rect.y + GUI_TITLE_HEIGHT + 5;
    rel_x = x - ox;
    rel_y = y - oy;
    if (rel_x < 0 || rel_y < 0) return 0;
    c = (uint32_t)(rel_x / GUI_CHAR_W);
    r = (uint32_t)(rel_y / (GUI_CHAR_H + 1));
    if (c >= g_gui.terminal.cols || r >= g_gui.terminal.rows) return 0;
    *col = c;
    *row = r;
    return 1;
}

static void gui_terminal_update_selection(uint32_t col, uint32_t row) {
    uint32_t a = g_gui.terminal.selection_anchor_y * g_gui.terminal.cols + g_gui.terminal.selection_anchor_x;
    uint32_t b = row * g_gui.terminal.cols + col;
    if (b < a) {
        g_gui.terminal.selection_start_x = col;
        g_gui.terminal.selection_start_y = row;
        g_gui.terminal.selection_end_x = g_gui.terminal.selection_anchor_x;
        g_gui.terminal.selection_end_y = g_gui.terminal.selection_anchor_y;
    } else {
        g_gui.terminal.selection_start_x = g_gui.terminal.selection_anchor_x;
        g_gui.terminal.selection_start_y = g_gui.terminal.selection_anchor_y;
        g_gui.terminal.selection_end_x = col;
        g_gui.terminal.selection_end_y = row;
    }
    g_gui.terminal.has_selection = (a != b) ? 1 : 0;
    gui_terminal_invalidate_body();
}

static int gui_terminal_cell_selected(uint32_t col, uint32_t row) {
    uint32_t p, s, e;
    if (!g_gui.terminal.has_selection || g_gui.terminal.cols == 0) return 0;
    p = row * g_gui.terminal.cols + col;
    s = g_gui.terminal.selection_start_y * g_gui.terminal.cols + g_gui.terminal.selection_start_x;
    e = g_gui.terminal.selection_end_y * g_gui.terminal.cols + g_gui.terminal.selection_end_x;
    return (p >= s && p <= e) ? 1 : 0;
}

void gui_terminal_clear_selection(void) {
    if (!g_gui.terminal.has_selection && !g_gui.terminal.selecting) return;
    g_gui.terminal.selecting = 0;
    g_gui.terminal.has_selection = 0;
    gui_terminal_invalidate_body();
}

int gui_terminal_copy_selection(void) {
    uint32_t r, c;
    uint32_t len = 0;
    uint32_t copied = 0;
    if (!g_gui.initialized || !g_gui.terminal.has_selection || g_gui.terminal.cols == 0) return 0;
    for (r = g_gui.terminal.selection_start_y; r <= g_gui.terminal.selection_end_y && r < g_gui.terminal.rows; r++) {
        uint32_t start_c = 0;
        uint32_t end_c = g_gui.terminal.cols - 1;
        int last_non_space = -1;
        if (r == g_gui.terminal.selection_start_y) start_c = g_gui.terminal.selection_start_x;
        if (r == g_gui.terminal.selection_end_y) end_c = g_gui.terminal.selection_end_x;
        for (c = start_c; c <= end_c && c < g_gui.terminal.cols; c++) {
            if (g_gui.terminal.cells[r][c] != ' ') last_non_space = (int)c;
        }
        if (last_non_space < (int)start_c && r != g_gui.terminal.selection_end_y) {
            if (len + 1 < GUI_TERM_CLIPBOARD_SIZE) g_gui.terminal.clipboard[len++] = '\n';
            continue;
        }
        if (last_non_space >= (int)start_c) {
            for (c = start_c; c <= (uint32_t)last_non_space && c < g_gui.terminal.cols; c++) {
                if (len + 1 >= GUI_TERM_CLIPBOARD_SIZE) break;
                g_gui.terminal.clipboard[len++] = g_gui.terminal.cells[r][c];
                copied = 1;
            }
        }
        if (r != g_gui.terminal.selection_end_y && len + 1 < GUI_TERM_CLIPBOARD_SIZE) {
            g_gui.terminal.clipboard[len++] = '\n';
        }
        if (len + 1 >= GUI_TERM_CLIPBOARD_SIZE) break;
    }
    g_gui.terminal.clipboard[len] = '\0';
    g_gui.terminal.clipboard_len = len;
    return copied || len > 0;
}

int gui_terminal_has_clipboard_text(void) {
    return g_gui.terminal.clipboard_len > 0 && g_gui.terminal.clipboard[0] != '\0';
}

const char *gui_terminal_get_clipboard_text(void) {
    return gui_terminal_has_clipboard_text() ? g_gui.terminal.clipboard : "";
}

__attribute__((optimize("no-jump-tables")))
static void gui_handle_mouse_down(int x, int y) {
    if (gui_desktop_handle_click(x, y)) {
        gui_set_focused_widget(0);
        return;
    }

    if (gui_taskbar_terminal_button_at(x, y)) {
        gui_set_focused_widget(0);
        gui_terminal_open();
        return;
    }

    gui_window_t *tw = gui_taskbar_window_at(x, y);
    if (tw) {
        if (tw == g_gui.terminal.window || (tw->flags & GUI_WINDOW_FLAG_TERMINAL)) {
            gui_set_focused_widget(0);
            gui_terminal_open();
            return;
        }
        gui_restore_window(tw);
        return;
    }

    if (gui_is_taskbar_at(x, y)) {
        return;
    }

    gui_window_t *w = gui_window_at(x, y);
    if (w) {
        gui_rect_t close;
        gui_rect_t minr;

        if (w == g_gui.terminal.window) {
            uint32_t tc, trc;
            gui_set_focused_widget(0);
            gui_terminal_set_input_focus(1);
            if (gui_terminal_point_to_cell(x, y, &tc, &trc)) {
                g_gui.terminal.selecting = 1;
                g_gui.terminal.selection_anchor_x = tc;
                g_gui.terminal.selection_anchor_y = trc;
                g_gui.terminal.selection_start_x = tc;
                g_gui.terminal.selection_start_y = trc;
                g_gui.terminal.selection_end_x = tc;
                g_gui.terminal.selection_end_y = trc;
                g_gui.terminal.has_selection = 0;
                gui_terminal_invalidate_body();
            } else {
                gui_terminal_clear_selection();
            }
        }

        gui_set_active_window(w);
        close = gui_close_rect(w);
        minr = gui_min_rect(w);

        if ((w->flags & GUI_WINDOW_FLAG_CLOSABLE) && gui_rect_contains(&close, x, y)) {
            gui_set_focused_widget(0);
            if (g_gui.drag_window == w) g_gui.drag_window = 0;
            if (g_gui.pressed_widget && g_gui.pressed_widget->owner == w) g_gui.pressed_widget = 0;
            gui_destroy_window(w);
            return;
        }

        if ((w->flags & GUI_WINDOW_FLAG_MINIMIZABLE) && gui_rect_contains(&minr, x, y)) {
            gui_set_focused_widget(0);
            if (g_gui.drag_window == w) g_gui.drag_window = 0;
            if (g_gui.pressed_widget && g_gui.pressed_widget->owner == w) g_gui.pressed_widget = 0;
            gui_minimize_window(w);
            return;
        }

        gui_rect_t tr = gui_title_rect(w);
        if (gui_rect_contains(&tr, x, y)) {
            gui_set_focused_widget(0);
            w->dragging = 1;
            w->drag_offset_x = x - w->rect.x;
            w->drag_offset_y = y - w->rect.y;
            g_gui.drag_window = w;
            return;
        }

        int sx = x - w->rect.x - GUI_BORDER_SIZE;
        int sy = y - w->rect.y - GUI_TITLE_HEIGHT;
        gui_widget_t *wg = gui_widget_at(w, sx, sy);
        if (gui_widget_is_clickable(wg)) {
            gui_set_focused_widget(wg);
            wg->pressed = 1;
            g_gui.pressed_widget = wg;
            gui_invalidate_all();
        } else if (gui_widget_can_focus(wg)) {
            gui_set_focused_widget(wg);
        } else {
            gui_set_focused_widget(0);
        }
        return;
    }


    gui_set_focused_widget(0);
}

__attribute__((optimize("no-jump-tables")))
static void gui_handle_mouse_up(int x, int y) {
    if (g_gui.terminal.selecting) {
        uint32_t tc, trc;
        if (gui_terminal_point_to_cell(x, y, &tc, &trc)) {
            gui_terminal_update_selection(tc, trc);
        }
        g_gui.terminal.selecting = 0;
    }
    if (g_gui.drag_window) {
        g_gui.drag_window->dragging = 0;
        g_gui.drag_window = 0;
    }
    if (g_gui.pressed_widget) {
        gui_widget_t *wg = g_gui.pressed_widget;
        int still_inside = 0;
        gui_widget_t *under = gui_widget_at_screen(x, y);
        wg->pressed = 0;
        g_gui.pressed_widget = 0;
        still_inside = (under == wg && gui_widget_is_clickable(wg));
        gui_invalidate_all();
        if (still_inside) {
            gui_button_activate(wg);
        }
    }
}

__attribute__((optimize("no-jump-tables")))
static void gui_handle_mouse_move(int x, int y) {
    gui_set_hovered_widget(gui_widget_at_screen(x, y));
    if (g_gui.terminal.selecting) {
        uint32_t tc, trc;
        if (gui_terminal_point_to_cell(x, y, &tc, &trc)) {
            gui_terminal_update_selection(tc, trc);
        }
        gui_invalidate_rect(x - 18, y - 18, 36, 36);
    } else if (g_gui.drag_window && g_gui.drag_window->dragging) {
        gui_window_t *w = g_gui.drag_window;
        w->rect.x = x - w->drag_offset_x;
        w->rect.y = y - w->drag_offset_y;
        if (w->rect.x < 0) w->rect.x = 0;
        if (w->rect.y < 0) w->rect.y = 0;
        if (w->rect.x + w->rect.w > (int)g_gui.width) w->rect.x = (int)g_gui.width - w->rect.w;
        if (w->rect.y + w->rect.h > (int)g_gui.height - GUI_TASKBAR_HEIGHT) w->rect.y = (int)g_gui.height - GUI_TASKBAR_HEIGHT - w->rect.h;
        gui_invalidate_all();
    } else {
        gui_invalidate_rect(x - 18, y - 18, 36, 36);
    }
}

__attribute__((optimize("no-jump-tables")))
void gui_process_events(void) {
    gui_event_t ev;
    while (gui_event_pop(&ev)) {
        if (ev.type == GUI_EVENT_KEY_DOWN) {
            if (ev.key == GUI_KEY_TAB) {
                gui_focus_next_widget();
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_TEXTBOX) {
                gui_textbox_on_key(g_gui.focused_widget, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_BUTTON &&
                       (ev.key == GUI_KEY_ENTER || ev.key == GUI_KEY_SPACE)) {
                gui_button_activate(g_gui.focused_widget);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused) {
                /* Focused widgets consume keys that they do not handle. */
            } else {
                /* Terminal input must flow through shell_run(), not GUI key events. */
            }
        } else if (ev.type == GUI_EVENT_MOUSE_DOWN) {
            if (ev.button & 1u) gui_handle_mouse_down(ev.x, ev.y);
        } else if (ev.type == GUI_EVENT_MOUSE_UP) {
            if (ev.button & 1u) gui_handle_mouse_up(ev.x, ev.y);
        } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
            gui_handle_mouse_move(ev.x, ev.y);
        } else if (ev.type == GUI_EVENT_BUTTON_CLICK) {
            if (ev.widget && gui_widget_is_clickable(ev.widget)) {
                if (ev.widget->on_click) {
                    ev.widget->on_click(ev.widget, ev.widget->user_data);
                }
            }
        } else if (ev.type == GUI_EVENT_WINDOW_CLOSE) {
            gui_destroy_window(ev.window);
        } else if (ev.type == GUI_EVENT_WINDOW_MINIMIZE) {
            gui_minimize_window(ev.window);
        }
    }
}

static void gui_poll_mouse(void) {
    mouse_state_t ms;
    if (!g_gui.initialized) return;

    usb_tablet_poll((int)g_gui.width, (int)g_gui.height);
    mouse_snapshot_and_clear_delta(&ms);
    if (!ms.present) return;

    if (ms.x < 0) ms.x = 0;
    if (ms.y < 0) ms.y = 0;
    if (ms.x > (int)g_gui.width - 1) ms.x = (int)g_gui.width - 1;
    if (ms.y > (int)g_gui.height - 1) ms.y = (int)g_gui.height - 1;

    if ((ms.buttons & 1u) && !(g_gui.last_mouse_buttons & 1u)) {
        gui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = GUI_EVENT_MOUSE_DOWN;
        ev.x = ms.x;
        ev.y = ms.y;
        ev.button = 1u;
        ev.window = gui_window_at(ms.x, ms.y);
        ev.widget = gui_widget_at_screen(ms.x, ms.y);
        gui_event_push(ev);
    } else if (!(ms.buttons & 1u) && (g_gui.last_mouse_buttons & 1u)) {
        gui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = GUI_EVENT_MOUSE_UP;
        ev.x = ms.x;
        ev.y = ms.y;
        ev.button = 1u;
        ev.window = gui_window_at(ms.x, ms.y);
        ev.widget = gui_widget_at_screen(ms.x, ms.y);
        gui_event_push(ev);
    }

    if (ms.x != g_gui.mouse_x || ms.y != g_gui.mouse_y) {
        int complex_move;
        gui_taskbar_invalidate_hover_changes(g_gui.mouse_x, g_gui.mouse_y, ms.x, ms.y);
        complex_move = (g_gui.drag_window != 0) || g_gui.terminal.selecting ||
                       ((ms.buttons & 1u) != 0) || ((g_gui.last_mouse_buttons & 1u) != 0);
        if (complex_move) {
            gui_event_t ev;
            gui_invalidate_rect(g_gui.mouse_x - 2, g_gui.mouse_y - 2, 22, 22);
            gui_invalidate_rect(ms.x - 2, ms.y - 2, 22, 22);
            memset(&ev, 0, sizeof(ev));
            ev.type = GUI_EVENT_MOUSE_MOVE;
            ev.x = ms.x;
            ev.y = ms.y;
            ev.dx = ms.x - g_gui.mouse_x;
            ev.dy = ms.y - g_gui.mouse_y;
            ev.button = (uint8_t)(ms.buttons & 0xffu);
            ev.window = gui_window_at(ms.x, ms.y);
            ev.widget = gui_widget_at_screen(ms.x, ms.y);
            gui_event_push(ev);
        } else {
            g_gui.mouse_x = ms.x;
            g_gui.mouse_y = ms.y;
            gui_cursor_present_fast();
        }
    }

    g_gui.mouse_x = ms.x;
    g_gui.mouse_y = ms.y;
    g_gui.last_mouse_buttons = ms.buttons;
}

void gui_init(void) {
    memset(&g_gui, 0, sizeof(g_gui));
    memset(&g_gui_accel, 0, sizeof(g_gui_accel));
    g_gui_accel.enabled = 1;
    g_gui.colors.desktop_bg = gui_rgb(18, 28, 45);
    g_gui.colors.window_bg = gui_rgb(236, 238, 244);
    g_gui.colors.window_border = gui_rgb(70, 80, 95);
    g_gui.colors.title_bg = gui_rgb(30, 105, 210);
    g_gui.colors.title_fg = gui_rgb(255, 255, 255);
    g_gui.colors.text_fg = gui_rgb(20, 25, 32);
    g_gui.colors.button_bg = gui_rgb(220, 224, 232);
    g_gui.colors.button_border = gui_rgb(250, 250, 250);
    g_gui.colors.button_fg = gui_rgb(10, 10, 10);
    g_gui.colors.accent = gui_rgb(70, 145, 255);
    g_gui.next_window_id = 1;
    g_gui.next_widget_id = 1;
    g_gui.next_app_id = 1;
    g_gui.mouse_x = 512;
    g_gui.mouse_y = 384;
    /* Default on shows the OpenOS software cursor inside the GUI.
     * Use `cursor off` to hide it when the host cursor is preferred.
     */
    g_gui.cursor_visible = 1;
    g_gui.cursor_drawn = 0;
    g_gui.cursor_fb_x = 0;
    g_gui.cursor_fb_y = 0;
    serial_write("[OK] GUI object pool\n");
}

int gui_start(uint32_t width, uint32_t height) {
    const framebuffer_info_t *info;
    uint32_t pixels;
    mouse_state_t ms;
    if (!framebuffer_is_available()) return -1;
    if (framebuffer_set_mode(width, height, 32) != 0) return -1;
    info = framebuffer_get_info();
    if (!info || !info->mode_set) return -1;

    g_gui.width = info->width;
    g_gui.height = info->height;

    /* 鍏堣缃紶鏍囪竟鐣屼负鐪熷疄鍒嗚鲸鐜?*/
    mouse_set_bounds((int)g_gui.width, (int)g_gui.height);

    g_gui.initialized = 1;

    /* 浠庨紶鏍囬┍鍔ㄨ幏鍙栧綋鍓嶅潗鏍?*/
    mouse_snapshot_and_clear_delta(&ms);

    /* 濡傛灉榧犳爣杩樻病鏀跺埌涓柇鍖咃紝浣跨敤灞忓箷涓績 */
    if (ms.packet_count == 0) {
        g_gui.mouse_x = (int)(g_gui.width / 2);
        g_gui.mouse_y = (int)(g_gui.height / 2);
        mouse_set_position(g_gui.mouse_x, g_gui.mouse_y);
    } else {
        g_gui.mouse_x = ms.x;
        g_gui.mouse_y = ms.y;
    }

    /* 鍧愭爣杈圭晫鏈€缁堟鏌?*/
    if (g_gui.mouse_x < 0) g_gui.mouse_x = 0;
    if (g_gui.mouse_y < 0) g_gui.mouse_y = 0;
    if (g_gui.mouse_x >= (int)g_gui.width) g_gui.mouse_x = (int)g_gui.width - 1;
    if (g_gui.mouse_y >= (int)g_gui.height) g_gui.mouse_y = (int)g_gui.height - 1;

#if GUI_DEBUG_LOG
    serial_write("[GUI] mouse sync: x=");
    gui_write_dec((uint32_t)g_gui.mouse_x);
    serial_write(" y=");
    gui_write_dec((uint32_t)g_gui.mouse_y);
    serial_write(" bounds=");
    gui_write_dec(g_gui.width);
    serial_write("x");
    gui_write_dec(g_gui.height);
    serial_write("\n");
#endif

    pixels = g_gui.width * g_gui.height;
    if (!g_gui.backbuffer || g_gui.backbuffer_pixels < pixels) {
        g_gui.backbuffer = (uint32_t *)kmalloc(pixels * sizeof(uint32_t));
        g_gui.backbuffer_pixels = g_gui.backbuffer ? pixels : 0;
    }
    g_gui.double_buffered = g_gui.backbuffer ? 1 : 0;
    g_gui.compositor_enabled = g_gui.double_buffered;

    gui_terminal_init();
    gui_desktop_init();
    gui_render();
    return 0;
}

static void gui_launcher_add(uint32_t index, const char *name, const char *title, uint32_t action, uint32_t color) {
    gui_launcher_entry_t *entry;
    uint32_t i;

    if (index >= GUI_LAUNCHER_MAX_APPS || !name || !title) return;
    entry = &g_gui.launcher_entries[index];
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->action = action;
    entry->color = color;
    for (i = 0; i < sizeof(entry->name) - 1 && name[i]; i++) entry->name[i] = name[i];
    entry->name[i] = 0;
    for (i = 0; i < sizeof(entry->title) - 1 && title[i]; i++) entry->title[i] = title[i];
    entry->title[i] = 0;
}

static void gui_launcher_init(void) {
    memset(g_gui.launcher_entries, 0, sizeof(g_gui.launcher_entries));
    g_gui.launcher_enabled = 1;
    g_gui.launcher_app_count = 3;
    gui_launcher_add(0, "terminal", "Terminal", GUI_DESKTOP_ACTION_TERMINAL, gui_rgb(68, 144, 245));
    gui_launcher_add(1, "demo", "Window Demo", GUI_DESKTOP_ACTION_DEMO, gui_rgb(170, 112, 235));
    gui_launcher_add(2, "about", "About OpenOS", GUI_DESKTOP_ACTION_ABOUT, gui_rgb(88, 196, 128));
}

static void gui_desktop_add_icon(uint32_t index, int x, int y, const char *label, uint32_t color, uint32_t action) {
    gui_desktop_icon_t *icon;
    uint32_t i;

    if (index >= GUI_DESKTOP_MAX_ICONS || !label) return;
    icon = &g_gui.desktop_icons[index];
    memset(icon, 0, sizeof(*icon));
    icon->used = 1;
    icon->rect.x = x;
    icon->rect.y = y;
    icon->rect.w = GUI_DESKTOP_ICON_W;
    icon->rect.h = GUI_DESKTOP_ICON_H;
    icon->color = color;
    icon->action = action;
    for (i = 0; i < sizeof(icon->label) - 1 && label[i]; i++) icon->label[i] = label[i];
    icon->label[i] = 0;
}

static void gui_desktop_init(void) {
    uint32_t top;

    g_gui.desktop_enabled = 1;
    g_gui.desktop_start_menu_open = 0;
    g_gui.desktop_icon_count = 0;
    gui_launcher_init();

    top = g_gui.height > GUI_TASKBAR_HEIGHT ? g_gui.height - GUI_TASKBAR_HEIGHT : 0;
    g_gui.desktop_taskbar_rect.x = 0;
    g_gui.desktop_taskbar_rect.y = (int)top;
    g_gui.desktop_taskbar_rect.w = (int)g_gui.width;
    g_gui.desktop_taskbar_rect.h = GUI_TASKBAR_HEIGHT;
    g_gui.desktop_start_button_rect.x = 6;
    g_gui.desktop_start_button_rect.y = (int)top + 4;
    g_gui.desktop_start_button_rect.w = 86;
    g_gui.desktop_start_button_rect.h = 24;
    g_gui.desktop_start_menu_rect.x = 6;
    g_gui.desktop_start_menu_rect.y = (int)top - GUI_DESKTOP_MENU_H - 4;
    g_gui.desktop_start_menu_rect.w = GUI_DESKTOP_MENU_W;
    g_gui.desktop_start_menu_rect.h = GUI_DESKTOP_MENU_H;

    memset(g_gui.desktop_icons, 0, sizeof(g_gui.desktop_icons));
    gui_desktop_add_icon(0, 32, 72, "Files", gui_rgb(242, 194, 74), GUI_DESKTOP_ACTION_FILES);
    g_gui.desktop_icon_count = 1;
}

static void gui_desktop_draw_icon(gui_desktop_icon_t *icon) {
    int cx;
    int iy;

    if (!icon || !icon->used) return;
    gui_raw_fill_rect(icon->rect.x, icon->rect.y, icon->rect.w, icon->rect.h, gui_rgb(30, 45, 68));
    gui_raw_line(icon->rect.x, icon->rect.y, icon->rect.x + icon->rect.w - 1, icon->rect.y, gui_rgb(92, 125, 170));
    gui_raw_line(icon->rect.x, icon->rect.y, icon->rect.x, icon->rect.y + icon->rect.h - 1, gui_rgb(92, 125, 170));
    gui_raw_line(icon->rect.x + icon->rect.w - 1, icon->rect.y, icon->rect.x + icon->rect.w - 1, icon->rect.y + icon->rect.h - 1, gui_rgb(14, 20, 32));
    gui_raw_line(icon->rect.x, icon->rect.y + icon->rect.h - 1, icon->rect.x + icon->rect.w - 1, icon->rect.y + icon->rect.h - 1, gui_rgb(14, 20, 32));
    cx = icon->rect.x + (icon->rect.w - 28) / 2;
    iy = icon->rect.y + 8;
    if (icon->action == GUI_DESKTOP_ACTION_FILES) {
        gui_raw_fill_rect(cx + 2, iy + 6, 11, 6, gui_rgb(255, 220, 105));
        gui_raw_fill_rect(cx, iy + 11, 28, 20, icon->color);
        gui_raw_fill_rect(cx + 2, iy + 15, 24, 14, gui_rgb(255, 211, 86));
        gui_raw_line(cx, iy + 11, cx + 27, iy + 11, gui_rgb(255, 238, 160));
        gui_raw_line(cx, iy + 11, cx, iy + 30, gui_rgb(255, 238, 160));
        gui_raw_line(cx + 27, iy + 11, cx + 27, iy + 30, gui_rgb(130, 90, 24));
        gui_raw_line(cx, iy + 30, cx + 27, iy + 30, gui_rgb(130, 90, 24));
    } else {
        gui_raw_fill_rect(cx, iy, 28, 28, icon->color);
        gui_raw_line(cx, iy, cx + 27, iy, gui_rgb(225, 235, 255));
        gui_raw_line(cx, iy, cx, iy + 27, gui_rgb(225, 235, 255));
        gui_raw_line(cx + 27, iy, cx + 27, iy + 27, gui_rgb(18, 25, 38));
        gui_raw_line(cx, iy + 27, cx + 27, iy + 27, gui_rgb(18, 25, 38));
    }
    gui_draw_text(icon->rect.x + 6, icon->rect.y + 44, icon->label, gui_rgb(232, 240, 255));
}

static void gui_desktop_draw_start_menu(void) {
    gui_rect_t *r = &g_gui.desktop_start_menu_rect;
    uint32_t i;
    if (!g_gui.desktop_start_menu_open) return;

    gui_raw_fill_rect(r->x, r->y, r->w, r->h, gui_rgb(28, 36, 54));
    gui_raw_line(r->x, r->y, r->x + r->w - 1, r->y, gui_rgb(112, 146, 198));
    gui_raw_line(r->x, r->y, r->x, r->y + r->h - 1, gui_rgb(112, 146, 198));
    gui_raw_line(r->x + r->w - 1, r->y, r->x + r->w - 1, r->y + r->h - 1, gui_rgb(10, 13, 20));
    gui_raw_line(r->x, r->y + r->h - 1, r->x + r->w - 1, r->y + r->h - 1, gui_rgb(10, 13, 20));
    gui_draw_text(r->x + 12, r->y + 12, "OpenOS Launcher", gui_rgb(245, 250, 255));

    for (i = 0; i < g_gui.launcher_app_count && i < GUI_LAUNCHER_MAX_APPS; i++) {
        gui_launcher_entry_t *entry = &g_gui.launcher_entries[i];
        int iy = r->y + 36 + (int)i * GUI_LAUNCHER_ITEM_H;
        if (!entry->used) continue;
        gui_raw_fill_rect(r->x + 10, iy, r->w - 20, GUI_LAUNCHER_ITEM_H - 2, gui_rgb(46, 64, 92));
        gui_raw_fill_rect(r->x + 14, iy + 5, 12, 12, entry->color);
        gui_draw_text(r->x + 34, iy + 5, entry->title, gui_rgb(232, 240, 255));
    }
}

static void gui_desktop_draw(void) {
    uint32_t i;

    if (!g_gui.desktop_enabled) return;
    gui_draw_text(116, 72, "Welcome to OpenOS", gui_rgb(235, 242, 255));
    gui_draw_text(116, 104, "Desktop environment is ready.", gui_rgb(205, 220, 245));
    gui_draw_text(116, 132, "Use menu icon to launch tools.", gui_rgb(170, 195, 230));
    for (i = 0; i < g_gui.desktop_icon_count && i < GUI_DESKTOP_MAX_ICONS; i++) {
        gui_desktop_draw_icon(&g_gui.desktop_icons[i]);
    }
}

static void gui_desktop_run_action(uint32_t action) {
    if (action == GUI_DESKTOP_ACTION_TERMINAL) {
        gui_terminal_open();
        return;
    }
    if (action == GUI_DESKTOP_ACTION_ABOUT) {
        serial_write("[GUI] OpenOS launcher about\n");
        gui_demo();
        return;
    }
    if (action == GUI_DESKTOP_ACTION_DEMO) {
        gui_demo();
        return;
    }
    if (action == GUI_DESKTOP_ACTION_FILES) {
        gui_file_preview_open();
        return;
    }
    if (action == GUI_DESKTOP_ACTION_MENU) {
        g_gui.desktop_start_menu_open = !g_gui.desktop_start_menu_open;
        gui_invalidate_all();
    }
}

static int gui_desktop_handle_click(int x, int y) {
    uint32_t i;
    gui_rect_t item;

    if (!g_gui.desktop_enabled) return 0;
    if (gui_rect_contains(&g_gui.desktop_start_button_rect, x, y)) {
        gui_desktop_run_action(GUI_DESKTOP_ACTION_MENU);
        return 1;
    }
    if (g_gui.desktop_start_menu_open && gui_rect_contains(&g_gui.desktop_start_menu_rect, x, y)) {
        for (i = 0; i < g_gui.launcher_app_count && i < GUI_LAUNCHER_MAX_APPS; i++) {
            item.x = g_gui.desktop_start_menu_rect.x + 10;
            item.y = g_gui.desktop_start_menu_rect.y + 36 + (int)i * GUI_LAUNCHER_ITEM_H;
            item.w = g_gui.desktop_start_menu_rect.w - 20;
            item.h = GUI_LAUNCHER_ITEM_H - 2;
            if (gui_rect_contains(&item, x, y)) {
                g_gui.desktop_start_menu_open = 0;
                gui_launcher_launch(i);
                gui_invalidate_all();
                return 1;
            }
        }
        return 1;
    }
    if (g_gui.desktop_start_menu_open) {
        g_gui.desktop_start_menu_open = 0;
        gui_invalidate_all();
    }
    for (i = 0; i < g_gui.desktop_icon_count && i < GUI_DESKTOP_MAX_ICONS; i++) {
        gui_desktop_icon_t *icon = &g_gui.desktop_icons[i];
        if (icon->used && gui_rect_contains(&icon->rect, x, y)) {
            gui_desktop_run_action(icon->action);
            return 1;
        }
    }
    return 0;
}

int gui_start_desktop(void) {
    if (g_gui.initialized) {
        gui_terminal_minimize();
        gui_render();
        return 0;
    }

    if (gui_start(1024, 768) != 0) return -1;
    gui_terminal_write("\n[GUI] desktop started. Click TERMINAL on the taskbar to open command line.\n");
    gui_terminal_minimize();
    gui_render();
    return 0;
}

int gui_is_ready(void) { return g_gui.initialized; }

int gui_has_focused_widget(void) {
    return g_gui.initialized && g_gui.focused_widget && g_gui.focused_widget->focused;
}

gui_widget_t *gui_get_focused_widget(void) {
    if (!g_gui.initialized || !g_gui.focused_widget || !g_gui.focused_widget->focused) return 0;
    return g_gui.focused_widget;
}

int gui_should_capture_key_code(int key) {
    gui_widget_t *wg;

    if (!g_gui.initialized) return 0;

    /* GUI Terminal is the Shell's graphical output window. Do not capture
     * printable keys here: shell_run() must receive them so command editing,
     * history, backspace and enter keep working. Ordinary GUI widgets are the
     * only keyboard-capture targets.
     */
    if (!g_gui.focused_widget || !g_gui.focused_widget->focused) return 0;

    wg = g_gui.focused_widget;
    if (!wg->visible || !wg->enabled) return 0;

    if (key == GUI_KEY_TAB) return 1;

    if (key == GUI_KEY_UP || key == GUI_KEY_DOWN) return 0;

    if (wg->type == GUI_WIDGET_TEXTBOX) return 1;

    if (wg->type == GUI_WIDGET_BUTTON) {
        return key == GUI_KEY_ENTER || key == GUI_KEY_SPACE;
    }

    return 0;
}

void gui_shutdown_to_text_note(void) { serial_write("[GUI] text mode restore is not implemented yet\n"); }

void gui_set_cursor_visible(int visible) {
    g_gui.cursor_visible = visible ? 1 : 0;
    if (g_gui.initialized) {
        gui_invalidate_all();
        gui_render();
    }
}

int gui_is_cursor_visible(void) { return g_gui.cursor_visible; }

const gui_system_t *gui_get_system(void) { return &g_gui; }

void gui_get_compositor_info(gui_compositor_info_t *info) {
    if (!info) return;
    info->enabled = g_gui.compositor_enabled;
    info->active = gui_compositor_active();
    info->double_buffered = g_gui.double_buffered;
    info->width = g_gui.width;
    info->height = g_gui.height;
    info->backbuffer_pixels = g_gui.backbuffer_pixels;
    info->dirty_count = g_gui.dirty_count;
    info->full_dirty = g_gui.full_dirty;
}

int gui_compositor_is_active(void) {
    return gui_compositor_active();
}

void gui_set_compositor_enabled(int enabled) {
    int next = enabled ? 1 : 0;
    if (g_gui.compositor_enabled == next) return;
    g_gui.compositor_enabled = next;
    if (g_gui.initialized) gui_invalidate_all();
}

void gui_compositor_flush(void) {
    if (!gui_compositor_active()) return;
    gui_cursor_restore_fb();
    gui_flush_backbuffer();
    gui_cursor_draw_fb();
}

void gui_print_info(void) {
    serial_write("[GUI] ready="); gui_write_dec((uint32_t)g_gui.initialized);
    serial_write(" size="); gui_write_dec(g_gui.width); serial_write("x"); gui_write_dec(g_gui.height);
    serial_write(" windows="); gui_write_dec(g_gui.window_count);
    serial_write(" apps="); gui_write_dec(g_gui.next_app_id > 1 ? g_gui.next_app_id - 1 : 0);
    serial_write(" active="); gui_write_dec(g_gui.active_window ? g_gui.active_window->id : 0);
    serial_write(" active_app="); gui_write_dec(g_gui.active_app ? g_gui.active_app->id : 0);
    serial_write(" events="); gui_write_dec(g_gui.event_count);
    serial_write(" dblbuf="); gui_write_dec((uint32_t)g_gui.double_buffered);
    serial_write(" compositor="); gui_write_dec((uint32_t)gui_compositor_active());
    serial_write(" accel="); gui_write_dec((uint32_t)gui_accel_is_enabled());
    serial_write(" fills="); gui_write_dec(g_gui_accel.rect_fills);
    serial_write(" flush_px="); gui_write_dec(g_gui_accel.flush_pixels);
    serial_write(" cursor="); gui_write_dec((uint32_t)g_gui.cursor_visible);
    serial_write(" mouse="); gui_write_dec((uint32_t)g_gui.mouse_x); serial_write(","); gui_write_dec((uint32_t)g_gui.mouse_y);
    serial_write("\n");
}

gui_app_t *gui_register_app(const char *name, const char *title, gui_app_entry_t entry, void *user_data) {
    uint32_t i;
    gui_app_t *app;
    if (!entry) return 0;
    for (i = 0; i < GUI_MAX_APPS; i++) {
        if (!g_gui.apps[i].used) break;
    }
    if (i >= GUI_MAX_APPS) return 0;
    app = &g_gui.apps[i];
    memset(app, 0, sizeof(gui_app_t));
    app->used = 1;
    app->id = g_gui.next_app_id++;
    gui_copy_text(app->name, name ? name : "app", sizeof(app->name));
    gui_copy_text(app->title, title ? title : app->name, sizeof(app->title));
    app->entry = entry;
    app->user_data = user_data;
    return app;
}

int gui_start_app(gui_app_t *app) {
    gui_app_t *prev;
    int rc;
    if (!app || !app->used || !app->entry) return -1;
    if (app->running) return 0;
    prev = g_gui.launching_app;
    g_gui.launching_app = app;
    app->running = 1;
    app->window_count = 0;
    app->main_window = 0;
    rc = app->entry(app, app->user_data);
    g_gui.launching_app = prev;
    if (rc < 0 || app->window_count == 0) {
        gui_exit_app(app);
        return rc < 0 ? rc : -1;
    }
    g_gui.active_app = app;
    return 0;
}

void gui_exit_app(gui_app_t *app) {
    uint32_t i;
    if (!app || !app->used) return;
    for (i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (g_gui.windows[i].used && g_gui.windows[i].owner_app == app) {
            gui_destroy_window(&g_gui.windows[i]);
        }
    }
    app->running = 0;
    app->main_window = 0;
    app->window_count = 0;
    if (g_gui.active_app == app) g_gui.active_app = 0;
    gui_refresh_active_app();
}

gui_app_t *gui_get_active_app(void) { return g_gui.active_app; }

gui_app_t *gui_get_window_app(gui_window_t *window) { return gui_app_for_window(window); }

static gui_app_t *gui_app_for_window(gui_window_t *window) {
    return (window && window->used) ? window->owner_app : 0;
}

static void gui_refresh_active_app(void) {
    g_gui.active_app = gui_app_for_window(g_gui.active_window);
}

gui_window_t *gui_create_app_window(gui_app_t *app, int x, int y, int w, int h, const char *title) {
    gui_app_t *prev;
    gui_window_t *win;
    if (!app || !app->used) return 0;
    prev = g_gui.launching_app;
    g_gui.launching_app = app;
    win = gui_create_window(x, y, w, h, title ? title : app->title);
    g_gui.launching_app = prev;
    if (win) win->owner_app = app;
    return win;
}

gui_window_t *gui_create_window(int x, int y, int w, int h, const char *title) {
    gui_window_t *win;
    gui_app_t *owner;
    uint32_t idx;
    if (g_gui.window_count >= GUI_MAX_WINDOWS) return 0;
    for (idx = 0; idx < GUI_MAX_WINDOWS; idx++) {
        if (!g_gui.windows[idx].used) break;
    }
    if (idx >= GUI_MAX_WINDOWS) return 0;
    win = &g_gui.windows[idx];
    memset(win, 0, sizeof(gui_window_t));
    win->used = 1;
    win->id = g_gui.next_window_id++;
    win->rect.x = x; win->rect.y = y; win->rect.w = w; win->rect.h = h;
    gui_copy_text(win->title, title ? title : "Window", sizeof(win->title));
    win->bg_color = g_gui.colors.window_bg;
    win->flags = GUI_WINDOW_FLAG_CLOSABLE | GUI_WINDOW_FLAG_MINIMIZABLE;
    win->visible = 1;
    owner = g_gui.launching_app;
    if (owner && owner->used) {
        win->owner_app = owner;
        owner->window_count++;
        if (!owner->main_window) owner->main_window = win;
    }
    g_gui.z_order[g_gui.window_count++] = idx;
    gui_set_active_window(win);
    return win;
}

static gui_widget_t *gui_alloc_widget(gui_window_t *window, gui_widget_type_t type, int x, int y, int w, int h, const char *text) {
    gui_widget_t *wg;
    if (!window || window->widget_count >= GUI_MAX_WIDGETS_PER_WIN) return 0;
    wg = &window->widgets[window->widget_count++];
    memset(wg, 0, sizeof(gui_widget_t));
    wg->id = g_gui.next_widget_id++;
    wg->type = type;
    wg->rect.x = x; wg->rect.y = y; wg->rect.w = w; wg->rect.h = h;
    gui_copy_text(wg->text, text, sizeof(wg->text));
    wg->fg_color = g_gui.colors.text_fg;
    wg->bg_color = g_gui.colors.button_bg;
    wg->owner = window;
    wg->visible = 1;
    wg->enabled = 1;
    wg->cursor = (uint32_t)strlen(wg->text);
    return wg;
}

gui_widget_t *gui_add_label(gui_window_t *window, int x, int y, int w, int h, const char *text) {
    return gui_alloc_widget(window, GUI_WIDGET_LABEL, x, y, w, h, text);
}

gui_widget_t *gui_add_button(gui_window_t *window, int x, int y, int w, int h, const char *text, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_BUTTON, x, y, w, h, text);
    if (wg) { wg->on_click = cb; wg->user_data = user_data; }
    return wg;
}

gui_widget_t *gui_add_panel(gui_window_t *window, int x, int y, int w, int h, uint32_t color) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_PANEL, x, y, w, h, "");
    if (wg) wg->bg_color = color;
    return wg;
}

gui_widget_t *gui_add_textbox(gui_window_t *window, int x, int y, int w, int h, const char *text) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_TEXTBOX, x, y, w, h, text);
    if (wg) {
        wg->fg_color = gui_rgb(20, 20, 20);
        wg->bg_color = gui_rgb(250, 250, 250);
        wg->cursor = (uint32_t)strlen(wg->text);
    }
    return wg;
}

static void gui_widget_invalidate(gui_widget_t *widget) {
    if (!widget || !widget->owner) return;
    gui_invalidate_rect(widget->owner->rect.x, widget->owner->rect.y,
                        widget->owner->rect.w, widget->owner->rect.h);
}

gui_widget_t *gui_find_widget(gui_window_t *window, uint32_t id) {
    uint32_t i;
    if (!window || !window->used || id == 0) return 0;
    for (i = 0; i < window->widget_count; i++) {
        if (window->widgets[i].id == id) return &window->widgets[i];
    }
    return 0;
}

void gui_widget_set_enabled(gui_widget_t *widget, int enabled) {
    if (!widget) return;
    widget->enabled = enabled ? 1 : 0;
    if (!widget->enabled) {
        widget->pressed = 0;
        widget->hovered = 0;
        if (g_gui.pressed_widget == widget) g_gui.pressed_widget = 0;
        if (g_gui.hovered_widget == widget) g_gui.hovered_widget = 0;
        if (g_gui.focused_widget == widget) gui_set_focused_widget(0);
    }
    gui_widget_invalidate(widget);
}

void gui_widget_set_visible(gui_widget_t *widget, int visible) {
    if (!widget) return;
    widget->visible = visible ? 1 : 0;
    if (!widget->visible) {
        widget->pressed = 0;
        widget->hovered = 0;
        if (g_gui.pressed_widget == widget) g_gui.pressed_widget = 0;
        if (g_gui.hovered_widget == widget) g_gui.hovered_widget = 0;
        if (g_gui.focused_widget == widget) gui_set_focused_widget(0);
    }
    gui_widget_invalidate(widget);
}

void gui_widget_set_text(gui_widget_t *widget, const char *text) {
    uint32_t len;
    if (!widget) return;
    gui_copy_text(widget->text, text ? text : "", sizeof(widget->text));
    len = (uint32_t)strlen(widget->text);
    if (widget->cursor > len || widget->type != GUI_WIDGET_TEXTBOX) widget->cursor = len;
    gui_widget_invalidate(widget);
}

const char *gui_widget_get_text(const gui_widget_t *widget) {
    return widget ? widget->text : "";
}

void gui_widget_set_colors(gui_widget_t *widget, uint32_t bg_color, uint32_t fg_color) {
    if (!widget) return;
    widget->bg_color = bg_color;
    widget->fg_color = fg_color;
    gui_widget_invalidate(widget);
}

void gui_widget_set_on_click(gui_widget_t *widget, gui_widget_callback_t cb, void *user_data) {
    if (!widget) return;
    widget->on_click = cb;
    widget->user_data = user_data;
}

void gui_widget_focus(gui_widget_t *widget) {
    gui_set_focused_widget(widget);
}

void gui_terminal_clear(void) {
    uint32_t r, c;
    if (!g_gui.initialized || !g_gui.terminal.enabled) return;

    for (r = 0; r < GUI_TERM_ROWS; r++) {
        for (c = 0; c < GUI_TERM_COLS; c++) {
            g_gui.terminal.cells[r][c] = ' ';
        }
    }

    g_gui.terminal.cursor_x = 0;
    g_gui.terminal.cursor_y = 0;
    g_gui.terminal.selecting = 0;
    g_gui.terminal.has_selection = 0;
    g_gui.terminal.dirty = 1;
    gui_terminal_invalidate_body();
}

void gui_terminal_init(void) {
    gui_window_t *term;
    if (g_gui.terminal.window) return;
    term = gui_create_window(24, 420, (int)g_gui.width - 48, (int)g_gui.height - 448, "TERMINAL");
    if (!term) return;
    term->flags |= GUI_WINDOW_FLAG_TERMINAL;
    term->bg_color = gui_rgb(10, 14, 22);
    g_gui.terminal.window = term;
    g_gui.terminal.cols = (uint32_t)((term->rect.w - 12) / GUI_CHAR_W);
    g_gui.terminal.rows = (uint32_t)((term->rect.h - GUI_TITLE_HEIGHT - 10) / (GUI_CHAR_H + 1));
    if (g_gui.terminal.cols > GUI_TERM_COLS) g_gui.terminal.cols = GUI_TERM_COLS;
    if (g_gui.terminal.rows > GUI_TERM_ROWS) g_gui.terminal.rows = GUI_TERM_ROWS;
    g_gui.terminal.enabled = 1;
    g_gui.terminal.input_focused = 1;
    g_gui.terminal.cursor_visible = 1;
    g_gui.terminal.cursor_blink_ticks = 0;
    g_gui.terminal.selecting = 0;
    g_gui.terminal.has_selection = 0;
    g_gui.terminal.clipboard_len = 0;
    g_gui.terminal.clipboard[0] = '\0';
    gui_terminal_clear();
    gui_terminal_write("TERMINAL ready. Keyboard input is routed here.\n> ");
}

static void gui_terminal_scroll(void) {
    uint32_t r;
    if (g_gui.terminal.rows == 0 || g_gui.terminal.cols == 0) return;
    for (r = 1; r < g_gui.terminal.rows; r++) {
        memcpy(g_gui.terminal.cells[r - 1], g_gui.terminal.cells[r], g_gui.terminal.cols);
    }
    memset(g_gui.terminal.cells[g_gui.terminal.rows - 1], ' ', g_gui.terminal.cols);
    if (g_gui.terminal.cursor_y > 0) g_gui.terminal.cursor_y--;
    gui_terminal_invalidate_body();
}

static void gui_terminal_handle_carriage_return(void) {
    /* CR returns to the beginning of the current line. It must not scroll or move down. */
    g_gui.terminal.cursor_x = 0;
}

static void gui_terminal_handle_line_feed(int *body_dirty) {
    /* LF advances to the next display line. Keep the existing console convention of column 0. */
    g_gui.terminal.cursor_x = 0;
    g_gui.terminal.cursor_y++;
    if (g_gui.terminal.cursor_y >= g_gui.terminal.rows) {
        gui_terminal_scroll();
        *body_dirty = 1;
    }
}

static void gui_terminal_handle_backspace(void) {
    if (g_gui.terminal.cursor_x > 0) {
        g_gui.terminal.cursor_x--;
    } else if (g_gui.terminal.cursor_y > 0) {
        g_gui.terminal.cursor_y--;
        g_gui.terminal.cursor_x = g_gui.terminal.cols - 1;
    }
}

void gui_terminal_putc(char ch) {
    uint32_t old_x, old_y;
    int body_dirty = 0;
    if (!g_gui.initialized || !g_gui.terminal.enabled) return;
    if (g_gui.terminal.cols == 0 || g_gui.terminal.rows == 0) return;
    if (g_gui.terminal.has_selection && ch != 0x03 && ch != 0x16) gui_terminal_clear_selection();

    old_x = g_gui.terminal.cursor_x;
    old_y = g_gui.terminal.cursor_y;
    gui_terminal_invalidate_cursor_at(old_x, old_y);

    if (ch == '\n') {
        gui_terminal_handle_line_feed(&body_dirty);
    } else if (ch == '\r') {
        gui_terminal_handle_carriage_return();
    } else if (ch == '\b') {
        gui_terminal_handle_backspace();
    } else {
        if (g_gui.terminal.cursor_x >= g_gui.terminal.cols) {
            gui_terminal_handle_line_feed(&body_dirty);
        }
        if (g_gui.terminal.cursor_y >= g_gui.terminal.rows) {
            gui_terminal_scroll();
            body_dirty = 1;
        }
        g_gui.terminal.cells[g_gui.terminal.cursor_y][g_gui.terminal.cursor_x] = ch;
        if (!body_dirty) gui_terminal_invalidate_cell(g_gui.terminal.cursor_x, g_gui.terminal.cursor_y);
        g_gui.terminal.cursor_x++;
    }
    if (g_gui.terminal.cursor_y >= g_gui.terminal.rows) {
        gui_terminal_scroll();
        body_dirty = 1;
    }
    if (!body_dirty) gui_terminal_invalidate_cursor_at(g_gui.terminal.cursor_x, g_gui.terminal.cursor_y);
    g_gui.terminal.cursor_visible = 1;
    g_gui.terminal.cursor_blink_ticks = 0;
    g_gui.terminal.dirty = 1;
}

void gui_terminal_set_input_focus(int focused) {
    g_gui.terminal.input_focused = focused ? 1 : 0;
    g_gui.terminal.cursor_visible = g_gui.terminal.input_focused ? 1 : 0;
    g_gui.terminal.cursor_blink_ticks = 0;
    gui_terminal_invalidate_cursor();
}

void gui_terminal_open(void) {
    extern void kernel_start_shell_thread(void);
    int already_active;

    if (!g_gui.initialized) return;

    if (!g_gui.terminal.window) {
#if GUI_DEBUG_LOG
        serial_write("[GUI] terminal rebuild\n");
#endif
        gui_terminal_init();
    }
    if (!g_gui.terminal.window) {
#if GUI_DEBUG_LOG
        serial_write("[GUI] terminal open failed\n");
#endif
        return;
    }

    already_active = g_gui.terminal.enabled &&
                     g_gui.terminal.input_focused &&
                     g_gui.terminal.window->visible &&
                     !(g_gui.terminal.window->flags & GUI_WINDOW_FLAG_MINIMIZED) &&
                     g_gui.active_window == g_gui.terminal.window;

    gui_set_focused_widget(0);
    gui_restore_window(g_gui.terminal.window);
    gui_set_active_window(g_gui.terminal.window);
    g_gui.terminal.enabled = 1;
    g_gui.terminal.input_focused = 1;
    g_gui.terminal.cursor_visible = 1;
    g_gui.terminal.cursor_blink_ticks = 0;

    gui_invalidate_rect(g_gui.terminal.window->rect.x, g_gui.terminal.window->rect.y,
                        g_gui.terminal.window->rect.w, g_gui.terminal.window->rect.h);
    gui_invalidate_all();

    if (already_active) return;

    input_flush();
#if GUI_TERMINAL_START_SHELL
    kernel_start_shell_thread();
#else
#endif
}

int gui_terminal_is_active(void) {
    return g_gui.initialized &&
           g_gui.terminal.enabled &&
           g_gui.terminal.window &&
           g_gui.terminal.window->visible &&
           !(g_gui.terminal.window->flags & GUI_WINDOW_FLAG_MINIMIZED);
}

void gui_terminal_minimize(void) {
    if (!g_gui.initialized || !g_gui.terminal.window) return;
    gui_minimize_window(g_gui.terminal.window);
    g_gui.terminal.input_focused = 0;
    g_gui.terminal.cursor_visible = 0;
    g_gui.terminal.cursor_blink_ticks = 0;
}

void gui_terminal_on_input(char ch) {
    if (!g_gui.initialized || !g_gui.terminal.enabled || !g_gui.terminal.input_focused) return;
    gui_terminal_putc(ch);
}

void gui_terminal_write(const char *text) {
    if (!text) return;
    while (*text) gui_terminal_putc(*text++);
}

void gui_terminal_enqueue_output(const char *text) {
    if (!text) return;
    while (*text) {
        uint32_t head = g_terminal_out_head;
        uint32_t next = (head + 1u) % GUI_TERMINAL_OUTPUT_QUEUE_SIZE;
        if (next == g_terminal_out_tail) {
            return;
        }
        g_terminal_out_queue[head] = *text++;
        g_terminal_out_head = next;
    }
}

static void gui_terminal_drain_output_queue(void) {
    uint32_t drained = 0;
    while (g_terminal_out_tail != g_terminal_out_head && drained < GUI_TERMINAL_OUTPUT_DRAIN_LIMIT) {
        char ch = g_terminal_out_queue[g_terminal_out_tail];
        g_terminal_out_tail = (g_terminal_out_tail + 1u) % GUI_TERMINAL_OUTPUT_QUEUE_SIZE;
        gui_terminal_putc(ch);
        drained++;
    }
}

void gui_terminal_redraw(void) {
    uint32_t r, c;
    uint32_t r_start = 0, r_end;
    uint32_t c_start = 0, c_end;
    gui_window_t *w = g_gui.terminal.window;
    int ox, oy;
    if (!w || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;
    ox = w->rect.x + 6;
    oy = w->rect.y + GUI_TITLE_HEIGHT + 5;
    r_end = g_gui.terminal.rows;
    c_end = g_gui.terminal.cols;
    {
        gui_rect_t client;
        client.x = w->rect.x + GUI_BORDER_SIZE;
        client.y = w->rect.y + GUI_TITLE_HEIGHT;
        client.w = w->rect.w - GUI_BORDER_SIZE * 2;
        client.h = w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE;
        gui_set_clip_rect(&client);
    }
    if (g_gui.clip_enabled && g_gui.clip_rect.w <= 0) {
        gui_clear_clip_rect();
        return;
    }
    if (g_gui.clip_enabled) {
        int left = g_gui.clip_rect.x - ox;
        int top = g_gui.clip_rect.y - oy;
        int right = g_gui.clip_rect.x + g_gui.clip_rect.w - ox + GUI_CHAR_W - 1;
        int bottom = g_gui.clip_rect.y + g_gui.clip_rect.h - oy + GUI_CHAR_H;
        if (left > 0) c_start = (uint32_t)(left / GUI_CHAR_W);
        if (top > 0) r_start = (uint32_t)(top / (GUI_CHAR_H + 1));
        if (right > 0) c_end = (uint32_t)(right / GUI_CHAR_W);
        if (bottom > 0) r_end = (uint32_t)(bottom / (GUI_CHAR_H + 1));
        if (c_end > g_gui.terminal.cols) c_end = g_gui.terminal.cols;
        if (r_end > g_gui.terminal.rows) r_end = g_gui.terminal.rows;
        if (c_start > c_end) c_start = c_end;
        if (r_start > r_end) r_start = r_end;
    }
    for (r = r_start; r < r_end; r++) {
        for (c = c_start; c < c_end; c++) {
            char ch = g_gui.terminal.cells[r][c];
            int px = ox + (int)c * GUI_CHAR_W;
            int py = oy + (int)r * (GUI_CHAR_H + 1);
            int selected = gui_terminal_cell_selected(c, r);
            if (selected) {
                gui_raw_fill_rect(px, py, GUI_CHAR_W, GUI_CHAR_H + 1, gui_rgb(70, 105, 180));
            }
            if (ch != ' ') {
                gui_draw_char(px, py, ch, selected ? gui_rgb(255, 255, 255) : gui_rgb(185, 255, 185));
            }
        }
    }
    if (g_gui.terminal.input_focused && g_gui.terminal.cursor_visible) {
        gui_raw_fill_rect(ox + (int)g_gui.terminal.cursor_x * GUI_CHAR_W,
                          oy + (int)g_gui.terminal.cursor_y * (GUI_CHAR_H + 1) + GUI_CHAR_H,
                          GUI_CHAR_W - 1, 1, gui_rgb(185, 255, 185));
    }
    gui_clear_clip_rect();
}

static void gui_terminal_invalidate_cursor(void) {
    gui_terminal_invalidate_cursor_at(g_gui.terminal.cursor_x, g_gui.terminal.cursor_y);
}

static void gui_draw_taskbar_start_icon(gui_rect_t rect) {
    int hover = gui_taskbar_icon_hovered(rect);
    int x = rect.x + (rect.w - 17) / 2;
    int y = rect.y + (rect.h - 17) / 2;
    uint32_t blue = hover ? gui_rgb(118, 184, 255) : gui_rgb(86, 160, 255);
    uint32_t green = hover ? gui_rgb(154, 255, 188) : gui_rgb(120, 255, 160);
    uint32_t yellow = hover ? gui_rgb(255, 214, 112) : gui_rgb(255, 196, 86);
    uint32_t red = hover ? gui_rgb(255, 138, 154) : gui_rgb(255, 110, 130);

    if (hover) {
        y -= gui_taskbar_icon_hover_lift(rect);
        gui_taskbar_draw_icon_shadow(x + 1, y, 7, 7);
        gui_taskbar_draw_icon_shadow(x + 11, y, 7, 7);
        gui_taskbar_draw_icon_shadow(x + 1, y + 10, 7, 7);
        gui_taskbar_draw_icon_shadow(x + 11, y + 10, 7, 7);
    }

    gui_raw_fill_rect(x, y, 7, 7, blue);
    gui_raw_fill_rect(x + 10, y, 7, 7, green);
    gui_raw_fill_rect(x, y + 10, 7, 7, yellow);
    gui_raw_fill_rect(x + 10, y + 10, 7, 7, red);
}

static void gui_draw_taskbar_terminal_icon(gui_rect_t rect) {
    int hover = gui_taskbar_icon_hovered(rect);
    int x = rect.x + (rect.w - 26) / 2;
    int y = rect.y + (rect.h - 22) / 2;
    uint32_t bg = hover ? gui_rgb(14, 24, 40) : gui_rgb(8, 14, 24);
    uint32_t border = hover ? gui_rgb(230, 242, 255) : gui_rgb(205, 225, 255);
    uint32_t shadow = hover ? gui_rgb(24, 32, 48) : gui_rgb(60, 80, 115);
    uint32_t prompt = hover ? gui_rgb(154, 255, 188) : gui_rgb(120, 255, 160);
    uint32_t text = hover ? gui_rgb(220, 244, 255) : gui_rgb(190, 230, 255);

    if (hover) {
        y -= gui_taskbar_icon_hover_lift(rect);
        gui_taskbar_draw_icon_shadow(x, y, 26, 22);
    }

    gui_raw_fill_rect(x, y, 26, 22, bg);
    gui_raw_line(x, y, x + 25, y, border);
    gui_raw_line(x, y, x, y + 21, border);
    gui_raw_line(x + 25, y, x + 25, y + 21, shadow);
    gui_raw_line(x, y + 21, x + 25, y + 21, shadow);
    gui_raw_line(x + 5, y + 7, x + 9, y + 11, prompt);
    gui_raw_line(x + 9, y + 11, x + 5, y + 15, prompt);
    gui_raw_line(x + 13, y + 15, x + 21, y + 15, text);
}

static void gui_draw_taskbar_window_icon(gui_rect_t rect, int minimized) {
    int hover = gui_taskbar_icon_hovered(rect);
    int x = rect.x + (rect.w - 26) / 2;
    int y = rect.y + (rect.h - 22) / 2;
    uint32_t title = minimized ? gui_rgb(95, 125, 175) : gui_rgb(86, 130, 210);
    uint32_t body = minimized ? gui_rgb(28, 36, 55) : gui_rgb(32, 44, 70);
    uint32_t border = gui_rgb(205, 225, 255);
    uint32_t shadow = gui_rgb(60, 76, 110);
    if (hover) {
        y -= gui_taskbar_icon_hover_lift(rect);
        title = minimized ? gui_rgb(118, 150, 205) : gui_rgb(112, 158, 232);
        body = minimized ? gui_rgb(38, 50, 74) : gui_rgb(44, 60, 92);
        border = gui_rgb(230, 242, 255);
        shadow = gui_rgb(24, 32, 48);
        gui_taskbar_draw_icon_shadow(x, y, 26, 22);
    }

    gui_raw_fill_rect(x, y, 26, 22, body);
    gui_raw_fill_rect(x + 1, y + 1, 24, 5, title);
    gui_raw_line(x, y, x + 25, y, border);
    gui_raw_line(x, y, x, y + 21, border);
    gui_raw_line(x + 25, y, x + 25, y + 21, shadow);
    gui_raw_line(x, y + 21, x + 25, y + 21, shadow);
    if (minimized) {
        gui_raw_line(x + 8, y + 16, x + 17, y + 16, gui_rgb(170, 195, 235));
    } else {
        gui_raw_fill_rect(x + 5, y + 10, 16, 6, gui_rgb(70, 92, 130));
    }
}

static void gui_draw_taskbar(void) {
    uint32_t i;
    gui_taskbar_layout_t layout;
    int bx;
    gui_taskbar_get_layout(&layout);
    bx = layout.first_window_x;

    gui_raw_fill_rect(layout.bar.x, layout.bar.y, layout.bar.w, layout.bar.h, gui_rgb(24, 28, 38));
    gui_raw_line(layout.bar.x, layout.bar.y, layout.bar.x + layout.bar.w - 1, layout.bar.y, gui_rgb(76, 86, 112));
    gui_raw_line(layout.bar.x, layout.bar.y, layout.bar.x, layout.bar.y + layout.bar.h - 1, gui_rgb(58, 66, 88));
    gui_raw_line(layout.bar.x + layout.bar.w - 1, layout.bar.y, layout.bar.x + layout.bar.w - 1, layout.bar.y + layout.bar.h - 1, gui_rgb(10, 13, 20));
    gui_raw_line(layout.bar.x, layout.bar.y + layout.bar.h - 1, layout.bar.x + layout.bar.w - 1, layout.bar.y + layout.bar.h - 1, gui_rgb(10, 13, 20));

    g_gui.desktop_start_button_rect = layout.start_button;
    gui_draw_taskbar_start_icon(layout.start_button);

    gui_draw_taskbar_terminal_icon(layout.terminal_button);

    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        gui_rect_t button;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!w->used || !w->visible) continue;
        if (w->flags & GUI_WINDOW_FLAG_TERMINAL) continue;
        button.x = bx;
        button.y = layout.item_y;
        button.w = gui_taskbar_button_width(w);
        button.h = layout.item_h;
        if (button.x + button.w > layout.bar.x + layout.bar.w - 8) button.w = layout.bar.x + layout.bar.w - 8 - button.x;
        if (button.w <= 0) break;
        gui_draw_taskbar_window_icon(button, (w->flags & GUI_WINDOW_FLAG_MINIMIZED) != 0);
        bx += gui_taskbar_button_width(w) + 6;
    }
}

static void gui_draw_wallpaper(void) {
    int y;
    int x;
    int width = (int)g_gui.width;
    int height = (int)g_gui.height;
    int taskbar_top = height > GUI_TASKBAR_HEIGHT ? height - GUI_TASKBAR_HEIGHT : height;
    int horizon = taskbar_top * 56 / 100;
    uint32_t sky_top = gui_rgb(18, 34, 70);
    uint32_t sky_mid = gui_rgb(44, 84, 145);
    uint32_t glow = gui_rgb(238, 158, 114);
    uint32_t ground = gui_rgb(22, 45, 58);

    if (width <= 0 || height <= 0) return;

    for (y = 0; y < taskbar_top; y++) {
        uint32_t amount = taskbar_top > 1 ? (uint32_t)(y * 255 / taskbar_top) : 0u;
        uint32_t color = gui_mix_rgb(sky_top, sky_mid, amount);
        uint32_t warm = 0u;
        int dy = y - horizon;
        if (dy < 0) dy = -dy;
        if (dy < 140) warm = (uint32_t)((140 - dy) * 90 / 140);
        if (warm) color = gui_mix_rgb(color, glow, warm);
        gui_raw_fill_rect(0, y, width, 1, color);
    }

    for (y = 0; y < taskbar_top; y += 41) {
        int sx = (y * 37 + 53) % (width > 0 ? width : 1);
        if (y < horizon - 35) {
            gui_raw_put_pixel(sx, y + 9, gui_rgb(180, 210, 255));
            if (sx + 1 < width) gui_raw_put_pixel(sx + 1, y + 9, gui_rgb(118, 160, 220));
        }
    }


    for (y = horizon - 112; y < taskbar_top; y++) {
        int left_peak = horizon - 72 - y;
        int right_peak = horizon - 42 - y;
        int d1 = left_peak > 0 ? left_peak : -left_peak;
        int d2 = right_peak > 0 ? right_peak : -right_peak;
        int top1 = horizon - 18 + d1 * 2 / 3;
        int top2 = horizon - 8 + d2 * 3 / 5;
        int top = top1 < top2 ? top1 : top2;
        if (y >= top) {
            uint32_t c = y < horizon + 18 ? gui_rgb(45, 74, 98) : gui_rgb(32, 57, 76);
            gui_raw_fill_rect(0, y, width, 1, c);
        }
    }

    for (y = horizon + 36; y < taskbar_top; y++) {
        uint32_t amount = taskbar_top > horizon ? (uint32_t)((y - horizon) * 255 / (taskbar_top - horizon)) : 255u;
        uint32_t color = gui_mix_rgb(gui_rgb(38, 77, 92), ground, amount);
        gui_raw_fill_rect(0, y, width, 1, color);
    }

    for (x = -80; x < width + 120; x += 92) {
        int base = taskbar_top - 28 - ((x * 7) & 23);
        gui_raw_line(x, base, x + 42, horizon + 62, gui_rgb(74, 112, 132));
        gui_raw_line(x + 42, horizon + 62, x + 96, base + 10, gui_rgb(42, 76, 98));
    }

    gui_raw_fill_rect(0, taskbar_top - 50, width, 50, gui_rgb(18, 39, 52));
    for (x = 0; x < width; x += 36) {
        int wave_y = taskbar_top - 42 + ((x / 36) & 1) * 5;
        gui_raw_line(x, wave_y, x + 22, wave_y + 3, gui_rgb(67, 116, 136));
    }
}

static int gui_has_dirty(void) {
    return g_gui.full_dirty || g_gui.dirty_count > 0;
}

static void gui_terminal_tick_cursor(void) {
    if (!g_gui.initialized || !g_gui.terminal.enabled || !g_gui.terminal.input_focused) return;
    if (!g_gui.terminal.window || !g_gui.terminal.window->visible ||
        (g_gui.terminal.window->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;

    if (!g_gui.terminal.cursor_visible) {
        g_gui.terminal.cursor_visible = 1;
        g_gui.terminal.cursor_blink_ticks = 0;
        gui_terminal_invalidate_cursor();
    }
}

static void gui_render_scene(void) {
    uint32_t i;
    gui_draw_wallpaper();
    gui_desktop_draw();
    gui_draw_taskbar();
    gui_desktop_draw_start_menu();

    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        if (idx < GUI_MAX_WINDOWS) gui_draw_window(&g_gui.windows[idx]);
    }
    gui_terminal_redraw();
}

void gui_render(void) {
    uint32_t i;
    uint32_t dirty_count;
    gui_rect_t dirty_rects[GUI_MAX_DIRTY_RECTS];
    if (!g_gui.initialized) return;
    if (gui_compositor_active() && !gui_has_dirty()) return;

    gui_cursor_restore_fb();

    if (g_gui.full_dirty || !gui_compositor_active()) {
        gui_pop_render_clip();
        gui_render_scene();
        if (gui_compositor_active()) {
            gui_flush_backbuffer();
        } else {
            g_gui.full_dirty = 0;
            g_gui.dirty_count = 0;
        }
        gui_cursor_draw_fb();
        return;
    }

    dirty_count = g_gui.dirty_count;
    if (dirty_count > GUI_MAX_DIRTY_RECTS) dirty_count = GUI_MAX_DIRTY_RECTS;
    for (i = 0; i < dirty_count; i++) dirty_rects[i] = g_gui.dirty_rects[i];

    if (dirty_count > 0) {
        gui_rect_t merged = dirty_rects[0];
        for (i = 1; i < dirty_count; i++) gui_rect_union_inplace(&merged, &dirty_rects[i]);
        gui_push_render_clip(&merged);
        gui_render_scene();
        gui_pop_render_clip();
        g_gui.dirty_count = 1;
        g_gui.dirty_rects[0] = merged;
    }

    gui_flush_backbuffer();
    gui_cursor_draw_fb();
}

void gui_poll(void) {
    if (!g_gui.initialized) return;
    gui_poll_mouse();
    gui_process_events();
    gui_terminal_drain_output_queue();
    gui_terminal_tick_cursor();
    if (gui_has_dirty()) gui_render();
}

static void gui_demo_button(gui_widget_t *widget, void *user_data) {
    (void)widget;
    (void)user_data;
#if GUI_DEBUG_LOG
    gui_terminal_write("\n[GUI] button clicked\n> ");
#endif
}

static int gui_demo_app_entry(gui_app_t *app, void *user_data) {
    gui_window_t *w1;
    gui_window_t *w2;
    (void)user_data;
    w1 = gui_create_app_window(app, 70, 70, 380, 230, "OpenOS Control Center");
    if (w1) {
        gui_add_label(w1, 18, 22, 300, 18, "Welcome to OpenOS GUI");
        gui_add_panel(w1, 18, 52, 335, 48, gui_rgb(210, 225, 245));
        gui_add_label(w1, 28, 66, 300, 18, "Window drag, focus and buttons enabled.");
        gui_add_textbox(w1, 18, 108, 260, 26, "edit me");
        gui_add_button(w1, 18, 150, 120, 28, "Click", gui_demo_button, 0);
        gui_add_button(w1, 150, 150, 120, 28, "Minimize", gui_demo_button, 0);
    }
    w2 = gui_create_app_window(app, 500, 120, 330, 170, "About OpenOS");
    if (w2) {
        gui_add_label(w2, 18, 24, 260, 18, "openos GUI framework MVP");
        gui_add_label(w2, 18, 48, 260, 18, "Framebuffer + windows + events");
        gui_add_button(w2, 18, 90, 100, 28, "OK", gui_demo_button, 0);
    }
#if GUI_DEBUG_LOG
    gui_terminal_write("\n[GUI] demo app started\n> ");
#endif
    return (w1 || w2) ? 0 : -1;
}

/* === File Preview (enhanced) === */

#define GUI_FP_MAX_PATH        256
#define GUI_FP_MAX_NAME        64
#define GUI_FP_LIST_PER_PAGE   12
#define GUI_FP_VIEW_MAX_LINES  13
#define GUI_FP_VIEW_LINE_CHARS 56
#define GUI_FP_VIEW_BUF_SIZE   2048

static gui_window_t *fp_window = 0;
static char          fp_path[GUI_FP_MAX_PATH];
static int           fp_page = 0;
static int           fp_mode = 0;            /* 0=list, 1=view */
static char          fp_view_name[GUI_FP_MAX_NAME];

/* path helpers --------------------------------------------------- */
static int fp_is_root(void) {
    return fp_path[0] == '/' && fp_path[1] == 0;
}

static void fp_path_set_root(void) {
    fp_path[0] = '/';
    fp_path[1] = 0;
}

static void fp_path_push(const char *name) {
    int len = 0;
    while (fp_path[len]) len++;
    if (!fp_is_root()) {
        if (len < GUI_FP_MAX_PATH - 1) fp_path[len++] = '/';
    }
    while (*name && len < GUI_FP_MAX_PATH - 1) {
        fp_path[len++] = *name++;
    }
    fp_path[len] = 0;
}

static void fp_path_pop(void) {
    int len = 0, last = -1, i;
    while (fp_path[len]) len++;
    if (len <= 1) return;
    for (i = 0; i < len; i++) {
        if (fp_path[i] == '/') last = i;
    }
    if (last <= 0) {
        fp_path_set_root();
    } else {
        fp_path[last] = 0;
    }
}

static void fp_path_join(const char *dir, const char *name, char *out, int cap) {
    int i = 0, j;
    for (j = 0; dir[j] && i < cap - 1; j++) out[i++] = dir[j];
    if (i > 0 && out[i - 1] != '/' && i < cap - 1) out[i++] = '/';
    for (j = 0; name[j] && i < cap - 1; j++) out[i++] = name[j];
    out[i] = 0;
}

static void fp_itoa(int n, char *buf) {
    char tmp[12];
    int i = 0, j = 0;
    if (n < 0) { buf[j++] = '-'; n = -n; }
    if (n == 0) tmp[i++] = '0';
    while (n > 0) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static int fp_str_append(char *dst, int pos, int cap, const char *src) {
    while (*src && pos < cap - 1) dst[pos++] = *src++;
    dst[pos] = 0;
    return pos;
}

static int fp_entry_is_dot(const dentry_t *e) {
    return e->name[0] == '.' &&
           (e->name[1] == 0 ||
            (e->name[1] == '.' && e->name[2] == 0));
}

static int fp_entry_is_dir(const dentry_t *e) {
    return e && e->inode && (e->inode->mode & FS_DIR);
}

/* count real entries (skipping . and ..) ------------------------- */
static int fp_count_entries(void) {
    int i, count = 0;
    dentry_t *e;
    for (i = 0; ; i++) {
        e = vfs_readdir(fp_path, i);
        if (!e) break;
        if (fp_entry_is_dot(e)) continue;
        count++;
    }
    return count;
}

static int fp_total_items(void) {
    int n = fp_count_entries();
    if (!fp_is_root()) n++; /* leading ".." */
    return n;
}

static int fp_total_pages(void) {
    int n = fp_total_items();
    if (n <= 0) return 1;
    return (n + GUI_FP_LIST_PER_PAGE - 1) / GUI_FP_LIST_PER_PAGE;
}

/* fetch the Nth real entry (N=0 = first non-dot child) ---------- */
static dentry_t *fp_get_real_entry(int target) {
    int i, idx = 0;
    dentry_t *e;
    for (i = 0; ; i++) {
        e = vfs_readdir(fp_path, i);
        if (!e) return 0;
        if (fp_entry_is_dot(e)) continue;
        if (idx == target) return e;
        idx++;
    }
}

/* callbacks ------------------------------------------------------ */
static void fp_on_back(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    fp_mode = 0;
    fp_page = 0;
    gui_file_preview_rebuild();
}

static void fp_on_prev(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (fp_page > 0) {
        fp_page--;
        gui_file_preview_rebuild();
    }
}

static void fp_on_next(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (fp_page + 1 < fp_total_pages()) {
        fp_page++;
        gui_file_preview_rebuild();
    }
}

static void fp_on_entry(gui_widget_t *w, void *ud) {
    int slot = (int)(intptr_t)ud;
    int global_index, real_index;
    dentry_t *e;
    (void)w;

    global_index = fp_page * GUI_FP_LIST_PER_PAGE + slot;

    /* slot 0 of page 0 in non-root is the ".." shortcut */
    if (!fp_is_root() && global_index == 0) {
        fp_path_pop();
        fp_page = 0;
        gui_file_preview_rebuild();
        return;
    }

    real_index = fp_is_root() ? global_index : (global_index - 1);
    e = fp_get_real_entry(real_index);
    if (!e) return;

    if (fp_entry_is_dir(e)) {
        fp_path_push(e->name);
        fp_page = 0;
        gui_file_preview_rebuild();
    } else {
        int i = 0;
        while (e->name[i] && i < GUI_FP_MAX_NAME - 1) {
            fp_view_name[i] = e->name[i];
            i++;
        }
        fp_view_name[i] = 0;
        fp_mode = 1;
        gui_file_preview_rebuild();
    }
}

/* render list mode ---------------------------------------------- */
static void gui_file_preview_render_list(void) {
    char header[GUI_FP_MAX_PATH + 16];
    char pageinfo[32];
    char buf[12];
    gui_widget_t *btn;
    int y, slot, total_pages, total_items, base;
    int pos;

    if (!fp_window) return;

    /* path header */
    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), "Path: ");
    pos = fp_str_append(header, pos, sizeof(header), fp_path);
    (void)pos;
    gui_add_label(fp_window, 8, 28, 384, 16, header);

    /* nav buttons */
    btn = gui_add_button(fp_window, 8, 48, 60, 20, "< Prev", fp_on_prev, 0);
    (void)btn;
    btn = gui_add_button(fp_window, 76, 48, 60, 20, "Next >", fp_on_next, 0);
    (void)btn;

    total_pages = fp_total_pages();
    total_items = fp_total_items();
    pos = 0;
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), "Page ");
    fp_itoa(fp_page + 1, buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), "/");
    fp_itoa(total_pages, buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), " (");
    fp_itoa(total_items, buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), " items)");
    (void)pos;
    gui_add_label(fp_window, 144, 50, 240, 16, pageinfo);

    /* entries */
    y = 76;
    base = fp_page * GUI_FP_LIST_PER_PAGE;
    for (slot = 0; slot < GUI_FP_LIST_PER_PAGE; slot++) {
        char line[80];
        const char *prefix;
        int gidx = base + slot;
        int real_index;
        dentry_t *e;

        if (gidx >= total_items) break;

        if (!fp_is_root() && gidx == 0) {
            prefix = "[..]   ";
            pos = 0;
            pos = fp_str_append(line, pos, sizeof(line), prefix);
            pos = fp_str_append(line, pos, sizeof(line), "parent directory");
            (void)pos;
            gui_add_button(fp_window, 8, y, 384, 18, line,
                           fp_on_entry, (void *)(intptr_t)slot);
            y += 20;
            continue;
        }

        real_index = fp_is_root() ? gidx : (gidx - 1);
        e = fp_get_real_entry(real_index);
        if (!e) break;

        prefix = fp_entry_is_dir(e) ? "[DIR]  " : "[FILE] ";
        pos = 0;
        pos = fp_str_append(line, pos, sizeof(line), prefix);
        pos = fp_str_append(line, pos, sizeof(line), e->name);
        (void)pos;
        gui_add_button(fp_window, 8, y, 384, 18, line,
                       fp_on_entry, (void *)(intptr_t)slot);
        y += 20;
    }
}

/* render view mode ---------------------------------------------- */
static void gui_file_preview_render_view(void) {
    char header[GUI_FP_MAX_PATH + 16];
    char full[GUI_FP_MAX_PATH];
    char buf[GUI_FP_VIEW_BUF_SIZE + 1];
    char line[GUI_FP_VIEW_LINE_CHARS + 1];
    int fd, total, i, pos, lines, n;
    int line_pos;
    int y;

    if (!fp_window) return;

    /* header */
    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), "File: ");
    pos = fp_str_append(header, pos, sizeof(header), fp_view_name);
    (void)pos;
    gui_add_label(fp_window, 8, 28, 384, 16, header);

    gui_add_button(fp_window, 8, 48, 60, 20, "< Back", fp_on_back, 0);

    /* load file content */
    fp_path_join(fp_path, fp_view_name, full, sizeof(full));
    fd = vfs_open(full, O_RDONLY, 0);
    if (fd < 0) {
        gui_add_label(fp_window, 8, 76, 384, 16,
                      "(cannot open file)");
        return;
    }

    total = 0;
    while (total < GUI_FP_VIEW_BUF_SIZE) {
        n = vfs_read(fd, buf + total, GUI_FP_VIEW_BUF_SIZE - total);
        if (n <= 0) break;
        total += n;
    }
    vfs_close(fd);
    buf[total] = 0;

    /* split into lines, render up to GUI_FP_VIEW_MAX_LINES */
    y = 76;
    lines = 0;
    i = 0;
    line_pos = 0;
    while (i <= total && lines < GUI_FP_VIEW_MAX_LINES) {
        char c = (i < total) ? buf[i] : '\n';
        int flush = 0;

        if (c == '\n') {
            flush = 1;
        } else if (line_pos >= GUI_FP_VIEW_LINE_CHARS) {
            flush = 1;
        }

        if (flush) {
            line[line_pos] = 0;
            gui_add_label(fp_window, 8, y, 384, 14, line);
            y += 16;
            lines++;
            line_pos = 0;
            if (c == '\n') {
                i++;
                continue;
            }
            /* hard wrap: keep current char */
        }

        if (i < total) {
            unsigned char uc = (unsigned char)c;
            if (uc < 32 || uc > 126) c = '.';
            line[line_pos++] = c;
        }
        i++;
    }

    if (i < total && lines >= GUI_FP_VIEW_MAX_LINES) {
        /* nothing to do - file truncated visually */
    }
}

/* rebuild and open ---------------------------------------------- */
static void gui_file_preview_rebuild(void) {
    const char *title = (fp_mode == 0) ? "Files" : "File Viewer";
    if (fp_window) {
        gui_destroy_window(fp_window);
        fp_window = 0;
    }
    fp_window = gui_create_window(60, 60, 400, 360, title);
    if (!fp_window) return;
    if (fp_mode == 0) {
        gui_file_preview_render_list();
    } else {
        gui_file_preview_render_view();
    }
    gui_render();
}

static void gui_file_preview_open(void) {
    if (fp_path[0] == 0) fp_path_set_root();
    fp_mode = 0;
    fp_page = 0;
    gui_file_preview_rebuild();
}

void gui_demo(void) {
    gui_app_t *app;
    if (!g_gui.initialized) return;
    app = gui_register_app("demo", "OpenOS Demo", gui_demo_app_entry, 0);
    if (app) gui_start_app(app);
    gui_render();
}




