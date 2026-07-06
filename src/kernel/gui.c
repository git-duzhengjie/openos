/* ============================================================
 * openos - Minimal GUI / Window System
 *
 * 闁衡偓椤栨稑鐦柨娑欘儞UI 缂備礁鐗忛顒勫Υ娑撶爤/2 濮捬呭У閻栵綁宕楁径瀣灱闁靛棔妞掔花銊︾閸洘袝闁告帗銇滈埀顑跨劍鐎垫粓鏌﹂鎯т化闁告垳绱幏?
 *       缂佹劖顨呰ぐ娑㈠箯閺嵮冿拷?缂傚喚鍣ｉ妴?闁稿繑濞婂Λ?闁哄牃鍋撻悘蹇撶箰鐎垫煡濡存担绋胯摕缂傚倹鎸搁崯鍨€掗崣澶屽帬锟?
 * ============================================================ */

#include "gui.h"
#include "gui_user.h"
#include "framebuffer.h"
#include "mouse.h"
#include "usb_tablet.h"
#include "font.h"
#include "serial.h"
#include "string.h"
#include "heap.h"
#include "input_buffer.h"
#include "core/fs/vfs.h"
#include "i18n.h"
#include "net/net.h"
#include "net/dhcp.h"
#include "net/dns.h"
#include "net/net_config.h"
#include "tls_parser.h"
#include "process.h"
#include "gui_internal.h"

/* 阶段二：RAMFS 磁盘快照持久化接口（实现于 x86_64 gui64/ramfs64.c） */
extern int ramfs_snapshot_save(void);
extern int ramfs_snapshot_load(void);
extern int spawn_user_process(const char *path, char *const argv[]);
extern uint32_t sched_time_ms(void);


static void gui_desktop_run_action(uint32_t action);
static int  gui_taskbar_search_handle_key(int key);
int  gui_is_enter_key(int key);
static int  gui_select_dropdown_index_at(gui_widget_t *wg, int sx, int sy);
static void gui_select_activate(gui_widget_t *wg);
static void gui_select_commit(gui_widget_t *wg, int index);
static void gui_select_handle_key(gui_widget_t *wg, int key);
static void gui_listview_handle_key(gui_widget_t *wg, int key);
static int gui_listview_scroll(gui_widget_t *wg, int delta_rows);
static void gui_tableview_handle_key(gui_widget_t *wg, int key);
static int gui_tableview_scroll(gui_widget_t *wg, int delta_rows);
static void gui_treeview_handle_key(gui_widget_t *wg, int key);
static int gui_treeview_scroll(gui_widget_t *wg, int delta_rows);
static void gui_iconview_handle_key(gui_widget_t *wg, int key);
static int gui_iconview_scroll(gui_widget_t *wg, int delta_rows);
static void gui_draw_select_dropdown(gui_widget_t *wg);
static void gui_textbox_ensure_cursor_visible(gui_widget_t *wg);
static void gui_textarea_ensure_cursor_visible(gui_widget_t *wg);
static int  gui_scrollview_clamp_x(gui_widget_t *widget, int scroll_x);
static int  gui_scrollview_clamp_y(gui_widget_t *widget, int scroll_y);
static void gui_taskbar_search_open_result(uint32_t index);
static int  gui_taskbar_search_result_index_at(int x, int y);
static void gui_taskbar_search_reset_results(void);
static void gui_taskbar_search_refresh_results(void);
static void gui_handle_mouse_right_down(int x, int y);
static void gui_ctxmenu_close(void);
static int  gui_ctxmenu_handle_click(int x, int y);
static void gui_ctxmenu_draw(void);
static int  gui_ctxmenu_is_open(void);
static void gui_ctxmenu_invalidate_hover_changes(int old_x, int old_y, int new_x, int new_y);
void gui_file_preview_open(void);
static void gui_file_preview_render_list(void);
static void gui_file_preview_render_view(void);
static void gui_file_preview_render_edit(void);
static void gui_file_preview_rebuild(void);
void gui_about_open(void);
void gui_recycle_open(void);
void gui_settings_open(void);
void gui_network_open(void);
void gui_wifi_open(void);
int  gui_tray_network_is_wireless(void);
static void gui_notif_open(void);
static void gui_launcher_scan_bin(uint32_t start_index);
void gui_notify(const char *text);
int  fp_str_append(char *dst, int pos, int cap, const char *src);
void fp_itoa(int n, char *buf);
int gui_append_uint(char *dst, int pos, int cap, uint32_t v);
int gui_append_hex_byte(char *dst, int pos, int cap, uint8_t v);
void gui_format_ipv4_inline(uint32_t ip, char *buf, int cap);
void gui_format_mac_inline(const uint8_t mac[6], char *buf, int cap);
int gui_settings_append_field(char *dst, int pos, int cap, i18n_key_t key, const char *value);
void gui_terminal_redraw(void);
void gui_terminal_clear(void);
void gui_terminal_clear_selection(void);
int gui_terminal_copy_selection(void);

gui_system_t g_gui;
static gui_accel_info_t g_gui_accel;
static gui_rect_t g_network_widget_rect;
static gui_rect_t g_taskbar_search_rect;
static gui_window_t *g_wifi_win;

#define GUI_TASKBAR_SEARCH_MAX   63u
#define GUI_TASKBAR_SEARCH_W     180
#define GUI_TASKBAR_SEARCH_MIN_W 96

#ifndef GUI_DEBUG_LOG
#define GUI_DEBUG_LOG 0
#endif

#ifndef GUI_TERMINAL_START_SHELL
#define GUI_TERMINAL_START_SHELL 1
#endif

#define GUI_DESKTOP_ACTION_TERMINAL 1u
#define GUI_DESKTOP_ACTION_ABOUT    2u
#define GUI_DESKTOP_ACTION_MENU     3u
#define GUI_DESKTOP_ACTION_DEMO     4u
#define GUI_DESKTOP_ACTION_FILES    5u
#define GUI_DESKTOP_ACTION_RECYCLE  6u
#define GUI_DESKTOP_ACTION_BROWSER  7u
#define GUI_DESKTOP_ACTION_STICKY   11u
#define GUI_DESKTOP_ACTION_THEME    8u
#define GUI_DESKTOP_ACTION_NOTIF    9u
#define GUI_DESKTOP_ACTION_SETTINGS 10u
#define GUI_DESKTOP_ACTION_LAUNCH_BIN_BASE 0x1000u  /* +index into binlist */

static int gui_compositor_active(void) {
    return g_gui.initialized && g_gui.compositor_enabled &&
           g_gui.double_buffered && g_gui.backbuffer;
}

int gui_accel_is_enabled(void) {
    return g_gui_accel.enabled;
}

void gui_get_accel_info(gui_accel_info_t *info) {
    const framebuffer_caps_t *caps;
    if (!info) return;
    *info = g_gui_accel;
    caps = framebuffer_get_caps();
    if (caps) {
        info->backend = caps->backend;
        info->backend_caps = caps->flags;
    }
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

int gui_rect_contains(const gui_rect_t *r, int x, int y);
static int gui_rect_intersect(const gui_rect_t *a, const gui_rect_t *b, gui_rect_t *out);
static gui_window_t *gui_top_window(void);
static void gui_set_hovered_widget(gui_widget_t *wg);
static gui_app_t *gui_app_for_window(gui_window_t *window);
static void gui_refresh_active_app(void);
static void gui_demo_button(gui_widget_t *widget, void *user_data);
static void gui_desktop_init(void);
static void gui_desktop_draw(void);
static int gui_desktop_handle_click(int x, int y);
static void gui_launcher_init(void);
void gui_raw_fill_rect(int x, int y, int w, int h, uint32_t color);

uint32_t gui_rgb(uint8_t r, uint8_t g, uint8_t b) {
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

static void gui_raw_put_pixel_alpha(int x, int y, uint32_t color, uint8_t alpha) {
    uint32_t *dst;
    if (alpha == 0u) return;
    if (g_gui.clip_enabled && !gui_rect_contains(&g_gui.clip_rect, x, y)) return;
    if (x < 0 || y < 0 || x >= (int)g_gui.width || y >= (int)g_gui.height) return;
    if (alpha == 255u) {
        gui_put_pixel_unclipped(x, y, color);
        return;
    }
    if (gui_compositor_active()) {
        dst = &g_gui.backbuffer[(uint32_t)y * g_gui.width + (uint32_t)x];
        *dst = framebuffer_blend_color(*dst, color, alpha);
    } else {
        framebuffer_put_pixel_alpha((uint32_t)x, (uint32_t)y, color, alpha);
    }
    g_gui_accel.alpha_pixels++;
}

static void gui_font_put_pixel_alpha(void *ctx, int x, int y, uint32_t color, uint8_t alpha) {
    (void)ctx;
    gui_raw_put_pixel_alpha(x, y, color, alpha);
}

void gui_raw_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    int yy;
    int xx;
    if (w <= 0 || h <= 0 || alpha == 0u) return;
    if (alpha == 255u) {
        gui_raw_fill_rect(x, y, w, h, color);
        return;
    }
    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {
            gui_raw_put_pixel_alpha(x + xx, y + yy, color, alpha);
        }
    }
}

void gui_raw_fill_rect(int x, int y, int w, int h, uint32_t color) {
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

void gui_raw_line(int x0, int y0, int x1, int y1, uint32_t color) {
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

void gui_set_clip_rect(const gui_rect_t *rect) {
    gui_apply_clip_rect(rect);
}

void gui_clear_clip_rect(void) {
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

typedef struct gui_text_soften_ctx {
    uint32_t color;
    uint8_t alpha;
} gui_text_soften_ctx_t;

static void gui_font_put_pixel_soft(void *ctx, int x, int y, uint32_t color) {
    gui_text_soften_ctx_t *soft = (gui_text_soften_ctx_t *)ctx;
    if (soft) {
        gui_raw_put_pixel_alpha(x, y, soft->color, soft->alpha);
    } else {
        gui_raw_put_pixel_alpha(x, y, color, 72u);
    }
}

void gui_draw_char(int x, int y, char ch, uint32_t color) {
    gui_text_soften_ctx_t soft;
    soft.color = color;
    soft.alpha = 64u;
    font_draw_char(font_get_default(), gui_font_put_pixel_soft, &soft, x + 1, y, ch, color);
    font_draw_char(font_get_default(), gui_font_put_pixel, 0, x, y, ch, color);
}

void gui_draw_text(int x, int y, const char *text, uint32_t color) {
    gui_text_soften_ctx_t soft;
    soft.color = color;
    soft.alpha = 58u;
    font_draw_text(font_get_default(), gui_font_put_pixel_soft, &soft, x + 1, y, text, color);
    font_draw_text(font_get_default(), gui_font_put_pixel, 0, x, y, text, color);
}

static void gui_title_rect_px(int x, int y, int w, int h, uint32_t color, const gui_rect_t *clip);
static void gui_menu_draw_item_fill(int x, int y, int w, int h, int selected, uint32_t bg, uint32_t selected_bg);
void gui_update_start_menu_layout(void);
static void gui_start_menu_scroll_by(int delta_items);
static void gui_start_menu_clamp_scroll(void);
static int gui_start_menu_scrollbar_rects(gui_rect_t *track, gui_rect_t *thumb);
static int gui_start_menu_scrollbar_begin_drag(int x, int y);
static void gui_start_menu_scrollbar_drag_to(int y);

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

static void gui_draw_window_title_text(int x, int y, const char *text, uint32_t color, const gui_rect_t *clip);
int gui_text_line_height_px(void);
int gui_text_center_y(int top, int height);
static void gui_draw_file_icon(gui_icon_id_t id, int x, int y);
static void gui_draw_icon_button_face(const gui_rect_t *rect, int selected,
                                      int highlighted);
void gui_draw_icon_button_frame(const gui_rect_t *rect, const char *label,
                                       int icon_w, int icon_h, int gap,
                                       int selected, int highlighted,
                                       uint32_t text_color,
                                       int *icon_x, int *icon_y);
static void gui_draw_file_icon_cell(const gui_rect_t *rect, const char *label,
                                    gui_icon_id_t icon, int selected,
                                    int highlighted, uint32_t text_color);

uint32_t gui_text_len_until_break(const char *text) {
    uint32_t n = 0;
    if (!text) return 0;
    while (text[n] && text[n] != '\n') n++;
    return n;
}

uint32_t gui_utf8_step_bytes(const char *s) {
    unsigned char c;
    if (!s || !s[0]) return 0;
    c = (unsigned char)s[0];
    if (c < 0x80u) return 1;
    if ((c & 0xE0u) == 0xC0u && (s[1] & 0xC0u) == 0x80u) return 2;
    if ((c & 0xF0u) == 0xE0u && (s[1] & 0xC0u) == 0x80u && (s[2] & 0xC0u) == 0x80u) return 3;
    if ((c & 0xF8u) == 0xF0u && (s[1] & 0xC0u) == 0x80u &&
        (s[2] & 0xC0u) == 0x80u && (s[3] & 0xC0u) == 0x80u) return 4;
    return 1;
}

uint32_t gui_utf8_prefix_for_width(const char *src, uint32_t max_bytes, int max_width_px) {
    uint32_t used = 0;
    if (!src || max_width_px <= 0) return 0;
    while (src[used] && src[used] != '\n' && used < max_bytes) {
        uint32_t step = gui_utf8_step_bytes(src + used);
        char tmp[256];
        if (step == 0) break;
        if (used + step > max_bytes || used + step >= sizeof(tmp)) break;
        memcpy(tmp, src, used + step);
        tmp[used + step] = 0;
        if ((int)font_measure_text_width(font_get_default(), tmp) > max_width_px) break;
        used += step;
    }
    return used;
}

void gui_make_ellipsis_line_px(char *dst, uint32_t dst_size, const char *src,
                                      uint32_t max_src_bytes, int max_width_px,
                                      int use_ellipsis) {
    uint32_t n;
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src || max_src_bytes == 0 || max_width_px <= 0) return;
    if (max_src_bytes >= dst_size) max_src_bytes = dst_size - 1;
    n = gui_utf8_prefix_for_width(src, max_src_bytes, max_width_px);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    if (use_ellipsis && src[n] && src[n] != '\n') {
        int ell_w = (int)font_measure_text_width(font_get_default(), "...");
        int avail = max_width_px > ell_w ? max_width_px - ell_w : 0;
        n = gui_utf8_prefix_for_width(src, max_src_bytes, avail);
        if (n + 3 >= dst_size) n = dst_size > 4 ? dst_size - 4 : 0;
        memcpy(dst, src, n);
        dst[n++] = '.';
        dst[n++] = '.';
        dst[n++] = '.';
        dst[n] = 0;
    }
}

static int gui_draw_inline_icon(gui_icon_id_t icon, int x, int top, int height) {
    if (icon == GUI_ICON_NONE) return 0;
    gui_draw_file_icon(icon, x, top + (height - 14) / 2);
    return 14 + 4;
}

static int gui_label_aligned_x_px(const gui_widget_t *wg, int left, int width, int text_w, int icon_offset) {
    int area_w = width - icon_offset;
    if (!wg || area_w <= text_w) return left + icon_offset;
    if (wg->label_align == GUI_LABEL_ALIGN_RIGHT) return left + icon_offset + area_w - text_w;
    if (wg->label_align == GUI_LABEL_ALIGN_CENTER) return left + icon_offset + (area_w - text_w) / 2;
    return left + icon_offset;
}

static void gui_draw_label_widget(gui_widget_t *wg, int ax, int ay) {
    int text_off = 0;
    uint32_t color;
    gui_rect_t clip;
    int max_text_width;
    uint32_t line_height;
    uint32_t line_cap;
    uint32_t line;
    const char *p;
    if (!wg) return;
    text_off = gui_draw_inline_icon(wg->icon, ax, ay, wg->rect.h);
    clip.x = ax + text_off;
    clip.y = ay;
    clip.w = wg->rect.w - text_off;
    clip.h = wg->rect.h;
    if (clip.w <= 0 || clip.h <= 0) return;
    color = wg->fg_color ? wg->fg_color : g_gui.colors.text_fg;
    max_text_width = clip.w;
    if (max_text_width <= 0) return;
    line_height = (uint32_t)gui_text_line_height_px();
    if (line_height == 0) line_height = 10;
    line_cap = (wg->label_flags & GUI_LABEL_FLAG_MULTILINE) ? (uint32_t)(clip.h / (int)line_height) : 1u;
    if (line_cap == 0) line_cap = 1;
    if (line_cap > 8) line_cap = 8;
    p = wg->text;
    for (line = 0; line < line_cap && p && *p; line++) {
        char line_buf[256];
        uint32_t src_len = gui_text_len_until_break(p);
        uint32_t consume_len = src_len;
        int line_w;
        int multiline = (wg->label_flags & GUI_LABEL_FLAG_MULTILINE) != 0;
        int hard_wrap;
        int truncated;
        int y = ay + (int)(line * line_height);
        if (!multiline) y = gui_text_center_y(ay, wg->rect.h);
        if (y > ay + wg->rect.h - 1) break;
        consume_len = gui_utf8_prefix_for_width(p, src_len, max_text_width);
        hard_wrap = multiline && consume_len < src_len;
        truncated = consume_len < src_len;
        if (!hard_wrap) consume_len = src_len;
        gui_make_ellipsis_line_px(line_buf, sizeof(line_buf), p,
                                  hard_wrap ? consume_len : src_len,
                                  max_text_width,
                                  truncated && !hard_wrap && (wg->label_flags & GUI_LABEL_FLAG_ELLIPSIS));
        line_w = (int)font_measure_text_width(font_get_default(), line_buf);
        gui_draw_window_title_text(gui_label_aligned_x_px(wg, ax, wg->rect.w, line_w, text_off), y, line_buf, color, &clip);
        p += consume_len;
        if (!hard_wrap && *p == '\n') p++;
        if (!multiline) break;
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

int gui_text_line_height_px(void) {
    int h = (int)font_get_line_height(font_get_default());
    return h > 0 ? h : GUI_CHAR_H;
}

static int gui_text_glyph_height_px(void) {
    int ascii_h = (int)font_get_ascii_height(font_get_default());
    int unicode_h = (int)font_get_unicode_height();
    int h = ascii_h > unicode_h ? ascii_h : unicode_h;
    return h > 0 ? h : gui_text_line_height_px();
}

int gui_text_center_y(int top, int height) {
    int text_h = gui_text_glyph_height_px();
    int y = top + (height - text_h) / 2;
    return y < top ? top : y;
}

static void gui_draw_window_title_text(int x, int y, const char *text, uint32_t color, const gui_rect_t *clip) {
    gui_rect_t old_clip;
    int old_enabled;
    if (!text || !clip || clip->w <= 0 || clip->h <= 0) return;
    old_clip = g_gui.clip_rect;
    old_enabled = g_gui.clip_enabled;
    gui_set_clip_rect(clip);
    gui_draw_text(x, y, text, color);
    g_gui.clip_rect = old_clip;
    g_gui.clip_enabled = old_enabled;
}

static gui_window_t *g_settings_win = 0;
static gui_window_t *g_network_win = 0;
static gui_widget_t *g_network_ip_box = 0;
static gui_widget_t *g_network_mask_box = 0;
static gui_widget_t *g_network_gateway_box = 0;
static gui_widget_t *g_network_dns_box = 0;
static int g_settings_language_dropdown_open = 0;
static void network_refresh(gui_widget_t *w, void *ud);
static void network_toggle_admin(gui_widget_t *w, void *ud);
static void network_dhcp(gui_widget_t *w, void *ud);
static void network_apply_static(gui_widget_t *w, void *ud);
static void network_on_close(gui_window_t *win, void *ud) {
    (void)win;
    (void)ud;
    g_network_win = 0;
}

static int gui_get_primary_net_info(net_device_info_t *out) {
    net_device_t *dev;

    if (!out) {
        return -1;
    }

    dev = net_get_default_device();
    if (dev && net_get_device_info_by_name(dev->name, out) == 0) {
        return 0;
    }

    return net_get_device_info(0, out);
}

static void gui_network_build(int show_notice) {
    const font_renderer_t *font = font_get_default();
    int line_h = (int)font_get_line_height(font);
    int margin = (int)font_scale_value(14);
    int row_h = line_h + (int)font_scale_value(14);
    int button_h = line_h + (int)font_scale_value(12);
    int button_w = (int)font_scale_value(82);
    int label_w = (int)font_scale_value(130);
    int gap = (int)font_scale_value(8);
    int win_w = (int)font_scale_value(640);
    int win_h = margin * 2 + row_h * 14 + 56;
    int x;
    int y;
    int pos;
    net_device_info_t net_info;
    net_device_info_t list_info;
    int has_net;
    int dev_index;
    char line[224];
    char ip_buf[24];
    char mask_buf[24];
    char gw_buf[24];
    char dns_buf[24];
    char mac_buf[32];
    char rx_buf[16];
    char tx_buf[16];
    const char *mode_text;
    const char *up_text;
    const char *link_text;

    if (win_w < 640) win_w = 640;
    if (win_h < 460) win_h = 460;

    if (g_network_win) {
        gui_window_set_on_close(g_network_win, 0, 0);
        gui_destroy_window(g_network_win);
        g_network_win = 0;
    }
    g_network_ip_box = 0;
    g_network_mask_box = 0;
    g_network_gateway_box = 0;
    g_network_dns_box = 0;

    g_network_win = gui_create_window(210, 90, win_w, win_h, i18n_t(I18N_KEY_SETTINGS_NETWORK));
    if (!g_network_win) return;
    gui_window_set_on_close(g_network_win, network_on_close, 0);

    x = margin;
    y = 36;
    for (dev_index = 0; dev_index < 4; dev_index++) {
        if (net_get_device_info((uint32_t)dev_index, &list_info) != 0) break;
        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_DEVICE, list_info.name);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = fp_str_append(line, pos, sizeof(line), list_info.driver);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = fp_str_append(line, pos, sizeof(line), (list_info.flags & NET_DEVICE_FLAG_UP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_UP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_DOWN));
        pos = fp_str_append(line, pos, sizeof(line), "/");
        pos = fp_str_append(line, pos, sizeof(line), (list_info.flags & NET_DEVICE_FLAG_LINK_UP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_LINK_UP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_LINK_DOWN));
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;
    }

    has_net = (gui_get_primary_net_info(&net_info) == 0);
    if (!has_net) {
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, i18n_t(I18N_KEY_SETTINGS_NETWORK_NO_DEVICE));
    } else {
        gui_format_ipv4_inline(net_info.ip, ip_buf, sizeof(ip_buf));
        gui_format_ipv4_inline(net_info.netmask, mask_buf, sizeof(mask_buf));
        gui_format_ipv4_inline(net_info.gateway, gw_buf, sizeof(gw_buf));
        gui_format_ipv4_inline(net_info.dns, dns_buf, sizeof(dns_buf));
        gui_format_mac_inline(net_info.mac, mac_buf, sizeof(mac_buf));
        mode_text = (net_info.config_mode == NET_CONFIG_MODE_DHCP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_DHCP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_STATIC);
        up_text = (net_info.flags & NET_DEVICE_FLAG_UP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_UP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_DOWN);
        link_text = (net_info.flags & NET_DEVICE_FLAG_LINK_UP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_LINK_UP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_LINK_DOWN);

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_DEVICE, net_info.name);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = fp_str_append(line, pos, sizeof(line), net_info.driver);
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_STATUS, up_text);
        pos = fp_str_append(line, pos, sizeof(line), " / ");
        pos = fp_str_append(line, pos, sizeof(line), link_text);
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_MAC, mac_buf);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_MODE, mode_text);
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_IP, ip_buf);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_GATEWAY, gw_buf);
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_DNS, dns_buf);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_TRAFFIC, "RX/TX ");
        pos = gui_append_uint(line, pos, sizeof(line), net_info.rx_packets);
        pos = fp_str_append(line, pos, sizeof(line), "/");
        pos = gui_append_uint(line, pos, sizeof(line), net_info.tx_packets);
        (void)rx_buf;
        (void)tx_buf;
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        gui_add_button(g_network_win, x, y, button_w, button_h, i18n_t(I18N_KEY_BTN_REFRESH), network_refresh, 0);
        gui_add_toggle(g_network_win, x + button_w + gap, y, button_w * 2, button_h,
                       i18n_t(I18N_KEY_SETTINGS_NETWORK_UP),
                       (net_info.flags & NET_DEVICE_FLAG_UP) ? 1 : 0,
                       network_toggle_admin, 0);
        gui_add_button(g_network_win, x + button_w * 3 + gap * 2, y, button_w, button_h, i18n_t(I18N_KEY_SETTINGS_NETWORK_DHCP), network_dhcp, 0);
        y += row_h;

        gui_add_label(g_network_win, x, y + (button_h - line_h) / 2, label_w, line_h + 4, i18n_t(I18N_KEY_SETTINGS_NETWORK_IP));
        g_network_ip_box = gui_add_textbox(g_network_win, x + label_w + gap, y, button_w * 2, button_h, ip_buf);
        gui_add_label(g_network_win, x + label_w + gap + button_w * 2 + gap, y + (button_h - line_h) / 2, label_w / 2, line_h + 4, i18n_t(I18N_KEY_SETTINGS_NETWORK_NETMASK));
        g_network_mask_box = gui_add_textbox(g_network_win, x + label_w + gap + button_w * 2 + gap + label_w / 2, y, button_w * 2, button_h, mask_buf);
        y += row_h;

        gui_add_label(g_network_win, x, y + (button_h - line_h) / 2, label_w, line_h + 4, i18n_t(I18N_KEY_SETTINGS_NETWORK_GATEWAY));
        g_network_gateway_box = gui_add_textbox(g_network_win, x + label_w + gap, y, button_w * 2, button_h, gw_buf);
        gui_add_label(g_network_win, x + label_w + gap + button_w * 2 + gap, y + (button_h - line_h) / 2, label_w / 2, line_h + 4, i18n_t(I18N_KEY_SETTINGS_NETWORK_DNS));
        g_network_dns_box = gui_add_textbox(g_network_win, x + label_w + gap + button_w * 2 + gap + label_w / 2, y, button_w * 2, button_h, dns_buf);
        y += row_h;

        gui_add_button(g_network_win, x, y, button_w * 2, button_h, i18n_t(I18N_KEY_SETTINGS_NETWORK_APPLY_STATIC), network_apply_static, 0);
    }

    if (show_notice) gui_notify(i18n_t(I18N_KEY_SETTINGS_APPLIED));
    gui_render();
}

void gui_network_open(void) {
    gui_network_build(0);
}

static void settings_open_network(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    gui_network_open();
}

static void gui_settings_build(int show_notice);
static void gui_network_build(int show_notice);

int gui_rect_contains(const gui_rect_t *r, int x, int y) {
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
    r.x = w->rect.x + w->rect.w - GUI_TITLE_HEIGHT * 3 + 7;
    r.y = w->rect.y + 4;
    r.w = GUI_TITLE_HEIGHT - 8;
    r.h = GUI_TITLE_HEIGHT - 8;
    return r;
}

static gui_rect_t gui_max_rect(gui_window_t *w) {
    gui_rect_t r;
    r.x = w->rect.x + w->rect.w - GUI_TITLE_HEIGHT * 2 + 5;
    r.y = w->rect.y + 4;
    r.w = GUI_TITLE_HEIGHT - 8;
    r.h = GUI_TITLE_HEIGHT - 8;
    return r;
}

static gui_rect_t gui_resize_grip_rect(gui_window_t *w) {
    gui_rect_t r;
    r.w = 14;
    r.h = 14;
    r.x = w->rect.x + w->rect.w - r.w - 1;
    r.y = w->rect.y + w->rect.h - r.h - 1;
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

gui_window_t *gui_window_at(int x, int y) {
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

static gui_widget_t *gui_widget_parent(gui_widget_t *wg) {
    gui_widget_t *parent;
    if (!wg || !wg->owner || wg->parent_id == 0) return 0;
    parent = gui_find_widget(wg->owner, wg->parent_id);
    if (!parent || parent == wg || !parent->visible || parent->type != GUI_WIDGET_SCROLLVIEW) return 0;
    return parent;
}

static int gui_widget_absolute_origin_depth(gui_widget_t *wg, int *out_x, int *out_y, uint32_t depth) {
    gui_widget_t *parent;
    int px;
    int py;
    if (!wg || !wg->owner || !out_x || !out_y || depth > GUI_MAX_WIDGETS_PER_WIN) return 0;
    parent = gui_widget_parent(wg);
    if (!parent) {
        *out_x = wg->owner->rect.x + GUI_BORDER_SIZE + wg->rect.x;
        *out_y = wg->owner->rect.y + GUI_TITLE_HEIGHT + wg->rect.y;
        return 1;
    }
    if (!gui_widget_absolute_origin_depth(parent, &px, &py, depth + 1)) return 0;
    *out_x = px + wg->rect.x - parent->value;
    *out_y = py + wg->rect.y - parent->step;
    return 1;
}

static int gui_widget_absolute_origin(gui_widget_t *wg, int *out_x, int *out_y) {
    return gui_widget_absolute_origin_depth(wg, out_x, out_y, 0);
}

void gui_set_focused_widget(gui_widget_t *wg);

static int gui_widget_local_from_screen(gui_widget_t *wg, int x, int y, int *out_x, int *out_y) {
    int ax;
    int ay;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return 0;
    if (out_x) *out_x = x - ax;
    if (out_y) *out_y = y - ay;
    return 1;
}

static int gui_widget_is_child_of(gui_widget_t *wg, gui_widget_t *parent) {
    gui_widget_t *p;
    uint32_t guard = 0;
    if (!wg || !parent || wg->owner != parent->owner) return 0;
    p = gui_widget_parent(wg);
    while (p && guard++ < GUI_MAX_WIDGETS_PER_WIN) {
        if (p == parent) return 1;
        p = gui_widget_parent(p);
    }
    return 0;
}

static gui_widget_t *gui_widget_at(gui_window_t *w, int sx, int sy) {
    int i;
    if (!w) return 0;
    for (i = (int)w->widget_count - 1; i >= 0; i--) {
        gui_widget_t *wg = &w->widgets[i];
        int ax;
        int ay;
        if (!wg->visible || !wg->enabled || wg->parent_id != 0) continue;
        if (!gui_widget_absolute_origin(wg, &ax, &ay)) continue;
        if (sx + w->rect.x + GUI_BORDER_SIZE < ax || sy + w->rect.y + GUI_TITLE_HEIGHT < ay ||
            sx + w->rect.x + GUI_BORDER_SIZE >= ax + wg->rect.w || sy + w->rect.y + GUI_TITLE_HEIGHT >= ay + wg->rect.h) continue;
        if (wg->type == GUI_WIDGET_SCROLLVIEW) {
            int j;
            int screen_x = sx + w->rect.x + GUI_BORDER_SIZE;
            int screen_y = sy + w->rect.y + GUI_TITLE_HEIGHT;
            int content_w = wg->min_value > wg->rect.w ? wg->min_value : wg->rect.w;
            int content_h = wg->max_value > wg->rect.h ? wg->max_value : wg->rect.h;
            int show_v = content_h > wg->rect.h;
            int show_h = content_w > wg->rect.w;
            int view_x = ax + 1;
            int view_y = ay + 1;
            int view_w = wg->rect.w - 2 - (show_v ? 10 : 0);
            int view_h = wg->rect.h - 2 - (show_h ? 10 : 0);
            if (view_w > 0 && view_h > 0 &&
                screen_x >= view_x && screen_y >= view_y &&
                screen_x < view_x + view_w && screen_y < view_y + view_h) {
                for (j = (int)w->widget_count - 1; j >= 0; j--) {
                    gui_widget_t *child = &w->widgets[j];
                    int cx;
                    int cy;
                    if (!child->visible || !child->enabled || !gui_widget_is_child_of(child, wg)) continue;
                    if (!gui_widget_absolute_origin(child, &cx, &cy)) continue;
                    if (screen_x >= cx && screen_y >= cy &&
                        screen_x < cx + child->rect.w && screen_y < cy + child->rect.h) return child;
                }
            }
        }
        return wg;
    }
    return 0;
}

static int gui_widget_can_focus(gui_widget_t *wg) {
    if (!wg || !wg->visible || !wg->enabled) return 0;
    if (wg->type == GUI_WIDGET_TOAST) return 0;
    if ((wg->type == GUI_WIDGET_TEXTBOX || wg->type == GUI_WIDGET_TEXTAREA) &&
        (wg->textbox_flags & GUI_TEXTBOX_FLAG_DISABLED)) return 0;
    return (wg->type == GUI_WIDGET_TEXTBOX || wg->type == GUI_WIDGET_TEXTAREA || wg->type == GUI_WIDGET_BUTTON ||
            wg->type == GUI_WIDGET_ICON_BUTTON || wg->type == GUI_WIDGET_ICONVIEW || wg->type == GUI_WIDGET_TOGGLE ||
            wg->type == GUI_WIDGET_CHECKBOX || wg->type == GUI_WIDGET_RADIOBUTTON ||
            wg->type == GUI_WIDGET_SELECT || wg->type == GUI_WIDGET_COMBOBOX || wg->type == GUI_WIDGET_LISTVIEW ||
            wg->type == GUI_WIDGET_TABLEVIEW || wg->type == GUI_WIDGET_MENUBAR ||
            wg->type == GUI_WIDGET_TABVIEW || wg->type == GUI_WIDGET_SPLITVIEW || wg->type == GUI_WIDGET_CONTEXTMENU || wg->type == GUI_WIDGET_DIALOG ||
            (wg->type == GUI_WIDGET_LABEL &&
             (wg->label_flags & (GUI_LABEL_FLAG_SELECTABLE | GUI_LABEL_FLAG_COPYABLE)) != 0));
}

static int gui_widget_is_clickable(gui_widget_t *wg) {
    return wg && wg->visible && wg->enabled &&
           (wg->type == GUI_WIDGET_BUTTON || wg->type == GUI_WIDGET_ICON_BUTTON ||
            wg->type == GUI_WIDGET_TOGGLE || wg->type == GUI_WIDGET_CHECKBOX ||
            wg->type == GUI_WIDGET_RADIOBUTTON || wg->type == GUI_WIDGET_SELECT ||
            wg->type == GUI_WIDGET_COMBOBOX ||
            wg->type == GUI_WIDGET_LISTVIEW || wg->type == GUI_WIDGET_TABLEVIEW ||
            wg->type == GUI_WIDGET_MENUBAR || wg->type == GUI_WIDGET_TABVIEW || wg->type == GUI_WIDGET_SPLITVIEW || wg->type == GUI_WIDGET_CONTEXTMENU || wg->type == GUI_WIDGET_DIALOG || wg->type == GUI_WIDGET_TOAST || wg->type == GUI_WIDGET_TREEVIEW || wg->type == GUI_WIDGET_ICONVIEW ||
            (wg->type == GUI_WIDGET_LABEL && (wg->label_flags & GUI_LABEL_FLAG_COPYABLE)));
}

static int gui_widget_is_hoverable(gui_widget_t *wg) {
    return wg && wg->visible && wg->enabled &&
           (wg->type == GUI_WIDGET_BUTTON || wg->type == GUI_WIDGET_ICON_BUTTON ||
            wg->type == GUI_WIDGET_TOGGLE || wg->type == GUI_WIDGET_CHECKBOX ||
            wg->type == GUI_WIDGET_RADIOBUTTON || wg->type == GUI_WIDGET_SELECT ||
            wg->type == GUI_WIDGET_COMBOBOX ||
            wg->type == GUI_WIDGET_LISTVIEW || wg->type == GUI_WIDGET_TABLEVIEW ||
            wg->type == GUI_WIDGET_MENUBAR || wg->type == GUI_WIDGET_TABVIEW || wg->type == GUI_WIDGET_CONTEXTMENU || wg->type == GUI_WIDGET_DIALOG || wg->type == GUI_WIDGET_TOAST ||
            wg->type == GUI_WIDGET_SLIDER || wg->type == GUI_WIDGET_SCROLLBAR || wg->type == GUI_WIDGET_ICONVIEW);
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

static int gui_dialog_has_cancel(const gui_widget_t *wg) {
    uint32_t dialog_type;
    if (!wg || wg->type != GUI_WIDGET_DIALOG) return 0;
    dialog_type = wg->label_flags & GUI_DIALOG_TYPE_MASK;
    return ((wg->label_flags & GUI_DIALOG_FLAG_CANCEL) || dialog_type == GUI_DIALOG_TYPE_CONFIRM) ? 1 : 0;
}

static int gui_dialog_default_result(const gui_widget_t *wg) {
    if (!wg || wg->type != GUI_WIDGET_DIALOG) return GUI_DIALOG_RESULT_NONE;
    if ((wg->label_flags & GUI_DIALOG_FLAG_DEFAULT_CANCEL) && gui_dialog_has_cancel(wg)) return GUI_DIALOG_RESULT_CANCEL;
    return GUI_DIALOG_RESULT_OK;
}

static void gui_dialog_close(gui_widget_t *wg, int result) {
    if (!wg || wg->type != GUI_WIDGET_DIALOG) return;
    wg->value = result;
    wg->visible = 0;
    wg->pressed = 0;
    if (g_gui.pressed_widget == wg) g_gui.pressed_widget = 0;
    if (g_gui.focused_widget == wg) gui_set_focused_widget(0);
    if (wg->on_click) wg->on_click(wg, wg->user_data);
    if (wg->owner && wg->owner->user_owner_pid != 0) gui_user_post_value_event(wg);
    gui_invalidate_all();
}

static int gui_dialog_hit_button(gui_widget_t *wg, int screen_x, int screen_y) {
    int ax, ay;
    const int button_w = 54;
    const int button_h = 20;
    const int button_gap = 8;
    int ok_x, cancel_x, button_y;
    if (!wg || wg->type != GUI_WIDGET_DIALOG || !wg->visible || !wg->enabled) return GUI_DIALOG_RESULT_NONE;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return GUI_DIALOG_RESULT_NONE;
    ok_x = ax + wg->rect.w - button_w - 12;
    cancel_x = ok_x - button_w - button_gap;
    button_y = ay + wg->rect.h - button_h - 10;
    if (screen_x >= ok_x && screen_x < ok_x + button_w &&
        screen_y >= button_y && screen_y < button_y + button_h) return GUI_DIALOG_RESULT_OK;
    if (gui_dialog_has_cancel(wg) &&
        screen_x >= cancel_x && screen_x < cancel_x + button_w &&
        screen_y >= button_y && screen_y < button_y + button_h) return GUI_DIALOG_RESULT_CANCEL;
    return GUI_DIALOG_RESULT_NONE;
}

static int gui_dialog_handle_key(gui_widget_t *wg, int key) {
    if (!wg || wg->type != GUI_WIDGET_DIALOG || !wg->visible || !wg->enabled) return 0;
    if (key == GUI_KEY_ESCAPE) {
        gui_dialog_close(wg, gui_dialog_has_cancel(wg) ? GUI_DIALOG_RESULT_CANCEL : GUI_DIALOG_RESULT_OK);
        return 1;
    }
    if (key == GUI_KEY_ENTER || key == GUI_KEY_SPACE) {
        gui_dialog_close(wg, gui_dialog_default_result(wg));
        return 1;
    }
    return 0;
}

int gui_toggle_set_checked(gui_widget_t *widget, int checked) {
    if (!widget || widget->type != GUI_WIDGET_TOGGLE) return -1;
    widget->value = checked ? 1 : 0;
    if (widget->value) widget->button_flags |= GUI_TOGGLE_FLAG_ON;
    else widget->button_flags &= ~GUI_TOGGLE_FLAG_ON;
    return 0;
}

int gui_toggle_get_checked(const gui_widget_t *widget) {
    if (!widget || widget->type != GUI_WIDGET_TOGGLE) return 0;
    return widget->value ? 1 : 0;
}

static void gui_toggle_activate(gui_widget_t *wg) {
    int old_value;
    if (!wg || wg->type != GUI_WIDGET_TOGGLE || !wg->enabled) return;
    old_value = wg->value;
    gui_toggle_set_checked(wg, !wg->value);
    if (wg->value != old_value) {
        if (wg->on_click) wg->on_click(wg, wg->user_data);
        if (wg->owner && wg->owner->user_owner_pid != 0) {
            gui_user_post_value_event(wg);
        }
        gui_invalidate_all();
    }
}

int gui_checkbox_set_checked(gui_widget_t *widget, int checked) {
    if (!widget || widget->type != GUI_WIDGET_CHECKBOX) return -1;
    widget->value = checked ? 1 : 0;
    if (widget->value) widget->button_flags |= GUI_CHECKBOX_FLAG_CHECKED;
    else widget->button_flags &= ~GUI_CHECKBOX_FLAG_CHECKED;
    return 0;
}

int gui_checkbox_get_checked(const gui_widget_t *widget) {
    if (!widget || widget->type != GUI_WIDGET_CHECKBOX) return 0;
    return widget->value ? 1 : 0;
}

static void gui_checkbox_activate(gui_widget_t *wg) {
    int old_value;
    if (!wg || wg->type != GUI_WIDGET_CHECKBOX || !wg->enabled) return;
    old_value = wg->value;
    gui_checkbox_set_checked(wg, !wg->value);
    if (wg->value != old_value) {
        if (wg->on_click) wg->on_click(wg, wg->user_data);
        if (wg->owner && wg->owner->user_owner_pid != 0) {
            gui_user_post_value_event(wg);
        }
        gui_invalidate_all();
    }
}

static void gui_radiobutton_uncheck_group(gui_widget_t *widget) {
    uint32_t i;
    gui_window_t *win;
    if (!widget || widget->type != GUI_WIDGET_RADIOBUTTON || !widget->owner) return;
    win = widget->owner;
    for (i = 0; i < win->widget_count; i++) {
        gui_widget_t *other = &win->widgets[i];
        if (other == widget || other->type != GUI_WIDGET_RADIOBUTTON) continue;
        if (other->group_id != widget->group_id) continue;
        if (!other->value) continue;
        other->value = 0;
        other->button_flags &= ~GUI_RADIOBUTTON_FLAG_CHECKED;
        if (other->owner && other->owner->user_owner_pid != 0) {
            gui_user_post_value_event(other);
        }
    }
}

int gui_radiobutton_set_checked(gui_widget_t *widget, int checked) {
    if (!widget || widget->type != GUI_WIDGET_RADIOBUTTON) return -1;
    if (checked) gui_radiobutton_uncheck_group(widget);
    widget->value = checked ? 1 : 0;
    if (widget->value) widget->button_flags |= GUI_RADIOBUTTON_FLAG_CHECKED;
    else widget->button_flags &= ~GUI_RADIOBUTTON_FLAG_CHECKED;
    return 0;
}

int gui_radiobutton_get_checked(const gui_widget_t *widget) {
    if (!widget || widget->type != GUI_WIDGET_RADIOBUTTON) return 0;
    return widget->value ? 1 : 0;
}

static void gui_radiobutton_activate(gui_widget_t *wg) {
    int old_value;
    if (!wg || wg->type != GUI_WIDGET_RADIOBUTTON || !wg->enabled) return;
    old_value = wg->value;
    gui_radiobutton_set_checked(wg, 1);
    if (wg->value != old_value) {
        if (wg->on_click) wg->on_click(wg, wg->user_data);
        if (wg->owner && wg->owner->user_owner_pid != 0) {
            gui_user_post_value_event(wg);
        }
        gui_invalidate_all();
    }
}

static int gui_slider_normalize_step(gui_widget_t *wg) {
    int range;
    if (!wg || wg->type != GUI_WIDGET_SLIDER) return 1;
    range = wg->max_value - wg->min_value;
    if (range <= 0) return 1;
    if (wg->step <= 0) wg->step = 1;
    if (wg->step > range) wg->step = range;
    return wg->step;
}

static int gui_slider_snap_value(gui_widget_t *wg, int value) {
    int step;
    int offset;
    int snapped;
    if (!wg || wg->type != GUI_WIDGET_SLIDER) return value;
    if (wg->max_value <= wg->min_value) wg->max_value = wg->min_value + 1;
    if (value < wg->min_value) value = wg->min_value;
    if (value > wg->max_value) value = wg->max_value;
    step = gui_slider_normalize_step(wg);
    if (step <= 1) return value;
    offset = value - wg->min_value;
    snapped = wg->min_value + ((offset + step / 2) / step) * step;
    if (snapped < wg->min_value) snapped = wg->min_value;
    if (snapped > wg->max_value) snapped = wg->max_value;
    return snapped;
}

static int gui_slider_value_from_screen_x(gui_widget_t *wg, int screen_x) {
    int min;
    int max;
    int track_x;
    int track_w;
    int rel;
    int value;
    if (!wg || wg->type != GUI_WIDGET_SLIDER || !wg->owner) return 0;
    min = wg->min_value;
    max = wg->max_value;
    if (max <= min) max = min + 1;
    if (!gui_widget_absolute_origin(wg, &track_x, &rel)) return gui_slider_snap_value(wg, min);
    track_x += 8;
    track_w = wg->rect.w - 16;
    if (track_w <= 0) track_w = 1;
    rel = screen_x - track_x;
    if (rel < 0) rel = 0;
    if (rel > track_w) rel = track_w;
    value = min + (rel * (max - min) + track_w / 2) / track_w;
    return gui_slider_snap_value(wg, value);
}

static void gui_slider_apply_screen_x(gui_widget_t *wg, int screen_x) {
    int value;
    if (!wg || wg->type != GUI_WIDGET_SLIDER || !wg->enabled) return;
    value = gui_slider_value_from_screen_x(wg, screen_x);
    if (value != wg->value) {
        wg->value = value;
        if (wg->on_click) wg->on_click(wg, wg->user_data);
        if (wg->owner && wg->owner->user_owner_pid != 0) {
            gui_user_post_value_event(wg);
        }
        gui_invalidate_all();
    }
}

static int gui_scrollbar_is_horizontal(gui_widget_t *wg) {
    return wg && wg->rect.w > wg->rect.h;
}

static int gui_scrollbar_value_from_screen(gui_widget_t *wg, int screen_x, int screen_y) {
    int min;
    int max;
    int track_pos;
    int track_len;
    int rel;
    int value;
    if (!wg || wg->type != GUI_WIDGET_SCROLLBAR || !wg->owner) return 0;
    min = wg->min_value;
    max = wg->max_value;
    if (max <= min) max = min + 1;
    if (!gui_widget_absolute_origin(wg, &track_pos, &rel)) return gui_slider_snap_value(wg, min);
    if (gui_scrollbar_is_horizontal(wg)) {
        track_pos += 8;
        track_len = wg->rect.w - 16;
        rel = screen_x - track_pos;
    } else {
        track_pos = rel + 8;
        track_len = wg->rect.h - 16;
        rel = screen_y - track_pos;
    }
    if (track_len <= 0) track_len = 1;
    if (rel < 0) rel = 0;
    if (rel > track_len) rel = track_len;
    value = min + (rel * (max - min) + track_len / 2) / track_len;
    return gui_slider_snap_value(wg, value);
}

static void gui_scrollbar_apply_value(gui_widget_t *wg, int value) {
    if (!wg || wg->type != GUI_WIDGET_SCROLLBAR || !wg->enabled) return;
    value = gui_slider_snap_value(wg, value);
    if (value != wg->value) {
        wg->value = value;
        if (wg->on_click) wg->on_click(wg, wg->user_data);
        if (wg->owner && wg->owner->user_owner_pid != 0) {
            gui_user_post_value_event(wg);
        }
        gui_invalidate_all();
    }
}

static void gui_scrollbar_apply_screen(gui_widget_t *wg, int screen_x, int screen_y) {
    if (!wg || wg->type != GUI_WIDGET_SCROLLBAR || !wg->enabled) return;
    gui_scrollbar_apply_value(wg, gui_scrollbar_value_from_screen(wg, screen_x, screen_y));
}

static void gui_scrollbar_scroll_steps(gui_widget_t *wg, int steps) {
    int step;
    if (!wg || wg->type != GUI_WIDGET_SCROLLBAR || !wg->enabled || steps == 0) return;
    step = gui_slider_normalize_step(wg);
    gui_scrollbar_apply_value(wg, wg->value + steps * step);
}

static void gui_set_hovered_widget(gui_widget_t *wg) {
    if (wg && !gui_widget_is_hoverable(wg)) wg = 0;
    if (g_gui.hovered_widget == wg) return;
    if (g_gui.hovered_widget) g_gui.hovered_widget->hovered = 0;
    g_gui.hovered_widget = wg;
    if (g_gui.hovered_widget) g_gui.hovered_widget->hovered = 1;
    gui_invalidate_all();
}

static gui_widget_t *gui_widget_at_screen(int x, int y) {
    gui_window_t *w = gui_window_at(x, y);
    uint32_t i;
    int sx;
    int sy;
    if (!w) return 0;
    sx = x - w->rect.x - GUI_BORDER_SIZE;
    sy = y - w->rect.y - GUI_TITLE_HEIGHT;
    for (i = w->widget_count; i > 0; --i) {
        gui_widget_t *sw = &w->widgets[i - 1];
        if ((sw->type == GUI_WIDGET_SELECT || sw->type == GUI_WIDGET_COMBOBOX) &&
            gui_select_dropdown_index_at(sw, x, y) >= 0) {
            return sw;
        }
    }
    return gui_widget_at(w, sx, sy);
}

void gui_set_focused_widget(gui_widget_t *wg) {
    gui_widget_t *old_widget;
    if (g_gui.focused_widget == wg) return;
    old_widget = g_gui.focused_widget;
    if (old_widget) {
        old_widget->focused = 0;
        gui_user_post_text_event(old_widget, GUI_EVENT_BLUR);
    }
    g_gui.focused_widget = 0;
    if (gui_widget_can_focus(wg)) {
        if (wg->type == GUI_WIDGET_TEXTBOX) {
            gui_textbox_ensure_cursor_visible(wg);
        } else if (wg->type == GUI_WIDGET_TEXTAREA) {
            gui_textarea_ensure_cursor_visible(wg);
        }
        wg->focused = 1;
        g_gui.focused_widget = wg;
        gui_user_post_text_event(wg, GUI_EVENT_FOCUS);
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

static void gui_focus_prev_widget(void) {
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
    if (start < 0) start = (int)count;
    for (i = 1; i <= count; i++) {
        int raw = start - (int)i;
        uint32_t idx = (uint32_t)((raw < 0) ? (raw + (int)count) : raw);
        if (gui_widget_can_focus(&w->widgets[idx])) {
            gui_set_focused_widget(&w->widgets[idx]);
            return;
        }
    }
}

static uint32_t gui_textbox_visible_chars(const gui_widget_t *wg) {
    int text_w;
    if (!wg || GUI_CHAR_W <= 0) return 0;
    text_w = wg->rect.w - 8;
    if (text_w <= 0) return 0;
    return (uint32_t)(text_w / GUI_CHAR_W);
}

static void gui_textbox_ensure_cursor_visible(gui_widget_t *wg) {
    uint32_t len;
    uint32_t visible;
    if (!wg || wg->type != GUI_WIDGET_TEXTBOX) return;
    len = (uint32_t)strlen(wg->text);
    if (wg->cursor > len) wg->cursor = len;
    if (wg->text_scroll > len) wg->text_scroll = len;
    visible = gui_textbox_visible_chars(wg);
    if (visible == 0 || len <= visible) {
        wg->text_scroll = 0;
        return;
    }
    if (wg->cursor < wg->text_scroll) {
        wg->text_scroll = wg->cursor;
    } else if (wg->cursor > wg->text_scroll + visible) {
        wg->text_scroll = wg->cursor - visible;
    }
    if (wg->text_scroll > len) wg->text_scroll = len;
}

static void gui_textbox_set_cursor_from_local_x(gui_widget_t *wg, int local_x) {
    uint32_t len;
    int text_x;
    int rel_x;
    uint32_t cursor;
    if (!wg || wg->type != GUI_WIDGET_TEXTBOX) return;
    len = (uint32_t)strlen(wg->text);
    text_x = local_x - 4;
    if (text_x <= 0 || GUI_CHAR_W <= 0) cursor = wg->text_scroll;
    else {
        rel_x = text_x + (GUI_CHAR_W / 2);
        cursor = wg->text_scroll + (uint32_t)(rel_x / GUI_CHAR_W);
        if (cursor > len) cursor = len;
    }
    if (wg->cursor != cursor) {
        wg->cursor = cursor;
        gui_textbox_ensure_cursor_visible(wg);
        if (wg->owner) {
            gui_invalidate_rect(wg->owner->rect.x, wg->owner->rect.y,
                                wg->owner->rect.w, wg->owner->rect.h);
        }
    }
}

static uint32_t gui_textarea_line_height(void) {
    return (uint32_t)(GUI_CHAR_H + 2);
}

static uint32_t gui_textarea_visible_lines(const gui_widget_t *wg) {
    int text_h;
    uint32_t line_h = gui_textarea_line_height();
    if (!wg || line_h == 0) return 0;
    text_h = wg->rect.h - 8;
    if (text_h <= 0) return 0;
    return (uint32_t)text_h / line_h;
}

static void gui_textarea_cursor_line_col(const gui_widget_t *wg, uint32_t *out_line, uint32_t *out_col) {
    uint32_t i;
    uint32_t line = 0;
    uint32_t col = 0;
    uint32_t len;
    if (!wg) {
        if (out_line) *out_line = 0;
        if (out_col) *out_col = 0;
        return;
    }
    len = (uint32_t)strlen(wg->text);
    for (i = 0; i < wg->cursor && i < len; i++) {
        if (wg->text[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    if (out_line) *out_line = line;
    if (out_col) *out_col = col;
}

static void gui_textarea_ensure_cursor_visible(gui_widget_t *wg) {
    uint32_t len;
    uint32_t line = 0;
    uint32_t col = 0;
    uint32_t visible;
    if (!wg || wg->type != GUI_WIDGET_TEXTAREA) return;
    len = (uint32_t)strlen(wg->text);
    if (wg->cursor > len) wg->cursor = len;
    gui_textarea_cursor_line_col(wg, &line, &col);
    (void)col;
    visible = gui_textarea_visible_lines(wg);
    if (visible == 0) {
        wg->text_scroll = 0;
        return;
    }
    if (line < wg->text_scroll) wg->text_scroll = line;
    else if (line >= wg->text_scroll + visible) wg->text_scroll = line - visible + 1;
}

static uint32_t gui_textarea_count_lines(const gui_widget_t *wg) {
    uint32_t i;
    uint32_t lines = 1;
    if (!wg || wg->type != GUI_WIDGET_TEXTAREA) return 0;
    for (i = 0; wg->text[i] != '\0'; i++) {
        if (wg->text[i] == '\n') lines++;
    }
    return lines;
}

static uint32_t gui_textarea_index_for_line_col(const gui_widget_t *wg, uint32_t target_line, uint32_t target_col) {
    uint32_t line = 0;
    uint32_t col = 0;
    uint32_t i = 0;
    uint32_t len;
    if (!wg || wg->type != GUI_WIDGET_TEXTAREA) return 0;
    len = (uint32_t)strlen(wg->text);
    while (i < len) {
        if (line == target_line && col >= target_col) break;
        if (wg->text[i] == '\n') {
            if (line == target_line) break;
            line++;
            col = 0;
            i++;
            continue;
        }
        col++;
        i++;
    }
    return i;
}

static uint32_t gui_textarea_current_line_start(const gui_widget_t *wg) {
    uint32_t pos;
    if (!wg || wg->type != GUI_WIDGET_TEXTAREA) return 0;
    pos = wg->cursor;
    while (pos > 0 && wg->text[pos - 1] != '\n') pos--;
    return pos;
}

static uint32_t gui_textarea_current_line_end(const gui_widget_t *wg) {
    uint32_t pos;
    uint32_t len;
    if (!wg || wg->type != GUI_WIDGET_TEXTAREA) return 0;
    len = (uint32_t)strlen(wg->text);
    pos = wg->cursor;
    if (pos > len) pos = len;
    while (pos < len && wg->text[pos] != '\n') pos++;
    return pos;
}

static void gui_textarea_set_cursor_from_local_xy(gui_widget_t *wg, int local_x, int local_y) {
    uint32_t target_line;
    uint32_t target_col;
    int text_x;
    int text_y;
    uint32_t line_h;
    if (!wg || wg->type != GUI_WIDGET_TEXTAREA) return;
    line_h = gui_textarea_line_height();
    text_x = local_x - 4;
    text_y = local_y - 4;
    target_line = wg->text_scroll;
    if (text_y > 0 && line_h > 0) target_line += (uint32_t)(text_y / (int)line_h);
    target_col = 0;
    if (text_x > 0 && GUI_CHAR_W > 0) target_col = (uint32_t)((text_x + (GUI_CHAR_W / 2)) / GUI_CHAR_W);

    wg->cursor = gui_textarea_index_for_line_col(wg, target_line, target_col);
    gui_textarea_ensure_cursor_visible(wg);
    if (wg->owner) {
        gui_invalidate_rect(wg->owner->rect.x, wg->owner->rect.y,
                            wg->owner->rect.w, wg->owner->rect.h);
    }
}

static int gui_textarea_scroll_lines(gui_widget_t *wg, int delta_lines) {
    uint32_t total;
    uint32_t visible;
    uint32_t max_scroll;
    uint32_t old_scroll;
    int next;
    if (!wg || wg->type != GUI_WIDGET_TEXTAREA) return 0;
    total = gui_textarea_count_lines(wg);
    visible = gui_textarea_visible_lines(wg);
    if (visible == 0) visible = 1;
    max_scroll = total > visible ? total - visible : 0;
    old_scroll = wg->text_scroll;
    next = (int)wg->text_scroll + delta_lines;
    if (next < 0) next = 0;
    if ((uint32_t)next > max_scroll) next = (int)max_scroll;
    wg->text_scroll = (uint32_t)next;
    if (wg->text_scroll != old_scroll && wg->owner) {
        gui_invalidate_rect(wg->owner->rect.x, wg->owner->rect.y,
                            wg->owner->rect.w, wg->owner->rect.h);
        return 1;
    }
    return 0;
}

static void gui_textarea_line_col_for_index(const gui_widget_t *wg, uint32_t index, uint32_t *out_line, uint32_t *out_col) {
    uint32_t i;
    uint32_t line = 0;
    uint32_t col = 0;
    uint32_t len;
    if (!wg || wg->type != GUI_WIDGET_TEXTAREA) {
        if (out_line) *out_line = 0;
        if (out_col) *out_col = 0;
        return;
    }
    len = (uint32_t)strlen(wg->text);
    if (index > len) index = len;
    for (i = 0; i < index; i++) {
        if (wg->text[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    if (out_line) *out_line = line;
    if (out_col) *out_col = col;
}

static int gui_text_widget_has_selection(const gui_widget_t *wg) {
    if (!wg || (wg->type != GUI_WIDGET_TEXTBOX && wg->type != GUI_WIDGET_TEXTAREA)) return 0;
    return wg->selection_start != wg->selection_end;
}

static void gui_text_widget_selection_bounds(const gui_widget_t *wg, uint32_t *start, uint32_t *end) {
    uint32_t len = wg ? (uint32_t)strlen(wg->text) : 0;
    uint32_t a = wg ? wg->selection_start : 0;
    uint32_t b = wg ? wg->selection_end : 0;
    if (a > len) a = len;
    if (b > len) b = len;
    if (a <= b) {
        if (start) *start = a;
        if (end) *end = b;
    } else {
        if (start) *start = b;
        if (end) *end = a;
    }
}

static void gui_text_widget_clear_selection(gui_widget_t *wg) {
    if (!wg) return;
    wg->selection_anchor = wg->cursor;
    wg->selection_start = wg->cursor;
    wg->selection_end = wg->cursor;
    if (g_gui.text_select_widget == wg) g_gui.text_select_widget = 0;
}

static void gui_text_widget_set_selection(gui_widget_t *wg, uint32_t anchor, uint32_t cursor) {
    uint32_t len;
    if (!wg) return;
    len = (uint32_t)strlen(wg->text);
    if (anchor > len) anchor = len;
    if (cursor > len) cursor = len;
    wg->selection_anchor = anchor;
    wg->selection_start = anchor;
    wg->selection_end = cursor;
    wg->cursor = cursor;
}

static int gui_text_widget_delete_selection(gui_widget_t *wg) {
    uint32_t start;
    uint32_t end;
    uint32_t len;
    if (!gui_text_widget_has_selection(wg)) return 0;
    gui_text_widget_selection_bounds(wg, &start, &end);
    if (start == end) {
        gui_text_widget_clear_selection(wg);
        return 0;
    }
    len = (uint32_t)strlen(wg->text);
    if (end > len) end = len;
    memmove(wg->text + start, wg->text + end, len - end + 1);
    wg->cursor = start;
    gui_text_widget_clear_selection(wg);
    return 1;
}

static int gui_text_widget_copy_selection(gui_widget_t *wg) {
    char buf[256];
    uint32_t start;
    uint32_t end;
    uint32_t n;
    if (!gui_text_widget_has_selection(wg)) return 0;
    if (wg->type == GUI_WIDGET_TEXTBOX && (wg->textbox_flags & GUI_TEXTBOX_FLAG_PASSWORD)) return 0;
    gui_text_widget_selection_bounds(wg, &start, &end);
    if (end <= start) return 0;
    n = end - start;
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, wg->text + start, n);
    buf[n] = '\0';
    return gui_terminal_set_clipboard_text(buf);
}

static int gui_text_widget_insert_text(gui_widget_t *wg, const char *text) {
    uint32_t len;
    uint32_t ins_len;
    uint32_t max_ins;
    if (!wg || !text || text[0] == '\0') return 0;
    if (wg->textbox_flags & GUI_TEXTBOX_FLAG_READONLY) return 0;
    if (gui_text_widget_has_selection(wg)) gui_text_widget_delete_selection(wg);
    len = (uint32_t)strlen(wg->text);
    if (wg->cursor > len) wg->cursor = len;
    max_ins = (uint32_t)sizeof(wg->text) - 1 - len;
    ins_len = (uint32_t)strlen(text);
    if (ins_len > max_ins) ins_len = max_ins;
    if (ins_len == 0) return 0;
    memmove(wg->text + wg->cursor + ins_len, wg->text + wg->cursor, len - wg->cursor + 1);
    memcpy(wg->text + wg->cursor, text, ins_len);
    wg->cursor += ins_len;
    gui_text_widget_clear_selection(wg);
    if (wg->owner && wg->owner->user_owner_pid != 0) {
        uint32_t cp = 0;
        const unsigned char *u = (const unsigned char *)text;
        if ((u[0] & 0x80u) == 0) cp = u[0];
        else if ((u[0] & 0xE0u) == 0xC0u && u[1]) cp = ((uint32_t)(u[0] & 0x1Fu) << 6) | (uint32_t)(u[1] & 0x3Fu);
        else if ((u[0] & 0xF0u) == 0xE0u && u[1] && u[2]) cp = ((uint32_t)(u[0] & 0x0Fu) << 12) | ((uint32_t)(u[1] & 0x3Fu) << 6) | (uint32_t)(u[2] & 0x3Fu);
        else if ((u[0] & 0xF8u) == 0xF0u && u[1] && u[2] && u[3]) cp = ((uint32_t)(u[0] & 0x07u) << 18) | ((uint32_t)(u[1] & 0x3Fu) << 12) | ((uint32_t)(u[2] & 0x3Fu) << 6) | (uint32_t)(u[3] & 0x3Fu);
        gui_user_post_text_input_event(wg, text, cp);
    }
    return 1;
}

static int gui_text_widget_paste_clipboard(gui_widget_t *wg) {
    const char *clip = gui_terminal_get_clipboard_text();
    return gui_text_widget_insert_text(wg, clip ? clip : "");
}

static void gui_text_widget_draw_selection(gui_widget_t *wg, int ax, int ay, const gui_rect_t *clip) {
    uint32_t start;
    uint32_t end;
    uint32_t color = gui_rgb(96, 150, 232);
    if (!gui_text_widget_has_selection(wg) || !clip) return;
    gui_text_widget_selection_bounds(wg, &start, &end);
    if (start == end) return;
    if (wg->type == GUI_WIDGET_TEXTBOX) {
        uint32_t vis_start = wg->text_scroll;
        uint32_t vis_end = vis_start + (uint32_t)(clip->w / GUI_CHAR_W) + 1;
        uint32_t a = start > vis_start ? start : vis_start;
        uint32_t b = end < vis_end ? end : vis_end;
        if (b > a) {
            int sx = ax + 4 + (int)((a - vis_start) * GUI_CHAR_W);
            int sw = (int)((b - a) * GUI_CHAR_W);
            gui_raw_fill_rect(sx, ay + 4, sw, wg->rect.h - 8, color);
        }
    } else if (wg->type == GUI_WIDGET_TEXTAREA) {
        uint32_t i = start;
        uint32_t line_h = gui_textarea_line_height();
        while (i < end) {
            uint32_t line = 0;
            uint32_t col = 0;
            uint32_t line_end;
            uint32_t seg_end;
            gui_textarea_line_col_for_index(wg, i, &line, &col);
            line_end = gui_textarea_index_for_line_col(wg, line, 0x7fffffffU);
            if (line_end < i) line_end = i;
            seg_end = end < line_end ? end : line_end;
            if (seg_end == i && i < end) seg_end = i + 1;
            if (line >= wg->text_scroll && line - wg->text_scroll < gui_textarea_visible_lines(wg)) {
                int sx = ax + 4 + (int)(col * GUI_CHAR_W);
                int sy = ay + 4 + (int)((line - wg->text_scroll) * line_h);
                int sw = (int)((seg_end - i) * GUI_CHAR_W);
                if (sw <= 0) sw = (int)GUI_CHAR_W;
                gui_raw_fill_rect(sx, sy, sw, (int)GUI_CHAR_H + 1, color);
            }
            if (seg_end <= i) break;
            i = seg_end;
            if (i < end && wg->text[i] == '\n') i++;
        }
    }
}

static int gui_textbox_on_key(gui_widget_t *wg, int key) {
    uint32_t len;
    uint32_t i;
    uint32_t old_cursor;
    int text_changed = 0;
    int cursor_changed = 0;
    if (!gui_widget_can_focus(wg)) return 0;
    len = (uint32_t)strlen(wg->text);
    if (wg->cursor > len) wg->cursor = len;
    old_cursor = wg->cursor;

    if (key == 1) {
        wg->selection_anchor = 0;
        wg->selection_start = 0;
        wg->selection_end = len;
        wg->cursor = len;
        return 1;
    } else if (key == 3) {
        return gui_text_widget_copy_selection(wg);
    } else if (key == 24) {
        if (wg->textbox_flags & GUI_TEXTBOX_FLAG_READONLY) return gui_text_widget_copy_selection(wg);
        if (gui_text_widget_copy_selection(wg)) text_changed = gui_text_widget_delete_selection(wg);
    } else if (key == 22) {
        if (wg->textbox_flags & GUI_TEXTBOX_FLAG_READONLY) return 0;
        text_changed = gui_text_widget_paste_clipboard(wg);
    } else if (wg->textbox_flags & GUI_TEXTBOX_FLAG_READONLY) {
        return 0;
    } else if (key == GUI_KEY_BACKSPACE) {
        if (gui_text_widget_has_selection(wg)) {
            text_changed = gui_text_widget_delete_selection(wg);
        } else if (wg->cursor > 0 && len > 0) {
            for (i = wg->cursor - 1; i < len; i++) wg->text[i] = wg->text[i + 1];
            wg->cursor--;
            text_changed = 1;
        }
    } else if (key == GUI_KEY_DELETE) {
        if (gui_text_widget_has_selection(wg)) {
            text_changed = gui_text_widget_delete_selection(wg);
        } else if (wg->cursor < len) {
            for (i = wg->cursor; i < len; i++) wg->text[i] = wg->text[i + 1];
            text_changed = 1;
        }
    } else if (key == GUI_KEY_LEFT) {
        if (wg->cursor > 0) wg->cursor--;
        gui_text_widget_clear_selection(wg);
    } else if (key == GUI_KEY_RIGHT) {
        if (wg->cursor < len) wg->cursor++;
        gui_text_widget_clear_selection(wg);
    } else if (key == GUI_KEY_UP && wg->type == GUI_WIDGET_TEXTAREA) {
        uint32_t line = 0;
        uint32_t col = 0;
        gui_textarea_cursor_line_col(wg, &line, &col);
        if (line > 0) wg->cursor = gui_textarea_index_for_line_col(wg, line - 1, col);
        gui_text_widget_clear_selection(wg);
    } else if (key == GUI_KEY_DOWN && wg->type == GUI_WIDGET_TEXTAREA) {
        uint32_t line = 0;
        uint32_t col = 0;
        uint32_t total = gui_textarea_count_lines(wg);
        gui_textarea_cursor_line_col(wg, &line, &col);
        if (line + 1 < total) wg->cursor = gui_textarea_index_for_line_col(wg, line + 1, col);
        gui_text_widget_clear_selection(wg);
    } else if (key == GUI_KEY_HOME) {
        uint32_t target = (wg->type == GUI_WIDGET_TEXTAREA) ? gui_textarea_current_line_start(wg) : 0;
        wg->cursor = target;
        gui_text_widget_clear_selection(wg);
    } else if (key == GUI_KEY_END) {
        uint32_t target = (wg->type == GUI_WIDGET_TEXTAREA) ? gui_textarea_current_line_end(wg) : len;
        wg->cursor = target;
        gui_text_widget_clear_selection(wg);
    } else if (gui_is_enter_key(key)) {
        if (wg->type == GUI_WIDGET_TEXTAREA) {
            text_changed = gui_text_widget_insert_text(wg, "\n");
        } else {
            if (wg->owner && wg->owner->user_owner_pid != 0) {
                gui_user_post_text_event(wg, GUI_USER_EVENT_TEXT_SUBMIT);
                gui_user_post_key_event(wg->owner, GUI_KEY_ENTER);
            }
            return 1;
        }
    } else if (key == GUI_KEY_TAB) {
        /* Tab changes focus at the event dispatcher level. */
    } else if (key >= 32 && key <= 126) {
        char ch[2];
        ch[0] = (char)key;
        ch[1] = '\0';
        text_changed = gui_text_widget_insert_text(wg, ch);
    }

    cursor_changed = (wg->cursor != old_cursor) || gui_text_widget_has_selection(wg);
    if (wg->type == GUI_WIDGET_TEXTAREA) gui_textarea_ensure_cursor_visible(wg);
    else gui_textbox_ensure_cursor_visible(wg);
    if ((text_changed || cursor_changed) && wg->owner) {
        gui_invalidate_rect(wg->owner->rect.x, wg->owner->rect.y, wg->owner->rect.w, wg->owner->rect.h);
        if (text_changed && wg->owner->user_owner_pid != 0) gui_user_post_text_event(wg, GUI_USER_EVENT_TEXT_CHANGED);
    }
    return text_changed || cursor_changed;
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
    g_gui.flush_generation++;
}

/* 14x14 file/folder icon renderer used by File Preview */
static void gui_draw_file_icon(gui_icon_id_t id, int x, int y) {
    uint32_t paper = gui_rgb(252, 252, 250);
    uint32_t ink   = gui_rgb(70, 70, 80);
    uint32_t fold  = gui_rgb(220, 180, 70);
    uint32_t fold_hi = gui_rgb(246, 216, 110);
    uint32_t fold2 = gui_rgb(180, 140, 40);
    uint32_t shadow = gui_rgb(55, 55, 60);
    int i;

    if (id == GUI_ICON_NONE) return;

    if (id == GUI_ICON_NAV_BACK || id == GUI_ICON_NAV_FORWARD) {
        uint32_t arrow = gui_rgb(38, 46, 62);
        uint32_t hi = gui_rgb(255, 255, 255);
        int dir = (id == GUI_ICON_NAV_BACK) ? -1 : 1;
        int tip_x = x + (dir < 0 ? 3 : 10);
        int tail_x = x + (dir < 0 ? 10 : 3);
        int cy = y + 7;
        gui_raw_line(tip_x + dir, cy, tail_x + dir, cy - 5, hi);
        gui_raw_line(tip_x + dir, cy, tail_x + dir, cy + 5, hi);
        gui_raw_line(tip_x, cy, tail_x, cy - 5, arrow);
        gui_raw_line(tip_x, cy, tail_x, cy + 5, arrow);
        gui_raw_line(tip_x + dir, cy, tail_x, cy, arrow);
        gui_raw_line(tip_x + dir * 2, cy, tail_x, cy, arrow);
        return;
    }

    if (id == GUI_ICON_NAV_RELOAD) {
        uint32_t arrow = gui_rgb(38, 46, 62);
        uint32_t hi = gui_rgb(255, 255, 255);
        gui_raw_line(x + 4, y + 3, x + 9, y + 3, arrow);
        gui_raw_line(x + 9, y + 3, x + 11, y + 5, arrow);
        gui_raw_line(x + 11, y + 5, x + 11, y + 8, arrow);
        gui_raw_line(x + 10, y + 4, x + 12, y + 4, arrow);
        gui_raw_line(x + 12, y + 4, x + 12, y + 6, arrow);
        gui_raw_line(x + 9, y + 11, x + 4, y + 11, arrow);
        gui_raw_line(x + 4, y + 11, x + 2, y + 9, arrow);
        gui_raw_line(x + 2, y + 9, x + 2, y + 6, arrow);
        gui_raw_line(x + 3, y + 10, x + 1, y + 10, arrow);
        gui_raw_line(x + 1, y + 10, x + 1, y + 8, arrow);
        gui_raw_line(x + 5, y + 4, x + 9, y + 4, hi);
        return;
    }

    gui_raw_fill_rect_alpha(x + 3, y + 3, 11, 11, shadow, 54u);
    g_gui_accel.icon_quality_passes++;

    if (id == GUI_ICON_FOLDER || id == GUI_ICON_UPDIR) {
        /* folder tab */
        gui_raw_fill_rect(x + 1, y + 3, 5, 2, fold);
        gui_raw_line(x + 2, y + 3, x + 5, y + 3, fold_hi);
        /* folder body */
        gui_raw_fill_rect(x + 1, y + 4, 12, 8, fold);
        gui_raw_line(x + 2, y + 5, x + 11, y + 5, fold_hi);
        /* border */
        gui_raw_line(x + 1, y + 3, x + 5, y + 3, fold2);
        gui_raw_line(x + 1, y + 11, x + 12, y + 11, fold2);
        gui_raw_line(x + 1, y + 4, x + 1, y + 11, fold2);
        gui_raw_line(x + 12, y + 4, x + 12, y + 11, fold2);
        if (id == GUI_ICON_UPDIR) {
            /* up arrow */
            gui_raw_line(x + 6, y + 5, x + 6, y + 10, ink);
            gui_raw_line(x + 7, y + 5, x + 7, y + 10, ink);
            gui_raw_line(x + 4, y + 7, x + 6, y + 5, ink);
            gui_raw_line(x + 9, y + 7, x + 7, y + 5, ink);
        }
        return;
    }

    /* generic paper sheet with folded corner */
    gui_raw_fill_rect(x + 2, y + 1, 9, 12, paper);
    gui_raw_line(x + 3, y + 2, x + 9, y + 2, gui_rgb(255, 255, 255));
    gui_raw_line(x + 2, y + 1, x + 10, y + 1, ink);
    gui_raw_line(x + 2, y + 12, x + 10, y + 12, ink);
    gui_raw_line(x + 2, y + 1, x + 2, y + 12, ink);
    gui_raw_line(x + 10, y + 1, x + 10, y + 12, ink);
    /* folded corner */
    gui_raw_line(x + 8, y + 1, x + 10, y + 3, ink);
    gui_raw_fill_rect(x + 8, y + 1, 2, 2, gui_rgb(225, 225, 225));

    if (id == GUI_ICON_FILE_TEXT) {
        uint32_t line = gui_rgb(100, 100, 110);
        for (i = 0; i < 4; i++) {
            gui_raw_line(x + 4, y + 4 + i * 2, x + 9, y + 4 + i * 2, line);
        }
    } else if (id == GUI_ICON_FILE_MARKUP) {
        /* # symbol */
        uint32_t line = gui_rgb(0, 120, 200);
        gui_raw_line(x + 5, y + 4, x + 5, y + 10, line);
        gui_raw_line(x + 8, y + 4, x + 8, y + 10, line);
        gui_raw_line(x + 4, y + 6, x + 9, y + 6, line);
        gui_raw_line(x + 4, y + 9, x + 9, y + 9, line);
    } else if (id == GUI_ICON_FILE_CODE) {
        /* < > */
        uint32_t line = gui_rgb(40, 140, 80);
        gui_raw_line(x + 5, y + 7, x + 4, y + 8, line);
        gui_raw_line(x + 4, y + 8, x + 5, y + 9, line);
        gui_raw_line(x + 8, y + 7, x + 9, y + 8, line);
        gui_raw_line(x + 9, y + 8, x + 8, y + 9, line);
    } else if (id == GUI_ICON_FILE_CONFIG) {
        /* gear-ish dot ring */
        uint32_t line = gui_rgb(140, 100, 30);
        gui_raw_fill_rect(x + 6, y + 4, 2, 2, line);
        gui_raw_fill_rect(x + 4, y + 6, 2, 2, line);
        gui_raw_fill_rect(x + 8, y + 6, 2, 2, line);
        gui_raw_fill_rect(x + 6, y + 8, 2, 2, line);
        gui_raw_fill_rect(x + 6, y + 6, 2, 2, gui_rgb(220, 220, 220));
    } else if (id == GUI_ICON_FILE_SHELL) {
        /* >_ */
        uint32_t line = gui_rgb(30, 30, 30);
        gui_raw_line(x + 4, y + 5, x + 6, y + 7, line);
        gui_raw_line(x + 6, y + 7, x + 4, y + 9, line);
        gui_raw_line(x + 7, y + 10, x + 9, y + 10, line);
    } else if (id == GUI_ICON_FILE_EXEC) {
        /* gear/arrow */
        uint32_t line = gui_rgb(180, 40, 40);
        gui_raw_fill_rect(x + 4, y + 4, 6, 2, line);
        gui_raw_fill_rect(x + 4, y + 7, 6, 2, line);
        gui_raw_fill_rect(x + 4, y + 10, 6, 2, line);
    } else if (id == GUI_ICON_FILE_IMAGE) {
        uint32_t sky = gui_rgb(120, 180, 230);
        uint32_t mtn = gui_rgb(70, 130, 80);
        uint32_t sun = gui_rgb(240, 200, 60);
        gui_raw_fill_rect(x + 3, y + 3, 7, 8, sky);
        gui_raw_fill_rect(x + 8, y + 4, 1, 1, sun);
        gui_raw_line(x + 3, y + 9, x + 6, y + 6, mtn);
        gui_raw_line(x + 6, y + 6, x + 9, y + 9, mtn);
    } else if (id == GUI_ICON_FILE_ARCHIVE) {
        uint32_t box = gui_rgb(150, 110, 60);
        gui_raw_fill_rect(x + 3, y + 3, 7, 9, box);
        gui_raw_fill_rect(x + 6, y + 3, 1, 9, gui_rgb(80, 60, 30));
        gui_raw_fill_rect(x + 5, y + 6, 3, 2, gui_rgb(230, 220, 180));
    }
    /* GUI_ICON_FILE_GENERIC: just the blank paper above */
    (void)i;
}

static int gui_select_is_separator(char ch) {
    return ch == '\n' || ch == '\r';
}

static const char *gui_select_skip_separators(const char *p) {
    while (p && gui_select_is_separator(*p)) p++;
    return p;
}

static int gui_select_item_count(const gui_widget_t *wg) {
    const char *p;
    int count = 0;
    if (!wg || !wg->placeholder[0]) return 0;
    p = wg->placeholder;
    while (*p) {
        p = gui_select_skip_separators(p);
        if (!*p) break;
        count++;
        while (*p && !gui_select_is_separator(*p)) p++;
    }
    return count;
}

static int gui_select_item_text(const gui_widget_t *wg, int index, char *out, int out_size) {
    const char *p;
    int cur = 0;
    int n = 0;
    if (!wg || !out || out_size <= 0 || index < 0) return -1;
    out[0] = 0;
    p = wg->placeholder;
    while (*p) {
        p = gui_select_skip_separators(p);
        if (!*p) break;
        if (cur == index) {
            while (*p && !gui_select_is_separator(*p) && n + 1 < out_size) out[n++] = *p++;
            out[n] = 0;
            return n > 0 ? 0 : -1;
        }
        while (*p && !gui_select_is_separator(*p)) p++;
        cur++;
    }
    return -1;
}

static void gui_select_close_others(gui_window_t *win, gui_widget_t *except) {
    uint32_t i;
    if (!win) return;
    for (i = 0; i < win->widget_count; ++i) {
        gui_widget_t *w = &win->widgets[i];
        if (w != except && (w->type == GUI_WIDGET_SELECT || w->type == GUI_WIDGET_COMBOBOX)) {
            w->step = 0;
            w->pressed = 0;
        }
    }
}

int gui_select_set_items(gui_widget_t *widget, const char *items) {
    int count;
    if (!widget || (widget->type != GUI_WIDGET_SELECT && widget->type != GUI_WIDGET_COMBOBOX)) return -1;
    gui_widget_set_placeholder(widget, items ? items : "");
    count = gui_select_item_count(widget);
    widget->max_value = count - 1;
    if (count <= 0) {
        widget->value = -1;
        widget->min_value = -1;
    } else {
        if (widget->value < 0) widget->value = 0;
        if (widget->value >= count) widget->value = count - 1;
        widget->min_value = widget->value;
    }
    return 0;
}

int gui_select_set_selected(gui_widget_t *widget, int selected_index) {
    int count;
    if (!widget || (widget->type != GUI_WIDGET_SELECT && widget->type != GUI_WIDGET_COMBOBOX)) return -1;
    count = gui_select_item_count(widget);
    if (count <= 0) {
        widget->value = -1;
        widget->min_value = -1;
        return selected_index < 0 ? 0 : -1;
    }
    if (selected_index < 0 || selected_index >= count) return -1;
    widget->value = selected_index;
    widget->min_value = selected_index;
    return 0;
}

int gui_select_get_selected(gui_widget_t *widget, int *out_selected_index) {
    if (!widget || !out_selected_index || (widget->type != GUI_WIDGET_SELECT && widget->type != GUI_WIDGET_COMBOBOX)) return -1;
    *out_selected_index = widget->value;
    return 0;
}

static void gui_select_activate(gui_widget_t *wg) {
    if (!wg || !wg->enabled || (wg->type != GUI_WIDGET_SELECT && wg->type != GUI_WIDGET_COMBOBOX)) return;
    if (wg->owner) gui_select_close_others(wg->owner, wg);
    wg->step = !wg->step;
    if (wg->min_value < 0) wg->min_value = wg->value;
    gui_invalidate_all();
}

static void gui_select_commit(gui_widget_t *wg, int index) {
    int old_value;
    if (!wg || !wg->enabled) return;
    old_value = wg->value;
    if (gui_select_set_selected(wg, index) == 0) {
        wg->step = 0;
        wg->pressed = 0;
        if (wg->value != old_value) gui_user_post_value_event(wg);
        gui_invalidate_all();
    }
}

static int gui_select_dropdown_index_at(gui_widget_t *wg, int sx, int sy) {
    int ax, ay;
    int idx;
    int count;
    if (!wg || !wg->step) return -1;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return -1;
    count = gui_select_item_count(wg);
    if (count <= 0) return -1;
    if (sx < ax || sx >= ax + wg->rect.w) return -1;
    if (sy < ay + wg->rect.h || sy >= ay + wg->rect.h + count * 20) return -1;
    idx = (sy - (ay + wg->rect.h)) / 20;
    return (idx >= 0 && idx < count) ? idx : -1;
}

static void gui_select_handle_key(gui_widget_t *wg, int key) {
    int count;
    if (!wg || !wg->enabled) return;
    count = gui_select_item_count(wg);
    if (count <= 0) return;
    if (key == GUI_KEY_SPACE || key == GUI_KEY_ENTER) {
        if (wg->step) gui_select_commit(wg, wg->min_value >= 0 ? wg->min_value : wg->value);
        else gui_select_activate(wg);
    } else if (key == GUI_KEY_DOWN) {
        if (!wg->step) wg->step = 1;
        if (wg->min_value < 0) wg->min_value = wg->value;
        if (wg->min_value < count - 1) wg->min_value++;
        gui_invalidate_all();
    } else if (key == GUI_KEY_UP) {
        if (!wg->step) wg->step = 1;
        if (wg->min_value < 0) wg->min_value = wg->value;
        if (wg->min_value > 0) wg->min_value--;
        gui_invalidate_all();
    } else if (key == 27) {
        wg->step = 0;
        gui_invalidate_all();
    }
}


static int gui_listview_item_height(void) { return (int)GUI_CHAR_H + 8; }

static int gui_listview_visible_rows(const gui_widget_t *wg) {
    int rows;
    if (!wg) return 1;
    rows = (wg->rect.h - 4) / gui_listview_item_height();
    return rows > 0 ? rows : 1;
}

static int gui_listview_item_selected(const gui_widget_t *wg, int index) {
    unsigned bit;
    if (!wg || index < 0) return 0;
    if ((wg->label_flags & GUI_LISTVIEW_FLAG_MULTI_SELECT) == 0) return wg->value == index;
    if (index >= 32) return 0;
    bit = 1u << (unsigned)index;
    return (wg->selection_anchor & bit) != 0;
}

static void gui_listview_ensure_visible(gui_widget_t *wg) {
    int count, rows, max_scroll;
    if (!wg || wg->type != GUI_WIDGET_LISTVIEW) return;
    count = gui_select_item_count(wg);
    rows = gui_listview_visible_rows(wg);
    max_scroll = count > rows ? count - rows : 0;
    if (wg->min_value < 0) wg->min_value = 0;
    if (wg->min_value > max_scroll) wg->min_value = max_scroll;
    if (wg->value < 0 && count > 0) wg->value = 0;
    if (wg->value >= count) wg->value = count - 1;
    if (wg->value < wg->min_value) wg->min_value = wg->value;
    if (wg->value >= wg->min_value + rows) wg->min_value = wg->value - rows + 1;
    if (wg->min_value < 0) wg->min_value = 0;
    if (wg->min_value > max_scroll) wg->min_value = max_scroll;
}

int gui_listview_set_items(gui_widget_t *widget, const char *items) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_LISTVIEW) return -1;
    gui_widget_set_placeholder(widget, items ? items : "");
    count = gui_select_item_count(widget);
    widget->max_value = count - 1;
    if (count <= 0) {
        widget->value = -1;
        widget->min_value = 0;
        widget->selection_anchor = 0;
    } else {
        if (widget->value < 0) widget->value = 0;
        if (widget->value >= count) widget->value = count - 1;
        if ((widget->label_flags & GUI_LISTVIEW_FLAG_MULTI_SELECT) == 0) widget->selection_anchor = 0;
        gui_listview_ensure_visible(widget);
    }
    return 0;
}

int gui_listview_set_selected(gui_widget_t *widget, int selected_index) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_LISTVIEW) return -1;
    count = gui_select_item_count(widget);
    if (count <= 0) {
        widget->value = -1;
        widget->selection_anchor = 0;
        return selected_index < 0 ? 0 : -1;
    }
    if (selected_index < 0 || selected_index >= count) return -1;
    widget->value = selected_index;
    if ((widget->label_flags & GUI_LISTVIEW_FLAG_MULTI_SELECT) != 0 && selected_index < 32) widget->selection_anchor |= (1u << (unsigned)selected_index);
    else widget->selection_anchor = 0;
    gui_listview_ensure_visible(widget);
    return 0;
}

int gui_listview_get_selected(gui_widget_t *widget, int *out_selected_index) {
    if (!widget || !out_selected_index || widget->type != GUI_WIDGET_LISTVIEW) return -1;
    *out_selected_index = widget->value;
    return 0;
}

static int gui_listview_index_at(gui_widget_t *wg, int sx, int sy) {
    int ax, ay, local_y, idx, count;
    if (!wg || wg->type != GUI_WIDGET_LISTVIEW) return -1;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return -1;
    if (sx < ax || sx >= ax + wg->rect.w || sy < ay || sy >= ay + wg->rect.h) return -1;
    local_y = sy - ay - 2;
    if (local_y < 0) return -1;
    idx = wg->min_value + local_y / gui_listview_item_height();
    count = gui_select_item_count(wg);
    return (idx >= 0 && idx < count) ? idx : -1;
}

static void gui_listview_select(gui_widget_t *wg, int index, int toggle) {
    int old_value, old_mask, count;
    if (!wg || wg->type != GUI_WIDGET_LISTVIEW || !wg->enabled) return;
    count = gui_select_item_count(wg);
    if (index < 0 || index >= count) return;
    old_value = wg->value;
    old_mask = (int)wg->selection_anchor;
    wg->value = index;
    if ((wg->label_flags & GUI_LISTVIEW_FLAG_MULTI_SELECT) != 0 && index < 32) {
        unsigned bit = 1u << (unsigned)index;
        if (toggle) wg->selection_anchor ^= bit;
        else wg->selection_anchor |= bit;
    } else {
        wg->selection_anchor = 0;
    }
    gui_listview_ensure_visible(wg);
    if (wg->value != old_value || (int)wg->selection_anchor != old_mask) gui_user_post_value_event(wg);
    gui_invalidate_all();
}

static int gui_listview_scroll(gui_widget_t *wg, int delta_rows) {
    int count, rows, max_scroll, next;
    if (!wg || wg->type != GUI_WIDGET_LISTVIEW || delta_rows == 0) return 0;
    count = gui_select_item_count(wg);
    rows = gui_listview_visible_rows(wg);
    max_scroll = count > rows ? count - rows : 0;
    next = wg->min_value + delta_rows;
    if (next < 0) next = 0;
    if (next > max_scroll) next = max_scroll;
    if (next == wg->min_value) return 0;
    wg->min_value = next;
    gui_invalidate_all();
    return 1;
}

static void gui_listview_handle_key(gui_widget_t *wg, int key) {
    int count, rows, next;
    if (!wg || wg->type != GUI_WIDGET_LISTVIEW || !wg->enabled) return;
    count = gui_select_item_count(wg);
    if (count <= 0) return;
    rows = gui_listview_visible_rows(wg);
    next = wg->value < 0 ? 0 : wg->value;
    if (key == GUI_KEY_UP) next--;
    else if (key == GUI_KEY_DOWN) next++;
    else if (key == GUI_KEY_HOME) next = 0;
    else if (key == GUI_KEY_END) next = count - 1;
    else if (key == GUI_KEY_SPACE || key == GUI_KEY_ENTER) { gui_listview_select(wg, next, (wg->label_flags & GUI_LISTVIEW_FLAG_MULTI_SELECT) != 0); return; }
    else if (key == GUI_KEY_LEFT) next -= rows;
    else if (key == GUI_KEY_RIGHT) next += rows;
    else return;
    if (next < 0) next = 0;
    if (next >= count) next = count - 1;
    gui_listview_select(wg, next, 0);
}

static int gui_iconview_item_width(const gui_widget_t *wg) {
    if (wg && (wg->label_flags & GUI_ICONVIEW_LIST_MODE)) return wg->rect.w > 12 ? wg->rect.w - 8 : 56;
    return (wg && (wg->label_flags & GUI_ICONVIEW_COMPACT)) ? 48 : 64;
}

static int gui_iconview_item_height(const gui_widget_t *wg) {
    if (wg && (wg->label_flags & GUI_ICONVIEW_LIST_MODE)) return 28;
    if (wg && (wg->label_flags & GUI_ICONVIEW_COMPACT)) return 48;
    return (wg && (wg->label_flags & GUI_ICONVIEW_SHOW_LABELS)) ? 64 : 44;
}

static int gui_iconview_columns(const gui_widget_t *wg) {
    int cell_w;
    if (!wg) return 1;
    if (wg->label_flags & GUI_ICONVIEW_LIST_MODE) return 1;
    cell_w = gui_iconview_item_width(wg);
    if (cell_w <= 0) return 1;
    return (wg->rect.w - 4) / cell_w > 0 ? (wg->rect.w - 4) / cell_w : 1;
}

static int gui_iconview_visible_rows(const gui_widget_t *wg) {
    int cell_h;
    if (!wg) return 1;
    cell_h = gui_iconview_item_height(wg);
    if (cell_h <= 0) return 1;
    return (wg->rect.h - 4) / cell_h > 0 ? (wg->rect.h - 4) / cell_h : 1;
}

static int gui_iconview_count(const gui_widget_t *wg) { return gui_select_item_count(wg); }

static gui_icon_id_t gui_iconview_icon_for_index(int index) {
    static const gui_icon_id_t icons[] = {
        GUI_ICON_FOLDER, GUI_ICON_FILE_GENERIC, GUI_ICON_FILE_TEXT, GUI_ICON_FILE_CODE,
        GUI_ICON_FILE_CONFIG, GUI_ICON_FILE_IMAGE, GUI_ICON_FILE_ARCHIVE, GUI_ICON_FILE_EXEC
    };
    if (index < 0) index = 0;
    return icons[index % (int)(sizeof(icons) / sizeof(icons[0]))];
}

static void gui_iconview_label_for_index(gui_widget_t *wg, int index, char *out, int out_size) {
    if (!out || out_size <= 0) return;
    out[0] = 0;
    if (gui_select_item_text(wg, index, out, out_size) < 0) return;
    if (out[0] >= '0' && out[0] <= '9') {
        int i = 0;
        while (out[i] >= '0' && out[i] <= '9') i++;
        if (out[i] == ':' || out[i] == '|' || out[i] == ',') {
            int j = i + 1;
            int k = 0;
            while (out[j] && k < out_size - 1) out[k++] = out[j++];
            out[k] = 0;
        }
    }
}

static gui_icon_id_t gui_iconview_parse_icon(gui_widget_t *wg, int index) {
    char item[GUI_WIDGET_TEXT_CAP];
    int i = 0;
    int icon = 0;
    if (gui_select_item_text(wg, index, item, sizeof(item)) < 0) return gui_iconview_icon_for_index(index);
    while (item[i] >= '0' && item[i] <= '9') { icon = icon * 10 + (item[i] - '0'); i++; }
    if ((item[i] == ':' || item[i] == '|' || item[i] == ',') && icon >= 0 && icon < GUI_ICON_COUNT) return (gui_icon_id_t)icon;
    return gui_iconview_icon_for_index(index);
}

static void gui_iconview_ensure_visible(gui_widget_t *wg) {
    int count, cols, rows, total_rows, row, max_scroll;
    if (!wg || wg->type != GUI_WIDGET_ICONVIEW) return;
    count = gui_iconview_count(wg);
    cols = gui_iconview_columns(wg);
    rows = gui_iconview_visible_rows(wg);
    total_rows = (count + cols - 1) / cols;
    max_scroll = total_rows > rows ? total_rows - rows : 0;
    if (wg->min_value < 0) wg->min_value = 0;
    if (wg->min_value > max_scroll) wg->min_value = max_scroll;
    if (count <= 0) { wg->value = -1; wg->min_value = 0; return; }
    if (wg->value < 0) wg->value = 0;
    if (wg->value >= count) wg->value = count - 1;
    row = wg->value / cols;
    if (row < wg->min_value) wg->min_value = row;
    if (row >= wg->min_value + rows) wg->min_value = row - rows + 1;
    if (wg->min_value < 0) wg->min_value = 0;
    if (wg->min_value > max_scroll) wg->min_value = max_scroll;
}

int gui_iconview_set_items(gui_widget_t *widget, const char *items) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_ICONVIEW) return -1;
    gui_widget_set_placeholder(widget, items ? items : "");
    count = gui_iconview_count(widget);
    widget->max_value = count - 1;
    if (count <= 0) { widget->value = -1; widget->min_value = 0; }
    else { if (widget->value < 0) widget->value = 0; if (widget->value >= count) widget->value = count - 1; }
    gui_iconview_ensure_visible(widget);
    return 0;
}

int gui_iconview_set_selected(gui_widget_t *widget, int selected_index) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_ICONVIEW) return -1;
    count = gui_iconview_count(widget);
    if (count <= 0) { widget->value = -1; return selected_index < 0 ? 0 : -1; }
    if (selected_index < 0 || selected_index >= count) return -1;
    widget->value = selected_index;
    gui_iconview_ensure_visible(widget);
    return 0;
}

int gui_iconview_get_selected(gui_widget_t *widget, int *out_selected_index) {
    if (!widget || !out_selected_index || widget->type != GUI_WIDGET_ICONVIEW) return -1;
    *out_selected_index = widget->value;
    return 0;
}

static int gui_iconview_index_at(gui_widget_t *wg, int sx, int sy) {
    int ax, ay, local_x, local_y, col, row, idx, cols, count;
    if (!wg || wg->type != GUI_WIDGET_ICONVIEW) return -1;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return -1;
    if (sx < ax + 2 || sx >= ax + wg->rect.w - 2 || sy < ay + 2 || sy >= ay + wg->rect.h - 2) return -1;
    local_x = sx - ax - 2;
    local_y = sy - ay - 2;
    cols = gui_iconview_columns(wg);
    col = (wg->label_flags & GUI_ICONVIEW_LIST_MODE) ? 0 : local_x / gui_iconview_item_width(wg);
    row = local_y / gui_iconview_item_height(wg) + wg->min_value;
    idx = row * cols + col;
    count = gui_iconview_count(wg);
    return (idx >= 0 && idx < count) ? idx : -1;
}

static void gui_iconview_select(gui_widget_t *wg, int index) {
    int old_value;
    if (!wg || wg->type != GUI_WIDGET_ICONVIEW || !wg->enabled) return;
    if (index < 0 || index >= gui_iconview_count(wg)) return;
    old_value = wg->value;
    wg->value = index;
    gui_iconview_ensure_visible(wg);
    if (old_value != wg->value) gui_user_post_value_event(wg);
    gui_invalidate_all();
}

static int gui_iconview_scroll(gui_widget_t *wg, int delta_rows) {
    int count, cols, rows, total_rows, max_scroll, next;
    if (!wg || wg->type != GUI_WIDGET_ICONVIEW || delta_rows == 0) return 0;
    count = gui_iconview_count(wg);
    cols = gui_iconview_columns(wg);
    rows = gui_iconview_visible_rows(wg);
    total_rows = (count + cols - 1) / cols;
    max_scroll = total_rows > rows ? total_rows - rows : 0;
    next = wg->min_value + delta_rows;
    if (next < 0) next = 0;
    if (next > max_scroll) next = max_scroll;
    if (next == wg->min_value) return 0;
    wg->min_value = next;
    gui_invalidate_all();
    return 1;
}

static void gui_iconview_handle_key(gui_widget_t *wg, int key) {
    int count, cols, next;
    if (!wg || wg->type != GUI_WIDGET_ICONVIEW || !wg->enabled) return;
    count = gui_iconview_count(wg);
    if (count <= 0) return;
    cols = gui_iconview_columns(wg);
    next = wg->value < 0 ? 0 : wg->value;
    if (key == GUI_KEY_UP) next -= cols;
    else if (key == GUI_KEY_DOWN) next += cols;
    else if (key == GUI_KEY_LEFT) next--;
    else if (key == GUI_KEY_RIGHT) next++;
    else if (key == GUI_KEY_HOME) next = 0;
    else if (key == GUI_KEY_END) next = count - 1;
    else if (key == GUI_KEY_SPACE || key == GUI_KEY_ENTER) { gui_iconview_select(wg, next); return; }
    else return;
    if (next < 0) next = 0;
    if (next >= count) next = count - 1;
    gui_iconview_select(wg, next);
}

static int gui_tableview_row_height(void) { return (int)GUI_CHAR_H + 8; }
static int gui_tableview_header_height(const gui_widget_t *wg) { return (wg && (wg->label_flags & GUI_TABLEVIEW_FLAG_SHOW_HEADER)) ? gui_tableview_row_height() : 0; }

static int gui_tableview_count_lines(const char *text) {
    int count = 0;
    int has = 0;
    const char *p = text;
    if (!p || !*p) return 0;
    while (*p) {
        if (*p == '\r') { p++; continue; }
        if (*p == '\n') { if (has) count++; has = 0; p++; continue; }
        has = 1;
        p++;
    }
    if (has) count++;
    return count;
}

static int gui_tableview_is_separator(char ch) { return ch == '|' || ch == ','; }

static int gui_tableview_col_count(const gui_widget_t *wg) {
    const char *ptext;
    int count = 1;
    if (!wg || !wg->text[0]) return 1;
    for (ptext = wg->text; *ptext; ++ptext) if (gui_tableview_is_separator(*ptext)) count++;
    if (count < 1) count = 1;
    if (count > 8) count = 8;
    return count;
}

static int gui_tableview_row_count(const gui_widget_t *wg) {
    if (!wg) return 0;
    return gui_tableview_count_lines(wg->placeholder);
}

static int gui_tableview_visible_rows(const gui_widget_t *wg) {
    int rows;
    int header = gui_tableview_header_height(wg);
    if (!wg) return 1;
    rows = (wg->rect.h - header - 4) / gui_tableview_row_height();
    return rows > 0 ? rows : 1;
}

static void gui_tableview_ensure_visible(gui_widget_t *wg) {
    int rows, vis;
    if (!wg || wg->type != GUI_WIDGET_TABLEVIEW) return;
    rows = gui_tableview_row_count(wg);
    vis = gui_tableview_visible_rows(wg);
    if (wg->min_value < 0) wg->min_value = 0;
    if (wg->value >= 0) {
        if (wg->value < wg->min_value) wg->min_value = wg->value;
        if (wg->value >= wg->min_value + vis) wg->min_value = wg->value - vis + 1;
    }
    if (rows > vis && wg->min_value > rows - vis) wg->min_value = rows - vis;
    if (wg->min_value < 0) wg->min_value = 0;
}

static void gui_tableview_copy_line(const char *text, int row, char *out, uint32_t out_size) {
    const char *ptext = text;
    uint32_t len = 0;
    int cur = 0;
    if (!out || out_size == 0) return;
    out[0] = 0;
    if (!ptext || row < 0) return;
    while (*ptext && cur < row) { if (*ptext == '\n') cur++; ptext++; }
    if (cur != row) return;
    while (*ptext && *ptext != '\n' && len + 1 < out_size) {
        if (*ptext != '\r') out[len++] = *ptext;
        ptext++;
    }
    out[len] = 0;
}

static void gui_tableview_cell_text_ex(const char *line, int col, char *out, uint32_t out_size, int strip_width) {
    const char *ptext = line;
    uint32_t len = 0;
    int cur = 0;
    if (!out || out_size == 0) return;
    out[0] = 0;
    if (!ptext || col < 0) return;
    while (*ptext && cur < col) { if (gui_tableview_is_separator(*ptext)) cur++; ptext++; }
    if (cur != col) return;
    while (*ptext && !gui_tableview_is_separator(*ptext) && len + 1 < out_size) out[len++] = *ptext++;
    out[len] = 0;
    if (strip_width) {
        int i = (int)len - 1;
        while (i >= 0 && out[i] >= '0' && out[i] <= '9') i--;
        if (i >= 0 && out[i] == ':' && i + 1 < (int)len) out[i] = 0;
    }
}

static void gui_tableview_cell_text(const char *line, int col, char *out, uint32_t out_size) {
    gui_tableview_cell_text_ex(line, col, out, out_size, 0);
}

static int gui_tableview_column_width(const gui_widget_t *wg, int col, int fallback) {
    char cell[64];
    int len, i, width = 0, mul = 1;
    if (!wg || col < 0) return fallback;
    gui_tableview_cell_text_ex(wg->text, col, cell, sizeof(cell), 0);
    len = (int)strlen(cell);
    i = len - 1;
    while (i >= 0 && cell[i] >= '0' && cell[i] <= '9') i--;
    if (i < 0 || cell[i] != ':' || i + 1 >= len) return fallback;
    for (i = len - 1; i >= 0 && cell[i] >= '0' && cell[i] <= '9'; --i) {
        width += (cell[i] - '0') * mul;
        mul *= 10;
    }
    if (width < 24) width = 24;
    if (width > wg->rect.w - 12) width = wg->rect.w - 12;
    return width;
}

static int gui_tableview_column_at(gui_widget_t *wg, int sx, int sy) {
    int ax, ay, cols, c, x, fallback;
    if (!wg || wg->type != GUI_WIDGET_TABLEVIEW) return -1;
    if (!(wg->label_flags & GUI_TABLEVIEW_FLAG_SHOW_HEADER)) return -1;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return -1;
    if (sy < ay + 1 || sy >= ay + gui_tableview_header_height(wg)) return -1;
    if (sx < ax || sx >= ax + wg->rect.w) return -1;
    cols = gui_tableview_col_count(wg);
    fallback = cols > 0 ? (wg->rect.w - 8) / cols : wg->rect.w - 8;
    if (fallback < 24) fallback = 24;
    x = ax + 3;
    for (c = 0; c < cols; ++c) {
        int cw = gui_tableview_column_width(wg, c, fallback);
        if (sx >= x && sx < x + cw) return c;
        x += cw;
    }
    return -1;
}

static int gui_tableview_cmp_column(const char *a, const char *b, int col, int ascending) {
    char ca[64], cb[64];
    int r;
    gui_tableview_cell_text(a, col, ca, sizeof(ca));
    gui_tableview_cell_text(b, col, cb, sizeof(cb));
    r = strcmp(ca, cb);
    return ascending ? r : -r;
}

static void gui_tableview_append(char *out, uint32_t out_size, const char *text) {
    uint32_t len;
    uint32_t i = 0;
    if (!out || out_size == 0 || !text) return;
    len = (uint32_t)strlen(out);
    while (len + 1 < out_size && text[i]) out[len++] = text[i++];
    out[len] = 0;
}

static void gui_tableview_sort(gui_widget_t *wg, int col) {
    char rows[32][128];
    char out[GUI_WIDGET_TEXT_CAP];
    int count, i, j;
    if (!wg || wg->type != GUI_WIDGET_TABLEVIEW || col < 0) return;
    count = gui_tableview_row_count(wg);
    if (count <= 1) return;
    if (count > 32) count = 32;
    if (wg->table_sort_column == col) wg->table_sort_ascending = !wg->table_sort_ascending;
    else { wg->table_sort_column = col; wg->table_sort_ascending = 1; }
    for (i = 0; i < count; ++i) gui_tableview_copy_line(wg->placeholder, i, rows[i], sizeof(rows[i]));
    for (i = 0; i < count - 1; ++i) {
        for (j = i + 1; j < count; ++j) {
            if (gui_tableview_cmp_column(rows[i], rows[j], col, wg->table_sort_ascending) > 0) {
                char tmp[128];
                strcpy(tmp, rows[i]);
                strcpy(rows[i], rows[j]);
                strcpy(rows[j], tmp);
            }
        }
    }
    out[0] = 0;
    for (i = 0; i < count; ++i) {
        if (i > 0) gui_tableview_append(out, sizeof(out), "\n");
        gui_tableview_append(out, sizeof(out), rows[i]);
    }
    gui_widget_set_placeholder(wg, out);
    wg->value = count > 0 ? 0 : -1;
    wg->min_value = 0;
    gui_invalidate_all();
}

static int gui_tableview_index_at(gui_widget_t *wg, int sx, int sy) {
    int ax, ay, header, row, count;
    if (!wg || wg->type != GUI_WIDGET_TABLEVIEW) return -1;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return -1;
    header = gui_tableview_header_height(wg);
    if (sx < ax || sx >= ax + wg->rect.w) return -1;
    if (sy < ay + header + 2 || sy >= ay + wg->rect.h - 2) return -1;
    row = (sy - (ay + header + 2)) / gui_tableview_row_height() + wg->min_value;
    count = gui_tableview_row_count(wg);
    return (row >= 0 && row < count) ? row : -1;
}

static void gui_tableview_select(gui_widget_t *wg, int row) {
    int old_value, count;
    if (!wg || wg->type != GUI_WIDGET_TABLEVIEW || !wg->enabled) return;
    count = gui_tableview_row_count(wg);
    if (row < 0 || row >= count) return;
    old_value = wg->value;
    wg->value = row;
    gui_tableview_ensure_visible(wg);
    if (old_value != wg->value) gui_user_post_value_event(wg);
    gui_invalidate_all();
}

static int gui_tableview_scroll(gui_widget_t *wg, int delta_rows) {
    int rows, vis, max_first, next;
    if (!wg || wg->type != GUI_WIDGET_TABLEVIEW || delta_rows == 0) return 0;
    rows = gui_tableview_row_count(wg);
    vis = gui_tableview_visible_rows(wg);
    max_first = rows > vis ? rows - vis : 0;
    next = wg->min_value + delta_rows;
    if (next < 0) next = 0;
    if (next > max_first) next = max_first;
    if (next == wg->min_value) return 0;
    wg->min_value = next;
    gui_invalidate_all();
    return 1;
}

static void gui_tableview_handle_key(gui_widget_t *wg, int key) {
    int count, rows, next;
    if (!wg || wg->type != GUI_WIDGET_TABLEVIEW || !wg->enabled) return;
    count = gui_tableview_row_count(wg);
    if (count <= 0) return;
    rows = gui_tableview_visible_rows(wg);
    next = wg->value < 0 ? 0 : wg->value;
    if (key == GUI_KEY_UP) next--;
    else if (key == GUI_KEY_DOWN) next++;
    else if (key == GUI_KEY_HOME) next = 0;
    else if (key == GUI_KEY_END) next = count - 1;
    else if (key == GUI_KEY_LEFT) next -= rows;
    else if (key == GUI_KEY_RIGHT) next += rows;
    else return;
    if (next < 0) next = 0;
    if (next >= count) next = count - 1;
    gui_tableview_select(wg, next);
}

int gui_tableview_set_rows(gui_widget_t *widget, const char *rows) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_TABLEVIEW) return -1;
    gui_widget_set_placeholder(widget, rows ? rows : "");
    count = gui_tableview_row_count(widget);
    widget->max_value = count - 1;
    if (count <= 0) { widget->value = -1; widget->min_value = 0; }
    else {
        if (widget->value < 0) widget->value = 0;
        if (widget->value >= count) widget->value = count - 1;
        gui_tableview_ensure_visible(widget);
    }
    return 0;
}

int gui_tableview_set_selected(gui_widget_t *widget, int selected_row) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_TABLEVIEW) return -1;
    count = gui_tableview_row_count(widget);
    if (count <= 0) { widget->value = -1; return selected_row < 0 ? 0 : -1; }
    if (selected_row < 0 || selected_row >= count) return -1;
    widget->value = selected_row;
    gui_tableview_ensure_visible(widget);
    return 0;
}

int gui_tableview_get_selected(gui_widget_t *widget, int *out_selected_row) {
    if (!widget || !out_selected_row || widget->type != GUI_WIDGET_TABLEVIEW) return -1;
    *out_selected_row = widget->value;
    return 0;
}



static int gui_menubar_item_count(const gui_widget_t *wg) {
    int count = 0;
    int has = 0;
    const char *ptext;
    if (!wg || !wg->text[0]) return 0;
    for (ptext = wg->text; *ptext; ++ptext) {
        if (*ptext == '|' || *ptext == '\n') {
            if (has) count++;
            has = 0;
        } else if (*ptext != '\r') {
            has = 1;
        }
    }
    if (has) count++;
    return count;
}

static void gui_menubar_item_text(const gui_widget_t *wg, int index, char *label, uint32_t label_size, char *shortcut, uint32_t shortcut_size) {
    const char *ptext;
    uint32_t len = 0;
    int cur = 0;
    int in_shortcut = 0;
    char *out = label;
    uint32_t out_size = label_size;
    if (label && label_size) label[0] = 0;
    if (shortcut && shortcut_size) shortcut[0] = 0;
    if (!wg || index < 0 || !label || label_size == 0) return;
    ptext = wg->text;
    while (*ptext && cur < index) {
        if (*ptext == '|' || *ptext == '\n') cur++;
        ptext++;
    }
    if (cur != index) return;
    while (*ptext == ' ' || *ptext == '\t') ptext++;
    while (*ptext && *ptext != '|' && *ptext != '\n' && *ptext != '\r') {
        if (*ptext == ':' && shortcut && shortcut_size) {
            if (out && out_size) out[len] = 0;
            out = shortcut;
            out_size = shortcut_size;
            len = 0;
            in_shortcut = 1;
            ptext++;
            while (*ptext == ' ' || *ptext == '\t') ptext++;
            continue;
        }
        if (len + 1 < out_size) out[len++] = *ptext;
        ptext++;
    }
    if (out && out_size) out[len] = 0;
    if (!in_shortcut && shortcut && shortcut_size) shortcut[0] = 0;
}

static int gui_menubar_item_width(const gui_widget_t *wg, int index) {
    char label[64];
    char shortcut[64];
    int width;
    gui_menubar_item_text(wg, index, label, sizeof(label), shortcut, sizeof(shortcut));
    width = 18 + (int)font_measure_text_width(font_get_default(), label);
    if (shortcut[0]) width += 10 + (int)font_measure_text_width(font_get_default(), shortcut);
    if (width < 48) width = 48;
    return width;
}

static int gui_menubar_index_at(gui_widget_t *wg, int sx, int sy) {
    int ax, ay, count, i, x;
    if (!wg || wg->type != GUI_WIDGET_MENUBAR) return -1;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return -1;
    if (sx < ax || sx >= ax + wg->rect.w || sy < ay || sy >= ay + wg->rect.h) return -1;
    count = gui_menubar_item_count(wg);
    x = ax + 4;
    for (i = 0; i < count; ++i) {
        int iw = gui_menubar_item_width(wg, i);
        if (sx >= x && sx < x + iw) return i;
        x += iw;
    }
    return -1;
}

static void gui_menubar_activate(gui_widget_t *wg, int index) {
    int old_value, count;
    if (!wg || wg->type != GUI_WIDGET_MENUBAR || !wg->enabled) return;
    count = gui_menubar_item_count(wg);
    if (count <= 0 || index < 0 || index >= count) return;
    old_value = wg->value;
    wg->value = index;
    wg->pressed = 0;
    if (old_value != wg->value) gui_user_post_value_event(wg);
    if (wg->on_click) wg->on_click(wg, wg->user_data);
    gui_invalidate_all();
}

static void gui_menubar_handle_key(gui_widget_t *wg, int key) {
    int count, next;
    if (!wg || wg->type != GUI_WIDGET_MENUBAR || !wg->enabled) return;
    count = gui_menubar_item_count(wg);
    if (count <= 0) return;
    next = wg->value < 0 ? 0 : wg->value;
    if (key == GUI_KEY_LEFT) next--;
    else if (key == GUI_KEY_RIGHT || key == GUI_KEY_TAB) next++;
    else if (key == GUI_KEY_HOME) next = 0;
    else if (key == GUI_KEY_END) next = count - 1;
    else if (key == GUI_KEY_ENTER || key == GUI_KEY_SPACE) { gui_menubar_activate(wg, next); return; }
    else return;
    if (next < 0) next = count - 1;
    if (next >= count) next = 0;
    gui_menubar_activate(wg, next);
}

int gui_menubar_set_menus(gui_widget_t *widget, const char *menus) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_MENUBAR) return -1;
    gui_widget_set_text(widget, menus ? menus : "");
    count = gui_menubar_item_count(widget);
    widget->max_value = count - 1;
    if (count <= 0) widget->value = -1;
    else if (widget->value < 0 || widget->value >= count) widget->value = 0;
    gui_invalidate_all();
    return 0;
}

int gui_menubar_set_active(gui_widget_t *widget, int active_index) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_MENUBAR) return -1;
    count = gui_menubar_item_count(widget);
    if (count <= 0) { widget->value = -1; return active_index < 0 ? 0 : -1; }
    if (active_index < 0 || active_index >= count) return -1;
    widget->value = active_index;
    gui_invalidate_all();
    return 0;
}

int gui_menubar_get_active(gui_widget_t *widget, int *out_active_index) {
    if (!widget || !out_active_index || widget->type != GUI_WIDGET_MENUBAR) return -1;
    *out_active_index = widget->value;
    return 0;
}


int gui_contextmenu_row_height(void) { return gui_text_line_height_px() + 8; }

static int gui_contextmenu_index_at(gui_widget_t *wg, int sx, int sy) {
    int ax, ay, row;
    if (!wg || wg->type != GUI_WIDGET_CONTEXTMENU || !wg->visible) return -1;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return -1;
    if (sx < ax || sx >= ax + wg->rect.w || sy < ay || sy >= ay + wg->rect.h) return -1;
    row = (sy - ay - 2) / gui_contextmenu_row_height();
    if (row < 0 || row >= gui_menubar_item_count(wg)) return -1;
    return row;
}

static int gui_contextmenu_item_disabled(gui_widget_t *wg, int index) {
    if (!wg || index < 0 || index >= 32) return 0;
    return (wg->label_flags & (1u << index)) != 0;
}

static void gui_contextmenu_activate(gui_widget_t *wg, int index) {
    int old_value, count;
    if (!wg || wg->type != GUI_WIDGET_CONTEXTMENU || !wg->enabled || !wg->visible) return;
    count = gui_menubar_item_count(wg);
    if (count <= 0 || index < 0 || index >= count) return;
    if (gui_contextmenu_item_disabled(wg, index)) return;
    old_value = wg->value;
    wg->value = index;
    wg->pressed = 0;
    wg->visible = 0;
    if (old_value != wg->value) gui_user_post_value_event(wg);
    if (wg->on_click) wg->on_click(wg, wg->user_data);
    gui_invalidate_all();
}

static void gui_contextmenu_handle_key(gui_widget_t *wg, int key) {
    int count, next, guard;
    if (!wg || wg->type != GUI_WIDGET_CONTEXTMENU || !wg->enabled || !wg->visible) return;
    count = gui_menubar_item_count(wg);
    if (count <= 0) return;
    next = wg->value < 0 ? 0 : wg->value;
    if (key == 27) { gui_contextmenu_hide(wg); return; }
    if (key == GUI_KEY_UP) next--;
    else if (key == GUI_KEY_DOWN || key == GUI_KEY_TAB) next++;
    else if (key == GUI_KEY_HOME) next = 0;
    else if (key == GUI_KEY_END) next = count - 1;
    else if (key == GUI_KEY_ENTER || key == GUI_KEY_SPACE) { gui_contextmenu_activate(wg, next); return; }
    else return;
    if (next < 0) next = count - 1;
    if (next >= count) next = 0;
    for (guard = 0; guard < count && gui_contextmenu_item_disabled(wg, next); ++guard) {
        next += (key == GUI_KEY_UP) ? -1 : 1;
        if (next < 0) next = count - 1;
        if (next >= count) next = 0;
    }
    if (!gui_contextmenu_item_disabled(wg, next)) wg->value = next;
    gui_invalidate_all();
}

int gui_contextmenu_set_items(gui_widget_t *widget, const char *items) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_CONTEXTMENU) return -1;
    gui_widget_set_text(widget, items ? items : "");
    count = gui_menubar_item_count(widget);
    widget->max_value = count - 1;
    if (count <= 0) widget->value = -1;
    else if (widget->value < 0 || widget->value >= count || gui_contextmenu_item_disabled(widget, widget->value)) widget->value = 0;
    gui_invalidate_all();
    return 0;
}

int gui_contextmenu_set_selected(gui_widget_t *widget, int selected_index) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_CONTEXTMENU) return -1;
    count = gui_menubar_item_count(widget);
    if (count <= 0) { widget->value = -1; return selected_index < 0 ? 0 : -1; }
    if (selected_index < 0 || selected_index >= count || gui_contextmenu_item_disabled(widget, selected_index)) return -1;
    widget->value = selected_index;
    gui_invalidate_all();
    return 0;
}

int gui_contextmenu_get_selected(gui_widget_t *widget, int *out_selected_index) {
    if (!widget || !out_selected_index || widget->type != GUI_WIDGET_CONTEXTMENU) return -1;
    *out_selected_index = widget->value;
    return 0;
}

int gui_contextmenu_set_disabled_mask(gui_widget_t *widget, uint32_t disabled_mask) {
    if (!widget || widget->type != GUI_WIDGET_CONTEXTMENU) return -1;
    widget->label_flags = disabled_mask;
    if (gui_contextmenu_item_disabled(widget, widget->value)) widget->value = -1;
    gui_invalidate_all();
    return 0;
}

int gui_contextmenu_show(gui_widget_t *widget, int x, int y) {
    int count, h, max_x, max_y;
    if (!widget || widget->type != GUI_WIDGET_CONTEXTMENU || !widget->owner) return -1;
    count = gui_menubar_item_count(widget);
    h = count * gui_contextmenu_row_height() + 4;
    if (h < 24) h = 24;
    widget->rect.x = x;
    widget->rect.y = y;
    widget->rect.h = h;
    max_x = widget->owner->rect.w - GUI_BORDER_SIZE * 2 - widget->rect.w;
    max_y = widget->owner->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE - widget->rect.h;
    if (widget->rect.x > max_x) widget->rect.x = max_x;
    if (widget->rect.y > max_y) widget->rect.y = max_y;
    if (widget->rect.x < 0) widget->rect.x = 0;
    if (widget->rect.y < 0) widget->rect.y = 0;
    widget->visible = 1;
    gui_set_focused_widget(widget);
    gui_invalidate_all();
    return 0;
}

int gui_contextmenu_hide(gui_widget_t *widget) {
    if (!widget || widget->type != GUI_WIDGET_CONTEXTMENU) return -1;
    widget->visible = 0;
    if (g_gui.focused_widget == widget) gui_set_focused_widget(0);
    gui_invalidate_all();
    return 0;
}

static int gui_treeview_row_height(void) { return (int)GUI_CHAR_H + 8; }
static int gui_treeview_node_count(const gui_widget_t *wg) { return wg ? gui_tableview_count_lines(wg->placeholder) : 0; }
static int gui_treeview_visible_rows(const gui_widget_t *wg) {
    int rows;
    if (!wg) return 1;
    rows = (wg->rect.h - 4) / gui_treeview_row_height();
    return rows > 0 ? rows : 1;
}
static void gui_treeview_copy_line(const char *text, int row, char *out, uint32_t out_size) { gui_tableview_copy_line(text, row, out, out_size); }

static int gui_treeview_parse_node(const char *line, int *out_level, int *out_folder, int *out_expanded, const char **out_label) {
    const char *ptext = line ? line : "";
    int level = 0, folder = 0, expanded = 0;
    while (*ptext == '>' || *ptext == ' ' || *ptext == '\t') { if (*ptext == '>' || *ptext == '\t') level++; ptext++; }
    if (*ptext == '-') { folder = 1; expanded = 1; ptext++; }
    else if (*ptext == '+') { folder = 1; expanded = 0; ptext++; }
    if (*ptext == 'F' && ptext[1] == ':') { folder = 1; ptext += 2; }
    else if (*ptext == 'L' && ptext[1] == ':') { folder = 0; ptext += 2; }
    while (*ptext == ' ') ptext++;
    if (out_level) *out_level = level;
    if (out_folder) *out_folder = folder;
    if (out_expanded) *out_expanded = expanded;
    if (out_label) *out_label = ptext;
    return 0;
}

static int gui_treeview_node_level(gui_widget_t *wg, int row) {
    char line[GUI_WIDGET_TEXT_CAP];
    int level = 0;
    gui_treeview_copy_line(wg ? wg->placeholder : "", row, line, sizeof(line));
    gui_treeview_parse_node(line, &level, NULL, NULL, NULL);
    return level;
}

static int gui_treeview_line_is_visible(gui_widget_t *wg, int row) {
    int i, level;
    if (!wg || row < 0) return 0;
    level = gui_treeview_node_level(wg, row);
    for (i = row - 1; i >= 0; --i) {
        char line[GUI_WIDGET_TEXT_CAP];
        int parent_level, folder, expanded;
        gui_treeview_copy_line(wg->placeholder, i, line, sizeof(line));
        gui_treeview_parse_node(line, &parent_level, &folder, &expanded, NULL);
        if (parent_level < level) {
            if (folder && !expanded) return 0;
            level = parent_level;
        }
    }
    return 1;
}

static int gui_treeview_visible_count(gui_widget_t *wg) {
    int count, i, vis = 0;
    if (!wg) return 0;
    count = gui_treeview_node_count(wg);
    for (i = 0; i < count; ++i) if (gui_treeview_line_is_visible(wg, i)) vis++;
    return vis;
}

static int gui_treeview_nth_visible(gui_widget_t *wg, int nth) {
    int count, i, vis = 0;
    if (!wg || nth < 0) return -1;
    count = gui_treeview_node_count(wg);
    for (i = 0; i < count; ++i) {
        if (!gui_treeview_line_is_visible(wg, i)) continue;
        if (vis == nth) return i;
        vis++;
    }
    return -1;
}

static int gui_treeview_visible_position(gui_widget_t *wg, int row) {
    int count, i, vis = 0;
    if (!wg || row < 0) return -1;
    count = gui_treeview_node_count(wg);
    for (i = 0; i < count; ++i) {
        if (!gui_treeview_line_is_visible(wg, i)) continue;
        if (i == row) return vis;
        vis++;
    }
    return -1;
}

static void gui_treeview_ensure_visible(gui_widget_t *wg) {
    int pos, rows, total;
    if (!wg || wg->type != GUI_WIDGET_TREEVIEW) return;
    rows = gui_treeview_visible_rows(wg);
    total = gui_treeview_visible_count(wg);
    pos = gui_treeview_visible_position(wg, wg->value);
    if (wg->min_value < 0) wg->min_value = 0;
    if (pos >= 0) {
        if (pos < wg->min_value) wg->min_value = pos;
        if (pos >= wg->min_value + rows) wg->min_value = pos - rows + 1;
    }
    if (total > rows && wg->min_value > total - rows) wg->min_value = total - rows;
    if (wg->min_value < 0) wg->min_value = 0;
}

static int gui_treeview_index_at(gui_widget_t *wg, int sx, int sy) {
    int ax, ay, row_pos;
    if (!wg || wg->type != GUI_WIDGET_TREEVIEW) return -1;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return -1;
    if (sx < ax || sx >= ax + wg->rect.w || sy < ay + 2 || sy >= ay + wg->rect.h - 2) return -1;
    row_pos = (sy - (ay + 2)) / gui_treeview_row_height() + wg->min_value;
    return gui_treeview_nth_visible(wg, row_pos);
}

static void gui_treeview_set_line_expanded(gui_widget_t *wg, int row, int expanded) {
    char out[GUI_WIDGET_TEXT_CAP];
    int count, i;
    if (!wg || row < 0) return;
    count = gui_treeview_node_count(wg);
    out[0] = 0;
    for (i = 0; i < count; ++i) {
        char line[GUI_WIDGET_TEXT_CAP];
        gui_treeview_copy_line(wg->placeholder, i, line, sizeof(line));
        if (i == row) {
            uint32_t j = 0;
            while (line[j] == '>' || line[j] == ' ' || line[j] == '\t') j++;
            if (line[j] == '+' || line[j] == '-') line[j] = expanded ? '-' : '+';
            else continue;
        }
        if (i > 0) gui_tableview_append(out, sizeof(out), "\n");
        gui_tableview_append(out, sizeof(out), line);
    }
    gui_widget_set_placeholder(wg, out);
}

static void gui_treeview_select(gui_widget_t *wg, int row) {
    int old_value;
    if (!wg || wg->type != GUI_WIDGET_TREEVIEW || !wg->enabled) return;
    if (row < 0 || row >= gui_treeview_node_count(wg) || !gui_treeview_line_is_visible(wg, row)) return;
    old_value = wg->value;
    wg->value = row;
    gui_treeview_ensure_visible(wg);
    if (old_value != wg->value) gui_user_post_value_event(wg);
    gui_invalidate_all();
}

static void gui_treeview_toggle(gui_widget_t *wg, int row) {
    char line[GUI_WIDGET_TEXT_CAP];
    int folder = 0, expanded = 0;
    if (!wg || row < 0) return;
    gui_treeview_copy_line(wg->placeholder, row, line, sizeof(line));
    gui_treeview_parse_node(line, NULL, &folder, &expanded, NULL);
    if (!folder) return;
    gui_treeview_set_line_expanded(wg, row, !expanded);
    gui_treeview_ensure_visible(wg);
    gui_invalidate_all();
}

static int gui_treeview_scroll(gui_widget_t *wg, int delta_rows) {
    int total, rows, max_first, next;
    if (!wg || wg->type != GUI_WIDGET_TREEVIEW || delta_rows == 0) return 0;
    total = gui_treeview_visible_count(wg);
    rows = gui_treeview_visible_rows(wg);
    max_first = total > rows ? total - rows : 0;
    next = wg->min_value + delta_rows;
    if (next < 0) next = 0;
    if (next > max_first) next = max_first;
    if (next == wg->min_value) return 0;
    wg->min_value = next;
    gui_invalidate_all();
    return 1;
}

static void gui_treeview_handle_key(gui_widget_t *wg, int key) {
    int pos, total, next_pos, next_row;
    if (!wg || wg->type != GUI_WIDGET_TREEVIEW || !wg->enabled) return;
    total = gui_treeview_visible_count(wg);
    if (total <= 0) return;
    pos = gui_treeview_visible_position(wg, wg->value);
    if (pos < 0) pos = 0;
    next_pos = pos;
    if (key == GUI_KEY_UP) next_pos--;
    else if (key == GUI_KEY_DOWN) next_pos++;
    else if (key == GUI_KEY_HOME) next_pos = 0;
    else if (key == GUI_KEY_END) next_pos = total - 1;
    else if (key == GUI_KEY_LEFT || key == GUI_KEY_RIGHT || key == GUI_KEY_ENTER || key == GUI_KEY_SPACE) { gui_treeview_toggle(wg, wg->value); return; }
    else return;
    if (next_pos < 0) next_pos = 0;
    if (next_pos >= total) next_pos = total - 1;
    next_row = gui_treeview_nth_visible(wg, next_pos);
    gui_treeview_select(wg, next_row);
}

int gui_treeview_set_nodes(gui_widget_t *widget, const char *nodes) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_TREEVIEW) return -1;
    gui_widget_set_placeholder(widget, nodes ? nodes : "");
    count = gui_treeview_node_count(widget);
    widget->max_value = count - 1;
    if (count <= 0) { widget->value = -1; widget->min_value = 0; }
    else {
        if (widget->value < 0) widget->value = 0;
        if (widget->value >= count) widget->value = count - 1;
        if (!gui_treeview_line_is_visible(widget, widget->value)) widget->value = gui_treeview_nth_visible(widget, 0);
        gui_treeview_ensure_visible(widget);
    }
    return 0;
}

int gui_treeview_set_selected(gui_widget_t *widget, int selected_node) {
    if (!widget || widget->type != GUI_WIDGET_TREEVIEW) return -1;
    if (gui_treeview_node_count(widget) <= 0) { widget->value = -1; return selected_node < 0 ? 0 : -1; }
    if (selected_node < 0 || selected_node >= gui_treeview_node_count(widget) || !gui_treeview_line_is_visible(widget, selected_node)) return -1;
    widget->value = selected_node;
    gui_treeview_ensure_visible(widget);
    return 0;
}

int gui_treeview_get_selected(gui_widget_t *widget, int *out_selected_node) {
    if (!widget || !out_selected_node || widget->type != GUI_WIDGET_TREEVIEW) return -1;
    *out_selected_node = widget->value;
    return 0;
}

static void gui_draw_select_dropdown(gui_widget_t *wg) {
    int ax, ay, i, count;
    char item[GUI_WIDGET_TEXT_CAP];
    if (!wg || !wg->step) return;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return;
    count = gui_select_item_count(wg);
    if (count <= 0) return;
    gui_raw_fill_rect(ax, ay + wg->rect.h, wg->rect.w, count * 20, gui_rgb(255, 255, 255));
    for (i = 0; i < count; ++i) {
        int iy = ay + wg->rect.h + i * 20;
        uint32_t bg = (i == wg->min_value) ? gui_rgb(222, 236, 255) : gui_rgb(255, 255, 255);
        gui_rect_t clip = { ax + 6, iy, wg->rect.w - 12, 20 };
        gui_raw_fill_rect(ax + 1, iy, wg->rect.w - 2, 20, bg);
        gui_raw_line(ax + 1, iy + 19, ax + wg->rect.w - 2, iy + 19, gui_rgb(230, 234, 242));
        if (gui_select_item_text(wg, i, item, sizeof(item)) == 0) {
            gui_draw_window_title_text(ax + 6, gui_text_center_y(iy, 20), item, g_gui.colors.text_fg, &clip);
        }
    }
    gui_raw_line(ax, ay + wg->rect.h, ax + wg->rect.w - 1, ay + wg->rect.h, g_gui.colors.button_border);
    gui_raw_line(ax, ay + wg->rect.h + count * 20 - 1, ax + wg->rect.w - 1, ay + wg->rect.h + count * 20 - 1, g_gui.colors.button_border);
    gui_raw_line(ax, ay + wg->rect.h, ax, ay + wg->rect.h + count * 20 - 1, g_gui.colors.button_border);
    gui_raw_line(ax + wg->rect.w - 1, ay + wg->rect.h, ax + wg->rect.w - 1, ay + wg->rect.h + count * 20 - 1, g_gui.colors.button_border);
}

static void gui_toolbar_draw_separator(int x, int ay, int h) {
    gui_raw_line(x, ay + 7, x, ay + h - 8, gui_rgb(184, 190, 200));
    gui_raw_line(x + 1, ay + 7, x + 1, ay + h - 8, gui_rgb(255, 255, 255));
}

static void gui_toolbar_draw_field(gui_widget_t *wg, int ax, int ay, int *cursor_x, const char *text, int search) {
    int w = search ? 136 : (wg->rect.w - *cursor_x - ((wg->toolbar_flags & GUI_TOOLBAR_HAS_SEARCH) ? 150 : 12));
    gui_rect_t clip;
    if (w < 96) w = 96;
    if (!search && w > wg->rect.w - *cursor_x - 8) w = wg->rect.w - *cursor_x - 8;
    if (w <= 24) return;
    gui_raw_fill_rect(ax + *cursor_x, ay + 5, w, wg->rect.h - 10, gui_rgb(255, 255, 255));
    gui_raw_line(ax + *cursor_x, ay + 5, ax + *cursor_x + w - 1, ay + 5, gui_rgb(172, 181, 195));
    gui_raw_line(ax + *cursor_x, ay + wg->rect.h - 6, ax + *cursor_x + w - 1, ay + wg->rect.h - 6, gui_rgb(218, 224, 232));
    gui_raw_line(ax + *cursor_x, ay + 5, ax + *cursor_x, ay + wg->rect.h - 6, gui_rgb(172, 181, 195));
    gui_raw_line(ax + *cursor_x + w - 1, ay + 5, ax + *cursor_x + w - 1, ay + wg->rect.h - 6, gui_rgb(218, 224, 232));
    clip.x = ax + *cursor_x + 8;
    clip.y = ay + 7;
    clip.w = w - 16;
    clip.h = wg->rect.h - 14;
    gui_draw_window_title_text(clip.x, gui_text_center_y(ay, wg->rect.h), text && text[0] ? text : (search ? "Search" : "Address"), gui_rgb(84, 94, 112), &clip);
    *cursor_x += w + 8;
}

static void gui_toolbar_draw_button(gui_widget_t *wg, int ax, int ay, int *cursor_x, const char *label) {
    int w = (int)font_measure_text_width(font_get_default(), label) + 18;
    int x = ax + *cursor_x;
    gui_rect_t clip;
    if (w < 26) w = 26;
    if (*cursor_x + w >= wg->rect.w - 8) return;
    if (wg->toolbar_flags & GUI_TOOLBAR_GROUPED_BUTTONS) {
        gui_raw_fill_rect(x, ay + 5, w, wg->rect.h - 10, gui_rgb(244, 247, 251));
        gui_raw_line(x, ay + 5, x + w - 1, ay + 5, gui_rgb(255, 255, 255));
        gui_raw_line(x, ay + wg->rect.h - 6, x + w - 1, ay + wg->rect.h - 6, gui_rgb(166, 174, 188));
        gui_raw_line(x, ay + 5, x, ay + wg->rect.h - 6, gui_rgb(255, 255, 255));
        gui_raw_line(x + w - 1, ay + 5, x + w - 1, ay + wg->rect.h - 6, gui_rgb(166, 174, 188));
    }
    clip.x = x + 7;
    clip.y = ay + 6;
    clip.w = w - 10;
    clip.h = wg->rect.h - 12;
    gui_draw_window_title_text(clip.x, gui_text_center_y(ay, wg->rect.h), label, g_gui.colors.text_fg, &clip);
    *cursor_x += w + ((wg->toolbar_flags & GUI_TOOLBAR_GROUPED_BUTTONS) ? 2 : 6);
}

static int gui_toolbar_starts_with(const char *text, const char *prefix) {
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (*text != *prefix) return 0;
        text++;
        prefix++;
    }
    return 1;
}

static void gui_toolbar_draw_token(gui_widget_t *wg, int ax, int ay, int *cursor_x, const char *token) {
    const char *text = token;
    while (text && *text == ' ') text++;
    if (!text || !*text) return;
    if (strcmp(text, "sep") == 0 || strcmp(text, "separator") == 0 || strcmp(text, "|") == 0) {
        gui_toolbar_draw_separator(ax + *cursor_x + 5, ay, wg->rect.h);
        *cursor_x += 12;
    } else if (gui_toolbar_starts_with(text, "addr:")) {
        gui_toolbar_draw_field(wg, ax, ay, cursor_x, text + 5, 0);
    } else if (gui_toolbar_starts_with(text, "search:")) {
        gui_toolbar_draw_field(wg, ax, ay, cursor_x, text + 7, 1);
    } else {
        gui_toolbar_draw_button(wg, ax, ay, cursor_x, text);
    }
}

static void gui_draw_toolbar_widget(gui_widget_t *wg, int ax, int ay) {
    char token[64];
    const char *p;
    int n = 0;
    int cursor_x = 8;
    int gy;
    if (!wg) return;
    gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, gui_rgb(247, 250, 253));
    gui_raw_fill_rect(ax, ay + wg->rect.h / 2, wg->rect.w, wg->rect.h - wg->rect.h / 2, gui_rgb(226, 232, 240));
    if (wg->toolbar_flags & GUI_TOOLBAR_BOTTOM_BORDER)
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, gui_rgb(166, 174, 188));
    if (wg->toolbar_flags & GUI_TOOLBAR_SHOW_GRIP) {
        for (gy = ay + 8; gy < ay + wg->rect.h - 8; gy += 4) {
            gui_raw_fill_rect(ax + 5, gy, 2, 2, gui_rgb(174, 182, 196));
            gui_raw_fill_rect(ax + 9, gy, 2, 2, gui_rgb(255, 255, 255));
        }
        cursor_x = 18;
    }
    p = wg->text;
    while (p && *p) {
        if (*p == '|' || *p == '\n' || *p == '\r') {
            token[n] = 0;
            gui_toolbar_draw_token(wg, ax, ay, &cursor_x, token);
            if (*p == '|') {
                gui_toolbar_draw_token(wg, ax, ay, &cursor_x, "|");
            }
            n = 0;
            p++;
            continue;
        }
        if (n < (int)sizeof(token) - 1) token[n++] = *p;
        p++;
    }
    if (n > 0) {
        token[n] = 0;
        gui_toolbar_draw_token(wg, ax, ay, &cursor_x, token);
    }
}

static void gui_statusbar_split(const char *text, char *left, char *center, char *right, int cap) {
    const char *p;
    char *dst = left;
    int n = 0;
    if (left && cap > 0) left[0] = 0;
    if (center && cap > 0) center[0] = 0;
    if (right && cap > 0) right[0] = 0;
    if (!text || cap <= 0) return;
    p = text;
    while (*p) {
        if (*p == '|') {
            if (dst == left) dst = center;
            else if (dst == center) dst = right;
            n = 0;
            p++;
            continue;
        }
        if (n < cap - 1) {
            dst[n++] = *p;
            dst[n] = 0;
        }
        p++;
    }
}

static int gui_splitview_is_horizontal(gui_widget_t *wg) {
    return wg && ((wg->splitview_flags & GUI_SPLITVIEW_HORIZONTAL) != 0);
}

static int gui_splitview_clamp_ratio(int ratio) {
    if (ratio < 10) return 10;
    if (ratio > 90) return 90;
    return ratio;
}

static int gui_splitview_bar_pos(gui_widget_t *wg) {
    int span;
    if (!wg) return 0;
    span = gui_splitview_is_horizontal(wg) ? wg->rect.h : wg->rect.w;
    if (span < 16) return span / 2;
    return (span * gui_splitview_clamp_ratio(wg->splitview_ratio)) / 100;
}

static int gui_splitview_hit_bar(gui_widget_t *wg, int sx, int sy) {
    int ax, ay, pos;
    if (!wg || wg->type != GUI_WIDGET_SPLITVIEW) return 0;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return 0;
    pos = gui_splitview_bar_pos(wg);
    if (gui_splitview_is_horizontal(wg)) {
        int by = ay + pos;
        return sx >= ax && sx < ax + wg->rect.w && sy >= by - 3 && sy <= by + 3;
    }
    {
        int bx = ax + pos;
        return sy >= ay && sy < ay + wg->rect.h && sx >= bx - 3 && sx <= bx + 3;
    }
}

static void gui_splitview_apply_screen(gui_widget_t *wg, int sx, int sy) {
    int ax, ay, span, rel, ratio;
    if (!wg || wg->type != GUI_WIDGET_SPLITVIEW) return;
    if (!(wg->splitview_flags & GUI_SPLITVIEW_RESIZABLE)) return;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return;
    if (gui_splitview_is_horizontal(wg)) {
        span = wg->rect.h;
        rel = sy - ay;
    } else {
        span = wg->rect.w;
        rel = sx - ax;
    }
    if (span <= 0) return;
    ratio = (rel * 100) / span;
    ratio = gui_splitview_clamp_ratio(ratio);
    if (ratio != wg->splitview_ratio) {
        wg->splitview_ratio = ratio;
        wg->value = ratio;
        gui_user_post_value_event(wg);
        gui_invalidate_all();
    }
}

int gui_splitview_set_ratio(gui_widget_t *widget, int ratio) {
    if (!widget || widget->type != GUI_WIDGET_SPLITVIEW) return -1;
    widget->splitview_ratio = gui_splitview_clamp_ratio(ratio);
    widget->value = widget->splitview_ratio;
    return 0;
}

int gui_splitview_get_ratio(gui_widget_t *widget, int *out_ratio) {
    if (!widget || !out_ratio || widget->type != GUI_WIDGET_SPLITVIEW) return -1;
    *out_ratio = widget->splitview_ratio;
    return 0;
}

static void gui_draw_groupbox_widget(gui_widget_t *wg, int ax, int ay) {
    uint32_t bg = wg->bg_color ? wg->bg_color : gui_rgb(248, 250, 252);
    uint32_t border = wg->panel_border_color ? wg->panel_border_color : g_gui.colors.button_border;
    uint32_t title_color = (wg->panel_flags & GUI_GROUPBOX_FLAG_ERROR) ? gui_rgb(185, 28, 28) : g_gui.colors.text_fg;
    uint32_t header_bg = (wg->panel_flags & GUI_GROUPBOX_FLAG_ERROR) ? gui_rgb(254, 242, 242) : gui_rgb(241, 245, 249);
    uint32_t bw = wg->panel_border_width ? wg->panel_border_width : 1;
    uint32_t i;
    int title_h = (wg->text[0] != '\0') ? 22 : 0;
    if (bw > 8) bw = 8;
    if (wg->panel_flags & GUI_GROUPBOX_FLAG_CARD) {
        gui_raw_fill_rect(ax + 2, ay + 2, wg->rect.w, wg->rect.h, gui_rgb(205, 210, 220));
    }
    gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, bg);
    if ((wg->panel_flags & GUI_GROUPBOX_FLAG_TITLEBAR) && title_h > 0) {
        gui_raw_fill_rect(ax + 1, ay + 1, wg->rect.w - 2, title_h, header_bg);
        gui_raw_line(ax + 1, ay + title_h + 1, ax + wg->rect.w - 2, ay + title_h + 1, border);
    }
    if (wg->panel_flags & GUI_GROUPBOX_FLAG_BORDER) {
        for (i = 0; i < bw; i++) {
            gui_raw_line(ax + (int)i, ay + (int)i, ax + wg->rect.w - 1 - (int)i, ay + (int)i, border);
            gui_raw_line(ax + (int)i, ay + wg->rect.h - 1 - (int)i, ax + wg->rect.w - 1 - (int)i, ay + wg->rect.h - 1 - (int)i, border);
            gui_raw_line(ax + (int)i, ay + (int)i, ax + (int)i, ay + wg->rect.h - 1 - (int)i, border);
            gui_raw_line(ax + wg->rect.w - 1 - (int)i, ay + (int)i, ax + wg->rect.w - 1 - (int)i, ay + wg->rect.h - 1 - (int)i, border);
        }
    }
    if (wg->text[0] != '\0') {
        gui_rect_t clip = { ax + 8, ay + 2, wg->rect.w - 16, title_h > 0 ? title_h : 18 };
        int ty = (wg->panel_flags & GUI_GROUPBOX_FLAG_TITLEBAR) ? gui_text_center_y(ay + 1, title_h) : ay - 1;
        gui_draw_window_title_text(ax + 8, ty, wg->text, title_color, &clip);
    }
}

static void gui_draw_splitview_widget(gui_widget_t *wg, int ax, int ay) {
    int pos = gui_splitview_bar_pos(wg);
    uint32_t pane_a = gui_rgb(248, 250, 252);
    uint32_t pane_b = gui_rgb(241, 245, 249);
    uint32_t border = gui_rgb(203, 213, 225);
    uint32_t bar = (wg->hovered || wg->pressed) ? gui_rgb(96, 165, 250) : gui_rgb(148, 163, 184);
    gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, pane_b);
    if (gui_splitview_is_horizontal(wg)) {
        int by = ay + pos;
        if (pos > 0) gui_raw_fill_rect(ax, ay, wg->rect.w, pos, pane_a);
        if (by < ay + wg->rect.h) gui_raw_fill_rect(ax, by + 3, wg->rect.w, wg->rect.h - pos - 3, pane_b);
        gui_raw_fill_rect(ax, by - 1, wg->rect.w, 3, bar);
        if (wg->splitview_flags & GUI_SPLITVIEW_SHOW_GRIP) {
            int cx = ax + wg->rect.w / 2;
            gui_raw_line(cx - 18, by - 4, cx + 18, by - 4, border);
            gui_raw_line(cx - 18, by + 4, cx + 18, by + 4, border);
        }
    } else {
        int bx = ax + pos;
        if (pos > 0) gui_raw_fill_rect(ax, ay, pos, wg->rect.h, pane_a);
        if (bx < ax + wg->rect.w) gui_raw_fill_rect(bx + 3, ay, wg->rect.w - pos - 3, wg->rect.h, pane_b);
        gui_raw_fill_rect(bx - 1, ay, 3, wg->rect.h, bar);
        if (wg->splitview_flags & GUI_SPLITVIEW_SHOW_GRIP) {
            int cy = ay + wg->rect.h / 2;
            gui_raw_line(bx - 4, cy - 18, bx - 4, cy + 18, border);
            gui_raw_line(bx + 4, cy - 18, bx + 4, cy + 18, border);
        }
    }
    if (wg->splitview_flags & GUI_SPLITVIEW_PANE_BORDER) {
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
    }
}

static int gui_tabview_tab_count(gui_widget_t *wg) {
    const char *p;
    int count = 0;
    int in_tab = 0;
    if (!wg) return 0;
    p = wg->text;
    while (p && *p) {
        if (*p == '|' || *p == '\n' || *p == '\r') {
            if (in_tab) count++;
            in_tab = 0;
        } else {
            in_tab = 1;
        }
        p++;
    }
    if (in_tab) count++;
    return count;
}

static int gui_tabview_copy_tab(gui_widget_t *wg, int index, char *out, uint32_t out_size) {
    const char *p;
    int cur = 0;
    uint32_t n = 0;
    int in_tab = 0;
    if (!wg || !out || out_size == 0 || index < 0) return -1;
    out[0] = 0;
    p = wg->text;
    while (p && *p) {
        if (*p == '|' || *p == '\n' || *p == '\r') {
            if (in_tab) {
                if (cur == index) { out[n] = 0; return 0; }
                cur++;
            }
            n = 0;
            in_tab = 0;
        } else {
            in_tab = 1;
            if (cur == index && n + 1 < out_size) out[n++] = *p;
        }
        p++;
    }
    if (in_tab && cur == index) { out[n] = 0; return 0; }
    out[0] = 0;
    return -1;
}

static const char *gui_tabview_visible_label(const char *label, int *out_icon_kind) {
    const unsigned char *p = (const unsigned char *)label;
    if (out_icon_kind) *out_icon_kind = 0;
    if (p && p[0] == 0x1f && p[1] >= '0' && p[1] <= '9' && p[2] == ':') {
        if (out_icon_kind) *out_icon_kind = (int)(p[1] - '0');
        return (const char *)(p + 3);
    }
    return label ? label : "";
}

static void gui_draw_tabview_site_icon(int x, int y, int kind, int active) {
    uint32_t blue = gui_rgb(50, 112, 220);
    uint32_t green = gui_rgb(42, 150, 96);
    uint32_t orange = gui_rgb(220, 132, 36);
    uint32_t red = gui_rgb(210, 64, 64);
    uint32_t gray = gui_rgb(128, 140, 156);
    uint32_t fill = active ? gui_rgb(255, 255, 255) : gui_rgb(246, 248, 252);
    uint32_t border = gray;

    if (kind <= 0) kind = 1;
    if (kind == 2) border = green;
    else if (kind == 3) border = orange;
    else if (kind == 4) border = red;
    else border = blue;

    gui_raw_fill_rect(x + 1, y + 1, 12, 12, fill);
    gui_raw_line(x + 2, y, x + 10, y, border);
    gui_raw_line(x + 1, y + 1, x + 12, y + 1, border);
    gui_raw_line(x, y + 2, x, y + 10, border);
    gui_raw_line(x + 13, y + 2, x + 13, y + 10, border);
    gui_raw_line(x + 1, y + 12, x + 12, y + 12, border);
    gui_raw_line(x + 2, y + 13, x + 10, y + 13, border);
    if (kind == 2) {
        gui_raw_line(x + 3, y + 7, x + 6, y + 10, green);
        gui_raw_line(x + 6, y + 10, x + 11, y + 4, green);
    } else if (kind == 3) {
        gui_raw_fill_rect(x + 3, y + 3, 8, 9, gui_rgb(255, 246, 226));
        gui_raw_line(x + 8, y + 3, x + 11, y + 6, orange);
        gui_raw_line(x + 3, y + 6, x + 9, y + 6, orange);
    } else if (kind == 4) {
        gui_raw_line(x + 4, y + 4, x + 9, y + 9, red);
        gui_raw_line(x + 9, y + 4, x + 4, y + 9, red);
    } else {
        gui_raw_line(x + 3, y + 5, x + 10, y + 5, blue);
        gui_raw_line(x + 3, y + 8, x + 10, y + 8, blue);
        gui_raw_line(x + 5, y + 3, x + 5, y + 10, blue);
        gui_raw_line(x + 8, y + 3, x + 8, y + 10, blue);
    }
}

static int gui_tabview_tab_width(const char *label, int closeable) {
    int icon_kind = 0;
    const char *visible = gui_tabview_visible_label(label, &icon_kind);
    int w = (int)font_measure_text_width(font_get_default(), visible ? visible : "") + 30 + (icon_kind ? 18 : 0) + (closeable ? 18 : 0);
    if (w < 58) w = 58;
    if (w > 150) w = 150;
    return w;
}

static int gui_tabview_index_at(gui_widget_t *wg, int sx, int sy, int *out_close) {
    int ax, ay, i, count, x;
    int closeable;
    char label[GUI_WIDGET_TEXT_CAP];
    if (out_close) *out_close = 0;
    if (!wg || wg->type != GUI_WIDGET_TABVIEW) return -1;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return -1;
    if (sx < ax || sx >= ax + wg->rect.w || sy < ay || sy >= ay + 28) return -1;
    count = gui_tabview_tab_count(wg);
    closeable = (wg->tabview_flags & GUI_TABVIEW_CLOSE_BUTTONS) != 0;
    x = ax + 2;
    for (i = 0; i < count; ++i) {
        int tw;
        if (gui_tabview_copy_tab(wg, i, label, sizeof(label)) < 0) continue;
        tw = gui_tabview_tab_width(label, closeable);
        if (sx >= x && sx < x + tw) {
            if (out_close && closeable && sx >= x + tw - 20 && sy >= ay + 6 && sy < ay + 22) *out_close = 1;
            return i;
        }
        x += tw - 1;
        if (x >= ax + wg->rect.w - 2) break;
    }
    return -1;
}

static void gui_tabview_select(gui_widget_t *wg, int index) {
    int old_value;
    int count;
    if (!wg || wg->type != GUI_WIDGET_TABVIEW || !wg->enabled) return;
    count = gui_tabview_tab_count(wg);
    if (index < 0 || index >= count) return;
    old_value = wg->value;
    wg->value = index;
    if (old_value != wg->value) gui_user_post_value_event(wg);
    gui_invalidate_all();
}

int gui_tabview_set_tabs(gui_widget_t *widget, const char *tabs) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_TABVIEW) return -1;
    gui_widget_set_text(widget, tabs ? tabs : "");
    count = gui_tabview_tab_count(widget);
    widget->max_value = count - 1;
    if (count <= 0) widget->value = -1;
    else if (widget->value < 0 || widget->value >= count) widget->value = 0;
    return 0;
}

int gui_tabview_set_active(gui_widget_t *widget, int active_index) {
    int count;
    if (!widget || widget->type != GUI_WIDGET_TABVIEW) return -1;
    count = gui_tabview_tab_count(widget);
    if (count <= 0) { widget->value = -1; return active_index < 0 ? 0 : -1; }
    if (active_index < 0 || active_index >= count) return -1;
    widget->value = active_index;
    return 0;
}

int gui_tabview_get_active(gui_widget_t *widget, int *out_active_index) {
    if (!widget || !out_active_index || widget->type != GUI_WIDGET_TABVIEW) return -1;
    *out_active_index = widget->value;
    return 0;
}

int gui_tabview_close_tab(gui_widget_t *widget, int tab_index) {
    char out[GUI_WIDGET_TEXT_CAP];
    char label[GUI_WIDGET_TEXT_CAP];
    int i, count, first = 1;
    if (!widget || widget->type != GUI_WIDGET_TABVIEW) return -1;
    count = gui_tabview_tab_count(widget);
    if (tab_index < 0 || tab_index >= count) return -1;
    out[0] = 0;
    for (i = 0; i < count; ++i) {
        if (i == tab_index) continue;
        if (gui_tabview_copy_tab(widget, i, label, sizeof(label)) < 0) continue;
        if (!first) gui_tableview_append(out, sizeof(out), "|");
        gui_tableview_append(out, sizeof(out), label);
        first = 0;
    }
    gui_widget_set_text(widget, out);
    count = gui_tabview_tab_count(widget);
    widget->max_value = count - 1;
    if (count <= 0) widget->value = -1;
    else if (widget->value > tab_index) widget->value--;
    else if (widget->value >= count) widget->value = count - 1;
    gui_user_post_value_event(widget);
    gui_invalidate_all();
    return 0;
}

static void gui_tabview_activate(gui_widget_t *wg, int tab_index, int close_hit) {
    if (!wg || wg->type != GUI_WIDGET_TABVIEW) return;
    if (close_hit && (wg->tabview_flags & GUI_TABVIEW_CLOSE_BUTTONS)) gui_tabview_close_tab(wg, tab_index);
    else gui_tabview_select(wg, tab_index);
}

static void gui_draw_tabview_widget(gui_widget_t *wg, int ax, int ay) {
    int count, i, x;
    int closeable;
    char label[GUI_WIDGET_TEXT_CAP];
    gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, gui_rgb(245, 247, 251));
    if (wg->rect.h > 28) gui_raw_fill_rect(ax, ay + 27, wg->rect.w, wg->rect.h - 27, gui_rgb(255, 255, 255));
    gui_raw_line(ax, ay + 27, ax + wg->rect.w - 1, ay + 27, gui_rgb(166, 174, 188));
    if (wg->tabview_flags & GUI_TABVIEW_BOTTOM_BORDER) gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, gui_rgb(166, 174, 188));
    count = gui_tabview_tab_count(wg);
    closeable = (wg->tabview_flags & GUI_TABVIEW_CLOSE_BUTTONS) != 0;
    x = ax + 2;
    for (i = 0; i < count; ++i) {
        int tw;
        int icon_kind = 0;
        int text_x;
        uint32_t bg;
        gui_rect_t clip;
        const char *visible_label;
        if (gui_tabview_copy_tab(wg, i, label, sizeof(label)) < 0) continue;
        visible_label = gui_tabview_visible_label(label, &icon_kind);
        tw = gui_tabview_tab_width(label, closeable);
        if (x + tw > ax + wg->rect.w - 2) tw = ax + wg->rect.w - 2 - x;
        if (tw <= 18) break;
        bg = (i == wg->value) ? gui_rgb(255, 255, 255) : gui_rgb(226, 232, 240);
        gui_raw_fill_rect(x, ay + 3, tw, 25, bg);
        gui_raw_line(x, ay + 3, x + tw - 1, ay + 3, gui_rgb(255, 255, 255));
        gui_raw_line(x, ay + 3, x, ay + 27, gui_rgb(166, 174, 188));
        gui_raw_line(x + tw - 1, ay + 3, x + tw - 1, ay + 27, gui_rgb(166, 174, 188));
        if (i != wg->value) gui_raw_line(x, ay + 27, x + tw - 1, ay + 27, gui_rgb(166, 174, 188));
        text_x = x + 10;
        if (icon_kind) {
            gui_draw_tabview_site_icon(x + 8, ay + 9, icon_kind, i == wg->value);
            text_x += 18;
        }
        clip.x = text_x;
        clip.y = ay + 5;
        clip.w = tw - (text_x - x) - 8 - (closeable ? 18 : 0);
        clip.h = 20;
        if (clip.w > 0) gui_draw_window_title_text(clip.x, gui_text_center_y(ay + 3, 25), visible_label, g_gui.colors.text_fg, &clip);
        if (closeable) {
            int cx = x + tw - 15;
            int cy = ay + 15;
            gui_raw_line(cx - 3, cy - 3, cx + 3, cy + 3, gui_rgb(92, 102, 118));
            gui_raw_line(cx + 3, cy - 3, cx - 3, cy + 3, gui_rgb(92, 102, 118));
        }
        x += tw - 1;
    }
}

static void gui_draw_statusbar_text_cell(int x, int y, int w, int h, const char *text, uint32_t color, int align) {
    gui_rect_t clip;
    int tx;
    int text_w;
    if (!text || !text[0] || w <= 8) return;
    clip.x = x + 4;
    clip.y = y + 2;
    clip.w = w - 8;
    clip.h = h - 4;
    text_w = (int)font_measure_text_width(font_get_default(), text);
    tx = clip.x;
    if (align == 1) tx = x + (w - text_w) / 2;
    else if (align == 2) tx = x + w - text_w - 6;
    if (tx < clip.x) tx = clip.x;
    gui_draw_window_title_text(tx, gui_text_center_y(y, h), text, color, &clip);
}

static void gui_draw_statusbar_widget(gui_widget_t *wg, int ax, int ay) {
    char left[96];
    char center[96];
    char right[96];
    int left_w;
    int center_w;
    int right_w;
    uint32_t text_color;
    if (!wg) return;
    gui_statusbar_split(wg->text, left, center, right, sizeof(left));
    gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, gui_rgb(238, 242, 247));
    gui_raw_fill_rect(ax, ay + wg->rect.h / 2, wg->rect.w, wg->rect.h - wg->rect.h / 2, gui_rgb(224, 230, 238));
    if (wg->statusbar_flags & GUI_STATUSBAR_TOP_BORDER) {
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, gui_rgb(166, 174, 188));
        gui_raw_line(ax, ay + 1, ax + wg->rect.w - 1, ay + 1, gui_rgb(255, 255, 255));
    }
    if (wg->statusbar_flags & GUI_STATUSBAR_LOADING) {
        int dot_x = ax + 8;
        int dot_y = ay + wg->rect.h / 2 - 2;
        gui_raw_fill_rect(dot_x, dot_y, 4, 4, gui_rgb(52, 120, 246));
        gui_raw_fill_rect(dot_x + 7, dot_y, 4, 4, gui_rgb(92, 150, 250));
        gui_raw_fill_rect(dot_x + 14, dot_y, 4, 4, gui_rgb(132, 176, 252));
        if (!left[0]) { left[0] = 'L'; left[1] = 'o'; left[2] = 'a'; left[3] = 'd'; left[4] = 'i'; left[5] = 'n'; left[6] = 'g'; left[7] = 0; }
        left_w = wg->rect.w / 3;
        gui_draw_statusbar_text_cell(ax + 28, ay, left_w - 28, wg->rect.h, left, g_gui.colors.text_fg, 0);
    } else {
        left_w = wg->rect.w / 3;
        gui_draw_statusbar_text_cell(ax, ay, left_w, wg->rect.h, left, g_gui.colors.text_fg, 0);
    }
    center_w = wg->rect.w / 3;
    right_w = wg->rect.w - left_w - center_w;
    gui_raw_line(ax + left_w, ay + 4, ax + left_w, ay + wg->rect.h - 5, gui_rgb(202, 210, 222));
    gui_raw_line(ax + left_w + center_w, ay + 4, ax + left_w + center_w, ay + wg->rect.h - 5, gui_rgb(202, 210, 222));
    gui_draw_statusbar_text_cell(ax + left_w, ay, center_w, wg->rect.h, center, gui_rgb(88, 98, 116), 1);
    text_color = (wg->statusbar_flags & GUI_STATUSBAR_LINK_PROMPT) ? gui_rgb(32, 98, 190) : gui_rgb(88, 98, 116);
    gui_draw_statusbar_text_cell(ax + left_w + center_w, ay, right_w - ((wg->statusbar_flags & GUI_STATUSBAR_SIZE_GRIP) ? 18 : 0), wg->rect.h, right, text_color, 2);
    if (wg->statusbar_flags & GUI_STATUSBAR_SIZE_GRIP) {
        int gx = ax + wg->rect.w - 15;
        int gy = ay + wg->rect.h - 5;
        gui_raw_line(gx + 8, gy - 8, gx + 12, gy - 12, gui_rgb(150, 160, 174));
        gui_raw_line(gx + 4, gy - 4, gx + 12, gy - 12, gui_rgb(150, 160, 174));
        gui_raw_line(gx, gy, gx + 12, gy - 12, gui_rgb(150, 160, 174));
        gui_raw_line(gx + 9, gy - 8, gx + 13, gy - 12, gui_rgb(255, 255, 255));
        gui_raw_line(gx + 5, gy - 4, gx + 13, gy - 12, gui_rgb(255, 255, 255));
        gui_raw_line(gx + 1, gy, gx + 13, gy - 12, gui_rgb(255, 255, 255));
    }
}

static void gui_draw_widget(gui_widget_t *wg) {
    uint32_t bg, fg;
    int ax;
    int ay;
    if (!wg || !wg->visible || !wg->owner) return;
    if (!gui_widget_absolute_origin(wg, &ax, &ay)) return;

    if (wg->type == GUI_WIDGET_LABEL) {
        gui_draw_label_widget(wg, ax, ay);
    } else if (wg->type == GUI_WIDGET_BUTTON || wg->type == GUI_WIDGET_ICON_BUTTON) {
        if (wg->button_flags & GUI_BUTTON_FLAG_TRANSPARENT) return;
        uint32_t light = g_gui.colors.button_border;
        uint32_t shadow = gui_rgb(20, 20, 20);
        int flat = (wg->button_flags & GUI_BUTTON_FLAG_FLAT) != 0;
        int text_dx = wg->pressed ? 9 : 8;
        int text_dy = wg->pressed ? 1 : 0;
        if (flat) {
            text_dx = wg->pressed ? 1 : 0;
        }
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
        } else if (wg->button_flags & GUI_BUTTON_FLAG_DANGER) {
            bg = wg->pressed ? gui_rgb(176, 32, 32) : (wg->hovered ? gui_rgb(220, 64, 64) : gui_rgb(202, 48, 48));
            fg = gui_rgb(255, 255, 255);
            light = gui_rgb(244, 138, 138);
            shadow = gui_rgb(126, 22, 22);
        } else if (wg->button_flags & GUI_BUTTON_FLAG_DEFAULT) {
            bg = wg->pressed ? gui_rgb(35, 96, 190) : (wg->hovered ? gui_rgb(74, 138, 238) : g_gui.colors.accent);
            fg = gui_rgb(255, 255, 255);
            light = gui_rgb(156, 196, 255);
            shadow = gui_rgb(30, 72, 142);
        } else if (wg->hovered) {
            bg = gui_rgb(235, 240, 250);
            fg = wg->fg_color ? wg->fg_color : g_gui.colors.button_fg;
        } else {
            bg = wg->bg_color ? wg->bg_color : g_gui.colors.button_bg;
            fg = wg->fg_color ? wg->fg_color : g_gui.colors.button_fg;
        }
        if (flat) {
            if (wg->pressed) {
                gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, gui_rgb(218, 226, 238));
            } else if (wg->hovered && wg->enabled) {
                gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, gui_rgb(232, 238, 248));
            }
            fg = wg->enabled ? (wg->fg_color ? wg->fg_color : g_gui.colors.text_fg) : gui_rgb(120, 126, 136);
        } else {
            gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, bg);
            gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, light);
            gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, light);
            gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, shadow);
            gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, shadow);
        }
        if (wg->focused && wg->enabled && !flat) {
            uint32_t focus = gui_rgb(255, 255, 255);
            gui_raw_line(ax + 3, ay + 3, ax + wg->rect.w - 4, ay + 3, focus);
            gui_raw_line(ax + 3, ay + wg->rect.h - 4, ax + wg->rect.w - 4, ay + wg->rect.h - 4, focus);
            gui_raw_line(ax + 3, ay + 3, ax + 3, ay + wg->rect.h - 4, focus);
            gui_raw_line(ax + wg->rect.w - 4, ay + 3, ax + wg->rect.w - 4, ay + wg->rect.h - 4, focus);
        }
        if (wg->type == GUI_WIDGET_ICON_BUTTON && wg->rect.h >= 44) {
            gui_rect_t icon_rect = { ax, ay, wg->rect.w, wg->rect.h };
            int icon_x = 0;
            int icon_y = 0;
            gui_draw_icon_button_frame(&icon_rect, wg->text, 14, 14, 4,
                                       wg->focused && wg->enabled,
                                       wg->hovered && wg->enabled,
                                       fg, &icon_x, &icon_y);
            if (wg->icon != GUI_ICON_NONE) {
                gui_draw_file_icon(wg->icon, icon_x, icon_y + text_dy);
            }
        } else {
            if (flat && wg->icon != GUI_ICON_NONE && !wg->text[0]) {
                gui_draw_file_icon(wg->icon, ax + (wg->rect.w - 14) / 2 + text_dx,
                                   ay + (wg->rect.h - 14) / 2 + text_dy);
            } else {
                text_dx += gui_draw_inline_icon(wg->icon, ax + text_dx,
                                                ay + text_dy, wg->rect.h);
                {
                    gui_rect_t clip = { ax + text_dx, ay + 2, wg->rect.w - text_dx - 3, wg->rect.h - 4 };
                    gui_draw_window_title_text(ax + text_dx, gui_text_center_y(ay, wg->rect.h) + text_dy,
                                               wg->text, fg, &clip);
                }
            }
        }
    } else if (wg->type == GUI_WIDGET_TOOLBAR) {
        gui_draw_toolbar_widget(wg, ax, ay);
    } else if (wg->type == GUI_WIDGET_STATUSBAR) {
        gui_draw_statusbar_widget(wg, ax, ay);
    } else if (wg->type == GUI_WIDGET_TABVIEW) {
        gui_draw_tabview_widget(wg, ax, ay);
    } else if (wg->type == GUI_WIDGET_SPLITVIEW) {
        gui_draw_splitview_widget(wg, ax, ay);
    } else if (wg->type == GUI_WIDGET_GROUPBOX) {
        gui_draw_groupbox_widget(wg, ax, ay);
    } else if (wg->type == GUI_WIDGET_ICONVIEW) {
        int count = gui_iconview_count(wg);
        int cols = gui_iconview_columns(wg);
        int cell_w = gui_iconview_item_width(wg);
        int cell_h = gui_iconview_item_height(wg);
        int rows = gui_iconview_visible_rows(wg);
        int i;
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, wg->bg_color ? wg->bg_color : gui_rgb(248, 250, 252));
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, g_gui.colors.button_border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, g_gui.colors.button_border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, g_gui.colors.button_border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, g_gui.colors.button_border);
        for (i = 0; i < rows * cols; ++i) {
            int index = wg->min_value * cols + i;
            int row = i / cols;
            int col = i % cols;
            gui_rect_t cell;
            char label[GUI_WIDGET_TEXT_CAP];
            gui_icon_id_t icon;
            int icon_x = 0;
            int icon_y = 0;
            if (index >= count) break;
            cell.x = ax + 2 + col * cell_w;
            cell.y = ay + 2 + row * cell_h;
            cell.w = (wg->label_flags & GUI_ICONVIEW_LIST_MODE) ? wg->rect.w - 4 : cell_w;
            cell.h = cell_h;
            gui_iconview_label_for_index(wg, index, label, sizeof(label));
            icon = gui_iconview_parse_icon(wg, index);
            if (wg->label_flags & GUI_ICONVIEW_LIST_MODE) {
                gui_draw_file_icon_cell(&cell, label, icon, index == wg->value, wg->hovered && index == wg->value, g_gui.colors.text_fg);
            } else {
                gui_draw_icon_button_frame(&cell, (wg->label_flags & GUI_ICONVIEW_SHOW_LABELS) ? label : "", 18, 18, 4,
                                           index == wg->value, wg->hovered && index == wg->value,
                                           g_gui.colors.text_fg, &icon_x, &icon_y);
                gui_draw_file_icon(icon, icon_x, icon_y);
            }
        }
    } else if (wg->type == GUI_WIDGET_PANEL) {
        uint32_t bg = wg->bg_color ? wg->bg_color : g_gui.colors.window_bg;
        uint32_t border = wg->panel_border_color ? wg->panel_border_color : g_gui.colors.button_border;
        uint32_t bw = wg->panel_border_width;
        if (bw > 8) bw = 8;
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, bg);
        if ((wg->panel_flags & GUI_PANEL_FLAG_SHADOW) && wg->rect.w > 2 && wg->rect.h > 2) {
            uint32_t shadow = gui_rgb(170, 174, 184);
            gui_raw_line(ax + 2, ay + wg->rect.h, ax + wg->rect.w, ay + wg->rect.h, shadow);
            gui_raw_line(ax + wg->rect.w, ay + 2, ax + wg->rect.w, ay + wg->rect.h, shadow);
        }
        if ((wg->panel_flags & GUI_PANEL_FLAG_BORDER) && bw > 0) {
            uint32_t i;
            for (i = 0; i < bw; i++) {
                gui_raw_line(ax + (int)i, ay + (int)i, ax + wg->rect.w - 1 - (int)i, ay + (int)i, border);
                gui_raw_line(ax + (int)i, ay + wg->rect.h - 1 - (int)i, ax + wg->rect.w - 1 - (int)i, ay + wg->rect.h - 1 - (int)i, border);
                gui_raw_line(ax + (int)i, ay + (int)i, ax + (int)i, ay + wg->rect.h - 1 - (int)i, border);
                gui_raw_line(ax + wg->rect.w - 1 - (int)i, ay + (int)i, ax + wg->rect.w - 1 - (int)i, ay + wg->rect.h - 1 - (int)i, border);
            }
        }
    } else if (wg->type == GUI_WIDGET_TEXTBOX || wg->type == GUI_WIDGET_TEXTAREA) {
        uint32_t border = wg->focused ? g_gui.colors.accent : g_gui.colors.button_border;
        uint32_t text_x = (uint32_t)(ax + 4);
        uint32_t text_y = (uint32_t)(wg->type == GUI_WIDGET_TEXTAREA ? ay + 4 : gui_text_center_y(ay, wg->rect.h));
        uint32_t box_bg = wg->bg_color ? wg->bg_color : gui_rgb(250, 250, 250);
        if (wg->textbox_flags & GUI_TEXTBOX_FLAG_DISABLED) box_bg = gui_rgb(232, 234, 238);
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, box_bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        if (wg->type == GUI_WIDGET_TEXTAREA) gui_textarea_ensure_cursor_visible(wg);
        else gui_textbox_ensure_cursor_visible(wg);
        {
            gui_rect_t clip = { ax + 4, ay + 2, wg->rect.w - 8, wg->rect.h - 4 };
            uint32_t draw_color = wg->fg_color ? wg->fg_color : gui_rgb(20, 20, 20);
            char password_buf[256];
            if (wg->textbox_flags & GUI_TEXTBOX_FLAG_DISABLED) draw_color = gui_rgb(116, 122, 132);
            gui_text_widget_draw_selection(wg, ax, ay, &clip);
            if (wg->type == GUI_WIDGET_TEXTAREA) {
                uint32_t i;
                uint32_t line = 0;
                uint32_t visible_lines = gui_textarea_visible_lines(wg);
                uint32_t line_h = gui_textarea_line_height();
                uint32_t start = 0;
                if (visible_lines == 0) visible_lines = 1;
                if (wg->text[0] == '\0' && wg->placeholder[0] != '\0' && !wg->focused) {
                    gui_draw_window_title_text((int)text_x, (int)text_y, wg->placeholder, gui_rgb(128, 136, 148), &clip);
                } else {
                    for (i = 0; wg->text[i] != '\0'; i++) {
                        if (line == wg->text_scroll) { start = i; break; }
                        if (wg->text[i] == '\n') line++;
                    }
                    if (line < wg->text_scroll) start = i;
                    i = start;
                    for (line = 0; line < visible_lines && wg->text[i] != '\0'; line++) {
                        char line_buf[256];
                        uint32_t j = 0;
                        while (wg->text[i] != '\0' && wg->text[i] != '\n' && j < sizeof(line_buf) - 1) {
                            line_buf[j++] = wg->text[i++];
                        }
                        line_buf[j] = '\0';
                        if (wg->text[i] == '\n') i++;
                        gui_draw_window_title_text((int)text_x, (int)(text_y + line * line_h), line_buf, draw_color, &clip);
                    }
                }
            } else {
                const char *visible_text = wg->text + wg->text_scroll;
                if (wg->text[0] == '\0' && wg->placeholder[0] != '\0' && !wg->focused) {
                    visible_text = wg->placeholder;
                    draw_color = gui_rgb(128, 136, 148);
                } else if ((wg->textbox_flags & GUI_TEXTBOX_FLAG_PASSWORD) && wg->text[0] != '\0') {
                    uint32_t i;
                    uint32_t len = (uint32_t)strlen(wg->text);
                    if (wg->text_scroll > len) wg->text_scroll = len;
                    for (i = wg->text_scroll; i < len && (i - wg->text_scroll) < sizeof(password_buf) - 1; i++) password_buf[i - wg->text_scroll] = '*';
                    password_buf[i - wg->text_scroll] = '\0';
                    visible_text = password_buf;
                }
                gui_draw_window_title_text((int)text_x, (int)text_y, visible_text, draw_color, &clip);
            }
        }
        if (wg->focused) {
            if (wg->type == GUI_WIDGET_TEXTAREA) {
                uint32_t line = 0;
                uint32_t col = 0;
                uint32_t line_h = gui_textarea_line_height();
                gui_textarea_cursor_line_col(wg, &line, &col);
                if (line >= wg->text_scroll) {
                    int cx = ax + 4 + (int)(col * GUI_CHAR_W);
                    int cy = ay + 4 + (int)((line - wg->text_scroll) * line_h);
                    if (cx >= ax + 4 && cx < ax + wg->rect.w - 3 && cy >= ay + 4 && cy < ay + wg->rect.h - 4) {
                        gui_raw_line(cx, cy, cx, cy + (int)GUI_CHAR_H, gui_rgb(20, 20, 20));
                    }
                }
            } else {
                int cx = ax + 4 + (int)((wg->cursor - wg->text_scroll) * GUI_CHAR_W);
                if (cx >= ax + 4 && cx < ax + wg->rect.w - 3) gui_raw_line(cx, ay + 4, cx, ay + wg->rect.h - 5, gui_rgb(20, 20, 20));
            }
        }
    } else if (wg->type == GUI_WIDGET_SLIDER) {
        int min = wg->min_value;
        int max = wg->max_value;
        int val = wg->value;
        int track_x = ax + 8;
        int track_w = wg->rect.w - 16;
        int track_y = ay + wg->rect.h / 2;
        int knob_x;
        uint32_t track_bg = gui_rgb(170, 178, 192);
        uint32_t fill = wg->enabled ? g_gui.colors.accent : gui_rgb(120, 124, 132);
        uint32_t knob = wg->pressed ? gui_rgb(255, 255, 255) : gui_rgb(238, 242, 248);
        uint32_t border = wg->hovered || wg->pressed ? g_gui.colors.accent : g_gui.colors.button_border;
        if (max <= min) max = min + 1;
        if (val < min) val = min;
        if (val > max) val = max;
        knob_x = track_x + ((val - min) * track_w + (max - min) / 2) / (max - min);
        gui_raw_fill_rect(track_x, track_y - 2, track_w, 4, track_bg);
        gui_raw_fill_rect(track_x, track_y - 2, knob_x - track_x, 4, fill);
        gui_raw_fill_rect(knob_x - 5, track_y - 8, 11, 16, knob);
        gui_raw_line(knob_x - 5, track_y - 8, knob_x + 5, track_y - 8, border);
        gui_raw_line(knob_x - 5, track_y + 8, knob_x + 5, track_y + 8, border);
        gui_raw_line(knob_x - 5, track_y - 8, knob_x - 5, track_y + 8, border);
        gui_raw_line(knob_x + 5, track_y - 8, knob_x + 5, track_y + 8, border);
    } else if (wg->type == GUI_WIDGET_SELECT || wg->type == GUI_WIDGET_COMBOBOX) {
        uint32_t box_bg = wg->enabled ? gui_rgb(255, 255, 255) : gui_rgb(236, 238, 243);
        uint32_t border = wg->focused ? g_gui.colors.accent : (wg->hovered ? gui_rgb(120, 150, 210) : g_gui.colors.button_border);
        uint32_t text_color = wg->enabled ? g_gui.colors.text_fg : gui_rgb(145, 150, 160);
        char selected[GUI_WIDGET_TEXT_CAP];
        gui_rect_t clip = { ax + 6, ay, wg->rect.w - 28, wg->rect.h };
        int arrow_x = ax + wg->rect.w - 18;
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, box_bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(arrow_x, ay + 4, arrow_x, ay + wg->rect.h - 5, gui_rgb(220, 224, 232));
        gui_raw_line(arrow_x + 5, ay + wg->rect.h / 2 - 2, arrow_x + 9, ay + wg->rect.h / 2 + 2, text_color);
        gui_raw_line(arrow_x + 13, ay + wg->rect.h / 2 - 2, arrow_x + 9, ay + wg->rect.h / 2 + 2, text_color);
        if (gui_select_item_text(wg, wg->value, selected, sizeof(selected)) == 0) {
            gui_draw_window_title_text(clip.x, gui_text_center_y(ay, wg->rect.h), selected, text_color, &clip);
        } else if (wg->text[0]) {
            gui_draw_window_title_text(clip.x, gui_text_center_y(ay, wg->rect.h), wg->text, gui_rgb(128, 136, 148), &clip);
        }
    } else if (wg->type == GUI_WIDGET_LISTVIEW) {
        int count = gui_select_item_count(wg);
        int row_h = gui_listview_item_height();
        int rows = gui_listview_visible_rows(wg);
        int i;
        uint32_t box_bg = wg->enabled ? gui_rgb(255, 255, 255) : gui_rgb(236, 238, 243);
        uint32_t border = wg->focused ? g_gui.colors.accent : (wg->hovered ? gui_rgb(120, 150, 210) : g_gui.colors.button_border);
        gui_rect_t clip = { ax + 2, ay + 2, wg->rect.w - 4, wg->rect.h - 4 };
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, box_bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_listview_ensure_visible(wg);
        for (i = 0; i < rows && wg->min_value + i < count; ++i) {
            int item_index = wg->min_value + i;
            int iy = ay + 2 + i * row_h;
            int selected = gui_listview_item_selected(wg, item_index);
            int active = (item_index == wg->value);
            char item[GUI_WIDGET_TEXT_CAP];
            uint32_t bgc = selected ? gui_rgb(78, 132, 220) : (active && wg->focused ? gui_rgb(224, 236, 255) : box_bg);
            uint32_t fgc = selected ? gui_rgb(255, 255, 255) : (wg->enabled ? g_gui.colors.text_fg : gui_rgb(145, 150, 160));
            gui_raw_fill_rect(ax + 2, iy, wg->rect.w - 4, row_h, bgc);
            if ((wg->label_flags & GUI_LISTVIEW_FLAG_SHOW_CHECKBOXES) != 0) {
                int cy = iy + row_h / 2 - 5;
                gui_raw_fill_rect(ax + 7, cy, 10, 10, gui_rgb(255, 255, 255));
                gui_raw_line(ax + 7, cy, ax + 16, cy, border);
                gui_raw_line(ax + 7, cy + 9, ax + 16, cy + 9, border);
                gui_raw_line(ax + 7, cy, ax + 7, cy + 9, border);
                gui_raw_line(ax + 16, cy, ax + 16, cy + 9, border);
                if (selected) {
                    gui_raw_line(ax + 9, cy + 5, ax + 12, cy + 8, g_gui.colors.accent);
                    gui_raw_line(ax + 12, cy + 8, ax + 16, cy + 2, g_gui.colors.accent);
                }
            }
            if (gui_select_item_text(wg, item_index, item, sizeof(item)) == 0) {
                int tx = ax + ((wg->label_flags & GUI_LISTVIEW_FLAG_SHOW_CHECKBOXES) ? 22 : 6);
                gui_draw_window_title_text(tx, gui_text_center_y(iy, row_h), item, fgc, &clip);
            }
            if (active && wg->focused) {
                gui_raw_line(ax + 3, iy + 1, ax + wg->rect.w - 4, iy + 1, g_gui.colors.accent);
                gui_raw_line(ax + 3, iy + row_h - 1, ax + wg->rect.w - 4, iy + row_h - 1, g_gui.colors.accent);
            }
        }
        if (count > rows) {
            int track_h = wg->rect.h - 4;
            int thumb_h = (rows * track_h) / count;
            int max_scroll = count - rows;
            int thumb_y;
            if (thumb_h < 12) thumb_h = 12;
            if (thumb_h > track_h) thumb_h = track_h;
            thumb_y = ay + 2 + (max_scroll > 0 ? (wg->min_value * (track_h - thumb_h)) / max_scroll : 0);
            gui_raw_fill_rect(ax + wg->rect.w - 6, ay + 2, 4, track_h, gui_rgb(226, 230, 238));
            gui_raw_fill_rect(ax + wg->rect.w - 6, thumb_y, 4, thumb_h, gui_rgb(150, 162, 182));
        }
    } else if (wg->type == GUI_WIDGET_TABLEVIEW) {
        int cols = gui_tableview_col_count(wg);
        int rows = gui_tableview_row_count(wg);
        int row_h = gui_tableview_row_height();
        int header_h = gui_tableview_header_height(wg);
        int visible = gui_tableview_visible_rows(wg);
        int col_w = cols > 0 ? (wg->rect.w - 8) / cols : wg->rect.w - 8;
        int i;
        uint32_t box_bg = wg->enabled ? gui_rgb(255, 255, 255) : gui_rgb(236, 238, 243);
        uint32_t border = wg->focused ? g_gui.colors.accent : (wg->hovered ? gui_rgb(120, 150, 210) : g_gui.colors.button_border);
        gui_rect_t clip = { ax + 3, ay + 3, wg->rect.w - 8, wg->rect.h - 6 };
        if (col_w < 24) col_w = 24;
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, box_bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_tableview_ensure_visible(wg);
        if (header_h > 0) {
            int c;
            gui_raw_fill_rect(ax + 1, ay + 1, wg->rect.w - 2, header_h, gui_rgb(232, 236, 242));
            int cx = ax + 3;
            for (c = 0; c < cols; ++c) {
                char cell[64];
                int cw = gui_tableview_column_width(wg, c, col_w);
                gui_tableview_cell_text_ex(wg->text, c, cell, sizeof(cell), 1);
                if ((wg->label_flags & GUI_TABLEVIEW_FLAG_SORTABLE) && wg->table_sort_column == c) {
                    gui_tableview_append(cell, sizeof(cell), wg->table_sort_ascending ? " ^" : " v");
                }
                gui_draw_window_title_text(cx + 3, gui_text_center_y(ay + 1, header_h), cell, gui_rgb(45, 55, 70), &clip);
                if ((wg->label_flags & GUI_TABLEVIEW_FLAG_GRID_LINES) && c > 0) gui_raw_line(cx, ay + 1, cx, ay + wg->rect.h - 2, gui_rgb(218, 224, 232));
                cx += cw;
            }
            gui_raw_line(ax + 1, ay + header_h, ax + wg->rect.w - 2, ay + header_h, gui_rgb(200, 208, 218));
        }
        for (i = 0; i < visible && wg->min_value + i < rows; ++i) {
            int row = wg->min_value + i;
            int iy = ay + header_h + 2 + i * row_h;
            int c;
            char line[GUI_WIDGET_TEXT_CAP];
            uint32_t row_bg = (row == wg->value) ? gui_rgb(210, 228, 255) : ((row & 1) ? gui_rgb(248, 250, 252) : box_bg);
            gui_raw_fill_rect(ax + 2, iy, wg->rect.w - 4, row_h, row_bg);
            gui_tableview_copy_line(wg->placeholder, row, line, sizeof(line));
            int cx = ax + 3;
            for (c = 0; c < cols; ++c) {
                char cell[64];
                int cw = gui_tableview_column_width(wg, c, col_w);
                gui_tableview_cell_text(line, c, cell, sizeof(cell));
                gui_draw_window_title_text(cx + 3, gui_text_center_y(iy, row_h), cell, wg->enabled ? g_gui.colors.text_fg : gui_rgb(145, 150, 160), &clip);
                cx += cw;
            }
            if (wg->label_flags & GUI_TABLEVIEW_FLAG_GRID_LINES) gui_raw_line(ax + 2, iy + row_h - 1, ax + wg->rect.w - 3, iy + row_h - 1, gui_rgb(226, 231, 238));
        }
        if (rows > visible) {
            int track_h = wg->rect.h - header_h - 4;
            int thumb_h = (visible * track_h) / rows;
            int max_scroll = rows - visible;
            int thumb_y;
            if (thumb_h < 12) thumb_h = 12;
            if (thumb_h > track_h) thumb_h = track_h;
            thumb_y = ay + header_h + 2 + (max_scroll > 0 ? (wg->min_value * (track_h - thumb_h)) / max_scroll : 0);
            gui_raw_fill_rect(ax + wg->rect.w - 6, ay + header_h + 2, 4, track_h, gui_rgb(226, 230, 238));
            gui_raw_fill_rect(ax + wg->rect.w - 6, thumb_y, 4, thumb_h, gui_rgb(150, 162, 182));
        }
    } else if (wg->type == GUI_WIDGET_SPINNER) {
        int size = wg->rect.h < wg->rect.w ? wg->rect.h : wg->rect.w;
        int cx = ax + size / 2;
        int cy = ay + wg->rect.h / 2;
        int phase = (wg->label_flags & GUI_SPINNER_RUNNING) ? (wg->value & 7) : 0;
        int i;
        uint32_t base = wg->enabled ? g_gui.colors.accent : gui_rgb(150, 154, 162);
        gui_rect_t clip = { ax + size + 6, ay, wg->rect.w - size - 6, wg->rect.h };
        if (size < 10) size = 10;
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, wg->bg_color ? wg->bg_color : g_gui.colors.window_bg);
        for (i = 0; i < 8; ++i) {
            static const int dx[8] = { 0, 4, 6, 4, 0, -4, -6, -4 };
            static const int dy[8] = { -6, -4, 0, 4, 6, 4, 0, -4 };
            int idx = (i + phase) & 7;
            int dot = 2 + (i == 7 ? 1 : 0);
            int shade = 92 + i * 18;
            uint32_t color = wg->enabled ? gui_rgb(70, 120 + shade / 6, 180 + shade / 8) : gui_rgb(118 + shade / 8, 122 + shade / 8, 130 + shade / 8);
            int px = cx + (dx[idx] * size) / 16;
            int py = cy + (dy[idx] * size) / 16;
            gui_raw_fill_rect(px - dot / 2, py - dot / 2, dot, dot, color);
        }
        if ((wg->label_flags & GUI_SPINNER_SHOW_LABEL) && wg->text[0]) {
            gui_draw_window_title_text(ax + size + 6, gui_text_center_y(ay, wg->rect.h), wg->text, wg->enabled ? g_gui.colors.text_fg : gui_rgb(125, 130, 140), &clip);
        }
    } else if (wg->type == GUI_WIDGET_IMAGEVIEW) {
        uint32_t border = wg->enabled ? gui_rgb(148, 163, 184) : gui_rgb(199, 205, 214);
        uint32_t bg = wg->bg_color ? wg->bg_color : gui_rgb(248, 250, 252);
        int pad = 3;
        int inner_x = ax + pad;
        int inner_y = ay + pad;
        int inner_w = wg->rect.w - pad * 2;
        int inner_h = wg->rect.h - pad * 2;
        gui_rect_t text_clip = { inner_x, inner_y, inner_w, inner_h };
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        if (inner_w > 0 && inner_h > 0 && wg->image_pixels && wg->image_width > 0 && wg->image_height > 0) {
            int draw_w = inner_w;
            int draw_h = inner_h;
            int dx;
            int dy;
            if (wg->image_flags & GUI_IMAGEVIEW_KEEP_ASPECT) {
                uint32_t iw = wg->image_width;
                uint32_t ih = wg->image_height;
                if (iw > 0 && ih > 0) {
                    if ((uint32_t)inner_w * ih <= (uint32_t)inner_h * iw) {
                        draw_w = inner_w;
                        draw_h = (int)(((uint32_t)inner_w * ih) / iw);
                    } else {
                        draw_h = inner_h;
                        draw_w = (int)(((uint32_t)inner_h * iw) / ih);
                    }
                    if (draw_w < 1) draw_w = 1;
                    if (draw_h < 1) draw_h = 1;
                }
            }
            dx = inner_x + (inner_w - draw_w) / 2;
            dy = inner_y + (inner_h - draw_h) / 2;
            for (int yy = 0; yy < draw_h; ++yy) {
                uint32_t sy = (uint32_t)(((uint32_t)yy * wg->image_height) / (uint32_t)draw_h);
                for (int xx = 0; xx < draw_w; ++xx) {
                    uint32_t sx = (uint32_t)(((uint32_t)xx * wg->image_width) / (uint32_t)draw_w);
                    uint32_t color = wg->image_pixels[sy * wg->image_width + sx];
                    uint8_t alpha = (uint8_t)(color >> 24);
                    if (alpha == 0) continue;
                    gui_raw_put_pixel(dx + xx, dy + yy, color);
                }
            }
        } else {
            uint32_t line = (wg->image_flags & GUI_IMAGEVIEW_PLACEHOLDER) ? gui_rgb(148, 163, 184) : gui_rgb(203, 213, 225);
            gui_raw_fill_rect(inner_x, inner_y, inner_w, inner_h, gui_rgb(239, 246, 255));
            gui_raw_line(inner_x, inner_y, inner_x + inner_w - 1, inner_y, line);
            gui_raw_line(inner_x, inner_y, inner_x, inner_y + inner_h - 1, line);
            gui_raw_line(inner_x + inner_w - 1, inner_y, inner_x + inner_w - 1, inner_y + inner_h - 1, line);
            gui_raw_line(inner_x, inner_y + inner_h - 1, inner_x + inner_w - 1, inner_y + inner_h - 1, line);
            if (inner_w > 10 && inner_h > 10) {
                gui_raw_line(inner_x + 3, inner_y + 3, inner_x + inner_w - 4, inner_y + inner_h - 4, line);
                gui_raw_line(inner_x + inner_w - 4, inner_y + 3, inner_x + 3, inner_y + inner_h - 4, line);
            }
            gui_draw_window_title_text(inner_x + 6, gui_text_center_y(inner_y, inner_h), "Image", gui_rgb(100, 116, 139), &text_clip);
        }
    } else if (wg->type == GUI_WIDGET_PROGRESSBAR) {
        int min = wg->min_value;
        int max = wg->max_value;
        int val = wg->value;
        int pad = 2;
        int inner_w = wg->rect.w - pad * 2;
        int inner_h = wg->rect.h - pad * 2;
        int percent;
        uint32_t bg = wg->enabled ? gui_rgb(238, 242, 248) : gui_rgb(226, 229, 235);
        uint32_t border = wg->hovered || wg->focused ? g_gui.colors.accent : g_gui.colors.button_border;
        uint32_t fill = wg->enabled ? g_gui.colors.accent : gui_rgb(150, 154, 162);
        gui_rect_t clip = { ax + 2, ay + 1, wg->rect.w - 4, wg->rect.h - 2 };
        if (max <= min) max = min + 1;
        if (val < min) val = min;
        if (val > max) val = max;
        if (inner_w < 0) inner_w = 0;
        if (inner_h < 0) inner_h = 0;
        percent = ((val - min) * 100 + (max - min) / 2) / (max - min);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        if (wg->label_flags & GUI_PROGRESSBAR_INDETERMINATE) {
            int block_w = inner_w / 3;
            int offset = (wg->value + (int)(wg->id * 7u)) % (inner_w > 1 ? inner_w : 1);
            int bx = ax + pad + offset - block_w / 2;
            int stripe;
            if (block_w < 18) block_w = inner_w < 18 ? inner_w : 18;
            if (block_w > 0 && inner_h > 0) {
                if (bx < ax + pad) bx = ax + pad;
                if (bx + block_w > ax + pad + inner_w) bx = ax + pad + inner_w - block_w;
                gui_raw_fill_rect(bx, ay + pad, block_w, inner_h, fill);
            }
            for (stripe = -inner_h; stripe < inner_w + inner_h; stripe += 10) {
                int x0 = ax + pad + stripe;
                int y0 = ay + pad + inner_h - 1;
                int x1 = x0 + inner_h;
                int y1 = ay + pad;
                if (x1 >= ax + pad && x0 < ax + pad + inner_w) gui_raw_line(x0, y0, x1, y1, gui_rgb(190, 212, 245));
            }
        } else {
            int fill_w = ((val - min) * inner_w + (max - min) / 2) / (max - min);
            if (fill_w > 0 && inner_h > 0) gui_raw_fill_rect(ax + pad, ay + pad, fill_w, inner_h, fill);
            if (wg->label_flags & GUI_PROGRESSBAR_SHOW_PERCENT) {
                char pct[8];
                if (percent >= 100) { pct[0] = '1'; pct[1] = '0'; pct[2] = '0'; pct[3] = '%'; pct[4] = 0; }
                else if (percent >= 10) { pct[0] = (char)('0' + percent / 10); pct[1] = (char)('0' + percent % 10); pct[2] = '%'; pct[3] = 0; }
                else { pct[0] = (char)('0' + percent); pct[1] = '%'; pct[2] = 0; }
                gui_draw_window_title_text(ax + wg->rect.w / 2 - ((int)font_measure_text_width(font_get_default(), pct)) / 2, gui_text_center_y(ay, wg->rect.h), pct, wg->enabled ? g_gui.colors.text_fg : gui_rgb(125, 130, 140), &clip);
            }
        }
    } else if (wg->type == GUI_WIDGET_MENUBAR) {
        int count = gui_menubar_item_count(wg);
        int i;
        int cx = ax + 4;
        uint32_t bg = wg->enabled ? gui_rgb(245, 247, 251) : gui_rgb(235, 237, 242);
        uint32_t border = wg->focused ? g_gui.colors.accent : g_gui.colors.button_border;
        gui_rect_t clip = { ax + 2, ay + 1, wg->rect.w - 4, wg->rect.h - 2 };
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, bg);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        for (i = 0; i < count && cx < ax + wg->rect.w - 4; ++i) {
            char label[64];
            char shortcut[64];
            int iw = gui_menubar_item_width(wg, i);
            int active = (i == wg->value);
            uint32_t item_bg = active ? gui_rgb(216, 232, 255) : (wg->hovered ? gui_rgb(238, 243, 252) : bg);
            if (cx + iw > ax + wg->rect.w - 2) iw = ax + wg->rect.w - 2 - cx;
            gui_menubar_item_text(wg, i, label, sizeof(label), shortcut, sizeof(shortcut));
            gui_raw_fill_rect(cx, ay + 2, iw, wg->rect.h - 4, item_bg);
            if (active) {
                gui_raw_line(cx, ay + 2, cx + iw - 1, ay + 2, g_gui.colors.accent);
                gui_raw_line(cx, ay + wg->rect.h - 3, cx + iw - 1, ay + wg->rect.h - 3, g_gui.colors.accent);
            }
            gui_draw_window_title_text(cx + 8, gui_text_center_y(ay, wg->rect.h), label, wg->enabled ? g_gui.colors.text_fg : gui_rgb(145, 150, 160), &clip);
            if (shortcut[0]) {
                int sx = cx + iw - 8 - (int)font_measure_text_width(font_get_default(), shortcut);
                if (sx > cx + 8) gui_draw_window_title_text(sx, gui_text_center_y(ay, wg->rect.h), shortcut, gui_rgb(100, 110, 128), &clip);
            }
            cx += iw;
        }
    } else if (wg->type == GUI_WIDGET_TOAST) {
        uint32_t toast_type = wg->label_flags & GUI_DIALOG_TYPE_MASK;
        uint32_t accent = gui_rgb(59, 130, 246);
        uint32_t bg = gui_rgb(31, 41, 55);
        gui_rect_t title_clip = { ax + 12, ay + 7, wg->rect.w - 20, 14 };
        gui_rect_t msg_clip = { ax + 12, ay + 23, wg->rect.w - 20, wg->rect.h - 27 };
        const char *title = "Notification";
        if (toast_type == GUI_DIALOG_TYPE_WARNING) { accent = gui_rgb(217, 119, 6); title = "Warning"; }
        else if (toast_type == GUI_DIALOG_TYPE_ERROR) { accent = gui_rgb(220, 38, 38); title = "Error"; }
        else if (toast_type == GUI_DIALOG_TYPE_CONFIRM) { accent = gui_rgb(37, 99, 235); title = "Notice"; }
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, bg);
        gui_raw_fill_rect(ax, ay, 4, wg->rect.h, accent);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, accent);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, gui_rgb(17, 24, 39));
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, gui_rgb(17, 24, 39));
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, gui_rgb(17, 24, 39));
        gui_draw_window_title_text(ax + 12, ay + 7, wg->text[0] ? wg->text : title, gui_rgb(255, 255, 255), &title_clip);
        if (wg->placeholder[0]) gui_draw_window_title_text(ax + 12, ay + 23, wg->placeholder, gui_rgb(209, 213, 219), &msg_clip);
    } else if (wg->type == GUI_WIDGET_DIALOG) {
        const int title_h = 24;
        const int icon_size = 18;
        const int button_w = 54;
        const int button_h = 20;
        const int button_gap = 8;
        uint32_t dialog_type = wg->label_flags & GUI_DIALOG_TYPE_MASK;
        int has_cancel = (wg->label_flags & GUI_DIALOG_FLAG_CANCEL) || dialog_type == GUI_DIALOG_TYPE_CONFIRM;
        int ok_x = ax + wg->rect.w - button_w - 12;
        int cancel_x = ok_x - button_w - button_gap;
        int button_y = ay + wg->rect.h - button_h - 10;
        gui_rect_t msg_clip = { ax + 38, ay + title_h + 10, wg->rect.w - 50, wg->rect.h - title_h - button_h - 28 };
        uint32_t accent = gui_rgb(59, 130, 246);
        uint32_t title_bg = gui_rgb(235, 241, 252);
        const char *badge = "i";
        if (dialog_type == GUI_DIALOG_TYPE_WARNING) {
            accent = gui_rgb(217, 119, 6);
            title_bg = gui_rgb(255, 247, 237);
            badge = "!";
        } else if (dialog_type == GUI_DIALOG_TYPE_ERROR) {
            accent = gui_rgb(220, 38, 38);
            title_bg = gui_rgb(254, 242, 242);
            badge = "x";
        } else if (dialog_type == GUI_DIALOG_TYPE_CONFIRM) {
            accent = gui_rgb(37, 99, 235);
            title_bg = gui_rgb(239, 246, 255);
            badge = "?";
        }
        if (!wg->enabled) title_bg = gui_rgb(232, 235, 240);
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, wg->enabled ? gui_rgb(255, 255, 255) : gui_rgb(244, 245, 248));
        gui_raw_fill_rect(ax, ay, wg->rect.w, title_h, title_bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, accent);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, g_gui.colors.button_border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, g_gui.colors.button_border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, g_gui.colors.button_border);
        gui_raw_line(ax, ay + title_h, ax + wg->rect.w - 1, ay + title_h, gui_rgb(210, 218, 230));
        gui_draw_window_title_text(ax + 10, gui_text_center_y(ay, title_h), wg->text, g_gui.colors.text_fg, NULL);
        gui_raw_fill_rect(ax + 12, ay + title_h + 12, icon_size, icon_size, accent);
        gui_draw_text(ax + 18, ay + title_h + 16, badge, gui_rgb(255, 255, 255));
        gui_draw_window_title_text(msg_clip.x, msg_clip.y, wg->placeholder, wg->enabled ? g_gui.colors.text_fg : gui_rgb(145, 150, 160), &msg_clip);
        if (has_cancel) {
            uint32_t cancel_border = (wg->label_flags & GUI_DIALOG_FLAG_DEFAULT_CANCEL) ? accent : g_gui.colors.button_border;
            gui_raw_fill_rect(cancel_x, button_y, button_w, button_h, gui_rgb(248, 250, 252));
            gui_raw_line(cancel_x, button_y, cancel_x + button_w - 1, button_y, cancel_border);
            gui_raw_line(cancel_x, button_y + button_h - 1, cancel_x + button_w - 1, button_y + button_h - 1, cancel_border);
            gui_raw_line(cancel_x, button_y, cancel_x, button_y + button_h - 1, cancel_border);
            gui_raw_line(cancel_x + button_w - 1, button_y, cancel_x + button_w - 1, button_y + button_h - 1, cancel_border);
            if (wg->label_flags & GUI_DIALOG_FLAG_DEFAULT_CANCEL) {
                gui_raw_line(cancel_x + 2, button_y + 2, cancel_x + button_w - 3, button_y + 2, cancel_border);
            }
            gui_draw_text(cancel_x + 12, button_y + 5, "Cancel", g_gui.colors.text_fg);
        }
        gui_raw_fill_rect(ok_x, button_y, button_w, button_h, gui_rgb(224, 238, 255));
        gui_raw_line(ok_x, button_y, ok_x + button_w - 1, button_y, accent);
        gui_raw_line(ok_x, button_y + button_h - 1, ok_x + button_w - 1, button_y + button_h - 1, accent);
        gui_raw_line(ok_x, button_y, ok_x, button_y + button_h - 1, accent);
        gui_raw_line(ok_x + button_w - 1, button_y, ok_x + button_w - 1, button_y + button_h - 1, accent);
        if (!(wg->label_flags & GUI_DIALOG_FLAG_DEFAULT_CANCEL)) {
            gui_raw_line(ok_x + 2, button_y + 2, ok_x + button_w - 3, button_y + 2, accent);
        }
        gui_draw_text(ok_x + 20, button_y + 5, "OK", g_gui.colors.text_fg);
    } else if (wg->type == GUI_WIDGET_CONTEXTMENU) {
        int count = gui_menubar_item_count(wg);
        int row_h = gui_contextmenu_row_height();
        int i;
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, gui_rgb(255, 255, 255));
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, g_gui.colors.button_border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, g_gui.colors.button_border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, g_gui.colors.button_border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, g_gui.colors.button_border);
        for (i = 0; i < count; ++i) {
            char label[64];
            char shortcut[64];
            int yy = ay + 2 + i * row_h;
            int disabled = gui_contextmenu_item_disabled(wg, i);
            int selected = (i == wg->value);
            gui_menubar_item_text(wg, i, label, sizeof(label), shortcut, sizeof(shortcut));
            if (selected && !disabled) gui_raw_fill_rect(ax + 2, yy, wg->rect.w - 4, row_h, gui_rgb(219, 234, 254));
            gui_draw_text(ax + 8, yy + 4, label, disabled ? gui_rgb(148, 163, 184) : g_gui.colors.text_fg);
            if (shortcut[0]) {
                int sw = (int)font_measure_text_width(font_get_default(), shortcut);
                gui_draw_text(ax + wg->rect.w - sw - 8, yy + 4, shortcut, disabled ? gui_rgb(148, 163, 184) : gui_rgb(75, 85, 99));
            }
        }
    } else if (wg->type == GUI_WIDGET_TREEVIEW) {
        int total = gui_treeview_visible_count(wg);
        int row_h = gui_treeview_row_height();
        int visible = gui_treeview_visible_rows(wg);
        int i;
        uint32_t box_bg = wg->enabled ? gui_rgb(255, 255, 255) : gui_rgb(236, 238, 243);
        uint32_t border = wg->focused ? g_gui.colors.accent : (wg->hovered ? gui_rgb(120, 150, 210) : g_gui.colors.button_border);
        gui_rect_t clip = { ax + 3, ay + 3, wg->rect.w - 10, wg->rect.h - 6 };
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, box_bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_treeview_ensure_visible(wg);
        for (i = 0; i < visible && wg->min_value + i < total; ++i) {
            int row = gui_treeview_nth_visible(wg, wg->min_value + i);
            int iy = ay + 2 + i * row_h;
            int level = 0, folder = 0, expanded = 0;
            const char *label = "";
            char line[GUI_WIDGET_TEXT_CAP];
            char text[GUI_WIDGET_TEXT_CAP];
            int tx;
            uint32_t row_bg = (row == wg->value) ? gui_rgb(210, 228, 255) : box_bg;
            if (row < 0) continue;
            gui_treeview_copy_line(wg->placeholder, row, line, sizeof(line));
            gui_treeview_parse_node(line, &level, &folder, &expanded, &label);
            gui_raw_fill_rect(ax + 2, iy, wg->rect.w - 4, row_h, row_bg);
            tx = ax + 6 + level * 14;
            if (wg->label_flags & GUI_TREEVIEW_FLAG_SHOW_LINES) {
                gui_raw_line(tx - 7, iy, tx - 7, iy + row_h - 1, gui_rgb(220, 226, 235));
                gui_raw_line(tx - 7, iy + row_h / 2, tx - 1, iy + row_h / 2, gui_rgb(220, 226, 235));
            }
            if (folder) {
                gui_raw_fill_rect(tx, iy + row_h / 2 - 5, 10, 10, gui_rgb(245, 248, 252));
                gui_raw_line(tx, iy + row_h / 2 - 5, tx + 9, iy + row_h / 2 - 5, border);
                gui_raw_line(tx, iy + row_h / 2 + 4, tx + 9, iy + row_h / 2 + 4, border);
                gui_raw_line(tx, iy + row_h / 2 - 5, tx, iy + row_h / 2 + 4, border);
                gui_raw_line(tx + 9, iy + row_h / 2 - 5, tx + 9, iy + row_h / 2 + 4, border);
                gui_raw_line(tx + 2, iy + row_h / 2, tx + 7, iy + row_h / 2, g_gui.colors.accent);
                if (!expanded) gui_raw_line(tx + 4, iy + row_h / 2 - 3, tx + 4, iy + row_h / 2 + 3, g_gui.colors.accent);
            }
            text[0] = 0;
            if ((wg->label_flags & GUI_TREEVIEW_FLAG_SHOW_ICONS) && folder) gui_tableview_append(text, sizeof(text), expanded ? "[v] " : "[>] ");
            else if (wg->label_flags & GUI_TREEVIEW_FLAG_SHOW_ICONS) gui_tableview_append(text, sizeof(text), "[-] ");
            gui_tableview_append(text, sizeof(text), label);
            gui_draw_window_title_text(tx + 14, gui_text_center_y(iy, row_h), text, wg->enabled ? g_gui.colors.text_fg : gui_rgb(145, 150, 160), &clip);
        }
        if (total > visible) {
            int track_h = wg->rect.h - 4;
            int thumb_h = (visible * track_h) / total;
            int max_scroll = total - visible;
            int thumb_y;
            if (thumb_h < 12) thumb_h = 12;
            if (thumb_h > track_h) thumb_h = track_h;
            thumb_y = ay + 2 + (max_scroll > 0 ? (wg->min_value * (track_h - thumb_h)) / max_scroll : 0);
            gui_raw_fill_rect(ax + wg->rect.w - 6, ay + 2, 4, track_h, gui_rgb(226, 230, 238));
            gui_raw_fill_rect(ax + wg->rect.w - 6, thumb_y, 4, thumb_h, gui_rgb(150, 162, 182));
        }
    } else if (wg->type == GUI_WIDGET_SCROLLBAR) {
        int min = wg->min_value;
        int max = wg->max_value;
        int value = wg->value;
        int horizontal = gui_scrollbar_is_horizontal(wg);
        int track_x = horizontal ? ax + 8 : ax + wg->rect.w / 2;
        int track_y = horizontal ? ay + wg->rect.h / 2 : ay + 8;
        int track_len = (horizontal ? wg->rect.w : wg->rect.h) - 16;
        int knob_pos;
        uint32_t track_bg = wg->bg_color ? wg->bg_color : gui_rgb(214, 219, 229);
        uint32_t fill = wg->fg_color ? wg->fg_color : g_gui.colors.accent;
        uint32_t knob = wg->pressed ? gui_rgb(220, 236, 255) : gui_rgb(255, 255, 255);
        uint32_t border = (wg->hovered || wg->focused) ? g_gui.colors.accent : g_gui.colors.button_border;
        if (max <= min) max = min + 1;
        if (value < min) value = min;
        if (value > max) value = max;
        if (track_len <= 0) track_len = 1;
        knob_pos = ((value - min) * track_len) / (max - min);
        if (!wg->enabled) {
            fill = gui_rgb(150, 154, 162);
            knob = gui_rgb(232, 234, 238);
        }
        if (horizontal) {
            int knob_x = track_x + knob_pos;
            gui_raw_fill_rect(track_x, track_y - 2, track_len, 4, track_bg);
            gui_raw_fill_rect(track_x, track_y - 2, knob_x - track_x, 4, fill);
            gui_raw_fill_rect(knob_x - 5, track_y - 7, 11, 14, knob);
            gui_raw_line(knob_x - 5, track_y - 7, knob_x + 5, track_y - 7, border);
            gui_raw_line(knob_x - 5, track_y + 6, knob_x + 5, track_y + 6, border);
            gui_raw_line(knob_x - 5, track_y - 7, knob_x - 5, track_y + 6, border);
            gui_raw_line(knob_x + 5, track_y - 7, knob_x + 5, track_y + 6, border);
        } else {
            int knob_y = track_y + knob_pos;
            gui_raw_fill_rect(track_x - 2, track_y, 4, track_len, track_bg);
            gui_raw_fill_rect(track_x - 2, track_y, 4, knob_y - track_y, fill);
            gui_raw_fill_rect(track_x - 7, knob_y - 5, 14, 11, knob);
            gui_raw_line(track_x - 7, knob_y - 5, track_x + 6, knob_y - 5, border);
            gui_raw_line(track_x - 7, knob_y + 5, track_x + 6, knob_y + 5, border);
            gui_raw_line(track_x - 7, knob_y - 5, track_x - 7, knob_y + 5, border);
            gui_raw_line(track_x + 6, knob_y - 5, track_x + 6, knob_y + 5, border);
        }
    } else if (wg->type == GUI_WIDGET_SCROLLVIEW) {
        int content_w = wg->min_value > wg->rect.w ? wg->min_value : wg->rect.w;
        int content_h = wg->max_value > wg->rect.h ? wg->max_value : wg->rect.h;
        int scroll_x = gui_scrollview_clamp_x(wg, wg->value);
        int scroll_y = gui_scrollview_clamp_y(wg, wg->step);
        int show_v = content_h > wg->rect.h;
        int show_h = content_w > wg->rect.w;
        uint32_t bg = wg->bg_color ? wg->bg_color : gui_rgb(248, 250, 252);
        uint32_t border = (wg->hovered || wg->focused) ? g_gui.colors.accent : (wg->panel_border_color ? wg->panel_border_color : g_gui.colors.button_border);
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, bg);
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        {
            gui_rect_t old_clip = g_gui.clip_rect;
            int old_clip_enabled = g_gui.clip_enabled;
            gui_rect_t child_clip;
            uint32_t i;
            child_clip.x = ax + 1;
            child_clip.y = ay + 1;
            child_clip.w = wg->rect.w - 2 - (show_v ? 10 : 0);
            child_clip.h = wg->rect.h - 2 - (show_h ? 10 : 0);
            if (child_clip.w > 0 && child_clip.h > 0) {
                gui_set_clip_rect(&child_clip);
                for (i = 0; i < wg->owner->widget_count; i++) {
                    gui_widget_t *child = &wg->owner->widgets[i];
                    if (child == wg || !child->visible || !gui_widget_is_child_of(child, wg)) continue;
                    gui_draw_widget(child);
                }
            }
            if (old_clip_enabled) gui_set_clip_rect(&old_clip);
            else gui_clear_clip_rect();
        }
        if (show_v) {
            int track_h = wg->rect.h - (show_h ? 12 : 4) - 4;
            int track_x = ax + wg->rect.w - 8;
            int track_y = ay + 2;
            int thumb_h = (wg->rect.h * track_h) / content_h;
            int thumb_y;
            if (track_h < 1) track_h = 1;
            if (thumb_h < 10) thumb_h = 10;
            if (thumb_h > track_h) thumb_h = track_h;
            thumb_y = track_y + (scroll_y * (track_h - thumb_h)) / (content_h - wg->rect.h);
            gui_raw_fill_rect(track_x, track_y, 4, track_h, gui_rgb(222, 226, 236));
            gui_raw_fill_rect(track_x - 1, thumb_y, 6, thumb_h, g_gui.colors.accent);
        }
        if (show_h) {
            int track_w = wg->rect.w - (show_v ? 12 : 4) - 4;
            int track_x = ax + 2;
            int track_y = ay + wg->rect.h - 8;
            int thumb_w = (wg->rect.w * track_w) / content_w;
            int thumb_x;
            if (track_w < 1) track_w = 1;
            if (thumb_w < 10) thumb_w = 10;
            if (thumb_w > track_w) thumb_w = track_w;
            thumb_x = track_x + (scroll_x * (track_w - thumb_w)) / (content_w - wg->rect.w);
            gui_raw_fill_rect(track_x, track_y, track_w, 4, gui_rgb(222, 226, 236));
            gui_raw_fill_rect(thumb_x, track_y - 1, thumb_w, 6, g_gui.colors.accent);
        }
    } else if (wg->type == GUI_WIDGET_TOGGLE) {
        int on = gui_toggle_get_checked(wg);
        int sw = wg->rect.w < 44 ? wg->rect.w : 44;
        int sh = wg->rect.h < 22 ? wg->rect.h : 22;
        int sx = ax + wg->rect.w - sw;
        int sy = ay + (wg->rect.h - sh) / 2;
        int knob_size = sh - 6;
        int knob_x = on ? sx + sw - knob_size - 3 : sx + 3;
        uint32_t track = on ? g_gui.colors.accent : gui_rgb(176, 182, 194);
        uint32_t border = (wg->hovered || wg->focused) ? g_gui.colors.accent : g_gui.colors.button_border;
        uint32_t text_color = wg->fg_color ? wg->fg_color : g_gui.colors.text_fg;
        if (sw < 18) sw = 18;
        if (sh < 14) sh = 14;
        knob_size = sh - 6;
        knob_x = on ? sx + sw - knob_size - 3 : sx + 3;
        if (!wg->enabled) {
            track = gui_rgb(140, 144, 152);
            text_color = gui_rgb(116, 122, 132);
        }
        if (wg->text[0]) {
            gui_rect_t clip = { ax, ay, wg->rect.w - sw - 8, wg->rect.h };
            gui_draw_window_title_text(ax, gui_text_center_y(ay, wg->rect.h), wg->text, text_color, &clip);
        }
        gui_raw_fill_rect(sx, sy, sw, sh, track);
        gui_raw_line(sx, sy, sx + sw - 1, sy, border);
        gui_raw_line(sx, sy + sh - 1, sx + sw - 1, sy + sh - 1, border);
        gui_raw_line(sx, sy, sx, sy + sh - 1, border);
        gui_raw_line(sx + sw - 1, sy, sx + sw - 1, sy + sh - 1, border);
        gui_raw_fill_rect(knob_x, sy + 3, knob_size, knob_size, gui_rgb(255, 255, 255));
    } else if (wg->type == GUI_WIDGET_CHECKBOX) {
        int checked = gui_checkbox_get_checked(wg);
        int box = wg->rect.h - 8;
        int bx;
        int by;
        uint32_t border = (wg->hovered || wg->focused) ? g_gui.colors.accent : g_gui.colors.button_border;
        uint32_t fill = checked ? g_gui.colors.accent : gui_rgb(250, 250, 250);
        uint32_t text_color = wg->fg_color ? wg->fg_color : g_gui.colors.text_fg;
        if (box > 18) box = 18;
        if (box < 10) box = 10;
        bx = ax + 2;
        by = ay + (wg->rect.h - box) / 2;
        if (!wg->enabled) {
            fill = checked ? gui_rgb(140, 144, 152) : gui_rgb(226, 229, 236);
            border = gui_rgb(150, 156, 168);
            text_color = gui_rgb(116, 122, 132);
        }
        gui_raw_fill_rect(bx, by, box, box, fill);
        gui_raw_line(bx, by, bx + box - 1, by, border);
        gui_raw_line(bx, by + box - 1, bx + box - 1, by + box - 1, border);
        gui_raw_line(bx, by, bx, by + box - 1, border);
        gui_raw_line(bx + box - 1, by, bx + box - 1, by + box - 1, border);
        if (checked) {
            uint32_t mark = gui_rgb(255, 255, 255);
            gui_raw_line(bx + 3, by + box / 2, bx + box / 2 - 1, by + box - 4, mark);
            gui_raw_line(bx + box / 2 - 1, by + box - 4, bx + box - 3, by + 3, mark);
        }
        if (wg->text[0]) {
            gui_rect_t clip = { bx + box + 6, ay, wg->rect.w - box - 8, wg->rect.h };
            gui_draw_window_title_text(clip.x, gui_text_center_y(ay, wg->rect.h), wg->text, text_color, &clip);
        }
    } else if (wg->type == GUI_WIDGET_RADIOBUTTON) {
        int checked = gui_radiobutton_get_checked(wg);
        int box = wg->rect.h - 8;
        int bx;
        int by;
        int r;
        int cx;
        int cy;
        int dy;
        uint32_t border = (wg->hovered || wg->focused) ? g_gui.colors.accent : g_gui.colors.button_border;
        uint32_t fill = gui_rgb(250, 250, 250);
        uint32_t dot = g_gui.colors.accent;
        uint32_t text_color = wg->fg_color ? wg->fg_color : g_gui.colors.text_fg;
        if (box > 18) box = 18;
        if (box < 10) box = 10;
        bx = ax + 2;
        by = ay + (wg->rect.h - box) / 2;
        r = box / 2;
        cx = bx + r;
        cy = by + r;
        if (!wg->enabled) {
            fill = gui_rgb(226, 229, 236);
            border = gui_rgb(150, 156, 168);
            dot = gui_rgb(140, 144, 152);
            text_color = gui_rgb(116, 122, 132);
        }
        for (dy = -r; dy <= r; dy++) {
            int ad = dy < 0 ? -dy : dy;
            int half = r - 1;
            if (ad >= r) half = 0;
            else if (ad == r - 1) half = r / 2;
            gui_raw_line(cx - half, cy + dy, cx + half, cy + dy, fill);
            gui_raw_line(cx - half, cy + dy, cx - half, cy + dy, border);
            gui_raw_line(cx + half, cy + dy, cx + half, cy + dy, border);
        }
        gui_raw_line(cx - r / 2, cy - r + 1, cx + r / 2, cy - r + 1, border);
        gui_raw_line(cx - r / 2, cy + r - 1, cx + r / 2, cy + r - 1, border);
        if (checked) {
            int ir = r / 2;
            for (dy = -ir; dy <= ir; dy++) {
                int ad = dy < 0 ? -dy : dy;
                int half = ir - ad;
                gui_raw_line(cx - half, cy + dy, cx + half, cy + dy, dot);
            }
        }
        if (wg->text[0]) {
            gui_rect_t clip = { bx + box + 6, ay, wg->rect.w - box - 8, wg->rect.h };
            gui_draw_window_title_text(clip.x, gui_text_center_y(ay, wg->rect.h), wg->text, text_color, &clip);
        }
    }
}

static void gui_update_toasts(gui_window_t *w) {
    uint32_t now;
    uint32_t i;
    int changed = 0;
    if (!w) return;
    now = (uint32_t)sched_time_ms();
    for (i = 0; i < w->widget_count; ++i) {
        gui_widget_t *wg = &w->widgets[i];
        if (wg->type == GUI_WIDGET_TOAST && wg->visible && wg->max_value > 0 && now >= (uint32_t)wg->max_value) {
            wg->visible = 0;
            if (g_gui.focused_widget == wg) gui_set_focused_widget(0);
            changed = 1;
        }
    }
    if (changed) gui_invalidate_rect(w->rect.x, w->rect.y, w->rect.w, w->rect.h);
}

static void gui_draw_window(gui_window_t *w) {
    gui_update_toasts(w);
    uint32_t i;
    uint32_t border;
    uint32_t title;
    uint32_t title_text;
    uint32_t close_bg;
    uint32_t min_bg;
    uint32_t max_bg;
    uint32_t btn_fg;
    if (!w || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;

    border = w->active ? g_gui.colors.accent : g_gui.colors.window_border;
    title = w->active ? g_gui.colors.title_bg : gui_rgb(50, 55, 68);
    title_text = w->active ? gui_rgb(235, 242, 255) : gui_rgb(150, 158, 172);
    close_bg = w->active ? gui_rgb(160, 50, 55) : gui_rgb(95, 60, 65);
    min_bg = w->active ? gui_rgb(80, 90, 105) : gui_rgb(55, 62, 75);
    max_bg = w->active ? gui_rgb(80, 110, 90) : gui_rgb(55, 70, 60);
    btn_fg = w->active ? gui_rgb(255, 255, 255) : gui_rgb(170, 178, 192);

    gui_raw_fill_rect(w->rect.x, w->rect.y, w->rect.w, w->rect.h, border);
    gui_raw_fill_rect(w->rect.x + GUI_BORDER_SIZE, w->rect.y + GUI_BORDER_SIZE,
                      w->rect.w - GUI_BORDER_SIZE * 2, GUI_TITLE_HEIGHT - GUI_BORDER_SIZE,
                      title);
    gui_raw_fill_rect(w->rect.x + GUI_BORDER_SIZE, w->rect.y + GUI_TITLE_HEIGHT,
                      w->rect.w - GUI_BORDER_SIZE * 2, w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE,
                      w->bg_color ? w->bg_color : g_gui.colors.window_bg);

    if (w->flags & GUI_WINDOW_FLAG_CLOSABLE) {
        gui_rect_t c = gui_close_rect(w);
        gui_raw_fill_rect(c.x, c.y, c.w, c.h, close_bg);
        gui_raw_line(c.x + 3, c.y + 3, c.x + c.w - 4, c.y + c.h - 4, btn_fg);
        gui_raw_line(c.x + c.w - 4, c.y + 3, c.x + 3, c.y + c.h - 4, btn_fg);
    }

    if (w->flags & GUI_WINDOW_FLAG_MINIMIZABLE) {
        gui_rect_t m = gui_min_rect(w);
        gui_raw_fill_rect(m.x, m.y, m.w, m.h, min_bg);
        gui_raw_line(m.x + 3, m.y + m.h - 4, m.x + m.w - 4, m.y + m.h - 4, btn_fg);
    }

    if (w->flags & GUI_WINDOW_FLAG_MAXIMIZABLE) {
        gui_rect_t mx = gui_max_rect(w);
        gui_raw_fill_rect(mx.x, mx.y, mx.w, mx.h, max_bg);
        if (w->flags & GUI_WINDOW_FLAG_MAXIMIZED) {
            /* restore icon: two overlapping squares */
            gui_raw_line(mx.x + 5, mx.y + 3, mx.x + mx.w - 3, mx.y + 3, btn_fg);
            gui_raw_line(mx.x + 5, mx.y + 3, mx.x + 5, mx.y + 5, btn_fg);
            gui_raw_line(mx.x + mx.w - 3, mx.y + 3, mx.x + mx.w - 3, mx.y + mx.h - 5, btn_fg);
            gui_raw_line(mx.x + 5, mx.y + 5, mx.x + mx.w - 3, mx.y + 5, btn_fg);
            gui_raw_line(mx.x + 3, mx.y + 5, mx.x + mx.w - 5, mx.y + 5, btn_fg);
            gui_raw_line(mx.x + 3, mx.y + 5, mx.x + 3, mx.y + mx.h - 3, btn_fg);
            gui_raw_line(mx.x + mx.w - 5, mx.y + 5, mx.x + mx.w - 5, mx.y + mx.h - 3, btn_fg);
            gui_raw_line(mx.x + 3, mx.y + mx.h - 3, mx.x + mx.w - 5, mx.y + mx.h - 3, btn_fg);
        } else {
            /* maximize icon: single square */
            gui_raw_line(mx.x + 3, mx.y + 3, mx.x + mx.w - 4, mx.y + 3, btn_fg);
            gui_raw_line(mx.x + 3, mx.y + mx.h - 4, mx.x + mx.w - 4, mx.y + mx.h - 4, btn_fg);
            gui_raw_line(mx.x + 3, mx.y + 3, mx.x + 3, mx.y + mx.h - 4, btn_fg);
            gui_raw_line(mx.x + mx.w - 4, mx.y + 3, mx.x + mx.w - 4, mx.y + mx.h - 4, btn_fg);
        }
    }

    {
        gui_rect_t title_clip;
        int title_x = w->rect.x + 8;
        int title_y = gui_text_center_y(w->rect.y, GUI_TITLE_HEIGHT);
        int title_right = w->rect.x + w->rect.w - GUI_BORDER_SIZE - 6;
        if (w->flags & GUI_WINDOW_FLAG_CLOSABLE) {
            gui_rect_t c = gui_close_rect(w);
            title_right = c.x - 6;
        }
        if (w->flags & GUI_WINDOW_FLAG_MINIMIZABLE) {
            gui_rect_t m = gui_min_rect(w);
            if (m.x - 6 < title_right) title_right = m.x - 6;
        }
        if (w->flags & GUI_WINDOW_FLAG_MAXIMIZABLE) {
            gui_rect_t mx = gui_max_rect(w);
            if (mx.x - 6 < title_right) title_right = mx.x - 6;
        }
        title_clip.x = title_x;
        title_clip.y = w->rect.y + GUI_BORDER_SIZE;
        title_clip.w = title_right - title_x;
        title_clip.h = GUI_TITLE_HEIGHT - GUI_BORDER_SIZE;
        if (title_clip.w > 0 && title_clip.h > 0) {
            gui_draw_window_title_text(title_x, title_y, w->title, title_text, &title_clip);
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
            if (w->widgets[i].parent_id != 0) continue;
            gui_draw_widget(&w->widgets[i]);
        }
        gui_clear_clip_rect();
        for (i = 0; i < w->widget_count; i++) {
            if (w->widgets[i].parent_id != 0) continue;
            if (w->widgets[i].type == GUI_WIDGET_SELECT || w->widgets[i].type == GUI_WIDGET_COMBOBOX) {
                gui_draw_select_dropdown(&w->widgets[i]);
            }
        }
    }

    if (w->flags & GUI_WINDOW_FLAG_TERMINAL) {
        gui_terminal_redraw();
    }

    if ((w->flags & GUI_WINDOW_FLAG_RESIZABLE) && !(w->flags & GUI_WINDOW_FLAG_MAXIMIZED)) {
        gui_rect_t g = gui_resize_grip_rect(w);
        uint32_t gc = w->active ? gui_rgb(190, 200, 220) : gui_rgb(110, 118, 132);
        int k;
        for (k = 0; k < g.w; k += 3) {
            gui_raw_line(g.x + g.w - 1 - k, g.y + g.h - 1,
                         g.x + g.w - 1,     g.y + g.h - 1 - k, gc);
        }
    }
}

/* 锁屏捕获门闸：锁屏运行期间置 1，强制 gui_should_capture_key_code_*
 * 捕获所有按键（锁屏是自绘全屏遮罩，没有焦点 widget，否则按键会被丢弃）。 */
static volatile int g_lockscreen_capture = 0;
void gui_set_lockscreen_capture(int on) { g_lockscreen_capture = on ? 1 : 0; }
int  gui_get_lockscreen_capture(void)   { return g_lockscreen_capture; }

void gui_event_push(gui_event_t event) {
    if (event.type == GUI_EVENT_MOUSE_MOVE && g_gui.event_count > 0) {
        uint32_t last = (g_gui.event_tail + GUI_EVENT_QUEUE_SIZE - 1) % GUI_EVENT_QUEUE_SIZE;
        if (g_gui.events[last].type == GUI_EVENT_MOUSE_MOVE) {
            /* 闁告艾鐗嗛懟鐔告交閻愮數锟?MOVE闁挎稑鐭傛导鈺呭礂瀹ュ泦鈺呭礉閵娿倗鐨戝ù鐘烘硾閸╂稑顭ㄩ敓鐘承曢柛鎺擃殔椤曢亶鎳涚€电浠柛?闁归攱鐗曟慨鈺傜鐎ｂ晜顐藉☉鎾卞灩閵囨垿锟?*/
            g_gui.events[last] = event;
            return;
        }
    }

    if (g_gui.event_count >= GUI_EVENT_QUEUE_SIZE) {
        /* 闂傚啰鍠庨崹顏勵煥閳╁啯顦у☉鎾卞灩缁辨棃寮甸埀顒勬嚀娴ｉ鐨戝ù鐘侯啇缁辨繃绌卞┑濠勬 DOWN/UP 缂佹稑顦崣褔鏌ㄩ鑽ょ殤濞寸姾娉涜ぐ鍙夌閵夈儱寮抽梻鍐枂锟?*/
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

void gui_alt_tab_cycle(void) {
    uint32_t i;
    int found = -1;
    /* Pick second-topmost visible non-minimized window */
    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[g_gui.window_count - 1 - i];
        gui_window_t *w = &g_gui.windows[idx];
        if (!w->used || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) continue;
        if (w == g_gui.active_window) continue;
        found = (int)idx;
        break;
    }
    if (found < 0) {
        /* fallback: a minimized window? restore it */
        for (i = 0; i < g_gui.window_count; i++) {
            uint32_t idx = g_gui.z_order[g_gui.window_count - 1 - i];
            gui_window_t *w = &g_gui.windows[idx];
            if (!w->used) continue;
            if (w == g_gui.active_window) continue;
            if (w->flags & GUI_WINDOW_FLAG_MINIMIZED) {
                gui_restore_window(w);
                return;
            }
        }
        return;
    }
    gui_set_active_window(&g_gui.windows[found]);
}

void gui_toggle_start_menu(void) {
    g_gui.desktop_start_menu_open = g_gui.desktop_start_menu_open ? 0 : 1;
    g_gui.desktop_start_menu_scroll_dragging = 0;
    if (g_gui.desktop_start_menu_open) {
        g_gui.desktop_start_menu_scroll = 0;
        gui_launcher_scan_bin(2);
        gui_update_start_menu_layout();
    }
    gui_invalidate_all();
}

static void gui_widget_release_resources(gui_widget_t *widget) {
    if (!widget) return;
    if (widget->image_pixels) {
        kfree(widget->image_pixels);
        widget->image_pixels = 0;
    }
    widget->image_width = 0;
    widget->image_height = 0;
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
    for (i = 0; i < window->widget_count && i < GUI_MAX_WIDGETS_PER_WIN; ++i) {
        gui_widget_release_resources(&window->widgets[i]);
    }

    /* fire on_close hook (if any) before zeroing memory */
    if (window->on_close) {
        void (*cb)(gui_window_t *, void *) = window->on_close;
        void *ud = window->close_user_data;
        window->on_close = 0;
        window->close_user_data = 0;
        cb(window, ud);
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

void gui_window_set_on_close(gui_window_t *window,
                             void (*cb)(gui_window_t *win, void *user_data),
                             void *user_data) {
    if (!window || !window->used) return;
    window->on_close = cb;
    window->close_user_data = user_data;
}

void gui_minimize_window(gui_window_t *window) {
    if (!window || !window->used) return;

    window->flags |= GUI_WINDOW_FLAG_MINIMIZED;
    window->visible = 1;
    window->active = 0;
    window->dragging = 0;
    window->resizing = 0;

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

void gui_toggle_maximize_window(gui_window_t *window) {
    if (!window || !window->used) return;
    if (!(window->flags & GUI_WINDOW_FLAG_MAXIMIZABLE)) return;
    if (window->flags & GUI_WINDOW_FLAG_MAXIMIZED) {
        window->rect = window->saved_rect;
        window->flags &= ~GUI_WINDOW_FLAG_MAXIMIZED;
    } else {
        window->saved_rect = window->rect;
        window->rect.x = 0;
        window->rect.y = 0;
        window->rect.w = (int)g_gui.width;
        int max_h = (int)g_gui.height - GUI_TASKBAR_HEIGHT;
        if (max_h < 60) max_h = 60;
        window->rect.h = max_h;
        window->flags |= GUI_WINDOW_FLAG_MAXIMIZED;
    }
    window->dragging = 0;
    window->resizing = 0;
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
    gui_rect_t search_box;
    gui_rect_t terminal_button;
    gui_rect_t sticky_button;
    int first_window_x;
    int item_y;
    int item_h;
} gui_taskbar_layout_t;

static int gui_taskbar_button_width(gui_window_t *window) {
    (void)window;
    return GUI_TASKBAR_ICON_BUTTON_W;
}

static int gui_taskbar_search_width(void) {
    int w = GUI_TASKBAR_SEARCH_W;
    if ((int)g_gui.width < 520) w = 120;
    if ((int)g_gui.width < 420) w = GUI_TASKBAR_SEARCH_MIN_W;
    return w;
}

static int gui_taskbar_center_y(int item_h) {
    int bar_y = (int)g_gui.height - GUI_TASKBAR_HEIGHT;
    if (item_h < 0) item_h = 0;
    return bar_y + (GUI_TASKBAR_HEIGHT - item_h) / 2;
}

static int gui_taskbar_item_y(void) {
    return gui_taskbar_center_y(GUI_TASKBAR_HEIGHT - 6);
}

static int gui_taskbar_tray_y(int h) {
    return gui_taskbar_center_y(h);
}

static int gui_taskbar_icon_y(gui_rect_t rect, int icon_h) {
    if (icon_h < 0) icon_h = 0;
    return rect.y + (rect.h - icon_h) / 2;
}

static int gui_taskbar_text_y(gui_rect_t rect, int text_h) {
    if (text_h < 0) text_h = 0;
    return rect.y + (rect.h - text_h) / 2;
}

static int gui_taskbar_should_list_window(gui_window_t *w) {
    if (!w || !w->used || !w->visible) return 0;
    if (w->flags & GUI_WINDOW_FLAG_TERMINAL) return 0;
    if (strcmp(w->title, "桌面便签") == 0) return 0;
    return 1;
}

static int gui_taskbar_content_width(void) {
    uint32_t i;
    int width = GUI_TASKBAR_START_W + 6 + GUI_TASKBAR_START_W + 6 + GUI_TASKBAR_START_W;
    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!gui_taskbar_should_list_window(w)) continue;
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
    if (bar_w < GUI_TASKBAR_START_W * 3 + 12 + padding * 2) {
        bar_w = GUI_TASKBAR_START_W * 3 + 12 + padding * 2;
    }
    if (bar_w > (int)g_gui.width) bar_w = (int)g_gui.width;

    layout->bar.x = ((int)g_gui.width - bar_w) / 2;
    if (layout->bar.x < 0) layout->bar.x = 0;
    layout->bar.y = y;
    layout->bar.w = bar_w;
    layout->bar.h = GUI_TASKBAR_HEIGHT;
    layout->search_box.x = margin;
    layout->search_box.h = GUI_TASKBAR_HEIGHT - 6;
    layout->search_box.y = gui_taskbar_item_y();
    layout->search_box.w = gui_taskbar_search_width();
    layout->start_button.x = layout->bar.x + padding;
    layout->start_button.h = GUI_TASKBAR_HEIGHT - 6;
    layout->start_button.y = gui_taskbar_item_y();
    layout->start_button.w = GUI_TASKBAR_START_W;
    layout->terminal_button.x = layout->start_button.x + layout->start_button.w + 6;
    layout->terminal_button.h = GUI_TASKBAR_HEIGHT - 6;
    layout->terminal_button.y = gui_taskbar_item_y();
    layout->terminal_button.w = GUI_TASKBAR_START_W;
    layout->sticky_button.x = layout->terminal_button.x + layout->terminal_button.w + 6;
    layout->sticky_button.h = GUI_TASKBAR_HEIGHT - 6;
    layout->sticky_button.y = gui_taskbar_item_y();
    layout->sticky_button.w = GUI_TASKBAR_START_W;
    layout->first_window_x = layout->sticky_button.x + layout->sticky_button.w + 6;
    layout->item_h = GUI_TASKBAR_HEIGHT - 6;
    layout->item_y = gui_taskbar_item_y();
}

static int gui_taskbar_terminal_button_at(int x, int y) {
    gui_taskbar_layout_t layout;
    gui_taskbar_get_layout(&layout);
    return gui_rect_contains(&layout.terminal_button, x, y);
}

static int gui_taskbar_sticky_button_at(int x, int y) {
    gui_taskbar_layout_t layout;
    gui_taskbar_get_layout(&layout);
    return gui_rect_contains(&layout.sticky_button, x, y);
}

static int gui_is_taskbar_at(int x, int y) {
    gui_taskbar_layout_t layout;
    gui_taskbar_get_layout(&layout);
    return gui_rect_contains(&layout.bar, x, y) ||
           gui_rect_contains(&layout.search_box, x, y);
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
        if (!gui_taskbar_should_list_window(w)) continue;
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
    gui_taskbar_invalidate_icon_hover_change(layout.sticky_button, old_x, old_y, new_x, new_y);
    gui_taskbar_invalidate_icon_hover_change(g_network_widget_rect, old_x, old_y, new_x, new_y);
    bx = layout.first_window_x;
    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        gui_rect_t button;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!gui_taskbar_should_list_window(w)) continue;
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

static int      g_desktop_selected_icon = -1;
static int      g_desktop_last_click_icon = -1;
static uint32_t g_desktop_last_click_frame = 0;

static int gui_desktop_icon_selected(gui_desktop_icon_t *icon) {
    return icon &&
           g_desktop_selected_icon >= 0 &&
           g_desktop_selected_icon < (int)GUI_DESKTOP_MAX_ICONS &&
           icon == &g_gui.desktop_icons[g_desktop_selected_icon];
}

static int gui_desktop_icon_highlighted_at(gui_desktop_icon_t *icon, int x, int y) {
    return icon && icon->used &&
           !gui_desktop_icon_selected(icon) &&
           !g_gui.desktop_start_menu_open &&
           gui_rect_contains(&icon->rect, x, y) &&
           gui_window_at(x, y) == 0;
}

static void gui_desktop_invalidate_icon_hover_change(gui_desktop_icon_t *icon,
                                                     int old_x,
                                                     int old_y,
                                                     int new_x,
                                                     int new_y) {
    if (!icon || !icon->used || icon->rect.w <= 0 || icon->rect.h <= 0) return;
    if (gui_desktop_icon_highlighted_at(icon, old_x, old_y) !=
        gui_desktop_icon_highlighted_at(icon, new_x, new_y)) {
        gui_invalidate_rect(icon->rect.x - 3, icon->rect.y - 3,
                            icon->rect.w + 6, icon->rect.h + 6);
    }
}

static void gui_desktop_invalidate_hover_changes(int old_x, int old_y, int new_x, int new_y) {
    uint32_t i;
    if (g_gui.desktop_start_menu_open) return;
    for (i = 0; i < GUI_DESKTOP_MAX_ICONS; i++) {
        gui_desktop_invalidate_icon_hover_change(&g_gui.desktop_icons[i], old_x, old_y, new_x, new_y);
    }
}

static uint32_t g_gui_last_key_modifiers = 0;

uint32_t gui_get_last_key_modifiers(void) {
    return g_gui_last_key_modifiers;
}

void gui_post_key_code_with_modifiers(int key, uint32_t modifiers) {
    gui_event_t ev;
    if (!g_gui.initialized || !key) return;
    memset(&ev, 0, sizeof(ev));
    ev.type = GUI_EVENT_KEY_DOWN;
    ev.key = key;
    ev.modifiers = modifiers;
    ev.window = g_gui.active_window;
    g_gui_last_key_modifiers = modifiers;
    gui_event_push(ev);
}

void gui_post_key_code(int key) {
    gui_post_key_code_with_modifiers(key, 0);
}

void gui_post_key(char ch) {
    gui_post_key_code((int)(unsigned char)ch);
}










static int gui_desktop_note_begin_drag(int x, int y);
static void gui_desktop_note_drag_to(int x, int y);













__attribute__((optimize("no-jump-tables")))
static void gui_handle_mouse_down(int x, int y) {
    gui_window_t *event_window = gui_window_at(x, y);
    if (event_window) gui_user_post_mouse_event(event_window, GUI_USER_EVENT_MOUSE_DOWN, x, y, 1, 0);
    if (gui_ctxmenu_is_open()) {
        gui_ctxmenu_handle_click(x, y);
        return;
    }

    if (g_gui.taskbar_search_focused && g_gui.taskbar_search_results_rect.w > 0) {
        int idx = gui_taskbar_search_result_index_at(x, y);
        if (idx >= 0) {
            g_gui.taskbar_search_selected = idx;
            gui_invalidate_rect(g_gui.taskbar_search_results_rect.x,
                                g_gui.taskbar_search_results_rect.y,
                                g_gui.taskbar_search_results_rect.w,
                                g_gui.taskbar_search_results_rect.h);
            gui_taskbar_search_open_result((uint32_t)idx);
            return;
        }
        if (x >= g_gui.taskbar_search_results_rect.x &&
            x < g_gui.taskbar_search_results_rect.x + g_gui.taskbar_search_results_rect.w &&
            y >= g_gui.taskbar_search_results_rect.y &&
            y < g_gui.taskbar_search_results_rect.y + g_gui.taskbar_search_results_rect.h) {
            return;
        }
    }

    if (!event_window && gui_desktop_note_begin_drag(x, y)) {
        return;
    }

    if (gui_desktop_handle_click(x, y)) {
        gui_set_focused_widget(0);
        return;
    }

    if (gui_taskbar_terminal_button_at(x, y)) {
        gui_set_focused_widget(0);
        gui_terminal_open();
        return;
    }

    if (gui_taskbar_sticky_button_at(x, y)) {
        gui_set_focused_widget(0);
        gui_desktop_run_action(GUI_DESKTOP_ACTION_STICKY);
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
                gui_terminal_view_begin_selection(&g_gui.terminal.view, tc, trc);
                gui_terminal_invalidate_body();
            } else {
                gui_terminal_clear_selection();
            }
        }

        gui_set_active_window(w);
        close = gui_close_rect(w);
        minr = gui_min_rect(w);
        gui_rect_t maxr = gui_max_rect(w);

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

        if ((w->flags & GUI_WINDOW_FLAG_MAXIMIZABLE) && gui_rect_contains(&maxr, x, y)) {
            gui_set_focused_widget(0);
            if (g_gui.drag_window == w) g_gui.drag_window = 0;
            if (g_gui.pressed_widget && g_gui.pressed_widget->owner == w) g_gui.pressed_widget = 0;
            gui_toggle_maximize_window(w);
            return;
        }

        if ((w->flags & GUI_WINDOW_FLAG_RESIZABLE) && !(w->flags & GUI_WINDOW_FLAG_MAXIMIZED)) {
            gui_rect_t gr = gui_resize_grip_rect(w);
            if (gui_rect_contains(&gr, x, y)) {
                gui_set_focused_widget(0);
                w->resizing = 1;
                w->resize_start_mx = x;
                w->resize_start_my = y;
                w->resize_start_w = w->rect.w;
                w->resize_start_h = w->rect.h;
                g_gui.drag_window = w;
                return;
            }
        }

        gui_rect_t tr = gui_title_rect(w);
        if (gui_rect_contains(&tr, x, y)) {
            gui_set_focused_widget(0);
            /* double-click on title to toggle maximize */
            if ((w->flags & GUI_WINDOW_FLAG_MAXIMIZABLE) &&
                w->last_title_click_frame != 0 &&
                (g_gui.frame_counter - w->last_title_click_frame) < 18) {
                w->last_title_click_frame = 0;
                gui_toggle_maximize_window(w);
                return;
            }
            w->last_title_click_frame = g_gui.frame_counter;
            if (!(w->flags & GUI_WINDOW_FLAG_MAXIMIZED)) {
                w->dragging = 1;
                w->drag_offset_x = x - w->rect.x;
                w->drag_offset_y = y - w->rect.y;
                g_gui.drag_window = w;
            }
            return;
        }

        gui_widget_t *wg = gui_widget_at_screen(x, y);
        if (wg && wg->type == GUI_WIDGET_SLIDER && wg->enabled) {
            gui_set_focused_widget(0);
            wg->pressed = 1;
            g_gui.slider_widget = wg;
            gui_slider_apply_screen_x(wg, x);
            gui_invalidate_all();
        } else if (wg && wg->type == GUI_WIDGET_SCROLLBAR && wg->enabled) {
            gui_set_focused_widget(0);
            wg->pressed = 1;
            g_gui.scrollbar_widget = wg;
            gui_scrollbar_apply_screen(wg, x, y);
            gui_invalidate_all();
        } else if (wg && wg->type == GUI_WIDGET_SPLITVIEW && wg->enabled && gui_splitview_hit_bar(wg, x, y)) {
            gui_set_focused_widget(wg);
            wg->pressed = 1;
            g_gui.splitview_widget = wg;
            gui_splitview_apply_screen(wg, x, y);
            gui_invalidate_all();
        } else if (gui_widget_is_clickable(wg)) {
            int select_index = -1;
            if (wg->type == GUI_WIDGET_SELECT || wg->type == GUI_WIDGET_COMBOBOX) {
                select_index = gui_select_dropdown_index_at(wg, x, y);
                if (select_index >= 0) {
                    gui_set_focused_widget(wg);
                    gui_select_commit(wg, select_index);
                    return;
                }
            } else if (wg->type == GUI_WIDGET_LISTVIEW) {
                int list_index = gui_listview_index_at(wg, x, y);
                gui_set_focused_widget(wg);
                if (list_index >= 0) gui_listview_select(wg, list_index, (wg->label_flags & GUI_LISTVIEW_FLAG_MULTI_SELECT) != 0);
                return;
            } else if (wg->type == GUI_WIDGET_ICONVIEW) {
                int icon_index = gui_iconview_index_at(wg, x, y);
                gui_set_focused_widget(wg);
                if (icon_index >= 0) gui_iconview_select(wg, icon_index);
                return;
            } else if (wg->type == GUI_WIDGET_TABLEVIEW) {
                int col_index = gui_tableview_column_at(wg, x, y);
                int row_index = gui_tableview_index_at(wg, x, y);
                gui_set_focused_widget(wg);
                if (col_index >= 0 && (wg->label_flags & GUI_TABLEVIEW_FLAG_SORTABLE)) gui_tableview_sort(wg, col_index);
                else if (row_index >= 0) gui_tableview_select(wg, row_index);
                return;
            } else if (wg->type == GUI_WIDGET_MENUBAR) {
                int menu_index = gui_menubar_index_at(wg, x, y);
                gui_set_focused_widget(wg);
                if (menu_index >= 0) gui_menubar_activate(wg, menu_index);
                return;
            } else if (wg->type == GUI_WIDGET_TABVIEW) {
                int close_hit = 0;
                int tab_index = gui_tabview_index_at(wg, x, y, &close_hit);
                gui_set_focused_widget(wg);
                if (tab_index >= 0) gui_tabview_activate(wg, tab_index, close_hit);
                return;
            } else if (wg->type == GUI_WIDGET_CONTEXTMENU) {
                int item_index = gui_contextmenu_index_at(wg, x, y);
                gui_set_focused_widget(wg);
                if (item_index >= 0) gui_contextmenu_activate(wg, item_index);
                return;
            } else if (wg->type == GUI_WIDGET_TREEVIEW) {
                int tree_index = gui_treeview_index_at(wg, x, y);
                gui_set_focused_widget(wg);
                if (tree_index >= 0) { gui_treeview_select(wg, tree_index); gui_treeview_toggle(wg, tree_index); }
                return;
            } else if (wg->type == GUI_WIDGET_DIALOG) {
                int dialog_result = gui_dialog_hit_button(wg, x, y);
                gui_set_focused_widget(wg);
                if (dialog_result != GUI_DIALOG_RESULT_NONE) gui_dialog_close(wg, dialog_result);
                return;
            }
            gui_set_focused_widget(wg);
            wg->pressed = 1;
            g_gui.pressed_widget = wg;
            gui_invalidate_all();
        } else if (gui_widget_can_focus(wg)) {
            gui_set_focused_widget(wg);
            if (wg && wg->type == GUI_WIDGET_TEXTBOX) {
                int lx = 0;
                int ly = 0;
                gui_widget_local_from_screen(wg, x, y, &lx, &ly);
                gui_textbox_set_cursor_from_local_x(wg, lx);
                gui_text_widget_clear_selection(wg);
                wg->selection_anchor = wg->cursor;
                g_gui.text_select_widget = wg;
                if (wg->owner && wg->owner->user_owner_pid != 0) {
                    gui_user_widget_click_at(wg, lx, ly);
                }
            } else if (wg && wg->type == GUI_WIDGET_TEXTAREA) {
                int lx = 0;
                int ly = 0;
                gui_widget_local_from_screen(wg, x, y, &lx, &ly);
                gui_textarea_set_cursor_from_local_xy(wg, lx, ly);
                gui_text_widget_clear_selection(wg);
                wg->selection_anchor = wg->cursor;
                g_gui.text_select_widget = wg;
                if (wg->owner && wg->owner->user_owner_pid != 0) {
                    gui_user_widget_click_at(wg, lx, ly);
                }
            }
        } else {
            if (g_gui.focused_widget && g_gui.focused_widget->type == GUI_WIDGET_CONTEXTMENU) {
                gui_contextmenu_hide(g_gui.focused_widget);
            }
            gui_set_focused_widget(0);
        }
        return;
    }


    gui_set_focused_widget(0);
}

__attribute__((optimize("no-jump-tables")))
static void gui_handle_mouse_up(int x, int y) {
    gui_window_t *event_window = gui_window_at(x, y);
    if (event_window) gui_user_post_mouse_event(event_window, GUI_USER_EVENT_MOUSE_UP, x, y, 1, 0);
    if (g_gui.terminal.view.selecting) {
        uint32_t tc, trc;
        if (gui_terminal_point_to_cell(x, y, &tc, &trc)) {
            gui_terminal_update_selection(tc, trc);
        }
        gui_terminal_view_end_selection(&g_gui.terminal.view);
    }
    if (g_gui.drag_window) {
        g_gui.drag_window->dragging = 0;
        g_gui.drag_window->resizing = 0;
        g_gui.drag_window = 0;
    }
    if (g_gui.text_select_widget) {
        g_gui.text_select_widget = 0;
    }
    if (g_gui.slider_widget) {
        int rebuild_settings = (g_gui.slider_widget->owner == g_settings_win);
        g_gui.slider_widget->pressed = 0;
        g_gui.slider_widget = 0;
        if (rebuild_settings) gui_settings_build(0);
        else gui_invalidate_all();
    }
    if (g_gui.scrollbar_widget) {
        g_gui.scrollbar_widget->pressed = 0;
        g_gui.scrollbar_widget = 0;
        gui_invalidate_all();
    }
    if (g_gui.desktop_start_menu_scroll_dragging) {
        g_gui.desktop_start_menu_scroll_dragging = 0;
        gui_invalidate_all();
    }
    if (g_gui.desktop_note_dragging) {
        int was_click = !g_gui.desktop_note_moved;
        g_gui.desktop_note_dragging = 0;
        gui_invalidate_all();
        if (was_click) {
            /* 未发生拖动，视为点击便签：打开内核态便签窗口 */
            gui_stickynote_open();
        }
    }
    if (g_gui.splitview_widget) {
        g_gui.splitview_widget->pressed = 0;
        g_gui.splitview_widget = 0;
        gui_invalidate_all();
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
            if (wg->type == GUI_WIDGET_TOGGLE) {
                gui_toggle_activate(wg);
            } else if (wg->type == GUI_WIDGET_CHECKBOX) {
                gui_checkbox_activate(wg);
            } else if (wg->type == GUI_WIDGET_RADIOBUTTON) {
                gui_radiobutton_activate(wg);
            } else if (wg->type == GUI_WIDGET_SELECT || wg->type == GUI_WIDGET_COMBOBOX) {
                gui_select_activate(wg);
            } else if (wg->type == GUI_WIDGET_LABEL) {
                gui_terminal_set_clipboard_text(wg->text);
                gui_invalidate_all();
            } else {
                gui_button_activate(wg);
            }
        }
    }
}

__attribute__((optimize("no-jump-tables")))
static void gui_handle_mouse_move(int x, int y) {
    static int last_user_move_x = -32768;
    static int last_user_move_y = -32768;
    gui_window_t *event_window = gui_window_at(x, y);
    int search_idx;
    if (event_window && (x != last_user_move_x || y != last_user_move_y)) {
        gui_user_post_mouse_event(event_window, GUI_USER_EVENT_MOUSE_MOVE, x, y, 0, 0);
        last_user_move_x = x;
        last_user_move_y = y;
    }
    if (g_gui.desktop_note_dragging) {
        gui_desktop_note_drag_to(x, y);
        return;
    }
    if (g_gui.text_select_widget) {
        gui_widget_t *tw = g_gui.text_select_widget;
        if (tw->owner) {
            int lx = 0;
            int ly = 0;
            gui_widget_local_from_screen(tw, x, y, &lx, &ly);
            if (tw->type == GUI_WIDGET_TEXTAREA) gui_textarea_set_cursor_from_local_xy(tw, lx, ly);
            else gui_textbox_set_cursor_from_local_x(tw, lx);
            gui_text_widget_set_selection(tw, tw->selection_anchor, tw->cursor);
            if (tw->type == GUI_WIDGET_TEXTAREA) gui_textarea_ensure_cursor_visible(tw);
            else gui_textbox_ensure_cursor_visible(tw);
            gui_invalidate_all();
        }
        return;
    }
    if (g_gui.slider_widget) {
        gui_set_hovered_widget(g_gui.slider_widget);
        gui_slider_apply_screen_x(g_gui.slider_widget, x);
        return;
    }
    if (g_gui.scrollbar_widget) {
        gui_set_hovered_widget(g_gui.scrollbar_widget);
        gui_scrollbar_apply_screen(g_gui.scrollbar_widget, x, y);
        return;
    }
    if (g_gui.desktop_start_menu_scroll_dragging) {
        gui_start_menu_scrollbar_drag_to(y);
        return;
    }
    if (g_gui.splitview_widget) {
        gui_set_hovered_widget(g_gui.splitview_widget);
        gui_splitview_apply_screen(g_gui.splitview_widget, x, y);
        return;
    }
    search_idx = gui_taskbar_search_result_index_at(x, y);
    if (search_idx >= 0 && search_idx != g_gui.taskbar_search_selected) {
        g_gui.taskbar_search_selected = search_idx;
        gui_invalidate_rect(g_gui.taskbar_search_results_rect.x,
                            g_gui.taskbar_search_results_rect.y,
                            g_gui.taskbar_search_results_rect.w,
                            g_gui.taskbar_search_results_rect.h);
    }
    gui_set_hovered_widget(gui_widget_at_screen(x, y));
    if (g_gui.terminal.view.selecting) {
        uint32_t tc, trc;
        if (gui_terminal_point_to_cell(x, y, &tc, &trc)) {
            gui_terminal_update_selection(tc, trc);
        }
        gui_invalidate_rect(x - 18, y - 18, 36, 36);
    } else if (g_gui.drag_window && g_gui.drag_window->dragging) {
        gui_window_t *w = g_gui.drag_window;
        int old_x = w->rect.x;
        int old_y = w->rect.y;
        w->rect.x = x - w->drag_offset_x;
        w->rect.y = y - w->drag_offset_y;
        if (w->rect.x < 0) w->rect.x = 0;
        if (w->rect.y < 0) w->rect.y = 0;
        if (w->rect.x + w->rect.w > (int)g_gui.width) w->rect.x = (int)g_gui.width - w->rect.w;
        if (w->rect.y + w->rect.h > (int)g_gui.height - GUI_TASKBAR_HEIGHT) w->rect.y = (int)g_gui.height - GUI_TASKBAR_HEIGHT - w->rect.h;
        if (w->rect.x != old_x || w->rect.y != old_y) gui_user_post_window_event(w, GUI_USER_EVENT_MOVE);
        gui_invalidate_all();
    } else if (g_gui.drag_window && g_gui.drag_window->resizing) {
        gui_window_t *w = g_gui.drag_window;
        int nw = w->resize_start_w + (x - w->resize_start_mx);
        int nh = w->resize_start_h + (y - w->resize_start_my);
        int max_w = (int)g_gui.width - w->rect.x;
        int max_h = (int)g_gui.height - GUI_TASKBAR_HEIGHT - w->rect.y;
        if (nw < 160) nw = 160;
        if (nh < 100) nh = 100;
        if (nw > max_w) nw = max_w;
        if (nh > max_h) nh = max_h;
        if (w->rect.w != nw || w->rect.h != nh) {
            w->rect.w = nw;
            w->rect.h = nh;
            gui_user_post_window_event(w, GUI_USER_EVENT_RESIZE);
        }
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
            if (g_gui.taskbar_search_focused && gui_taskbar_search_handle_key(ev.key)) {
                /* taskbar search consumed the key */
            } else if (ev.key == GUI_KEY_ALT_TAB) {
                gui_alt_tab_cycle();
            } else if (ev.key == GUI_KEY_SUPER) {
                gui_toggle_start_menu();
            } else if (ev.key == GUI_KEY_TAB &&
                       !(g_gui.terminal.enabled && g_gui.terminal.input_focused &&
                         g_gui.active_window == g_gui.terminal.window)) {
                if (ev.modifiers & GUI_USER_KEYMOD_SHIFT) gui_focus_prev_widget();
                else gui_focus_next_widget();
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       (g_gui.focused_widget->type == GUI_WIDGET_TEXTBOX ||
                        g_gui.focused_widget->type == GUI_WIDGET_TEXTAREA)) {
                gui_textbox_on_key(g_gui.focused_widget, ev.key);
            } else if (browser_handle_address_enter(ev.key)) {
                /* Browser address bar consumed Enter. */
            } else if (g_gui.terminal.enabled && g_gui.terminal.input_focused &&
                       g_gui.active_window == g_gui.terminal.window) {
                /* Terminal window is active: route keystrokes into the command line
                 * BEFORE the generic focused-widget fallthrough swallows them. */
                char tch = 0;
                if (ev.key == GUI_KEY_ENTER || ev.key == '\n' || ev.key == '\r') {
                    tch = '\n';
                } else if (ev.key == GUI_KEY_BACKSPACE || ev.key == 8 || ev.key == 127) {
                    tch = '\b';
                } else if (ev.key == GUI_KEY_TAB || ev.key == '\t') {
                    tch = '\t';
                } else if (ev.key >= 32 && ev.key < 127) {
                    tch = (char)ev.key;
                }
                if (tch) gui_terminal_on_input(tch);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       (g_gui.focused_widget->type == GUI_WIDGET_SELECT ||
                        g_gui.focused_widget->type == GUI_WIDGET_COMBOBOX) &&
                       (ev.key == GUI_KEY_ENTER || ev.key == GUI_KEY_SPACE || ev.key == GUI_KEY_UP || ev.key == GUI_KEY_DOWN || ev.key == 27)) {
                gui_select_handle_key(g_gui.focused_widget, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_LISTVIEW) {
                gui_listview_handle_key(g_gui.focused_widget, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_ICONVIEW) {
                gui_iconview_handle_key(g_gui.focused_widget, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_TABLEVIEW) {
                gui_tableview_handle_key(g_gui.focused_widget, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_MENUBAR) {
                gui_menubar_handle_key(g_gui.focused_widget, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_CONTEXTMENU) {
                gui_contextmenu_handle_key(g_gui.focused_widget, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_TREEVIEW) {
                gui_treeview_handle_key(g_gui.focused_widget, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_DIALOG &&
                       gui_dialog_handle_key(g_gui.focused_widget, ev.key)) {
                /* Dialog consumed Enter/Space/Esc. */
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       gui_widget_is_clickable(g_gui.focused_widget) &&
                       (ev.key == GUI_KEY_ENTER || ev.key == GUI_KEY_SPACE)) {
                if (g_gui.focused_widget->type == GUI_WIDGET_TOGGLE) {
                    gui_toggle_activate(g_gui.focused_widget);
                } else if (g_gui.focused_widget->type == GUI_WIDGET_CHECKBOX) {
                    gui_checkbox_activate(g_gui.focused_widget);
                } else if (g_gui.focused_widget->type == GUI_WIDGET_RADIOBUTTON) {
                    gui_radiobutton_activate(g_gui.focused_widget);
                } else if (g_gui.focused_widget->type == GUI_WIDGET_LABEL) {
                    gui_terminal_set_clipboard_text(g_gui.focused_widget->text);
                    gui_invalidate_all();
                } else {
                    gui_button_activate(g_gui.focused_widget);
                }
            } else if (g_gui.active_window && g_gui.active_window->user_owner_pid != 0) {
                gui_user_post_key_event(g_gui.active_window, ev.key);
                gui_user_post_key_up_event(g_gui.active_window, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused) {
                /* Focused widgets consume keys that they do not handle. */
            } else {
                /* No focused widget or terminal; key is dropped. */
            }
        } else if (ev.type == GUI_EVENT_MOUSE_DOWN) {
            if (ev.button & 1u) gui_handle_mouse_down(ev.x, ev.y);
            else if (ev.button & 2u) gui_handle_mouse_right_down(ev.x, ev.y);
        } else if (ev.type == GUI_EVENT_MOUSE_UP) {
            if (ev.button & 1u) gui_handle_mouse_up(ev.x, ev.y);
        } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
            gui_handle_mouse_move(ev.x, ev.y);
        } else if (ev.type == GUI_EVENT_MOUSE_WHEEL) {
            gui_window_t *wheel_window;
            gui_widget_t *wheel_widget;
            if (g_gui.desktop_start_menu_open && gui_rect_contains(&g_gui.desktop_start_menu_rect, ev.x, ev.y)) {
                gui_start_menu_scroll_by(ev.dy > 0 ? -1 : 1);
                continue;
            }
            wheel_window = gui_window_at(ev.x, ev.y);
            wheel_widget = gui_widget_at_screen(ev.x, ev.y);
            if (wheel_window) gui_user_post_mouse_event(wheel_window, GUI_USER_EVENT_MOUSE_WHEEL, ev.x, ev.y, 0, ev.dy);
            if (wheel_widget && wheel_widget->type == GUI_WIDGET_TEXTAREA) {
                gui_textarea_scroll_lines(wheel_widget, ev.dy > 0 ? -1 : 1);
            } else if (wheel_widget && wheel_widget->type == GUI_WIDGET_SCROLLBAR) {
                gui_scrollbar_scroll_steps(wheel_widget, ev.dy > 0 ? -1 : 1);
            } else if (wheel_widget && wheel_widget->type == GUI_WIDGET_SCROLLVIEW) {
                int delta = ev.dy > 0 ? -24 : 24;
                gui_scrollview_set_offset(wheel_widget, wheel_widget->value, wheel_widget->step + delta);
                gui_invalidate_all();
            } else if (wheel_widget && wheel_widget->type == GUI_WIDGET_LISTVIEW) {
                gui_listview_scroll(wheel_widget, ev.dy > 0 ? -1 : 1);
            } else if (wheel_widget && wheel_widget->type == GUI_WIDGET_ICONVIEW) {
                gui_iconview_scroll(wheel_widget, ev.dy > 0 ? -1 : 1);
            } else if (wheel_widget && wheel_widget->type == GUI_WIDGET_TABLEVIEW) {
                gui_tableview_scroll(wheel_widget, ev.dy > 0 ? -1 : 1);
            } else if (wheel_widget && wheel_widget->type == GUI_WIDGET_TREEVIEW) {
                gui_treeview_scroll(wheel_widget, ev.dy > 0 ? -1 : 1);
            } else if (g_gui.desktop_start_menu_open && gui_rect_contains(&g_gui.desktop_start_menu_rect, ev.x, ev.y)) {
                gui_start_menu_scroll_by(ev.dy > 0 ? 1 : -1);
            }
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

    /* Polling the emulated USB tablet every GUI loop iteration is expensive
     * under QEMU and makes pointer movement stutter.  Keep consuming the
     * cheap shared mouse snapshot each frame, but sample the tablet at a
     * reduced rate; the absolute coordinates remain smooth enough while the
     * desktop has time to redraw. */
    g_gui.mouse_poll_divider++;
    if ((g_gui.mouse_poll_divider & 1u) == 0u) {
        usb_tablet_poll((int)g_gui.width, (int)g_gui.height);
    }
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

    if ((ms.buttons & 2u) && !(g_gui.last_mouse_buttons & 2u)) {
        gui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = GUI_EVENT_MOUSE_DOWN;
        ev.x = ms.x;
        ev.y = ms.y;
        ev.button = 2u;
        ev.window = gui_window_at(ms.x, ms.y);
        ev.widget = gui_widget_at_screen(ms.x, ms.y);
        gui_event_push(ev);
    }

    if (ms.wheel != 0) {
        gui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = GUI_EVENT_MOUSE_WHEEL;
        ev.x = ms.x;
        ev.y = ms.y;
        ev.dy = ms.wheel;
        ev.window = gui_window_at(ms.x, ms.y);
        ev.widget = gui_widget_at_screen(ms.x, ms.y);
        gui_event_push(ev);
    }

    if (ms.x != g_gui.mouse_x || ms.y != g_gui.mouse_y) {
        int complex_move;
        gui_taskbar_invalidate_hover_changes(g_gui.mouse_x, g_gui.mouse_y, ms.x, ms.y);
        gui_desktop_invalidate_hover_changes(g_gui.mouse_x, g_gui.mouse_y, ms.x, ms.y);
        gui_ctxmenu_invalidate_hover_changes(g_gui.mouse_x, g_gui.mouse_y, ms.x, ms.y);
        complex_move = (g_gui.drag_window != 0) || g_gui.terminal.view.selecting ||
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
            /* invalidate old & new cursor rects so the unified renderer
             * repaints the background under the cursor and redraws it,
             * avoiding the dual-path (present_fast vs gui_render) smear. */
            gui_invalidate_rect(g_gui.mouse_x - 2, g_gui.mouse_y - 2, 22, 22);
            gui_invalidate_rect(ms.x - 2, ms.y - 2, 22, 22);
        }
    }

    g_gui.mouse_x = ms.x;
    g_gui.mouse_y = ms.y;
    g_gui.last_mouse_buttons = ms.buttons;
}

void gui_init(void) {
    i18n_init();
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
    g_gui.mouse_poll_divider = 0;
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

    /* 闁稿繐鐗愰鏇犵磾椤曗偓缁卞爼寮介崶顏嗙彾闁伙絽濂旂拹鐔兼儑閻旈鏉介柛鎺戞妞存悂锟?*/
    mouse_set_bounds((int)g_gui.width, (int)g_gui.height);

    g_gui.initialized = 1;

    /* 濞寸姴閰ｇ槐鍫曞冀閸ヮ兘鏀抽柛鏂诲姀楠炲繘宕ｉ弽褏绉奸柛鎾崇Т濞兼锟?*/
    mouse_snapshot_and_clear_delta(&ms);

    /* 濠碘€冲€归悘澶嬑楅悩宕囧灱閺夆晜蓱閻ュ懘寮ㄧ捄鍝勭厒濞戞搩鍘介弻鍥礌閸滃啰绀夊ù锝堟硶閺併倗浠﹁箛鎾额啂濞戞搩鍘肩缓?*/
    if (ms.packet_count == 0) {
        g_gui.mouse_x = (int)(g_gui.width / 2);
        g_gui.mouse_y = (int)(g_gui.height / 2);
        mouse_set_position(g_gui.mouse_x, g_gui.mouse_y);
    } else {
        g_gui.mouse_x = ms.x;
        g_gui.mouse_y = ms.y;
    }

    /* 闁秆勫姈閻栵絾娼忛崷顓熸珪闁哄牃鍋撶紓浣哥墛椤ュ懘锟?*/
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
    serial_write("[gui] backbuffer alloc: ptr=");
    serial_write_hex((uint32_t)((uint64_t)(uintptr_t)g_gui.backbuffer >> 32));
    serial_write(":");
    serial_write_hex((uint32_t)((uint64_t)(uintptr_t)g_gui.backbuffer & 0xFFFFFFFFu));
    serial_write(" w=");
    serial_write_hex(g_gui.width);
    serial_write(" h=");
    serial_write_hex(g_gui.height);
    serial_write(" pixels=");
    serial_write_hex(pixels);
    serial_write("\n");

    gui_terminal_init();
    gui_desktop_init();
    gui_notify(i18n_t(I18N_KEY_NOTIFY_WELCOME));
    gui_notify(i18n_t(I18N_KEY_NOTIFY_THEME_TIP));
    gui_render();
    return 0;
}

void gui_copy_cached_text(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i;

    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    for (i = 0; i < dst_size - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static void gui_launcher_add(uint32_t index, const char *name, const char *title, uint32_t action, uint32_t color) {
    gui_launcher_entry_t *entry;

    if (index >= GUI_LAUNCHER_MAX_APPS || !name || !title) return;
    entry = &g_gui.launcher_entries[index];
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->action = action;
    entry->color = color;
    gui_copy_cached_text(entry->name, sizeof(entry->name), name);
    gui_copy_cached_text(entry->title, sizeof(entry->title), title);
}

/* like gui_launcher_add, but also records an executable path so the launcher
 * can spawn it via spawn_user_process() (used for /bin/* entries). */
static void gui_launcher_add_with_path(uint32_t index, const char *name, const char *title,
                                       uint32_t action, uint32_t color, const char *path) {
    gui_launcher_entry_t *entry;
    uint32_t i;

    gui_launcher_add(index, name, title, action, color);
    if (index >= GUI_LAUNCHER_MAX_APPS || !path) return;
    entry = &g_gui.launcher_entries[index];
    for (i = 0; i < sizeof(entry->path) - 1 && path[i]; i++) entry->path[i] = path[i];
    entry->path[i] = 0;
}

int gui_string_equals(const char *a, const char *b) {
    uint32_t i = 0;

    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int gui_launcher_is_system_bin(const char *name) {
    static const char *system_bins[] = {
        /* Shell and command-line utilities stay available from Terminal, but are
         * hidden from the desktop launcher so the menu only shows user-facing apps. */
        "sh", "pwd", "ls", "cat", "echo", "ai", "grep", "wc", "mkdir", "rm", "touch",
        "cp", "mv", "tee", "head", "tail", "sort", "env", "rmdir", "ln", "kill",
        "ping", "ifconfig", "netstat", "firewall",
        "id", "groups", "cap", "sandbox",

        /* User-mode smoke/regression tests are developer tools, not launcher apps. */
        "hello", "fault", "alarmtest", "mmaptest", "sbrktest", "argtest", "condtest",
        "envtest", "errnotest", "eventfdtest", "exit42", "forktest", "fstest",
        "futextest", "isotest", "kaddrtest", "libctest", "maintest", "malloctest",
        "micromsgtest", "mqtest", "mutextest", "nicetest", "orphan", "semtest",
        "servicetest", "shmtest", "socketpairtest", "stdiotest", "systest",
        "threadtest", "user_test", "waittest"
    };
    uint32_t i;

    if (!name) return 1;
    for (i = 0; i < sizeof(system_bins) / sizeof(system_bins[0]); i++) {
        if (gui_string_equals(name, system_bins[i])) return 1;
    }
    return 0;
}

/* scan /bin via vfs_readdir and append user-installed programs to launcher list */
static void gui_launcher_scan_bin(uint32_t start_index) {
    int i;
    uint32_t idx = start_index;
    static const uint32_t palette[6] = {
        0x4C90E8u, 0xAA70EBu, 0x58C480u, 0xE89B4Cu, 0xE85C7Au, 0x6A9BE0u
    };
    int color_i = 0;
    for (i = 0; i < 256 && idx < GUI_LAUNCHER_MAX_APPS; i++) {
        dentry_t *e = vfs_readdir("/bin", i);
        char path[80];
        int p, k;
        uint32_t color;
        if (!e) break;
        /* skip ., .., directories, and built-in system commands */
        if (e->name[0] == '.' && (e->name[1] == 0 || (e->name[1] == '.' && e->name[2] == 0))) continue;
        if (e->inode && (e->inode->mode & FS_DIR)) continue;
        if (gui_launcher_is_system_bin(e->name)) continue;
        /* build path "/bin/<name>" */
        path[0] = '/'; path[1] = 'b'; path[2] = 'i'; path[3] = 'n'; path[4] = '/'; p = 5;
        for (k = 0; k < (int)(sizeof(path) - 6) && e->name[k]; k++) path[p++] = e->name[k];
        path[p] = 0;
        color = palette[(color_i++) % 6];
        gui_launcher_add_with_path(idx, e->name, e->name,
                                   GUI_DESKTOP_ACTION_LAUNCH_BIN_BASE + idx, color, path);
        idx++;
    }
    g_gui.launcher_app_count = idx;
}

static void gui_launcher_init(void) {
    memset(g_gui.launcher_entries, 0, sizeof(g_gui.launcher_entries));
    g_gui.launcher_enabled = 1;
    g_gui.launcher_app_count = 2;
    gui_launcher_add(0, "terminal", i18n_t(I18N_KEY_APP_TERMINAL), GUI_DESKTOP_ACTION_TERMINAL, gui_rgb(68, 144, 245));
    gui_launcher_add(1, "settings", i18n_t(I18N_KEY_APP_SETTINGS), GUI_DESKTOP_ACTION_SETTINGS, gui_rgb(230, 160, 70));
    /* programs under /bin appear after the 2 built-ins; scan happens lazily */
}

static void gui_desktop_clear_icon_click_state(void) {
    g_desktop_selected_icon = -1;
    g_desktop_last_click_icon = -1;
    g_desktop_last_click_frame = 0;
}

static void gui_desktop_add_icon(uint32_t index, int x, int y, const char *label, uint32_t color, uint32_t action) {
    gui_desktop_icon_t *icon;

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
    gui_copy_cached_text(icon->label, sizeof(icon->label), label);
}

static void gui_desktop_refresh_i18n_labels(void) {
    if (g_gui.desktop_icons[0].used) {
        gui_copy_cached_text(g_gui.desktop_icons[0].label, sizeof(g_gui.desktop_icons[0].label), i18n_t(I18N_KEY_ICON_FILES));
    }
    if (g_gui.desktop_icons[1].used) {
        gui_copy_cached_text(g_gui.desktop_icons[1].label, sizeof(g_gui.desktop_icons[1].label), i18n_t(I18N_KEY_ICON_RECYCLE_BIN));
    }
    if (g_gui.desktop_icons[2].used) {
        gui_copy_cached_text(g_gui.desktop_icons[2].label, sizeof(g_gui.desktop_icons[2].label), i18n_t(I18N_KEY_ICON_BROWSER));
    }

    if (g_gui.launcher_entries[0].used) {
        gui_copy_cached_text(g_gui.launcher_entries[0].title, sizeof(g_gui.launcher_entries[0].title), i18n_t(I18N_KEY_APP_TERMINAL));
    }
    if (g_gui.launcher_entries[1].used) {
        gui_copy_cached_text(g_gui.launcher_entries[1].title, sizeof(g_gui.launcher_entries[1].title), i18n_t(I18N_KEY_APP_SETTINGS));
    }
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
    g_gui.desktop_start_menu_scroll = 0;
    g_gui.desktop_start_menu_rect.x = 6;
    g_gui.desktop_start_menu_rect.y = (int)top - GUI_DESKTOP_MENU_H - 4;
    g_gui.desktop_start_menu_rect.w = GUI_DESKTOP_MENU_W;
    g_gui.desktop_start_menu_rect.h = GUI_DESKTOP_MENU_H;

    memset(g_gui.desktop_icons, 0, sizeof(g_gui.desktop_icons));
    gui_desktop_clear_icon_click_state();
    /* 濡楀矂娼版穱婵堟殌鐢摜鏁ら崗銉ュ經閿涙碍鏋冩禒韬测偓浣告礀閺€鍓佺彲閵嗕焦绁荤憴鍫濇珤锟?*/
    gui_desktop_add_icon(0, 32, 72,  i18n_t(I18N_KEY_ICON_FILES),       gui_rgb(242, 194, 74),  GUI_DESKTOP_ACTION_FILES);
    gui_desktop_add_icon(1, 32, 160, i18n_t(I18N_KEY_ICON_RECYCLE_BIN), gui_rgb(168, 178, 198), GUI_DESKTOP_ACTION_RECYCLE);
    gui_desktop_add_icon(2, 32, 248, i18n_t(I18N_KEY_ICON_BROWSER),     gui_rgb(74, 158, 245),  GUI_DESKTOP_ACTION_BROWSER);
    g_gui.desktop_icon_count = 3;
}

void gui_draw_folder_icon_art(int x, int y, uint32_t color) {
    gui_raw_fill_rect(x + 2, y + 6, 11, 6, gui_rgb(255, 220, 105));
    gui_raw_fill_rect(x, y + 11, 28, 20, color);
    gui_raw_fill_rect(x + 2, y + 15, 24, 14, gui_rgb(255, 211, 86));
    gui_raw_line(x, y + 11, x + 27, y + 11, gui_rgb(255, 238, 160));
    gui_raw_line(x, y + 11, x, y + 30, gui_rgb(255, 238, 160));
    gui_raw_line(x + 27, y + 11, x + 27, y + 30, gui_rgb(130, 90, 24));
    gui_raw_line(x, y + 30, x + 27, y + 30, gui_rgb(130, 90, 24));
}

void gui_draw_browser_icon_art(int x, int y, uint32_t color) {
    uint32_t dark = gui_rgb(22, 72, 118);
    uint32_t light = gui_rgb(176, 226, 255);
    uint32_t green = gui_rgb(72, 220, 166);
    uint32_t white = gui_rgb(236, 248, 255);

    gui_raw_fill_rect(x + 3, y + 3, 22, 22, color);
    gui_raw_line(x + 8, y + 1, x + 19, y + 1, light);
    gui_raw_line(x + 5, y + 2, x + 22, y + 2, light);
    gui_raw_line(x + 2, y + 5, x + 2, y + 22, light);
    gui_raw_line(x + 25, y + 5, x + 25, y + 22, dark);
    gui_raw_line(x + 5, y + 25, x + 22, y + 25, dark);

    /* Globe latitude and longitude lines. */
    gui_raw_line(x + 5, y + 13, x + 23, y + 13, white);
    gui_raw_line(x + 13, y + 5, x + 13, y + 23, white);
    gui_raw_line(x + 7, y + 8, x + 21, y + 8, light);
    gui_raw_line(x + 7, y + 18, x + 21, y + 18, dark);
    gui_raw_line(x + 9, y + 6, x + 9, y + 22, light);
    gui_raw_line(x + 18, y + 6, x + 18, y + 22, dark);

    /* Small green orbit / arrow to make it recognizable as a browser. */
    gui_raw_line(x + 4, y + 20, x + 12, y + 24, green);
    gui_raw_line(x + 12, y + 24, x + 24, y + 16, green);
    gui_raw_fill_rect(x + 21, y + 15, 4, 2, green);
    gui_raw_fill_rect(x + 23, y + 13, 2, 4, green);
}

static void gui_draw_icon_button_face(const gui_rect_t *rect, int selected,
                                      int highlighted) {
    if (!rect || rect->w <= 4 || rect->h <= 4) return;
    if (selected || highlighted) {
        uint32_t fill = selected ? gui_rgb(42, 84, 144) : gui_rgb(54, 73, 106);
        uint32_t top = selected ? gui_rgb(126, 166, 226) : gui_rgb(98, 126, 172);
        uint32_t bottom = selected ? gui_rgb(20, 42, 80) : gui_rgb(28, 42, 66);
        gui_raw_fill_rect(rect->x + 2, rect->y + 2,
                          rect->w - 4, rect->h - 4, fill);
        gui_raw_line(rect->x + 2, rect->y + 2,
                     rect->x + rect->w - 3, rect->y + 2, top);
        gui_raw_line(rect->x + 2, rect->y + rect->h - 3,
                     rect->x + rect->w - 3, rect->y + rect->h - 3, bottom);
    }
}

void gui_draw_icon_button_frame(const gui_rect_t *rect, const char *label,
                                       int icon_w, int icon_h, int gap,
                                       int selected, int highlighted,
                                       uint32_t text_color,
                                       int *icon_x, int *icon_y) {
    int top_pad;
    int text_w;
    int text_x;
    int text_y;
    int line_h;

    if (!rect) return;
    gui_draw_icon_button_face(rect, selected, highlighted);
    if (icon_w < 0) icon_w = 0;
    if (icon_h < 0) icon_h = 0;
    if (gap < 0) gap = 0;
    line_h = (int)font_get_line_height(font_get_default());
    if (line_h < (int)GUI_CHAR_H) line_h = (int)GUI_CHAR_H;
    top_pad = (rect->h - (icon_h + gap + line_h)) / 2;
    if (top_pad < 0) top_pad = 0;
    if (icon_x) *icon_x = rect->x + (rect->w - icon_w) / 2;
    if (icon_y) *icon_y = rect->y + top_pad;
    if (label && label[0]) {
        text_w = (int)font_measure_text_width(font_get_default(), label);
        text_x = rect->x + (rect->w - text_w) / 2;
        if (text_x < rect->x) text_x = rect->x;
        if (text_x + text_w > rect->x + rect->w) text_x = rect->x + rect->w - text_w;
        if (text_x < rect->x) text_x = rect->x;
        text_y = rect->y + top_pad + icon_h + gap;
        if (text_y + line_h > rect->y + rect->h) text_y = rect->y + rect->h - line_h;
        if (text_y < rect->y) text_y = rect->y;
        gui_draw_text(text_x, text_y, label, text_color);
    }
}

static void gui_draw_file_icon_cell(const gui_rect_t *rect, const char *label,
                                    gui_icon_id_t icon, int selected,
                                    int highlighted, uint32_t text_color) {
    gui_rect_t face;
    int icon_x;
    int icon_y;
    const int icon_w = 18;
    const int icon_h = 18;
    const int gap = font_scale_value(4);
    int text_x;
    int text_y;

    if (!rect || rect->w <= 0 || rect->h <= 0) return;

    face = *rect;
    if (face.h < icon_h + 4) face.h = icon_h + 4;
    if (face.w < icon_w + gap + 16) face.w = icon_w + gap + 16;

    gui_draw_icon_button_face(&face, selected, highlighted);
    icon_x = face.x + 4;
    icon_y = face.y + (face.h - icon_h) / 2;
    if (icon_y < face.y + 1) icon_y = face.y + 1;
    gui_draw_file_icon(icon, icon_x, icon_y);

    text_x = icon_x + icon_w + gap;
    text_y = gui_text_center_y(face.y, face.h);
    if (text_y < face.y + 1) text_y = face.y + 1;
    gui_draw_text(text_x, text_y, label ? label : "", text_color);
}


typedef struct gui_list_view {
    gui_rect_t rect;
    int row_h;
    int selected_row;
    uint32_t bg;
    uint32_t alt_bg;
    uint32_t selected_bg;
    uint32_t border;
    uint32_t selected_border;
} gui_list_view_t;

static gui_rect_t gui_list_view_row_rect(const gui_list_view_t *list, int row) {
    gui_rect_t r;
    r.x = 0;
    r.y = 0;
    r.w = 0;
    r.h = 0;
    if (!list || list->row_h <= 0) return r;
    r.x = list->rect.x;
    r.y = list->rect.y + row * list->row_h;
    r.w = list->rect.w;
    r.h = list->row_h;
    return r;
}

static void gui_list_view_draw_row(const gui_list_view_t *list, int row) {
    gui_rect_t r;
    uint32_t bg;
    uint32_t border;
    if (!list || list->row_h <= 0) return;
    r = gui_list_view_row_rect(list, row);
    if (r.w <= 0 || r.h <= 0) return;
    bg = (row == list->selected_row) ? list->selected_bg :
         ((row & 1) ? list->alt_bg : list->bg);
    border = (row == list->selected_row) ? list->selected_border : list->border;
    gui_raw_fill_rect(r.x, r.y, r.w, r.h - 1, bg);
    gui_raw_line(r.x, r.y + r.h - 1, r.x + r.w - 1, r.y + r.h - 1, border);
}

static void gui_desktop_draw_icon(gui_desktop_icon_t *icon) {
    int cx;
    int iy;
    const int art_h = 28;
    const int gap   = 6;
    int selected;
    int highlighted;

    if (!icon || !icon->used) return;
    selected = gui_desktop_icon_selected(icon);
    highlighted = gui_desktop_icon_highlighted_at(icon, g_gui.mouse_x, g_gui.mouse_y);
    gui_draw_icon_button_frame(&icon->rect, icon->label, 28, art_h, gap,
                               selected, highlighted, gui_rgb(232, 240, 255),
                               &cx, &iy);
    /* no background plate / border 锟?let the icon art sit directly on wallpaper */
    if (icon->action == GUI_DESKTOP_ACTION_FILES) {
        gui_draw_folder_icon_art(cx, iy, icon->color);
    } else if (icon->action == GUI_DESKTOP_ACTION_BROWSER) {
        gui_draw_browser_icon_art(cx, iy, icon->color);
    } else if (icon->action == GUI_DESKTOP_ACTION_RECYCLE) {
        uint32_t lid    = gui_rgb(150, 152, 162);
        uint32_t body   = icon->color;
        uint32_t shade  = gui_rgb(110, 112, 124);
        uint32_t hl     = gui_rgb(225, 228, 236);
        uint32_t arrow  = gui_rgb(120, 220, 140);
        /* handle on top of lid */
        gui_raw_fill_rect(cx + 10, iy + 1, 8, 3, lid);
        gui_raw_line(cx + 10, iy + 1, cx + 17, iy + 1, hl);
        /* lid */
        gui_raw_fill_rect(cx + 1, iy + 4, 26, 4, lid);
        gui_raw_line(cx + 1, iy + 4, cx + 26, iy + 4, hl);
        gui_raw_line(cx + 1, iy + 7, cx + 26, iy + 7, shade);
        /* body (trash can with slight taper feel via shading) */
        gui_raw_fill_rect(cx + 3, iy + 9, 22, 19, body);
        gui_raw_line(cx + 3, iy + 9,  cx + 24, iy + 9,  hl);
        gui_raw_line(cx + 3, iy + 9,  cx + 3,  iy + 27, hl);
        gui_raw_line(cx + 24, iy + 9, cx + 24, iy + 27, shade);
        gui_raw_line(cx + 3, iy + 27, cx + 24, iy + 27, shade);
        /* recycle arrow triangle in the middle (3 little green pixels forming arrow) */
        gui_raw_fill_rect(cx + 12, iy + 14, 4, 1, arrow);
        gui_raw_fill_rect(cx + 11, iy + 15, 6, 1, arrow);
        gui_raw_fill_rect(cx + 10, iy + 16, 8, 1, arrow);
        gui_raw_fill_rect(cx + 13, iy + 17, 2, 5, arrow);
        /* vertical wear lines on body sides */
        gui_raw_line(cx + 7,  iy + 12, cx + 7,  iy + 25, shade);
        gui_raw_line(cx + 20, iy + 12, cx + 20, iy + 25, shade);
    } else {
        gui_raw_fill_rect(cx, iy, 28, 28, icon->color);
        gui_raw_line(cx, iy, cx + 27, iy, gui_rgb(225, 235, 255));
        gui_raw_line(cx, iy, cx, iy + 27, gui_rgb(225, 235, 255));
        gui_raw_line(cx + 27, iy, cx + 27, iy + 27, gui_rgb(18, 25, 38));
        gui_raw_line(cx, iy + 27, cx + 27, iy + 27, gui_rgb(18, 25, 38));
    }
}

static int gui_start_menu_item_height(void) {
    int h = font_get_line_height(font_get_default()) + font_scale_value(14);
    if (h < GUI_LAUNCHER_ITEM_H) h = GUI_LAUNCHER_ITEM_H;
    return h;
}

static int gui_start_menu_header_height(void) {
    int h = font_get_line_height(font_get_default()) + font_scale_value(24);
    if (h < 36) h = 36;
    return h;
}

static int gui_start_menu_visible_count(void) {
    int item_h = gui_start_menu_item_height();
    int header_h = gui_start_menu_header_height();
    int usable_h = g_gui.desktop_start_menu_rect.h - header_h - 8;
    int count;
    if (item_h <= 0) return 0;
    count = usable_h / item_h;
    if (count < 1 && g_gui.launcher_app_count > 0) count = 1;
    if (count > (int)g_gui.launcher_app_count) count = (int)g_gui.launcher_app_count;
    return count;
}

static int gui_start_menu_max_scroll(void) {
    int visible = gui_start_menu_visible_count();
    int max = (int)g_gui.launcher_app_count - visible;
    return max > 0 ? max : 0;
}

static int gui_start_menu_has_scrollbar(void) {
    return gui_start_menu_max_scroll() > 0;
}

static int gui_start_menu_scrollbar_rects(gui_rect_t *track, gui_rect_t *thumb) {
    gui_rect_t *r = &g_gui.desktop_start_menu_rect;
    int header_h;
    int visible;
    int track_h;
    int thumb_h;
    int thumb_y;
    int max_scroll;

    if (!g_gui.desktop_start_menu_open || !gui_start_menu_has_scrollbar()) return 0;
    header_h = gui_start_menu_header_height();
    visible = gui_start_menu_visible_count();
    track_h = r->h - header_h - 8;
    if (track_h <= 0 || visible <= 0 || g_gui.launcher_app_count == 0) return 0;

    thumb_h = (visible * track_h) / (int)g_gui.launcher_app_count;
    if (thumb_h < 12) thumb_h = 12;
    if (thumb_h > track_h) thumb_h = track_h;

    thumb_y = r->y + header_h;
    max_scroll = gui_start_menu_max_scroll();
    if (max_scroll > 0 && track_h > thumb_h) {
        thumb_y += (g_gui.desktop_start_menu_scroll * (track_h - thumb_h)) / max_scroll;
    }

    if (track) {
        track->x = r->x + r->w - 12;
        track->y = r->y + header_h;
        track->w = 6;
        track->h = track_h;
    }
    if (thumb) {
        thumb->x = r->x + r->w - 11;
        thumb->y = thumb_y;
        thumb->w = 4;
        thumb->h = thumb_h;
    }
    return 1;
}

static void gui_start_menu_scrollbar_drag_to(int y) {
    gui_rect_t track;
    gui_rect_t thumb;
    int max_scroll;
    int range;
    int pos;

    if (!gui_start_menu_scrollbar_rects(&track, &thumb)) return;
    max_scroll = gui_start_menu_max_scroll();
    range = track.h - thumb.h;
    if (max_scroll <= 0 || range <= 0) return;

    pos = y - g_gui.desktop_start_menu_scroll_drag_offset_y - track.y;
    if (pos < 0) pos = 0;
    if (pos > range) pos = range;
    g_gui.desktop_start_menu_scroll = (pos * max_scroll + range / 2) / range;
    gui_start_menu_clamp_scroll();
    gui_invalidate_all();
}

static int gui_start_menu_scrollbar_begin_drag(int x, int y) {
    gui_rect_t track;
    gui_rect_t thumb;
    int visible;

    if (!gui_start_menu_scrollbar_rects(&track, &thumb)) return 0;
    if (!gui_rect_contains(&track, x, y)) return 0;

    visible = gui_start_menu_visible_count();
    if (gui_rect_contains(&thumb, x, y)) {
        g_gui.desktop_start_menu_scroll_dragging = 1;
        g_gui.desktop_start_menu_scroll_drag_offset_y = y - thumb.y;
    } else {
        gui_start_menu_scroll_by(y < thumb.y ? -visible : visible);
        if (gui_start_menu_scrollbar_rects(&track, &thumb) && gui_rect_contains(&thumb, x, y)) {
            g_gui.desktop_start_menu_scroll_dragging = 1;
            g_gui.desktop_start_menu_scroll_drag_offset_y = y - thumb.y;
        }
    }
    gui_invalidate_all();
    return 1;
}

static void gui_start_menu_clamp_scroll(void) {
    int max = gui_start_menu_max_scroll();
    if (g_gui.desktop_start_menu_scroll < 0) g_gui.desktop_start_menu_scroll = 0;
    if (g_gui.desktop_start_menu_scroll > max) g_gui.desktop_start_menu_scroll = max;
}

void gui_update_start_menu_layout(void) {
    int line_h = font_get_line_height(font_get_default());
    int item_h = gui_start_menu_item_height();
    int header_h = gui_start_menu_header_height();
    int title_w = (int)font_measure_text_width(font_get_default(), i18n_t(I18N_KEY_LAUNCHER_TITLE));
    int max_title_w = title_w;
    int desired_w;
    int max_w;
    int content_h;
    int desired_h;
    int max_h;
    uint32_t i;

    for (i = 0; i < g_gui.launcher_app_count && i < GUI_LAUNCHER_MAX_APPS; i++) {
        gui_launcher_entry_t *entry = &g_gui.launcher_entries[i];
        int w;
        if (!entry->used) continue;
        w = (int)font_measure_text_width(font_get_default(), entry->title);
        if (w > max_title_w) max_title_w = w;
    }

    desired_w = max_title_w + 72;
    if (desired_w < GUI_DESKTOP_MENU_W) desired_w = GUI_DESKTOP_MENU_W;
    max_w = (int)g_gui.width - 12;
    if (max_w < 120) max_w = 120;
    if (desired_w > max_w) desired_w = max_w;

    content_h = header_h + 8 + (int)g_gui.launcher_app_count * item_h;
    desired_h = content_h + 8;
    max_h = (int)g_gui.height / 3;
    if (max_h < 1) max_h = 1;
    if (desired_h > max_h) desired_h = max_h;
    if (desired_h < header_h + item_h + 12 && g_gui.launcher_app_count > 0) {
        desired_h = header_h + item_h + 12;
    }
    if (line_h > 0 && desired_h < line_h + item_h + 28) {
        desired_h = line_h + item_h + 28;
    }
    if (desired_h > max_h) desired_h = max_h;

    g_gui.desktop_start_menu_rect.x = 6;
    g_gui.desktop_start_menu_rect.w = desired_w;
    g_gui.desktop_start_menu_rect.h = desired_h;
    g_gui.desktop_start_menu_rect.y = g_gui.desktop_taskbar_rect.y - desired_h - 4;
    if (g_gui.desktop_start_menu_rect.y < 2) g_gui.desktop_start_menu_rect.y = 2;
    gui_start_menu_clamp_scroll();
}

static int gui_start_menu_item_rect(uint32_t index, gui_rect_t *item) {
    int item_h;
    int header_h;
    int visible;
    int start;
    int end;
    int scrollbar_w;
    int item_w;
    if (!item || !g_gui.desktop_start_menu_open) return 0;
    if (index >= g_gui.launcher_app_count || index >= GUI_LAUNCHER_MAX_APPS) return 0;
    item_h = gui_start_menu_item_height();
    header_h = gui_start_menu_header_height();
    visible = gui_start_menu_visible_count();
    start = g_gui.desktop_start_menu_scroll;
    end = start + visible;
    if ((int)index < start || (int)index >= end) return 0;
    scrollbar_w = gui_start_menu_has_scrollbar() ? 10 : 0;
    item_w = g_gui.desktop_start_menu_rect.w - 20 - scrollbar_w;
    if (item_w < 32) item_w = 32;
    item->x = g_gui.desktop_start_menu_rect.x + 10;
    item->y = g_gui.desktop_start_menu_rect.y + header_h + ((int)index - start) * item_h;
    item->w = item_w;
    item->h = item_h - 2;
    return 1;
}

static int gui_start_menu_item_at(int x, int y, uint32_t *index_out) {
    uint32_t i;
    int start;
    int end;
    int visible;
    if (!g_gui.desktop_start_menu_open ||
        !gui_rect_contains(&g_gui.desktop_start_menu_rect, x, y)) return 0;
    gui_update_start_menu_layout();
    visible = gui_start_menu_visible_count();
    start = g_gui.desktop_start_menu_scroll;
    end = start + visible;
    for (i = (uint32_t)start; i < (uint32_t)end &&
         i < g_gui.launcher_app_count && i < GUI_LAUNCHER_MAX_APPS; i++) {
        gui_rect_t item;
        gui_launcher_entry_t *entry = &g_gui.launcher_entries[i];
        if (!entry->used) continue;
        if (gui_start_menu_item_rect(i, &item) && gui_rect_contains(&item, x, y)) {
            if (index_out) *index_out = i;
            return 1;
        }
    }
    return 0;
}

static void gui_start_menu_scroll_by(int delta_items) {
    if (delta_items == 0) return;
    g_gui.desktop_start_menu_scroll += delta_items;
    gui_start_menu_clamp_scroll();
    gui_invalidate_all();
}

static void gui_draw_launcher_terminal_icon(int x, int y) {
    uint32_t bg = gui_rgb(18, 24, 32);
    uint32_t edge = gui_rgb(92, 126, 172);
    uint32_t hi = gui_rgb(145, 192, 255);
    uint32_t text = gui_rgb(124, 238, 156);
    gui_raw_fill_rect_alpha(x + 2, y + 3, 13, 12, gui_rgb(0, 0, 0), 70u);
    gui_raw_fill_rect(x + 1, y + 1, 14, 12, bg);
    gui_raw_line(x + 1, y + 1, x + 14, y + 1, hi);
    gui_raw_line(x + 1, y + 1, x + 1, y + 12, edge);
    gui_raw_line(x + 1, y + 12, x + 14, y + 12, gui_rgb(8, 12, 18));
    gui_raw_line(x + 14, y + 1, x + 14, y + 12, gui_rgb(8, 12, 18));
    gui_raw_line(x + 4, y + 5, x + 6, y + 7, text);
    gui_raw_line(x + 6, y + 7, x + 4, y + 9, text);
    gui_raw_line(x + 8, y + 10, x + 11, y + 10, gui_rgb(205, 230, 255));
}

static void gui_draw_launcher_settings_icon(int x, int y) {
    uint32_t gear = gui_rgb(202, 216, 232);
    uint32_t dark = gui_rgb(70, 88, 112);
    uint32_t center = gui_rgb(68, 142, 238);
    gui_raw_fill_rect_alpha(x + 3, y + 4, 11, 11, gui_rgb(0, 0, 0), 58u);
    gui_raw_fill_rect(x + 7, y + 1, 2, 3, gear);
    gui_raw_fill_rect(x + 7, y + 12, 2, 3, gear);
    gui_raw_fill_rect(x + 1, y + 7, 3, 2, gear);
    gui_raw_fill_rect(x + 12, y + 7, 3, 2, gear);
    gui_raw_fill_rect(x + 4, y + 4, 2, 2, gear);
    gui_raw_fill_rect(x + 10, y + 4, 2, 2, gear);
    gui_raw_fill_rect(x + 4, y + 10, 2, 2, gear);
    gui_raw_fill_rect(x + 10, y + 10, 2, 2, gear);
    gui_raw_fill_rect(x + 5, y + 5, 6, 6, dark);
    gui_raw_fill_rect(x + 6, y + 6, 4, 4, gear);
    gui_raw_fill_rect(x + 7, y + 7, 2, 2, center);
}

static void gui_draw_launcher_app_icon(int x, int y, uint32_t accent) {
    uint32_t paper = gui_rgb(244, 248, 252);
    uint32_t edge = gui_rgb(88, 104, 126);
    uint32_t shadow = gui_rgb(0, 0, 0);
    uint32_t fold = gui_rgb(213, 224, 236);
    gui_raw_fill_rect_alpha(x + 3, y + 3, 11, 12, shadow, 55u);
    gui_raw_fill_rect(x + 2, y + 1, 10, 13, paper);
    gui_raw_line(x + 2, y + 1, x + 11, y + 1, edge);
    gui_raw_line(x + 2, y + 14, x + 11, y + 14, edge);
    gui_raw_line(x + 2, y + 1, x + 2, y + 14, edge);
    gui_raw_line(x + 11, y + 1, x + 11, y + 14, edge);
    gui_raw_fill_rect(x + 9, y + 2, 2, 2, fold);
    gui_raw_line(x + 9, y + 1, x + 11, y + 3, edge);
    gui_raw_line(x + 4, y + 5, x + 9, y + 5, accent);
    gui_raw_line(x + 4, y + 8, x + 9, y + 8, gui_rgb(126, 142, 160));
    gui_raw_line(x + 4, y + 11, x + 8, y + 11, gui_rgb(126, 142, 160));
}

static void gui_draw_launcher_icon(const gui_launcher_entry_t *entry, int x, int y) {
    if (!entry) return;
    if (entry->action == GUI_DESKTOP_ACTION_TERMINAL) {
        gui_draw_launcher_terminal_icon(x, y);
    } else if (entry->action == GUI_DESKTOP_ACTION_SETTINGS) {
        gui_draw_launcher_settings_icon(x, y);
    } else {
        gui_draw_launcher_app_icon(x, y, entry->color);
    }
    g_gui_accel.icon_quality_passes++;
}

void gui_draw_launcher_item(const gui_launcher_entry_t *entry,
                                   const gui_rect_t *rect,
                                   int selected, int highlighted) {
    int icon_y;
    int text_y;
    font_rect_t clip;
    gui_text_soften_ctx_t soft;
    uint32_t fg = gui_rgb(232, 240, 255);
    if (!entry || !rect || rect->w <= 0 || rect->h <= 0) return;
    gui_draw_icon_button_face(rect, selected, highlighted);
    icon_y = rect->y + (rect->h - 16) / 2;
    if (icon_y < rect->y + 2) icon_y = rect->y + 2;
    gui_draw_launcher_icon(entry, rect->x + 3, icon_y);
    text_y = rect->y + (rect->h - font_get_line_height(font_get_default())) / 2;
    if (text_y < rect->y + 2) text_y = rect->y + 2;
    clip.x = rect->x + 24;
    clip.y = rect->y;
    clip.w = rect->w - 28;
    clip.h = rect->h;
    if (clip.w <= 0 || clip.h <= 0) return;
    soft.color = fg;
    soft.alpha = 58u;
    font_draw_text_clipped(font_get_default(), gui_font_put_pixel_soft, &soft,
                           &clip, rect->x + 25, text_y, entry->title, fg);
    font_draw_text_clipped(font_get_default(), gui_font_put_pixel, 0,
                           &clip, rect->x + 24, text_y, entry->title, fg);
}

static void gui_desktop_draw_start_menu(void) {
    gui_rect_t *r = &g_gui.desktop_start_menu_rect;
    uint32_t i;
    int header_h;
    int visible;
    int start;
    int end;
    if (!g_gui.desktop_start_menu_open) return;

    gui_update_start_menu_layout();
    header_h = gui_start_menu_header_height();
    visible = gui_start_menu_visible_count();
    start = g_gui.desktop_start_menu_scroll;
    end = start + visible;

    gui_raw_fill_rect(r->x, r->y, r->w, r->h, gui_rgb(28, 36, 54));
    gui_raw_line(r->x, r->y, r->x + r->w - 1, r->y, gui_rgb(112, 146, 198));
    gui_raw_line(r->x, r->y, r->x, r->y + r->h - 1, gui_rgb(112, 146, 198));
    gui_raw_line(r->x + r->w - 1, r->y, r->x + r->w - 1, r->y + r->h - 1, gui_rgb(10, 13, 20));
    gui_raw_line(r->x, r->y + r->h - 1, r->x + r->w - 1, r->y + r->h - 1, gui_rgb(10, 13, 20));
    gui_draw_text(r->x + 12, r->y + 12, i18n_t(I18N_KEY_LAUNCHER_TITLE), gui_rgb(245, 250, 255));

    for (i = (uint32_t)start; i < (uint32_t)end && i < g_gui.launcher_app_count && i < GUI_LAUNCHER_MAX_APPS; i++) {
        gui_launcher_entry_t *entry = &g_gui.launcher_entries[i];
        gui_rect_t item;
        if (!entry->used) continue;
        if (!gui_start_menu_item_rect(i, &item)) continue;
        gui_draw_launcher_item(entry, &item, 0,
                               gui_rect_contains(&item, g_gui.mouse_x, g_gui.mouse_y));
    }

    if (gui_start_menu_has_scrollbar()) {
        gui_rect_t track;
        gui_rect_t thumb;
        if (gui_start_menu_scrollbar_rects(&track, &thumb)) {
            uint32_t thumb_color = g_gui.desktop_start_menu_scroll_dragging
                ? gui_rgb(146, 180, 232)
                : gui_rgb(112, 146, 198);
            gui_raw_fill_rect(track.x, track.y, track.w, track.h, gui_rgb(18, 24, 36));
            gui_raw_fill_rect(thumb.x, thumb.y, thumb.w, thumb.h, thumb_color);
        }
    }
}

#define GUI_DESKTOP_NOTE_FILE "/stickynotes.txt"
#define GUI_DESKTOP_NOTE_LEGACY_FILE "/home/stickynote.txt"
#define GUI_DESKTOP_NOTE_CARD_W 240
#define GUI_DESKTOP_NOTE_CARD_H 76
#define GUI_DESKTOP_NOTE_GAP 12
/* GUI_DESKTOP_NOTE_MAX_COUNT/MAX_TEXT + gui_desktop_note_store_t moved to gui_internal.h */

static int gui_desktop_note_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void gui_desktop_note_trim(char *text) {
    int len = 0;
    int start = 0;
    int i;
    if (!text) return;
    while (text[len]) len++;
    while (start < len && gui_desktop_note_is_space(text[start])) start++;
    if (start > 0) {
        for (i = 0; i <= len - start; i++) text[i] = text[i + start];
        len = 0;
        while (text[len]) len++;
    }
    while (len > 0 && gui_desktop_note_is_space(text[len - 1])) {
        text[len - 1] = '\0';
        len--;
    }
}

void gui_desktop_note_add(gui_desktop_note_store_t *store, const char *line, int len) {
    int i;
    if (!store || !line || len <= 0 || store->count >= GUI_DESKTOP_NOTE_MAX_COUNT) return;
    if (len > GUI_DESKTOP_NOTE_MAX_TEXT) len = GUI_DESKTOP_NOTE_MAX_TEXT;
    for (i = 0; i < len; i++) {
        char ch = line[i];
        store->items[store->count][i] = (ch == '\r' || ch == '\n') ? ' ' : ch;
    }
    store->items[store->count][len] = '\0';
    gui_desktop_note_trim(store->items[store->count]);
    if (store->items[store->count][0]) store->count++;
}

static void gui_desktop_notes_load_from_file(gui_desktop_note_store_t *store, const char *path) {
    int fd;
    int n;
    int start = 0;
    int i;
    char content[(GUI_DESKTOP_NOTE_MAX_TEXT + 2) * GUI_DESKTOP_NOTE_MAX_COUNT + 1];
    if (!store || !path) return;
    fd = vfs_open(path, 0, 0);
    if (fd < 0) return;
    n = vfs_read(fd, content, sizeof(content) - 1);
    vfs_close(fd);
    if (n <= 0) return;
    content[n] = '\0';
    for (i = 0; i < n && store->count < GUI_DESKTOP_NOTE_MAX_COUNT; i++) {
        if (content[i] == '\n') {
            gui_desktop_note_add(store, content + start, i - start);
            start = i + 1;
        }
    }
    if (start < n && store->count < GUI_DESKTOP_NOTE_MAX_COUNT) {
        gui_desktop_note_add(store, content + start, n - start);
    }
}


static void gui_desktop_notes_load(gui_desktop_note_store_t *store) {
    if (!store) return;
    store->count = 0;
    /* x86_64 VFS is read-only initrd: sticky notes live in memory only.
     * The in-memory sticky store is the single source of truth. When it is
     * empty (e.g. right after boot with no notes added), the desktop must
     * show nothing -- do NOT fall back to disk or a default welcome note,
     * otherwise the desktop would show data that the list view does not. */
    sticky_export_to_desktop_store(store);
}

static void gui_desktop_draw_note_wrapped_text(const char *text, int x, int y, int w, int h, uint32_t color) {
    const char *p = text ? text : "";
    int line_h = gui_text_line_height_px();
    int max_lines;
    int line;
    if (w <= 0 || h <= 0) return;
    if (line_h <= 0) line_h = 12;
    max_lines = h / line_h;
    if (max_lines <= 0) return;
    if (max_lines > 8) max_lines = 8;
    for (line = 0; line < max_lines && p && *p; line++) {
        char line_buf[256];
        uint32_t src_len = gui_text_len_until_break(p);
        uint32_t consume_len;
        int hard_wrap;
        int last_line = (line == max_lines - 1);
        if (src_len == 0) {
            p++;
            continue;
        }
        consume_len = gui_utf8_prefix_for_width(p, src_len, w);
        if (consume_len == 0) {
            consume_len = gui_utf8_step_bytes(p);
            if (consume_len > src_len) consume_len = src_len;
        }
        hard_wrap = consume_len < src_len;
        if (last_line && (hard_wrap || p[consume_len])) {
            gui_make_ellipsis_line_px(line_buf, sizeof(line_buf), p, src_len, w, 1);
        } else {
            if (consume_len >= sizeof(line_buf)) consume_len = sizeof(line_buf) - 1;
            memcpy(line_buf, p, consume_len);
            line_buf[consume_len] = 0;
        }
        gui_draw_text(x, y + line * line_h, line_buf, color);
        p += consume_len;
        if (!hard_wrap && *p == '\n') p++;
    }
}

static void gui_desktop_draw_note_card(const char *text, int x, int y, int w, int h, int index) {
    char title[24];
    int text_x;
    int text_y;
    int text_w;
    int text_h;
    gui_raw_fill_rect_alpha(x + 4, y + 5, w, h, gui_rgb(0, 0, 0), 52u);
    gui_raw_fill_rect(x, y, w, h, gui_rgb(255, 244, 176));
    gui_raw_fill_rect(x, y, w, 24, gui_rgb(255, 226, 111));
    gui_raw_line(x, y, x + w - 1, y, gui_rgb(255, 248, 204));
    gui_raw_line(x, y, x, y + h - 1, gui_rgb(255, 248, 204));
    gui_raw_line(x + w - 1, y, x + w - 1, y + h - 1, gui_rgb(196, 158, 62));
    gui_raw_line(x, y + h - 1, x + w - 1, y + h - 1, gui_rgb(196, 158, 62));
    title[0] = '#';
    title[1] = (char)('1' + index);
    title[2] = ' ';
    title[3] = 'N';
    title[4] = 'o';
    title[5] = 't';
    title[6] = 'e';
    title[7] = '\0';
    gui_draw_text(x + 10, y + 6, title, gui_rgb(94, 73, 28));
    text_x = x + 10;
    text_y = y + 32;
    text_w = w - 20;
    text_h = h - 38;
    gui_desktop_draw_note_wrapped_text(text, text_x, text_y, text_w, text_h, gui_rgb(72, 58, 26));
}

/* 按索引计算第 index 张便签的默认“瀑布式”坐标（未手动摆放时使用） */
static void gui_desktop_note_default_position(int index, int *out_x, int *out_y) {
    int x = (int)g_gui.width - GUI_DESKTOP_NOTE_CARD_W - 24;
    int y = 72 + index * (GUI_DESKTOP_NOTE_CARD_H + GUI_DESKTOP_NOTE_GAP);
    if (x < 24) x = 24;
    if (y < 16) y = 16;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

/* 确保第 index 张便签的坐标已初始化（未摆放则用默认瀑布位） */
static void gui_desktop_note_ensure_pos(int index) {
    if (index < 0 || index >= 8) return;
    if (!g_gui.desktop_note_pos_valid[index]) {
        gui_desktop_note_default_position(index,
            &g_gui.desktop_note_pos_x[index],
            &g_gui.desktop_note_pos_y[index]);
        g_gui.desktop_note_pos_valid[index] = 1;
    }
}

/* 对指定 index 便签的坐标做边界夹取 */
static void gui_desktop_note_clamp_index(int index) {
    int max_x = (int)g_gui.width - GUI_DESKTOP_NOTE_CARD_W;
    int max_y = (int)g_gui.height - GUI_TASKBAR_HEIGHT - GUI_DESKTOP_NOTE_CARD_H;
    if (index < 0 || index >= 8) return;
    gui_desktop_note_ensure_pos(index);
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (g_gui.desktop_note_pos_x[index] < 0) g_gui.desktop_note_pos_x[index] = 0;
    if (g_gui.desktop_note_pos_y[index] < 0) g_gui.desktop_note_pos_y[index] = 0;
    if (g_gui.desktop_note_pos_x[index] > max_x) g_gui.desktop_note_pos_x[index] = max_x;
    if (g_gui.desktop_note_pos_y[index] > max_y) g_gui.desktop_note_pos_y[index] = max_y;
}

static int gui_desktop_note_begin_drag(int x, int y) {
    gui_desktop_note_store_t store;
    int i;
    if (!g_gui.desktop_enabled) return 0;
    gui_desktop_notes_load(&store);
    if (store.count <= 0) return 0;
    /* 从最上层（后画的）往下找命中的那张便签 */
    for (i = store.count - 1; i >= 0; i--) {
        gui_rect_t r;
        if (i >= 8) continue;
        gui_desktop_note_clamp_index(i);
        r.x = g_gui.desktop_note_pos_x[i];
        r.y = g_gui.desktop_note_pos_y[i];
        r.w = GUI_DESKTOP_NOTE_CARD_W;
        r.h = GUI_DESKTOP_NOTE_CARD_H;
        if (gui_rect_contains(&r, x, y)) {
            g_gui.desktop_note_dragging = 1;
            g_gui.desktop_note_drag_index = i;
            g_gui.desktop_note_drag_offset_x = x - g_gui.desktop_note_pos_x[i];
            g_gui.desktop_note_drag_offset_y = y - g_gui.desktop_note_pos_y[i];
            g_gui.desktop_note_press_x = x;
            g_gui.desktop_note_press_y = y;
            g_gui.desktop_note_moved = 0;
            gui_set_focused_widget(0);
            gui_invalidate_all();
            return 1;
        }
    }
    return 0;
}

static void gui_desktop_note_drag_to(int x, int y) {
    int dx;
    int dy;
    int idx;
    if (!g_gui.desktop_note_dragging) return;
    idx = g_gui.desktop_note_drag_index;
    if (idx < 0 || idx >= 8) return;
    dx = x - g_gui.desktop_note_press_x;
    dy = y - g_gui.desktop_note_press_y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    /* 超过阈值才认定为拖拽，避免手抖把点击误判成拖动 */
    if (dx > 4 || dy > 4) g_gui.desktop_note_moved = 1;
    if (!g_gui.desktop_note_moved) return;
    g_gui.desktop_note_pos_x[idx] = x - g_gui.desktop_note_drag_offset_x;
    g_gui.desktop_note_pos_y[idx] = y - g_gui.desktop_note_drag_offset_y;
    gui_desktop_note_clamp_index(idx);
    gui_invalidate_all();
}

static void gui_desktop_draw_notes(void) {
    gui_desktop_note_store_t store;
    int i;
    if (!g_gui.desktop_enabled) return;
    gui_desktop_notes_load(&store);
    if (store.count <= 0) {
        g_gui.desktop_note_stack_rect.x = 0;
        g_gui.desktop_note_stack_rect.y = 0;
        g_gui.desktop_note_stack_rect.w = 0;
        g_gui.desktop_note_stack_rect.h = 0;
        return;
    }
    /* 每张便签用各自独立的坐标绘制 */
    for (i = 0; i < store.count && i < 8; i++) {
        gui_desktop_note_clamp_index(i);
        gui_desktop_draw_note_card(store.items[i],
            g_gui.desktop_note_pos_x[i],
            g_gui.desktop_note_pos_y[i],
            GUI_DESKTOP_NOTE_CARD_W, GUI_DESKTOP_NOTE_CARD_H, i);
    }
    /* stack_rect 保留为第 0 张的位置（兼容旧引用） */
    g_gui.desktop_note_stack_rect.x = g_gui.desktop_note_pos_x[0];
    g_gui.desktop_note_stack_rect.y = g_gui.desktop_note_pos_y[0];
    g_gui.desktop_note_stack_rect.w = GUI_DESKTOP_NOTE_CARD_W;
    g_gui.desktop_note_stack_rect.h = GUI_DESKTOP_NOTE_CARD_H;
}

static void gui_desktop_draw(void) {
    uint32_t i;
    /* center the 3-line welcome block on screen (above taskbar) */
    const char *line0 = i18n_t(I18N_KEY_BANNER_LINE0);
    const char *line1 = i18n_t(I18N_KEY_BANNER_LINE1);
    const char *line2 = i18n_t(I18N_KEY_BANNER_LINE2);
    const int line_gap = 12;                       /* extra gap between lines */
    int line_h = (int)font_get_line_height(font_get_default());
    int line_step;
    int block_h;
    int avail_h;
    int top_y;
    int w0, w1, w2;
    int x0, x1, x2;

    if (!g_gui.desktop_enabled) return;

    if (line_h < (int)GUI_CHAR_H) line_h = (int)GUI_CHAR_H;
    line_step = line_h + line_gap;                 /* full line stride */
    block_h = line_h + line_step * 2;              /* 3 lines */
    avail_h = (int)g_gui.height - GUI_TASKBAR_HEIGHT;
    if (avail_h < block_h) avail_h = block_h;
    top_y = (avail_h - block_h) / 2;
    if (top_y < 16) top_y = 16;

    w0 = (int)font_measure_text_width(font_get_default(), line0);
    w1 = (int)font_measure_text_width(font_get_default(), line1);
    w2 = (int)font_measure_text_width(font_get_default(), line2);
    x0 = ((int)g_gui.width - w0) / 2;
    x1 = ((int)g_gui.width - w1) / 2;
    x2 = ((int)g_gui.width - w2) / 2;
    if (x0 < 0) x0 = 0;
    if (x1 < 0) x1 = 0;
    if (x2 < 0) x2 = 0;

    gui_draw_text(x0, top_y,                  line0, gui_rgb(235, 242, 255));
    gui_draw_text(x1, top_y + line_step,      line1, gui_rgb(205, 220, 245));
    gui_draw_text(x2, top_y + line_step * 2,  line2, gui_rgb(170, 195, 230));
    for (i = 0; i < g_gui.desktop_icon_count && i < GUI_DESKTOP_MAX_ICONS; i++) {
        gui_desktop_draw_icon(&g_gui.desktop_icons[i]);
    }
    gui_desktop_draw_notes();
}

static int gui_ascii_case_equal_prefix(const char *text, const char *query) {
    while (*query) {
        char a = *text++;
        char b = *query++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

int gui_ascii_case_contains(const char *text, const char *query) {
    const char *p;
    if (!query || !query[0]) return 1;
    if (!text) return 0;
    for (p = text; *p; p++) {
        if (gui_ascii_case_equal_prefix(p, query)) return 1;
    }
    return 0;
}

int gui_ascii_case_ends_with(const char *text, const char *suffix) {
    uint32_t text_len = 0;
    uint32_t suffix_len = 0;
    const char *start;
    if (!text || !suffix) return 0;
    while (text[text_len]) text_len++;
    while (suffix[suffix_len]) suffix_len++;
    if (suffix_len > text_len) return 0;
    start = text + text_len - suffix_len;
    return gui_ascii_case_equal_prefix(start, suffix);
}

int gui_path_starts_with(const char *path, const char *prefix) {
    uint32_t i = 0;
    if (!path || !prefix) return 0;
    while (prefix[i]) {
        if (path[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static int gui_taskbar_search_is_executable_path(const char *path, int mode_executable) {
    if (mode_executable) return 1;
    if (!path || !path[0]) return 0;
    if (gui_path_starts_with(path, "/bin/")) return 1;
    if (gui_ascii_case_ends_with(path, ".elf")) return 1;
    if (gui_ascii_case_ends_with(path, ".bin")) return 1;
    if (gui_ascii_case_ends_with(path, ".app")) return 1;
    return 0;
}

static void gui_taskbar_search_reset_results(void) {
    uint32_t i;
    for (i = 0; i < GUI_TASKBAR_SEARCH_MAX_RESULTS; i++) {
        g_gui.taskbar_search_results[i].used = 0;
        g_gui.taskbar_search_results[i].is_dir = 0;
        g_gui.taskbar_search_results[i].name[0] = 0;
        g_gui.taskbar_search_results[i].path[0] = 0;
    }
    g_gui.taskbar_search_result_count = 0;
    g_gui.taskbar_search_selected = -1;
    g_gui.taskbar_search_results_rect.x = 0;
    g_gui.taskbar_search_results_rect.y = 0;
    g_gui.taskbar_search_results_rect.w = 0;
    g_gui.taskbar_search_results_rect.h = 0;
}

static void gui_taskbar_search_clear(void) {
    g_gui.taskbar_search_text[0] = 0;
    g_gui.taskbar_search_len = 0;
    gui_taskbar_search_reset_results();
}

void gui_taskbar_search_copy(char *dst, uint32_t dst_len, const char *src) {
    uint32_t i = 0;
    if (!dst || dst_len == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < dst_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void gui_taskbar_search_join_path(char *dst, uint32_t dst_len, const char *dir, const char *name) {
    uint32_t i = 0;
    uint32_t j = 0;
    if (!dst || dst_len == 0) return;
    if (!dir || !dir[0]) dir = "/";
    while (dir[i] && i + 1 < dst_len) {
        dst[i] = dir[i];
        i++;
    }
    if (i == 0) {
        dst[i++] = '/';
    } else if (i > 1 && dst[i - 1] != '/' && i + 1 < dst_len) {
        dst[i++] = '/';
    }
    while (name && name[j] && i + 1 < dst_len) {
        dst[i++] = name[j++];
    }
    dst[i] = 0;
}

static void gui_taskbar_search_add_result(const char *name, const char *path, int is_dir, int is_executable) {
    gui_taskbar_search_result_t *r;
    if (g_gui.taskbar_search_result_count >= GUI_TASKBAR_SEARCH_MAX_RESULTS) return;
    r = &g_gui.taskbar_search_results[g_gui.taskbar_search_result_count++];
    r->used = 1;
    r->is_dir = is_dir ? 1 : 0;
    r->is_executable = (!is_dir && is_executable) ? 1 : 0;
    gui_taskbar_search_copy(r->name, sizeof(r->name), name);
    gui_taskbar_search_copy(r->path, sizeof(r->path), path);
    if (g_gui.taskbar_search_selected < 0) g_gui.taskbar_search_selected = 0;
}

static void gui_taskbar_search_scan_dir(const char *dir, uint32_t depth) {
    uint32_t i;
    if (!dir || !dir[0]) return;
    if (depth > 4) return;
    for (i = 0; i < 128 && g_gui.taskbar_search_result_count < GUI_TASKBAR_SEARCH_MAX_RESULTS; i++) {
        dentry_t *e = vfs_readdir(dir, i);
        char child_path[GUI_TASKBAR_SEARCH_PATH_LEN];
        int is_dir;
        int is_executable;
        if (!e) break;
        if (!e->name[0] || gui_string_equals(e->name, ".") || gui_string_equals(e->name, "..")) continue;
        is_dir = (e->inode && (e->inode->mode & FS_DIR)) ? 1 : 0;
        is_executable = (e->inode && (e->inode->mode & (S_IXUSR | S_IXGRP | S_IXOTH))) ? 1 : 0;
        gui_taskbar_search_join_path(child_path, sizeof(child_path), dir, e->name);
        is_executable = gui_taskbar_search_is_executable_path(child_path, is_executable);
        if (gui_ascii_case_contains(e->name, g_gui.taskbar_search_text) ||
            gui_ascii_case_contains(child_path, g_gui.taskbar_search_text)) {
            gui_taskbar_search_add_result(e->name, child_path, is_dir, is_executable);
        }
        if (is_dir) {
            gui_taskbar_search_scan_dir(child_path, depth + 1);
        }
    }
}

static void gui_taskbar_search_refresh_results(void) {
    gui_taskbar_search_reset_results();
    if (!g_gui.taskbar_search_text[0]) return;
    gui_taskbar_search_scan_dir("/", 0);
}

void gui_file_preview_open_path(const char *path);
void gui_file_preview_open_file(const char *path);

static void gui_taskbar_search_finish_open(void) {
    gui_taskbar_search_clear();
    g_gui.taskbar_search_focused = 0;
    gui_invalidate_all();
}

static void gui_taskbar_search_open_result(uint32_t index) {
    gui_taskbar_search_result_t *r;
    char path[MAX_PATH];
    char *argv[2];
    int pid;
    int is_dir;
    int is_executable;
    if (index >= g_gui.taskbar_search_result_count) return;
    r = &g_gui.taskbar_search_results[index];
    if (!r->used || !r->path[0]) return;

    strncpy(path, r->path, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    is_dir = r->is_dir;
    is_executable = r->is_executable;

    if (is_dir) {
        gui_file_preview_open_path(path);
        gui_taskbar_search_finish_open();
        return;
    }

    if (is_executable) {
        argv[0] = path;
        argv[1] = 0;
        pid = spawn_user_process(path, argv);
        if (pid > 0) {
            gui_taskbar_search_finish_open();
            return;
        }
    }

    gui_file_preview_open_file(path);
    gui_taskbar_search_finish_open();
}

static int gui_taskbar_search_result_index_at(int x, int y) {
    int row_h;
    int idx;
    if (!g_gui.taskbar_search_focused || g_gui.taskbar_search_results_rect.w <= 0) return -1;
    if (x < g_gui.taskbar_search_results_rect.x ||
        x >= g_gui.taskbar_search_results_rect.x + g_gui.taskbar_search_results_rect.w ||
        y < g_gui.taskbar_search_results_rect.y ||
        y >= g_gui.taskbar_search_results_rect.y + g_gui.taskbar_search_results_rect.h) {
        return -1;
    }
    row_h = GUI_TEXT_LINE_H + 8;
    idx = (y - g_gui.taskbar_search_results_rect.y) / row_h;
    if (idx < 0 || (uint32_t)idx >= g_gui.taskbar_search_result_count) return -1;
    if (!g_gui.taskbar_search_results[idx].used) return -1;
    return idx;
}

int gui_is_enter_key(int key) {
    return key == GUI_KEY_ENTER || key == '\n' || key == '\r';
}

static void gui_taskbar_search_submit(void) {
    if (!g_gui.taskbar_search_text[0]) {
        gui_desktop_run_action(GUI_DESKTOP_ACTION_FILES);
        return;
    }
    if (g_gui.taskbar_search_result_count == 0) {
        gui_taskbar_search_refresh_results();
    }
    if (g_gui.taskbar_search_result_count > 0) {
        uint32_t index = g_gui.taskbar_search_selected >= 0 ? (uint32_t)g_gui.taskbar_search_selected : 0u;
        gui_taskbar_search_open_result(index);
    } else {
        gui_invalidate_all();
    }
}

static int gui_taskbar_search_handle_key(int key) {
    if (!g_gui.taskbar_search_focused) return 0;
    if (gui_is_enter_key(key)) {
        gui_taskbar_search_submit();
        return 1;
    }
    if (key == GUI_KEY_BACKSPACE) {
        if (g_gui.taskbar_search_len > 0) {
            g_gui.taskbar_search_len--;
            g_gui.taskbar_search_text[g_gui.taskbar_search_len] = 0;
            gui_taskbar_search_refresh_results();
            gui_invalidate_all();
        }
        return 1;
    }
    if (key == GUI_KEY_UP || key == GUI_KEY_DOWN) {
        if (g_gui.taskbar_search_result_count > 0) {
            if (g_gui.taskbar_search_selected < 0) {
                g_gui.taskbar_search_selected = 0;
            } else if (key == GUI_KEY_UP) {
                if (g_gui.taskbar_search_selected <= 0) {
                    g_gui.taskbar_search_selected = (int)g_gui.taskbar_search_result_count - 1;
                } else {
                    g_gui.taskbar_search_selected--;
                }
            } else {
                g_gui.taskbar_search_selected++;
                if ((uint32_t)g_gui.taskbar_search_selected >= g_gui.taskbar_search_result_count) {
                    g_gui.taskbar_search_selected = 0;
                }
            }
            gui_invalidate_rect(g_gui.taskbar_search_results_rect.x,
                                g_gui.taskbar_search_results_rect.y,
                                g_gui.taskbar_search_results_rect.w,
                                g_gui.taskbar_search_results_rect.h);
        }
        return 1;
    }
    if (key >= 32 && key < 127 && g_gui.taskbar_search_len < GUI_TASKBAR_SEARCH_MAX) {
        g_gui.taskbar_search_text[g_gui.taskbar_search_len++] = (char)key;
        g_gui.taskbar_search_text[g_gui.taskbar_search_len] = 0;
        gui_taskbar_search_refresh_results();
        gui_invalidate_all();
        return 1;
    }
    return 1;
}

void gui_set_wallpaper_theme(uint32_t theme) {
    g_gui.wallpaper_theme = theme % 3u;
    gui_invalidate_all();
}

static void gui_desktop_run_action(uint32_t action) {
    if (action == GUI_DESKTOP_ACTION_TERMINAL) {
        gui_terminal_open();
        return;
    }
    if (action == GUI_DESKTOP_ACTION_ABOUT) {
        gui_about_open();
        return;
    }
    if (action == GUI_DESKTOP_ACTION_RECYCLE) {
        gui_recycle_open();
        return;
    }
    if (action == GUI_DESKTOP_ACTION_BROWSER) {
        char *argv[] = { "/bin/browser", "example.com", "/", 0 };
        int pid = spawn_user_process("/bin/browser", argv);
        if (pid < 0) {
            serial_write("[GUI] /bin/browser unavailable, falling back to kernel browser\n");
            gui_browser_open();
        } else {
            serial_write("[GUI] launched /bin/browser\n");
        }
        return;
    }
    if (action == GUI_DESKTOP_ACTION_STICKY) {
        gui_stickynote_open();
        return;
    }
    if (action == GUI_DESKTOP_ACTION_SETTINGS) {
        gui_settings_open();
        return;
    }
    if (action == GUI_DESKTOP_ACTION_THEME) {
        gui_set_wallpaper_theme(g_gui.wallpaper_theme + 1u);
        return;
    }
    if (action == GUI_DESKTOP_ACTION_NOTIF) {
        gui_notif_open();
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
        g_gui.desktop_start_menu_scroll_dragging = 0;
        if (g_gui.desktop_start_menu_open) {
            g_gui.desktop_start_menu_scroll = 0;
            /* refresh /bin entries every time menu opens (lazy scan) */
            gui_launcher_scan_bin(g_gui.launcher_app_count);
            gui_update_start_menu_layout();
        }
        gui_invalidate_all();
    }
    /* launch /bin/* program: action encoded as LAUNCH_BIN_BASE + index */
    if (action >= GUI_DESKTOP_ACTION_LAUNCH_BIN_BASE) {
        uint32_t idx = action - GUI_DESKTOP_ACTION_LAUNCH_BIN_BASE;
        if (idx < GUI_LAUNCHER_MAX_APPS) {
            gui_launcher_entry_t *e = &g_gui.launcher_entries[idx];
            if (e->used && e->path[0]) {
                serial_write("[GUI] launching ");
                serial_write(e->path);
                serial_write("\n");
                spawn_user_process(e->path, 0);
            }
        }
        return;
    }
}

/* taskbar-anchored notification widget (drawn in gui_draw_taskbar, hit-tested here) */
static gui_rect_t g_notif_widget_rect;
static uint32_t   g_notif_count; /* tentative def; real def + initial value live with the log below */

static int gui_desktop_handle_click(int x, int y) {
    uint32_t i;

    if (!g_gui.desktop_enabled) return 0;
    if (g_taskbar_search_rect.w > 0 && g_taskbar_search_rect.h > 0 &&
        gui_rect_contains(&g_taskbar_search_rect, x, y)) {
        g_gui.taskbar_search_focused = 1;
        g_gui.desktop_start_menu_open = 0;
        g_gui.desktop_start_menu_scroll_dragging = 0;
        gui_set_focused_widget(0);
        gui_taskbar_search_refresh_results();
        gui_invalidate_all();
        return 1;
    }
    if (g_gui.taskbar_search_focused) {
        g_gui.taskbar_search_focused = 0;
        gui_taskbar_search_reset_results();
        gui_invalidate_all();
    }
    /* tray widgets pinned at the bottom-right: notifications, network, then clock */
    if (g_notif_widget_rect.w > 0 && g_notif_widget_rect.h > 0 &&
        gui_rect_contains(&g_notif_widget_rect, x, y)) {
        gui_desktop_run_action(GUI_DESKTOP_ACTION_NOTIF);
        return 1;
    }
    if (g_network_widget_rect.w > 0 && g_network_widget_rect.h > 0 &&
        gui_rect_contains(&g_network_widget_rect, x, y)) {
        if (gui_tray_network_is_wireless()) {
            gui_wifi_open();
        } else {
            gui_network_open();
        }
        return 1;
    }
    if (gui_rect_contains(&g_gui.desktop_start_button_rect, x, y)) {
        gui_desktop_run_action(GUI_DESKTOP_ACTION_MENU);
        return 1;
    }
    if (g_gui.desktop_start_menu_open && gui_rect_contains(&g_gui.desktop_start_menu_rect, x, y)) {
        uint32_t launcher_index;
        if (gui_start_menu_scrollbar_begin_drag(x, y)) {
            return 1;
        }
        if (gui_start_menu_item_at(x, y, &launcher_index)) {
            g_gui.desktop_start_menu_open = 0;
            g_gui.desktop_start_menu_scroll_dragging = 0;
            gui_launcher_launch(launcher_index);
            gui_invalidate_all();
            return 1;
        }
        return 1;
    }
    if (g_gui.desktop_start_menu_open) {
        g_gui.desktop_start_menu_open = 0;
        g_gui.desktop_start_menu_scroll_dragging = 0;
        gui_invalidate_all();
    }
    for (i = 0; i < g_gui.desktop_icon_count && i < GUI_DESKTOP_MAX_ICONS; i++) {
        gui_desktop_icon_t *icon = &g_gui.desktop_icons[i];
        if (icon->used && gui_rect_contains(&icon->rect, x, y)) {
            int is_double_click;
            is_double_click = (g_desktop_last_click_icon == (int)i &&
                               g_desktop_last_click_frame != 0 &&
                               (g_gui.frame_counter - g_desktop_last_click_frame) < 18);
            g_desktop_selected_icon = (int)i;
            g_desktop_last_click_icon = (int)i;
            g_desktop_last_click_frame = g_gui.frame_counter;
            if (is_double_click) {
                g_desktop_last_click_icon = -1;
                g_desktop_last_click_frame = 0;
                gui_desktop_run_action(icon->action);
            }
            gui_invalidate_all();
            return 1;
        }
    }
    if (g_desktop_selected_icon >= 0) {
        gui_desktop_clear_icon_click_state();
        gui_invalidate_all();
    }
    return 0;
}

/* ============================================================
 * Menu / Context Menu
 * ============================================================ */
#define GUI_CTXMENU_MAX_ITEMS 10
#define GUI_CTXMENU_ITEM_H    22
#define GUI_CTXMENU_W         190
#define GUI_CTXMENU_PADDING   6
#define GUI_MENU_LABEL_LEN    40
#define GUI_MENU_SHORTCUT_LEN 16

typedef void (*gui_menu_handler_t)(int item_id, void *user);

typedef struct {
    char label[GUI_MENU_LABEL_LEN];
    char shortcut[GUI_MENU_SHORTCUT_LEN];
    int  id;
    int  enabled;
    int  separator;
    int  has_submenu;
} gui_menu_item_t;

typedef struct {
    int open;
    int x;
    int y;
    int w;
    int item_h;
    int padding;
    int item_count;
    gui_menu_item_t items[GUI_CTXMENU_MAX_ITEMS];
    gui_menu_handler_t handler;
    void *user;
} gui_menu_t;

static gui_menu_t g_ctxmenu;

static void gui_menu_copy_text(char *dst, int cap, const char *src) {
    int i;
    if (!dst || cap <= 0) return;
    if (!src) src = "";
    for (i = 0; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int gui_menu_height(const gui_menu_t *menu) {
    int i;
    int h;
    if (!menu) return 0;
    h = menu->padding * 2;
    for (i = 0; i < menu->item_count; i++) {
        h += menu->items[i].separator ? (menu->item_h / 2) : menu->item_h;
    }
    return h;
}

static void gui_menu_reset(gui_menu_t *menu) {
    if (!menu) return;
    memset(menu, 0, sizeof(*menu));
    menu->w = GUI_CTXMENU_W;
    menu->item_h = gui_text_line_height_px() + 10;
    if (menu->item_h < GUI_CTXMENU_ITEM_H) menu->item_h = GUI_CTXMENU_ITEM_H;
    menu->padding = GUI_CTXMENU_PADDING;
}

static void gui_menu_add_ex(gui_menu_t *menu, const char *label, const char *shortcut,
                            int id, int enabled, int has_submenu) {
    int n;
    if (!menu) return;
    n = menu->item_count;
    if (n >= GUI_CTXMENU_MAX_ITEMS) return;
    gui_menu_copy_text(menu->items[n].label, GUI_MENU_LABEL_LEN, label);
    gui_menu_copy_text(menu->items[n].shortcut, GUI_MENU_SHORTCUT_LEN, shortcut);
    menu->items[n].id = id;
    menu->items[n].enabled = enabled ? 1 : 0;
    menu->items[n].separator = 0;
    menu->items[n].has_submenu = has_submenu ? 1 : 0;
    menu->item_count = n + 1;
}

static void gui_menu_add_separator(gui_menu_t *menu) {
    int n;
    if (!menu) return;
    n = menu->item_count;
    if (n >= GUI_CTXMENU_MAX_ITEMS) return;
    menu->items[n].label[0] = 0;
    menu->items[n].shortcut[0] = 0;
    menu->items[n].id = 0;
    menu->items[n].enabled = 0;
    menu->items[n].separator = 1;
    menu->items[n].has_submenu = 0;
    menu->item_count = n + 1;
}

static void gui_menu_open_at(gui_menu_t *menu, int x, int y, gui_menu_handler_t h, void *user) {
    int menu_h;
    if (!menu) return;
    menu->open = 1;
    menu->handler = h;
    menu->user = user;
    menu->x = x;
    menu->y = y;
    menu_h = gui_menu_height(menu);
    if (menu->x + menu->w > (int)g_gui.width) {
        menu->x = (int)g_gui.width - menu->w - 2;
    }
    if (menu->y + menu_h > (int)g_gui.height - GUI_TASKBAR_HEIGHT) {
        menu->y = (int)g_gui.height - GUI_TASKBAR_HEIGHT - menu_h - 2;
    }
    if (menu->x < 0) menu->x = 0;
    if (menu->y < 0) menu->y = 0;
    gui_invalidate_all();
}

static int gui_menu_item_at(const gui_menu_t *menu, int x, int y) {
    int i;
    int item_y;
    int menu_h;
    if (!menu || !menu->open) return -1;
    menu_h = gui_menu_height(menu);
    if (x < menu->x || x >= menu->x + menu->w || y < menu->y || y >= menu->y + menu_h) return -2;
    item_y = menu->y + menu->padding;
    for (i = 0; i < menu->item_count; i++) {
        int h = menu->items[i].separator ? (menu->item_h / 2) : menu->item_h;
        if (y >= item_y && y < item_y + h) return i;
        item_y += h;
    }
    return -1;
}

static void gui_menu_draw_item_fill(int x, int y, int w, int h, int selected, uint32_t bg, uint32_t selected_bg) {
    gui_raw_fill_rect(x, y, w, h, selected ? selected_bg : bg);
}

static void gui_menu_draw(const gui_menu_t *menu) {
    int i;
    int item_y;
    int menu_h;
    uint32_t bg, border, fg, dim, hint, selected_bg;
    if (!menu || !menu->open) return;
    bg          = gui_rgb(245, 246, 248);
    border      = gui_rgb(120, 130, 145);
    fg          = gui_rgb(30, 36, 48);
    dim         = gui_rgb(160, 165, 175);
    hint        = gui_rgb(95, 105, 120);
    selected_bg = gui_rgb(220, 232, 248);
    menu_h = gui_menu_height(menu);
    gui_raw_fill_rect(menu->x + 3, menu->y + 3, menu->w, menu_h, gui_rgb(0, 0, 0));
    gui_raw_fill_rect(menu->x, menu->y, menu->w, menu_h, bg);
    gui_raw_line(menu->x, menu->y, menu->x + menu->w - 1, menu->y, border);
    gui_raw_line(menu->x, menu->y + menu_h - 1, menu->x + menu->w - 1, menu->y + menu_h - 1, border);
    gui_raw_line(menu->x, menu->y, menu->x, menu->y + menu_h - 1, border);
    gui_raw_line(menu->x + menu->w - 1, menu->y, menu->x + menu->w - 1, menu->y + menu_h - 1, border);
    item_y = menu->y + menu->padding;
    for (i = 0; i < menu->item_count; i++) {
        const gui_menu_item_t *item = &menu->items[i];
        if (item->separator) {
            int sy = item_y + (menu->item_h / 4);
            gui_raw_line(menu->x + 8, sy, menu->x + menu->w - 9, sy, gui_rgb(208, 214, 224));
            item_y += menu->item_h / 2;
            continue;
        }
        int hovered = (i == gui_menu_item_at(menu, g_gui.mouse_x, g_gui.mouse_y)) && item->enabled;
        gui_menu_draw_item_fill(menu->x + 3, item_y, menu->w - 6, menu->item_h, hovered, bg, selected_bg);
        {
            int ty = item_y + (menu->item_h - gui_text_line_height_px()) / 2;
            gui_draw_text(menu->x + 12, ty, item->label, item->enabled ? fg : dim);
            if (item->shortcut[0]) {
                int sx = menu->x + menu->w - 12 - (int)font_measure_text_width(font_get_default(), item->shortcut);
                gui_draw_text(sx, ty, item->shortcut, item->enabled ? hint : dim);
            }
            if (item->has_submenu) {
                gui_draw_text(menu->x + menu->w - 16, ty, ">", item->enabled ? hint : dim);
            }
        }
        item_y += menu->item_h;
    }
}

static int gui_ctxmenu_is_open(void) {
    return g_ctxmenu.open;
}

static void gui_ctxmenu_close(void) {
    if (g_ctxmenu.open) {
        g_ctxmenu.open = 0;
        gui_invalidate_all();
    }
}

static void gui_ctxmenu_open_at(int x, int y, gui_menu_handler_t h, void *user) {
    gui_menu_open_at(&g_ctxmenu, x, y, h, user);
}

static void gui_ctxmenu_reset(void) {
    gui_menu_reset(&g_ctxmenu);
}

static void gui_ctxmenu_add_ex(const char *label, const char *shortcut, int id, int enabled, int has_submenu) {
    gui_menu_add_ex(&g_ctxmenu, label, shortcut, id, enabled, has_submenu);
}

static void gui_ctxmenu_add(const char *label, int id, int enabled) {
    gui_ctxmenu_add_ex(label, "", id, enabled, 0);
}

static void gui_ctxmenu_add_separator(void) {
    gui_menu_add_separator(&g_ctxmenu);
}

static int gui_ctxmenu_handle_click(int x, int y) {
    int index;
    gui_menu_item_t item;
    gui_menu_handler_t h;
    void *u;
    if (!g_ctxmenu.open) return 0;
    index = gui_menu_item_at(&g_ctxmenu, x, y);
    if (index == -2) {
        gui_ctxmenu_close();
        return 0;
    }
    if (index < 0 || index >= g_ctxmenu.item_count) return 1;
    item = g_ctxmenu.items[index];
    h = g_ctxmenu.handler;
    u = g_ctxmenu.user;
    gui_ctxmenu_close();
    if (!item.separator && item.enabled && h) h(item.id, u);
    return 1;
}

static void gui_ctxmenu_draw(void) {
    gui_menu_draw(&g_ctxmenu);
}

/* When the hovered item changes during a plain mouse move, invalidate the
 * whole menu rect (incl. shadow) so the unified renderer repaints it and the
 * old highlight is erased -- otherwise the hover fill smears across items. */
static void gui_ctxmenu_invalidate_hover_changes(int old_x, int old_y, int new_x, int new_y) {
    int old_i, new_i, menu_h;
    if (!g_ctxmenu.open) return;
    old_i = gui_menu_item_at(&g_ctxmenu, old_x, old_y);
    new_i = gui_menu_item_at(&g_ctxmenu, new_x, new_y);
    if (old_i == new_i) return;
    menu_h = gui_menu_height(&g_ctxmenu);
    /* +3 covers the drop shadow drawn at (x+3, y+3) */
    gui_invalidate_rect(g_ctxmenu.x, g_ctxmenu.y, g_ctxmenu.w + 3, menu_h + 3);
}

/* Handlers for taskbar window right-click menu */
static void gui_ctxmenu_taskbar_window_action(int id, void *user) {
    gui_window_t *w = (gui_window_t *)user;
    if (id != 1 || !w || !w->used) return;
    if (!(w->flags & GUI_WINDOW_FLAG_CLOSABLE)) return;

    gui_set_focused_widget(0);
    if (g_gui.drag_window == w) g_gui.drag_window = 0;
    if (g_gui.pressed_widget && g_gui.pressed_widget->owner == w) g_gui.pressed_widget = 0;
    gui_destroy_window(w);
}

static void gui_ctxmenu_terminal_action(int id, void *user) {
    (void)user;
    switch (id) {
        case 1:
            if (gui_terminal_copy_selection()) {
                gui_notify("Terminal selection copied");
            } else {
                gui_notify("No terminal selection");
            }
            break;
        case 2:
            gui_terminal_clear_selection();
            break;
        case 3:
            gui_terminal_clear();
            break;
    }
}

static void gui_ctxmenu_wallpaper_action(int id, void *user) {
    (void)user;
    if (id >= 0 && id <= 2) {
        gui_set_wallpaper_theme((uint32_t)id);
    }
}

static void gui_ctxmenu_open_wallpaper_submenu(int x, int y) {
    gui_ctxmenu_reset();
    gui_ctxmenu_add_ex(i18n_t(I18N_KEY_WALLPAPER_DEFAULT), "", 0, 1, 0);
    gui_ctxmenu_add_ex(i18n_t(I18N_KEY_WALLPAPER_DAY),     "", 1, 1, 0);
    gui_ctxmenu_add_ex(i18n_t(I18N_KEY_WALLPAPER_SOLID),   "", 2, 1, 0);
    gui_ctxmenu_open_at(x, y, gui_ctxmenu_wallpaper_action, 0);
}

/* Handlers for desktop right-click menu */
typedef struct gui_desktop_menu_state {
    int wallpaper_x;
    int wallpaper_y;
} gui_desktop_menu_state_t;

static gui_desktop_menu_state_t g_desktop_menu_state;

static void gui_ctxmenu_desktop_action(int id, void *user) {
    gui_desktop_menu_state_t *state = (gui_desktop_menu_state_t *)user;
    switch (id) {
        case 1: gui_file_preview_open(); break;        /* Open Files */
        case 2: gui_desktop_run_action(GUI_DESKTOP_ACTION_TERMINAL); break;
        case 3:
            if (state) {
                gui_ctxmenu_open_wallpaper_submenu(state->wallpaper_x, state->wallpaper_y);
            }
            break;
        case 4: gui_notify(i18n_t(I18N_KEY_NOTIFY_DESKTOP_REFRESHED)); gui_invalidate_all(); break;
        case 5: gui_about_open(); break;
        case 6: gui_desktop_run_action(GUI_DESKTOP_ACTION_SETTINGS); break;
    }
}

static void gui_handle_mouse_right_down(int x, int y) {
    gui_window_t *tw;

    /* close any existing menu */
    if (g_ctxmenu.open) {
        gui_ctxmenu_close();
        return;
    }

    tw = gui_taskbar_window_at(x, y);
    if (tw) {
        gui_set_focused_widget(0);
        gui_ctxmenu_reset();
        gui_ctxmenu_add_ex(i18n_t(I18N_KEY_BTN_CLOSE), "Alt+F4", 1,
                           (tw->flags & GUI_WINDOW_FLAG_CLOSABLE) ? 1 : 0, 0);
        gui_ctxmenu_open_at(x, y, gui_ctxmenu_taskbar_window_action, tw);
        return;
    }

    tw = gui_window_at(x, y);
    if (tw == g_gui.terminal.window || (tw && (tw->flags & GUI_WINDOW_FLAG_TERMINAL))) {
        int has_selection = g_gui.terminal.view.has_selection ? 1 : 0;
        gui_set_active_window(tw);
        gui_set_focused_widget(0);
        gui_terminal_set_input_focus(1);
        gui_ctxmenu_reset();
        gui_ctxmenu_add_ex("Copy selection", "Ctrl+C", 1, has_selection, 0);
        gui_ctxmenu_add_ex("Clear selection", "Esc", 2, has_selection, 0);
        gui_ctxmenu_add_separator();
        gui_ctxmenu_add_ex("Clear terminal", "", 3, g_gui.terminal.enabled ? 1 : 0, 0);
        gui_ctxmenu_open_at(x, y, gui_ctxmenu_terminal_action, 0);
        return;
    }

    /* desktop-area right-click menu (not on taskbar / window) */
    if (tw != 0) return;
    if (y >= (int)g_gui.height - GUI_TASKBAR_HEIGHT) return;
    gui_ctxmenu_reset();
    gui_ctxmenu_add_ex(i18n_t(I18N_KEY_CTXMENU_OPEN_FILES),       "",      1, 1, 0);
    gui_ctxmenu_add_ex(i18n_t(I18N_KEY_CTXMENU_OPEN_TERMINAL),    "Ctrl+T", 2, 1, 0);
    gui_ctxmenu_add_separator();
    gui_ctxmenu_add_ex(i18n_t(I18N_KEY_CTXMENU_CHANGE_WALLPAPER), "",      3, 1, 1);
    gui_ctxmenu_add_ex(i18n_t(I18N_KEY_CTXMENU_REFRESH),          "F5",    4, 1, 0);
    gui_ctxmenu_add_separator();
    gui_ctxmenu_add_ex(i18n_t(I18N_KEY_CTXMENU_ABOUT),            "",      5, 1, 0);
    gui_ctxmenu_add_ex(i18n_t(I18N_KEY_CTXMENU_SETTINGS),         "",      6, 1, 0);
    g_desktop_menu_state.wallpaper_x = x + GUI_CTXMENU_W - 8;
    g_desktop_menu_state.wallpaper_y = y + GUI_CTXMENU_PADDING +
                                       (GUI_CTXMENU_ITEM_H * 2) +
                                       (GUI_CTXMENU_ITEM_H / 2);
    gui_ctxmenu_open_at(x, y, gui_ctxmenu_desktop_action, &g_desktop_menu_state);
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

int gui_should_capture_key_code_with_modifiers(int key, uint32_t modifiers) {
    gui_widget_t *wg;

    (void)modifiers;
    if (!g_gui.initialized) return 0;

    /* 锁屏门闸：锁屏运行期间吸走所有按键，使其能进入事件队列供 lockscreen_run() 消费。 */
    if (g_lockscreen_capture) return 1;

    /* Global hotkeys: always capture regardless of focus. */
    if (key == GUI_KEY_ALT_TAB || key == GUI_KEY_SUPER) return 1;

    /* Taskbar search is not a regular widget, but it still needs to receive
     * keyboard input after being clicked. Capture only the keys it handles so
     * terminal command editing keeps working when search is not focused.
     */
    if (g_gui.taskbar_search_focused) {
        if (gui_is_enter_key(key) || key == GUI_KEY_BACKSPACE ||
            key == GUI_KEY_UP || key == GUI_KEY_DOWN) return 1;
        if (key >= 32 && key < 127) return 1;
        return 0;
    }

    /* User-owned windows receive editing/navigation keys even when widget
     * focus is temporarily stale. This keeps user text boxes, such as the
     * browser address bar, responsive for Backspace/Delete/Home/End/arrows.
     * Printable keys still require a focused widget so terminal typing keeps
     * flowing through shell_run() when no GUI textbox is focused.
     */
    if (g_gui.active_window && g_gui.active_window->user_owner_pid != 0) {
        if (key == GUI_KEY_BACKSPACE || key == GUI_KEY_DELETE ||
            key == GUI_KEY_LEFT || key == GUI_KEY_RIGHT ||
            key == GUI_KEY_HOME || key == GUI_KEY_END ||
            gui_is_enter_key(key) || key == GUI_KEY_TAB) return 1;
    }

    /* GUI Terminal command line: when the terminal window is active and has
     * input focus, capture printable keys plus Enter/Backspace so the built-in
     * command line (gui_terminal_on_input) can receive them. Without this the
     * capture gate below returns 0 for printable keys and they never reach the
     * event queue. */
    if (g_gui.terminal.enabled && g_gui.terminal.input_focused &&
        g_gui.active_window == g_gui.terminal.window) {
        serial_write("[TERMGATE] hit key\n");
        if (gui_is_enter_key(key) || key == GUI_KEY_BACKSPACE || key == GUI_KEY_TAB) return 1;
        if (key >= 32 && key < 127) return 1;
    } else if (g_gui.terminal.enabled) {
        serial_write("[TERMGATE] miss: focus=");
        serial_write(g_gui.terminal.input_focused ? "1" : "0");
        serial_write(" activematch=");
        serial_write(g_gui.active_window == g_gui.terminal.window ? "1" : "0");
        serial_write("\n");
    }

    /* GUI Terminal is the Shell's graphical output window. Do not capture
     * printable keys here: shell_run() must receive them so command editing,
     * history, backspace and enter keep working. Ordinary GUI widgets are the
     * only keyboard-capture targets.
     */
    if (!g_gui.focused_widget || !g_gui.focused_widget->focused) return 0;

    wg = g_gui.focused_widget;
    if (!wg->visible || !wg->enabled) return 0;

    if (key == GUI_KEY_TAB) return 1;

    if (wg->type == GUI_WIDGET_LISTVIEW || wg->type == GUI_WIDGET_TABLEVIEW || wg->type == GUI_WIDGET_MENUBAR || wg->type == GUI_WIDGET_CONTEXTMENU || wg->type == GUI_WIDGET_DIALOG || wg->type == GUI_WIDGET_TOAST || wg->type == GUI_WIDGET_TREEVIEW) return 1;

    if (key == GUI_KEY_UP || key == GUI_KEY_DOWN) return 0;

    if (wg->type == GUI_WIDGET_TEXTBOX || wg->type == GUI_WIDGET_TEXTAREA) return 1;

    if (wg->type == GUI_WIDGET_BUTTON) {
        return gui_is_enter_key(key) || key == GUI_KEY_SPACE;
    }

    return 0;
}

int gui_should_capture_key_code(int key) {
    return gui_should_capture_key_code_with_modifiers(key, 0);
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
    info->dirty_rect_capacity = GUI_MAX_DIRTY_RECTS;
    info->full_dirty = g_gui.full_dirty;
    info->flush_generation = g_gui.flush_generation;
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

void gui_destroy_windows_by_user_owner(uint32_t owner_pid) {
    uint32_t i;
    if (owner_pid == 0) return;
    for (i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (g_gui.windows[i].used && g_gui.windows[i].user_owner_pid == owner_pid) {
            gui_destroy_window(&g_gui.windows[i]);
        }
    }
    gui_refresh_active_app();
}

static int gui_window_client_clip(gui_window_t *win, gui_rect_t *clip) {
    if (!win || !win->used || !clip) return 0;
    clip->x = win->rect.x + GUI_BORDER_SIZE;
    clip->y = win->rect.y + GUI_TITLE_HEIGHT;
    clip->w = win->rect.w - GUI_BORDER_SIZE * 2;
    clip->h = win->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE;
    return clip->w > 0 && clip->h > 0;
}

int gui_window_fill_client_rect(gui_window_t *win, int x, int y, int w, int h, uint32_t color) {
    gui_rect_t clip;
    if (!gui_window_client_clip(win, &clip) || w <= 0 || h <= 0) return -1;
    gui_set_clip_rect(&clip);
    gui_raw_fill_rect(clip.x + x, clip.y + y, w, h, color);
    gui_clear_clip_rect();
    gui_invalidate_rect(clip.x + x, clip.y + y, w, h);
    return 0;
}

int gui_window_draw_client_text(gui_window_t *win, int x, int y, const char *text, uint32_t color) {
    gui_rect_t clip;
    if (!gui_window_client_clip(win, &clip) || !text) return -1;
    gui_set_clip_rect(&clip);
    gui_draw_text(clip.x + x, clip.y + y, text, color);
    gui_clear_clip_rect();
    gui_invalidate_rect(clip.x, clip.y, clip.w, clip.h);
    return 0;
}

int gui_window_blit_client_rgba32(gui_window_t *win, int x, int y, int w, int h, const uint32_t *pixels, uint32_t src_stride) {
    gui_rect_t clip;
    int rc;
    if (!gui_window_client_clip(win, &clip) || !pixels || w <= 0 || h <= 0) return -1;
    gui_set_clip_rect(&clip);
    rc = gui_blit_rgba32(clip.x + x, clip.y + y, w, h, pixels, src_stride);
    gui_clear_clip_rect();
    return rc;
}

int gui_window_scroll_client_rect(gui_window_t *win, int dst_x, int dst_y, int src_x, int src_y, int w, int h) {
    gui_rect_t clip;
    int rc;
    if (!gui_window_client_clip(win, &clip) || w <= 0 || h <= 0) return -1;
    gui_set_clip_rect(&clip);
    rc = gui_copy_rect(clip.x + dst_x, clip.y + dst_y, clip.x + src_x, clip.y + src_y, w, h);
    gui_clear_clip_rect();
    return rc;
}

int gui_window_present_client(gui_window_t *win) {
    gui_rect_t clip;
    if (!gui_window_client_clip(win, &clip)) return -1;
    gui_invalidate_rect(clip.x, clip.y, clip.w, clip.h);
    return 0;
}

static int gui_widget_client_clip(gui_widget_t *widget, gui_rect_t *clip) {
    gui_rect_t win_clip;
    gui_rect_t widget_clip;
    if (!widget || !widget->owner || !widget->owner->used || !clip) return 0;
    if (widget->type != GUI_WIDGET_CANVAS) return 0;
    if (!gui_window_client_clip(widget->owner, &win_clip)) return 0;
    widget_clip.x = win_clip.x + widget->rect.x;
    widget_clip.y = win_clip.y + widget->rect.y;
    widget_clip.w = widget->rect.w;
    widget_clip.h = widget->rect.h;
    return gui_rect_intersect(&widget_clip, &win_clip, clip);
}

int gui_widget_fill_rect(gui_widget_t *widget, int x, int y, int w, int h, uint32_t color) {
    gui_rect_t clip;
    if (!gui_widget_client_clip(widget, &clip) || w <= 0 || h <= 0) return -1;
    gui_set_clip_rect(&clip);
    gui_raw_fill_rect(clip.x + x, clip.y + y, w, h, color);
    gui_clear_clip_rect();
    gui_invalidate_rect(clip.x + x, clip.y + y, w, h);
    return 0;
}

int gui_widget_draw_text(gui_widget_t *widget, int x, int y, const char *text, uint32_t color) {
    gui_rect_t clip;
    if (!gui_widget_client_clip(widget, &clip) || !text) return -1;
    gui_set_clip_rect(&clip);
    gui_draw_text(clip.x + x, clip.y + y, text, color);
    gui_clear_clip_rect();
    gui_invalidate_rect(clip.x, clip.y, clip.w, clip.h);
    return 0;
}

int gui_widget_blit_rgba32(gui_widget_t *widget, int x, int y, int w, int h, const uint32_t *pixels, uint32_t src_stride) {
    gui_rect_t clip;
    int rc;
    if (!gui_widget_client_clip(widget, &clip) || !pixels || w <= 0 || h <= 0) return -1;
    gui_set_clip_rect(&clip);
    rc = gui_blit_rgba32(clip.x + x, clip.y + y, w, h, pixels, src_stride);
    gui_clear_clip_rect();
    return rc;
}

int gui_widget_scroll_rect(gui_widget_t *widget, int dst_x, int dst_y, int src_x, int src_y, int w, int h) {
    gui_rect_t clip;
    int rc;
    if (!gui_widget_client_clip(widget, &clip) || w <= 0 || h <= 0) return -1;
    gui_set_clip_rect(&clip);
    rc = gui_copy_rect(clip.x + dst_x, clip.y + dst_y, clip.x + src_x, clip.y + src_y, w, h);
    gui_clear_clip_rect();
    return rc;
}

int gui_widget_present(gui_widget_t *widget) {
    gui_rect_t clip;
    if (!gui_widget_client_clip(widget, &clip)) return -1;
    gui_invalidate_rect(clip.x, clip.y, clip.w, clip.h);
    return 0;
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
    gui_copy_text(win->title, title ? title : i18n_t(I18N_KEY_WINDOW_DEFAULT), sizeof(win->title));
    win->bg_color = g_gui.colors.window_bg;
    win->flags = GUI_WINDOW_FLAG_CLOSABLE | GUI_WINDOW_FLAG_MINIMIZABLE | GUI_WINDOW_FLAG_MAXIMIZABLE | GUI_WINDOW_FLAG_RESIZABLE;
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

gui_window_t *gui_find_window(uint32_t window_id) {
    uint32_t i;
    if (window_id == 0) return 0;
    for (i = 0; i < GUI_MAX_WINDOWS; ++i) {
        gui_window_t *win = &g_gui.windows[i];
        if (win->used && win->id == window_id) return win;
    }
    return 0;
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
    wg->min_value = 0;
    wg->max_value = 100;
    wg->value = 0;
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

gui_widget_t *gui_add_icon_button(gui_window_t *window, int x, int y, int w, int h, const char *text, gui_icon_id_t icon, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_ICON_BUTTON, x, y, w, h, text);
    if (wg) {
        wg->icon = icon;
        wg->on_click = cb;
        wg->user_data = user_data;
    }
    return wg;
}

gui_widget_t *gui_add_toggle(gui_window_t *window, int x, int y, int w, int h, const char *text, int checked, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_TOGGLE, x, y, w, h, text);
    if (wg) {
        wg->min_value = 0;
        wg->max_value = 1;
        wg->on_click = cb;
        wg->user_data = user_data;
        gui_toggle_set_checked(wg, checked);
    }
    return wg;
}

gui_widget_t *gui_add_checkbox(gui_window_t *window, int x, int y, int w, int h, const char *text, int checked, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_CHECKBOX, x, y, w, h, text);
    if (wg) {
        wg->min_value = 0;
        wg->max_value = 1;
        wg->on_click = cb;
        wg->user_data = user_data;
        gui_checkbox_set_checked(wg, checked);
    }
    return wg;
}

gui_widget_t *gui_add_radiobutton(gui_window_t *window, int x, int y, int w, int h, const char *text, int group_id, int checked, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_RADIOBUTTON, x, y, w, h, text);
    if (wg) {
        wg->min_value = 0;
        wg->max_value = 1;
        wg->group_id = group_id;
        wg->on_click = cb;
        wg->user_data = user_data;
        gui_radiobutton_set_checked(wg, checked);
    }
    return wg;
}

gui_widget_t *gui_add_select(gui_window_t *window, int x, int y, int w, int h, const char *items, int selected_index, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_SELECT, x, y, w, h, "Select");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        gui_select_set_items(wg, items ? items : "");
        if (selected_index >= 0) gui_select_set_selected(wg, selected_index);
        wg->step = 0;
    }
    return wg;
}

gui_widget_t *gui_add_combobox(gui_window_t *window, int x, int y, int w, int h, const char *items, int selected_index, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_COMBOBOX, x, y, w, h, "ComboBox");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        gui_select_set_items(wg, items ? items : "");
        if (selected_index >= 0) gui_select_set_selected(wg, selected_index);
        wg->step = 0;
    }
    return wg;
}

gui_widget_t *gui_add_slider(gui_window_t *window, int x, int y, int w, int h, int min, int max, int value, int step, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_SLIDER, x, y, w, h, "");
    if (wg) {
        if (max <= min) max = min + 1;
        wg->min_value = min;
        wg->max_value = max;
        wg->step = step;
        wg->value = gui_slider_snap_value(wg, value);
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->bg_color = 0;
    }
    return wg;
}

int gui_slider_set_value(gui_widget_t *widget, int value) {
    if (!widget || widget->type != GUI_WIDGET_SLIDER) return -1;
    widget->value = gui_slider_snap_value(widget, value);
    return 0;
}

int gui_slider_get_value(gui_widget_t *widget, int *out_value) {
    if (!widget || widget->type != GUI_WIDGET_SLIDER || !out_value) return -1;
    *out_value = widget->value;
    return 0;
}

int gui_slider_set_step(gui_widget_t *widget, int step) {
    if (!widget || widget->type != GUI_WIDGET_SLIDER) return -1;
    widget->step = step;
    widget->value = gui_slider_snap_value(widget, widget->value);
    return 0;
}

int gui_slider_get_step(gui_widget_t *widget, int *out_step) {
    if (!widget || widget->type != GUI_WIDGET_SLIDER || !out_step) return -1;
    *out_step = gui_slider_normalize_step(widget);
    return 0;
}

gui_widget_t *gui_add_progressbar(gui_window_t *window, int x, int y, int w, int h, int min, int max, int value, uint32_t flags) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_PROGRESSBAR, x, y, w, h, "");
    if (wg) {
        if (max <= min) max = min + 1;
        wg->min_value = min;
        wg->max_value = max;
        wg->step = 1;
        wg->label_flags = flags & (GUI_PROGRESSBAR_INDETERMINATE | GUI_PROGRESSBAR_SHOW_PERCENT);
        wg->value = gui_slider_snap_value(wg, value);
        wg->bg_color = 0;
    }
    return wg;
}

int gui_progressbar_set_value(gui_widget_t *widget, int value) {
    if (!widget || widget->type != GUI_WIDGET_PROGRESSBAR) return -1;
    widget->value = gui_slider_snap_value(widget, value);
    return 0;
}

int gui_progressbar_get_value(gui_widget_t *widget, int *out_value) {
    if (!widget || widget->type != GUI_WIDGET_PROGRESSBAR || !out_value) return -1;
    *out_value = widget->value;
    return 0;
}

int gui_progressbar_set_flags(gui_widget_t *widget, uint32_t flags) {
    if (!widget || widget->type != GUI_WIDGET_PROGRESSBAR) return -1;
    widget->label_flags = flags & (GUI_PROGRESSBAR_INDETERMINATE | GUI_PROGRESSBAR_SHOW_PERCENT);
    return 0;
}

gui_widget_t *gui_add_spinner(gui_window_t *window, int x, int y, int w, int h, const char *text, uint32_t flags) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_SPINNER, x, y, w, h, text ? text : "");
    if (wg) {
        wg->label_flags = flags & (GUI_SPINNER_RUNNING | GUI_SPINNER_SHOW_LABEL);
        wg->value = 0;
        wg->step = 1;
    }
    return wg;
}

int gui_spinner_set_running(gui_widget_t *widget, int running) {
    if (!widget || widget->type != GUI_WIDGET_SPINNER) return -1;
    if (running) widget->label_flags |= GUI_SPINNER_RUNNING;
    else widget->label_flags &= ~GUI_SPINNER_RUNNING;
    if (running) widget->value = (widget->value + 1) & 7;
    return 0;
}

int gui_spinner_set_text(gui_widget_t *widget, const char *text) {
    if (!widget || widget->type != GUI_WIDGET_SPINNER) return -1;
    gui_widget_set_text(widget, text ? text : "");
    return 0;
}

gui_widget_t *gui_add_imageview(gui_window_t *window, int x, int y, int w, int h, uint32_t flags) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_IMAGEVIEW, x, y, w, h, "");
    if (wg) {
        wg->image_flags = flags & (GUI_IMAGEVIEW_KEEP_ASPECT | GUI_IMAGEVIEW_PLACEHOLDER | GUI_IMAGEVIEW_BITMAP_ALPHA);
        wg->bg_color = 0xFFF8FAFC;
        wg->fg_color = 0xFF334155;
    }
    return wg;
}

int gui_imageview_set_rgba(gui_widget_t *widget, const uint32_t *pixels, uint32_t width, uint32_t height, uint32_t flags) {
    uint32_t count;
    uint32_t *copy;
    if (!widget || widget->type != GUI_WIDGET_IMAGEVIEW || !pixels || width == 0 || height == 0) return -1;
    if (width > 256 || height > 256) return -1;
    count = width * height;
    if (height != 0 && count / height != width) return -1;
    copy = (uint32_t *)kmalloc(count * sizeof(uint32_t));
    if (!copy) return -1;
    memcpy(copy, pixels, count * sizeof(uint32_t));
    gui_widget_release_resources(widget);
    widget->image_pixels = copy;
    widget->image_width = width;
    widget->image_height = height;
    widget->image_flags = flags & (GUI_IMAGEVIEW_KEEP_ASPECT | GUI_IMAGEVIEW_PLACEHOLDER | GUI_IMAGEVIEW_BITMAP_ALPHA);
    return 0;
}

int gui_imageview_set_bitmap(gui_widget_t *widget, const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t stride, uint32_t fg_color, uint32_t bg_color, uint32_t flags) {
    uint32_t count;
    uint32_t *copy;
    if (!widget || widget->type != GUI_WIDGET_IMAGEVIEW || !pixels || width == 0 || height == 0) return -1;
    if (width > 256 || height > 256) return -1;
    if (stride == 0) stride = width;
    if (stride < width) return -1;
    count = width * height;
    if (height != 0 && count / height != width) return -1;
    copy = (uint32_t *)kmalloc(count * sizeof(uint32_t));
    if (!copy) return -1;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t v = pixels[y * stride + x];
            if (flags & GUI_IMAGEVIEW_BITMAP_ALPHA) {
                uint8_t a = v;
                uint32_t rgb = fg_color & 0x00FFFFFFu;
                copy[y * width + x] = ((uint32_t)a << 24) | rgb;
            } else {
                copy[y * width + x] = v ? fg_color : bg_color;
            }
        }
    }
    gui_widget_release_resources(widget);
    widget->image_pixels = copy;
    widget->image_width = width;
    widget->image_height = height;
    widget->image_flags = flags & (GUI_IMAGEVIEW_KEEP_ASPECT | GUI_IMAGEVIEW_PLACEHOLDER | GUI_IMAGEVIEW_BITMAP_ALPHA);
    return 0;
}

gui_widget_t *gui_add_toolbar(gui_window_t *window, int x, int y, int w, int h, const char *items, uint32_t flags) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_TOOLBAR, x, y, w, h, items ? items : "");
    if (wg) {
        wg->toolbar_flags = flags & (GUI_TOOLBAR_SHOW_GRIP | GUI_TOOLBAR_GROUPED_BUTTONS | GUI_TOOLBAR_HAS_ADDRESS | GUI_TOOLBAR_HAS_SEARCH | GUI_TOOLBAR_BOTTOM_BORDER);
        wg->bg_color = 0;
    }
    return wg;
}

int gui_toolbar_set_items(gui_widget_t *widget, const char *items) {
    if (!widget || widget->type != GUI_WIDGET_TOOLBAR) return -1;
    gui_widget_set_text(widget, items ? items : "");
    return 0;
}

gui_widget_t *gui_add_statusbar(gui_window_t *window, int x, int y, int w, int h, const char *text, uint32_t flags) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_STATUSBAR, x, y, w, h, text ? text : "");
    if (wg) {
        wg->statusbar_flags = flags & (GUI_STATUSBAR_LOADING | GUI_STATUSBAR_SIZE_GRIP | GUI_STATUSBAR_LINK_PROMPT | GUI_STATUSBAR_TOP_BORDER);
        wg->bg_color = 0;
    }
    return wg;
}

int gui_statusbar_set_text(gui_widget_t *widget, const char *text) {
    if (!widget || widget->type != GUI_WIDGET_STATUSBAR) return -1;
    gui_widget_set_text(widget, text ? text : "");
    return 0;
}

int gui_statusbar_set_flags(gui_widget_t *widget, uint32_t flags) {
    if (!widget || widget->type != GUI_WIDGET_STATUSBAR) return -1;
    widget->statusbar_flags = flags & (GUI_STATUSBAR_LOADING | GUI_STATUSBAR_SIZE_GRIP | GUI_STATUSBAR_LINK_PROMPT | GUI_STATUSBAR_TOP_BORDER);
    return 0;
}

gui_widget_t *gui_add_splitview(gui_window_t *window, int x, int y, int w, int h, int ratio, uint32_t flags, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_SPLITVIEW, x, y, w, h, "SplitView");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->splitview_flags = flags & (GUI_SPLITVIEW_HORIZONTAL | GUI_SPLITVIEW_RESIZABLE | GUI_SPLITVIEW_SHOW_GRIP | GUI_SPLITVIEW_PANE_BORDER);
        wg->min_value = 10;
        wg->max_value = 90;
        wg->step = 1;
        gui_splitview_set_ratio(wg, ratio);
    }
    return wg;
}

gui_widget_t *gui_add_tabview(gui_window_t *window, int x, int y, int w, int h, const char *tabs, int active_index, uint32_t flags, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_TABVIEW, x, y, w, h, "TabView");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->tabview_flags = flags & (GUI_TABVIEW_CLOSE_BUTTONS | GUI_TABVIEW_SCROLLABLE | GUI_TABVIEW_BOTTOM_BORDER);
        wg->min_value = 0;
        wg->value = active_index;
        gui_tabview_set_tabs(wg, tabs ? tabs : "");
        if (active_index >= 0) gui_tabview_set_active(wg, active_index);
    }
    return wg;
}

gui_widget_t *gui_add_iconview(gui_window_t *window, int x, int y, int w, int h, const char *items, int selected_index, uint32_t flags, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_ICONVIEW, x, y, w, h, "IconView");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->label_flags = flags & (GUI_ICONVIEW_SHOW_LABELS | GUI_ICONVIEW_COMPACT | GUI_ICONVIEW_LIST_MODE);
        if ((wg->label_flags & GUI_ICONVIEW_SHOW_LABELS) == 0 && (wg->label_flags & GUI_ICONVIEW_LIST_MODE)) wg->label_flags |= GUI_ICONVIEW_SHOW_LABELS;
        wg->min_value = 0;
        gui_iconview_set_items(wg, items ? items : "");
        if (selected_index >= 0) gui_iconview_set_selected(wg, selected_index);
    }
    return wg;
}

gui_widget_t *gui_add_listview(gui_window_t *window, int x, int y, int w, int h, const char *items, int selected_index, uint32_t flags, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_LISTVIEW, x, y, w, h, "ListView");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->label_flags = flags & (GUI_LISTVIEW_FLAG_MULTI_SELECT | GUI_LISTVIEW_FLAG_SHOW_CHECKBOXES);
        wg->min_value = 0;
        wg->selection_anchor = 0;
        gui_listview_set_items(wg, items ? items : "");
        if (selected_index >= 0) gui_listview_set_selected(wg, selected_index);
    }
    return wg;
}

gui_widget_t *gui_add_tableview(gui_window_t *window, int x, int y, int w, int h, const char *columns, const char *rows, int selected_row, uint32_t flags, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_TABLEVIEW, x, y, w, h, "TableView");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->label_flags = flags & (GUI_TABLEVIEW_FLAG_SHOW_HEADER | GUI_TABLEVIEW_FLAG_GRID_LINES | GUI_TABLEVIEW_FLAG_ROW_SELECT | GUI_TABLEVIEW_FLAG_SORTABLE);
        if ((wg->label_flags & GUI_TABLEVIEW_FLAG_SHOW_HEADER) == 0) wg->label_flags |= GUI_TABLEVIEW_FLAG_SHOW_HEADER;
        if ((wg->label_flags & GUI_TABLEVIEW_FLAG_ROW_SELECT) == 0) wg->label_flags |= GUI_TABLEVIEW_FLAG_ROW_SELECT;
        wg->min_value = 0;
        wg->table_sort_column = -1;
        wg->table_sort_ascending = 1;
        gui_widget_set_text(wg, columns ? columns : "");
        gui_tableview_set_rows(wg, rows ? rows : "");
        if (selected_row >= 0) gui_tableview_set_selected(wg, selected_row);
    }
    return wg;
}


gui_widget_t *gui_add_menubar(gui_window_t *window, int x, int y, int w, int h, const char *menus, int active_index, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_MENUBAR, x, y, w, h, "MenuBar");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->min_value = 0;
        wg->max_value = -1;
        wg->value = active_index;
        gui_menubar_set_menus(wg, menus ? menus : "");
        if (active_index >= 0) gui_menubar_set_active(wg, active_index);
    }
    return wg;
}


gui_widget_t *gui_add_dialog(gui_window_t *window, int x, int y, int w, int h, const char *title, const char *message, uint32_t flags, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_DIALOG, x, y, w, h, title ? title : "Dialog");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->label_flags = flags & (GUI_DIALOG_TYPE_MASK | GUI_DIALOG_FLAG_CANCEL | GUI_DIALOG_FLAG_MODAL | GUI_DIALOG_FLAG_DEFAULT_OK | GUI_DIALOG_FLAG_DEFAULT_CANCEL);
        if ((wg->label_flags & GUI_DIALOG_FLAG_DEFAULT_CANCEL) && !((wg->label_flags & GUI_DIALOG_FLAG_CANCEL) || ((wg->label_flags & GUI_DIALOG_TYPE_MASK) == GUI_DIALOG_TYPE_CONFIRM))) {
            wg->label_flags &= ~GUI_DIALOG_FLAG_DEFAULT_CANCEL;
        }
        if (!(wg->label_flags & GUI_DIALOG_FLAG_DEFAULT_CANCEL)) wg->label_flags |= GUI_DIALOG_FLAG_DEFAULT_OK;
        wg->value = GUI_DIALOG_RESULT_NONE;
        gui_widget_set_text(wg, title ? title : "Dialog");
        gui_widget_set_placeholder(wg, message ? message : "");
        if (w < 160) wg->rect.w = 160;
        if (h < 90) wg->rect.h = 90;
    }
    return wg;
}

int gui_dialog_set_message(gui_widget_t *widget, const char *message) {
    if (!widget || widget->type != GUI_WIDGET_DIALOG) return -1;
    gui_widget_set_placeholder(widget, message ? message : "");
    return 0;
}

int gui_dialog_show(gui_widget_t *widget) {
    if (!widget || widget->type != GUI_WIDGET_DIALOG) return -1;
    widget->visible = 1;
    widget->value = GUI_DIALOG_RESULT_NONE;
    gui_set_focused_widget(widget);
    gui_invalidate_all();
    return 0;
}

int gui_dialog_hide(gui_widget_t *widget) {
    if (!widget || widget->type != GUI_WIDGET_DIALOG) return -1;
    widget->visible = 0;
    if (g_gui.focused_widget == widget) gui_set_focused_widget(0);
    gui_invalidate_all();
    return 0;
}

gui_widget_t *gui_add_toast(gui_window_t *window, int x, int y, int w, int h, const char *message, uint32_t flags, uint32_t duration_ms, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_TOAST, x, y, w, h, "Notification");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->label_flags = flags & GUI_DIALOG_TYPE_MASK;
        wg->value = (int)duration_ms;
        wg->max_value = duration_ms ? (int)((uint32_t)sched_time_ms() + duration_ms) : 0;
        gui_widget_set_placeholder(wg, message ? message : "");
        if (w < 180) wg->rect.w = 180;
        if (h < 46) wg->rect.h = 46;
        wg->visible = 1;
    }
    return wg;
}

int gui_toast_show(gui_widget_t *widget, uint32_t duration_ms) {
    if (!widget || widget->type != GUI_WIDGET_TOAST) return -1;
    widget->visible = 1;
    widget->value = (int)duration_ms;
    widget->max_value = duration_ms ? (int)((uint32_t)sched_time_ms() + duration_ms) : 0;
    gui_invalidate_all();
    return 0;
}

int gui_toast_hide(gui_widget_t *widget) {
    if (!widget || widget->type != GUI_WIDGET_TOAST) return -1;
    widget->visible = 0;
    if (g_gui.focused_widget == widget) gui_set_focused_widget(0);
    gui_invalidate_all();
    return 0;
}

gui_widget_t *gui_add_contextmenu(gui_window_t *window, int x, int y, int w, int h, const char *items, int selected_index, uint32_t disabled_mask, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_CONTEXTMENU, x, y, w, h, "ContextMenu");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->min_value = 0;
        wg->max_value = -1;
        wg->value = selected_index;
        wg->label_flags = disabled_mask;
        gui_contextmenu_set_items(wg, items ? items : "");
        if (selected_index >= 0) gui_contextmenu_set_selected(wg, selected_index);
        wg->visible = 0;
    }
    return wg;
}

gui_widget_t *gui_add_treeview(gui_window_t *window, int x, int y, int w, int h, const char *nodes, int selected_node, uint32_t flags, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_TREEVIEW, x, y, w, h, "TreeView");
    if (wg) {
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->label_flags = flags & (GUI_TREEVIEW_FLAG_SHOW_LINES | GUI_TREEVIEW_FLAG_SHOW_ICONS);
        wg->min_value = 0;
        gui_treeview_set_nodes(wg, nodes ? nodes : "");
        if (selected_node >= 0) gui_treeview_set_selected(wg, selected_node);
    }
    return wg;
}

gui_widget_t *gui_add_scrollbar(gui_window_t *window, int x, int y, int w, int h, int min, int max, int value, int step, gui_widget_callback_t cb, void *user_data) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_SCROLLBAR, x, y, w, h, "");
    if (wg) {
        if (max <= min) max = min + 1;
        wg->min_value = min;
        wg->max_value = max;
        wg->step = step;
        wg->value = gui_slider_snap_value(wg, value);
        wg->on_click = cb;
        wg->user_data = user_data;
        wg->bg_color = 0;
    }
    return wg;
}

int gui_scrollbar_set_value(gui_widget_t *widget, int value) {
    if (!widget || widget->type != GUI_WIDGET_SCROLLBAR) return -1;
    widget->value = gui_slider_snap_value(widget, value);
    return 0;
}

int gui_scrollbar_get_value(gui_widget_t *widget, int *out_value) {
    if (!widget || widget->type != GUI_WIDGET_SCROLLBAR || !out_value) return -1;
    *out_value = widget->value;
    return 0;
}

int gui_scrollbar_set_step(gui_widget_t *widget, int step) {
    if (!widget || widget->type != GUI_WIDGET_SCROLLBAR) return -1;
    widget->step = step;
    widget->value = gui_slider_snap_value(widget, widget->value);
    return 0;
}

int gui_scrollbar_get_step(gui_widget_t *widget, int *out_step) {
    if (!widget || widget->type != GUI_WIDGET_SCROLLBAR || !out_step) return -1;
    *out_step = gui_slider_normalize_step(widget);
    return 0;
}

static int gui_scrollview_clamp_x(gui_widget_t *widget, int scroll_x) {
    int max_scroll;
    if (!widget) return 0;
    max_scroll = widget->min_value - widget->rect.w;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_x < 0) scroll_x = 0;
    if (scroll_x > max_scroll) scroll_x = max_scroll;
    return scroll_x;
}

static int gui_scrollview_clamp_y(gui_widget_t *widget, int scroll_y) {
    int max_scroll;
    if (!widget) return 0;
    max_scroll = widget->max_value - widget->rect.h;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;
    return scroll_y;
}

gui_widget_t *gui_add_scrollview(gui_window_t *window, int x, int y, int w, int h, int content_w, int content_h) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_SCROLLVIEW, x, y, w, h, "");
    if (wg) {
        if (content_w < w) content_w = w;
        if (content_h < h) content_h = h;
        wg->min_value = content_w;
        wg->max_value = content_h;
        wg->value = 0;
        wg->step = 0;
        wg->bg_color = gui_rgb(248, 250, 252);
        wg->fg_color = g_gui.colors.button_border;
        wg->panel_border_color = g_gui.colors.button_border;
        wg->panel_border_width = 1;
        wg->panel_padding = 0;
    }
    return wg;
}

int gui_scrollview_set_offset(gui_widget_t *widget, int scroll_x, int scroll_y) {
    if (!widget || widget->type != GUI_WIDGET_SCROLLVIEW) return -1;
    widget->value = gui_scrollview_clamp_x(widget, scroll_x);
    widget->step = gui_scrollview_clamp_y(widget, scroll_y);
    return 0;
}

int gui_scrollview_get_offset(gui_widget_t *widget, int *out_scroll_x, int *out_scroll_y) {
    if (!widget || widget->type != GUI_WIDGET_SCROLLVIEW || !out_scroll_x || !out_scroll_y) return -1;
    *out_scroll_x = widget->value;
    *out_scroll_y = widget->step;
    return 0;
}

int gui_scrollview_set_content_size(gui_widget_t *widget, int content_w, int content_h) {
    if (!widget || widget->type != GUI_WIDGET_SCROLLVIEW) return -1;
    if (content_w < widget->rect.w) content_w = widget->rect.w;
    if (content_h < widget->rect.h) content_h = widget->rect.h;
    widget->min_value = content_w;
    widget->max_value = content_h;
    widget->value = gui_scrollview_clamp_x(widget, widget->value);
    widget->step = gui_scrollview_clamp_y(widget, widget->step);
    return 0;
}

int gui_scrollview_get_content_size(gui_widget_t *widget, int *out_content_w, int *out_content_h) {
    if (!widget || widget->type != GUI_WIDGET_SCROLLVIEW || !out_content_w || !out_content_h) return -1;
    *out_content_w = widget->min_value;
    *out_content_h = widget->max_value;
    return 0;
}

gui_widget_t *gui_add_panel(gui_window_t *window, int x, int y, int w, int h, uint32_t color) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_PANEL, x, y, w, h, "");
    if (wg) {
        wg->bg_color = color;
        wg->panel_border_color = g_gui.colors.button_border;
        wg->panel_border_width = 0;
        wg->panel_padding = 0;
        wg->panel_flags = 0;
    }
    return wg;
}

gui_widget_t *gui_add_groupbox(gui_window_t *window, int x, int y, int w, int h, const char *title) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_GROUPBOX, x, y, w, h, title ? title : "");
    if (wg) {
        wg->bg_color = gui_rgb(255, 255, 255);
        wg->fg_color = g_gui.colors.text_fg;
        wg->panel_border_color = g_gui.colors.button_border;
        wg->panel_border_width = 1;
        wg->panel_padding = 12;
        wg->panel_flags = GUI_GROUPBOX_FLAG_BORDER | GUI_GROUPBOX_FLAG_CARD | GUI_GROUPBOX_FLAG_TITLEBAR;
    }
    return wg;
}

gui_widget_t *gui_add_form(gui_window_t *window, int x, int y, int w, int h, const char *title, uint32_t flags) {
    uint32_t group_flags = GUI_GROUPBOX_FLAG_BORDER | GUI_GROUPBOX_FLAG_CARD | GUI_GROUPBOX_FLAG_TITLEBAR;
    gui_widget_t *form = gui_add_groupbox(window, x, y, w, h, title ? title : "Form");
    if (!form) return NULL;
    if (!(flags & GUI_FORM_FLAG_BORDER)) group_flags &= ~GUI_GROUPBOX_FLAG_BORDER;
    if (!(flags & GUI_FORM_FLAG_CARD)) group_flags &= ~GUI_GROUPBOX_FLAG_CARD;
    if (!(flags & GUI_FORM_FLAG_TITLEBAR)) group_flags &= ~GUI_GROUPBOX_FLAG_TITLEBAR;
    gui_widget_set_groupbox_options(form, title ? title : "Form", gui_rgb(255, 255, 255), g_gui.colors.button_border, group_flags, 12);
    return form;
}

gui_widget_t *gui_add_form_field(gui_window_t *window, gui_widget_t *form, int row, const char *label, const char *value, const char *hint, uint32_t flags) {
    int pad = 12;
    int title_h = (form && form->text[0] != '\0') ? 26 : 6;
    int row_h = 54;
    int label_w;
    int input_x;
    int y;
    int input_w;
    gui_widget_t *label_wg;
    gui_widget_t *input_wg;
    gui_widget_t *hint_wg;
    if (!window || !form || form->type != GUI_WIDGET_GROUPBOX) return NULL;
    if (row < 0) row = 0;
    pad = (int)(form->panel_padding ? form->panel_padding : 12);
    label_w = form->rect.w / 3;
    if (label_w < 72) label_w = 72;
    if (label_w > 160) label_w = 160;
    input_x = form->rect.x + pad + label_w + 8;
    input_w = form->rect.w - pad * 2 - label_w - 8;
    if (input_w < 48) input_w = 48;
    y = form->rect.y + title_h + pad + row * row_h;
    label_wg = gui_add_label(window, form->rect.x + pad, y + 5, label_w, 20, label ? label : "Label");
    if (label_wg) { label_wg->parent_id = form->id; label_wg->fg_color = g_gui.colors.text_fg; }
    input_wg = gui_add_textbox(window, input_x, y, input_w, 24, value ? value : "");
    if (!input_wg) return NULL;
    input_wg->parent_id = form->id;
    if (hint && hint[0]) {
        hint_wg = gui_add_label(window, input_x, y + 28, input_w, 18, hint);
        if (hint_wg) {
            hint_wg->parent_id = form->id;
            hint_wg->fg_color = (flags & GUI_FORM_FIELD_ERROR) ? gui_rgb(185, 28, 28) : gui_rgb(100, 116, 139);
        }
    }
    return input_wg;
}

gui_widget_t *gui_add_form_submit(gui_window_t *window, gui_widget_t *form, const char *text, int row) {
    int pad;
    int title_h;
    int row_h = 54;
    int y;
    gui_widget_t *button;
    if (!window || !form || form->type != GUI_WIDGET_GROUPBOX) return NULL;
    if (row < 0) row = 0;
    pad = (int)(form->panel_padding ? form->panel_padding : 12);
    title_h = (form->text[0] != '\0') ? 26 : 6;
    y = form->rect.y + title_h + pad + row * row_h + 6;
    button = gui_add_button(window, form->rect.x + form->rect.w - pad - 96, y, 96, 26, text ? text : "Submit", NULL, NULL);
    if (button) button->parent_id = form->id;
    return button;
}

gui_widget_t *gui_add_canvas(gui_window_t *window, int x, int y, int w, int h, uint32_t color) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_CANVAS, x, y, w, h, "");
    if (wg) {
        wg->bg_color = color;
        wg->panel_border_color = 0;
        wg->panel_border_width = 0;
        wg->panel_padding = 0;
        wg->panel_flags = 0;
    }
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

gui_widget_t *gui_add_textarea(gui_window_t *window, int x, int y, int w, int h, const char *text) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_TEXTAREA, x, y, w, h, text);
    if (wg) {
        wg->fg_color = gui_rgb(20, 20, 20);
        wg->bg_color = gui_rgb(250, 250, 250);
        wg->textbox_flags = GUI_TEXTBOX_FLAG_MULTILINE;
        wg->cursor = (uint32_t)strlen(wg->text);
        wg->text_scroll = 0;
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

int gui_widget_set_parent(gui_widget_t *widget, gui_widget_t *parent) {
    gui_widget_t *p;
    uint32_t guard = 0;
    if (!widget || !widget->owner) return -1;
    if (!parent) {
        widget->parent_id = 0;
        gui_invalidate_all();
        return 0;
    }
    if (parent == widget || parent->owner != widget->owner || parent->type != GUI_WIDGET_SCROLLVIEW) return -1;
    p = parent;
    while (p && guard++ < GUI_MAX_WIDGETS_PER_WIN) {
        if (p == widget) return -1;
        p = gui_widget_parent(p);
    }
    widget->parent_id = parent->id;
    gui_invalidate_all();
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

int gui_widget_get_enabled(const gui_widget_t *widget) {
    return widget ? widget->enabled : 0;
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
    if (widget->type == GUI_WIDGET_TEXTBOX || widget->type == GUI_WIDGET_TEXTAREA || widget->cursor > len) widget->cursor = len;
    if (widget->type == GUI_WIDGET_TEXTBOX || widget->type == GUI_WIDGET_TEXTAREA) gui_text_widget_clear_selection(widget);
    if (widget->type == GUI_WIDGET_TEXTBOX) gui_textbox_ensure_cursor_visible(widget);
    else if (widget->type == GUI_WIDGET_TEXTAREA) gui_textarea_ensure_cursor_visible(widget);
    gui_widget_invalidate(widget);
}

void gui_widget_set_placeholder(gui_widget_t *widget, const char *placeholder) {
    if (!widget || (widget->type != GUI_WIDGET_TEXTBOX && widget->type != GUI_WIDGET_TEXTAREA && widget->type != GUI_WIDGET_LISTVIEW)) return;
    gui_copy_text(widget->placeholder, placeholder ? placeholder : "", sizeof(widget->placeholder));
    gui_widget_invalidate(widget);
}

void gui_widget_set_textbox_flags(gui_widget_t *widget, uint32_t flags) {
    if (!widget || (widget->type != GUI_WIDGET_TEXTBOX && widget->type != GUI_WIDGET_TEXTAREA)) return;
    widget->textbox_flags = flags & (GUI_TEXTBOX_FLAG_READONLY | GUI_TEXTBOX_FLAG_DISABLED | GUI_TEXTBOX_FLAG_PASSWORD | GUI_TEXTBOX_FLAG_MULTILINE | GUI_TEXTBOX_FLAG_WRAP);
    if (widget->type == GUI_WIDGET_TEXTAREA) widget->textbox_flags |= GUI_TEXTBOX_FLAG_MULTILINE;
    if (widget->textbox_flags & GUI_TEXTBOX_FLAG_DISABLED) {
        widget->enabled = 0;
        if (g_gui.focused_widget == widget) gui_set_focused_widget(0);
    } else {
        widget->enabled = 1;
    }
    gui_widget_invalidate(widget);
}

uint32_t gui_widget_get_textbox_flags(const gui_widget_t *widget) {
    return (widget && (widget->type == GUI_WIDGET_TEXTBOX || widget->type == GUI_WIDGET_TEXTAREA)) ? widget->textbox_flags : 0;
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

void gui_widget_set_icon(gui_widget_t *widget, gui_icon_id_t icon) {
    if (!widget) return;
    widget->icon = icon;
    gui_widget_invalidate(widget);
}

void gui_widget_set_button_flags(gui_widget_t *widget, uint32_t flags) {
    if (!widget || (widget->type != GUI_WIDGET_BUTTON && widget->type != GUI_WIDGET_ICON_BUTTON)) return;
    widget->button_flags = flags & (GUI_BUTTON_FLAG_DEFAULT | GUI_BUTTON_FLAG_DANGER | GUI_BUTTON_FLAG_FLAT);
    if ((widget->button_flags & GUI_BUTTON_FLAG_DEFAULT) && (widget->button_flags & GUI_BUTTON_FLAG_DANGER)) {
        widget->button_flags &= ~GUI_BUTTON_FLAG_DEFAULT;
    }
    gui_widget_invalidate(widget);
}

uint32_t gui_widget_get_button_flags(const gui_widget_t *widget) {
    return (widget && (widget->type == GUI_WIDGET_BUTTON || widget->type == GUI_WIDGET_ICON_BUTTON)) ? widget->button_flags : 0;
}

void gui_widget_set_label_options(gui_widget_t *widget, uint32_t flags, uint32_t align) {
    if (!widget || widget->type != GUI_WIDGET_LABEL) return;
    widget->label_flags = flags & (GUI_LABEL_FLAG_ELLIPSIS | GUI_LABEL_FLAG_MULTILINE | GUI_LABEL_FLAG_SELECTABLE | GUI_LABEL_FLAG_COPYABLE);
    if (align > GUI_LABEL_ALIGN_RIGHT) align = GUI_LABEL_ALIGN_LEFT;
    widget->label_align = align;
    gui_widget_invalidate(widget);
}

uint32_t gui_widget_get_label_flags(const gui_widget_t *widget) {
    return (widget && widget->type == GUI_WIDGET_LABEL) ? widget->label_flags : 0;
}

uint32_t gui_widget_get_label_align(const gui_widget_t *widget) {
    return (widget && widget->type == GUI_WIDGET_LABEL) ? widget->label_align : GUI_LABEL_ALIGN_LEFT;
}

int gui_widget_measure_label(const gui_widget_t *widget, int max_width, int *out_width, int *out_height) {
    const char *p;
    int char_w = GUI_CHAR_W;
    int line_h = gui_text_line_height_px();
    int max_chars;
    int lines = 0;
    int width_chars = 0;
    if (!widget || widget->type != GUI_WIDGET_LABEL || !out_width || !out_height || max_width <= 0) return -1;
    if (char_w <= 0) char_w = GUI_CHAR_W;
    max_chars = max_width / char_w;
    if (max_chars < 1) max_chars = 1;
    p = widget->text;
    if (!p || !*p) {
        *out_width = 0;
        *out_height = line_h;
        return 0;
    }
    while (*p) {
        uint32_t src_len = gui_text_len_until_break(p);
        int multiline = (widget->label_flags & GUI_LABEL_FLAG_MULTILINE) != 0;
        if (multiline) {
            while (src_len > (uint32_t)max_chars) {
                if (width_chars < max_chars) width_chars = max_chars;
                lines++;
                p += max_chars;
                src_len -= (uint32_t)max_chars;
            }
        }
        if ((int)src_len > width_chars) width_chars = (int)src_len;
        lines++;
        p += src_len;
        if (*p == '\n') p++;
        if (!multiline) break;
    }
    if (width_chars > max_chars) width_chars = max_chars;
    *out_width = width_chars * char_w;
    *out_height = (lines > 0 ? lines : 1) * line_h;
    return 0;
}

void gui_widget_set_panel_options(gui_widget_t *widget, uint32_t bg_color, uint32_t border_color, uint32_t flags, uint32_t border_width, uint32_t padding) {
    if (!widget || widget->type != GUI_WIDGET_PANEL) return;
    widget->bg_color = bg_color;
    widget->panel_border_color = border_color;
    widget->panel_flags = flags & (GUI_PANEL_FLAG_BORDER | GUI_PANEL_FLAG_ROUNDED | GUI_PANEL_FLAG_SHADOW);
    if (border_width > 8) border_width = 8;
    if (padding > 64) padding = 64;
    widget->panel_border_width = border_width;
    widget->panel_padding = padding;
    gui_widget_invalidate(widget);
}

void gui_widget_set_groupbox_options(gui_widget_t *widget, const char *title, uint32_t bg_color, uint32_t border_color, uint32_t flags, uint32_t padding) {
    size_t len;
    if (!widget || widget->type != GUI_WIDGET_GROUPBOX) return;
    if (title) {
        len = strlen(title);
        if (len >= sizeof(widget->text)) len = sizeof(widget->text) - 1;
        memcpy(widget->text, title, len);
        widget->text[len] = '\0';
    }
    widget->bg_color = bg_color ? bg_color : gui_rgb(255, 255, 255);
    widget->panel_border_color = border_color ? border_color : g_gui.colors.button_border;
    widget->panel_flags = flags & (GUI_GROUPBOX_FLAG_BORDER | GUI_GROUPBOX_FLAG_CARD | GUI_GROUPBOX_FLAG_ERROR | GUI_GROUPBOX_FLAG_TITLEBAR);
    if (padding > 64) padding = 64;
    widget->panel_padding = padding;
    widget->panel_border_width = 1;
    gui_widget_invalidate(widget);
}

void gui_widget_focus(gui_widget_t *widget) {
    gui_set_focused_widget(widget);
}

















int gui_str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}


/* ---- terminal path helpers ---- */

/* --- Route-2 bridges: run user programs from the GUI terminal --- */
extern int  arch_x86_64_usermode_launch_path(const char *path, int argc, const char **argv, int envc, const char **envp);

/* M2.0: 网络工具内建别名——识别 wget/ping/nslookup/ifconfig 作为第一个词，
 * 命中后直接映射到 /bin/<name>，免去用户敲 `run /bin/` 前缀。
 * 返回命中的命令名长度（>0），未命中返回 0。 */
int gui_net_alias_match(const char *c) {
    static const char *const names[] = { "wget", "ping", "nslookup", "ifconfig", 0 };
    for (int i = 0; names[i]; i++) {
        int n = 0; while (names[i][n]) n++;
        int eq = 1;
        for (int k = 0; k < n; k++) { if (c[k] != names[i][k]) { eq = 0; break; } }
        if (eq && (c[n] == ' ' || c[n] == '\0')) return n;
    }
    return 0;
}
extern void arch_x86_64_fd_set_stdout_mirror(void (*sink)(char c));
void gui_terminal_set_capture(int on);
void gui_terminal_write(const char *text);
void gui_terminal_enqueue_output(const char *text);

/* resolve arg (may be relative/absolute) against cwd -> out (abs, normalized) */

/* ---- Tab 补全 ---- */

/* s 是否以 pre 为前缀 */
/* 把追加内容 tail 逐字符送入命令行缓冲并显示 */

/* 处理 Tab 键：命令名补全 / 路径补全 */




/* Mirror sink for foreground user programs: called by the syscall layer
 * for every stdout/stderr byte while capture is active. We push into the
 * thread-safe output queue rather than drawing directly, so it is safe to
 * invoke from the ring3 dispatch context. */







static void gui_draw_taskbar_start_icon(gui_rect_t rect) {
    int hover = gui_taskbar_icon_hovered(rect);
    int x = rect.x + (rect.w - 17) / 2;
    int y = gui_taskbar_icon_y(rect, 17);
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
    int y = gui_taskbar_icon_y(rect, 22);
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

static void gui_draw_taskbar_sticky_icon(gui_rect_t rect) {
    int hover = gui_taskbar_icon_hovered(rect);
    int x = rect.x + (rect.w - 24) / 2;
    int y = gui_taskbar_icon_y(rect, 24);
    uint32_t paper = hover ? gui_rgb(255, 237, 127) : gui_rgb(255, 221, 92);
    uint32_t fold = hover ? gui_rgb(255, 250, 185) : gui_rgb(255, 241, 150);
    uint32_t edge = hover ? gui_rgb(210, 165, 58) : gui_rgb(190, 145, 44);
    uint32_t line = hover ? gui_rgb(116, 94, 52) : gui_rgb(92, 76, 44);

    if (hover) {
        y -= gui_taskbar_icon_hover_lift(rect);
        gui_taskbar_draw_icon_shadow(x, y, 24, 24);
    }

    gui_raw_fill_rect(x, y, 24, 24, paper);
    gui_raw_line(x, y, x + 23, y, edge);
    gui_raw_line(x, y, x, y + 23, edge);
    gui_raw_line(x + 23, y, x + 23, y + 23, edge);
    gui_raw_line(x, y + 23, x + 23, y + 23, edge);
    gui_raw_fill_rect(x + 16, y + 16, 7, 7, fold);
    gui_raw_line(x + 16, y + 16, x + 23, y + 16, edge);
    gui_raw_line(x + 16, y + 16, x + 16, y + 23, edge);
    gui_raw_line(x + 5, y + 7, x + 18, y + 7, line);
    gui_raw_line(x + 5, y + 12, x + 16, y + 12, line);
    gui_raw_line(x + 5, y + 17, x + 12, y + 17, line);
}


static void gui_draw_taskbar_window_icon(gui_rect_t rect, gui_window_t *w) {
    int hover = gui_taskbar_icon_hovered(rect);
    int minimized = w && ((w->flags & GUI_WINDOW_FLAG_MINIMIZED) != 0);
    int x;
    int y;
    uint32_t title;
    uint32_t body;
    uint32_t border = gui_rgb(205, 225, 255);
    uint32_t shadow = gui_rgb(60, 76, 110);

    if (w == gui_file_preview_window()) {
        const int folder_w = 28;
        const int folder_visible_top = 6;
        const int folder_visible_h = 25;
        x = rect.x + (rect.w - folder_w) / 2;
        y = gui_taskbar_icon_y(rect, folder_visible_h) - folder_visible_top;
        if (hover) {
            y -= gui_taskbar_icon_hover_lift(rect);
            gui_taskbar_draw_icon_shadow(x, y + folder_visible_top, folder_w, folder_visible_h);
        }
        gui_draw_folder_icon_art(x, y, gui_rgb(242, 194, 74));
        return;
    }
    if (w == gui_browser_window()) {
        const int browser_w = 28;
        const int browser_h = 26;
        x = rect.x + (rect.w - browser_w) / 2;
        y = gui_taskbar_icon_y(rect, browser_h);
        if (hover) {
            y -= gui_taskbar_icon_hover_lift(rect);
            gui_taskbar_draw_icon_shadow(x + 2, y + 2, 24, 24);
        }
        gui_draw_browser_icon_art(x, y, gui_rgb(74, 158, 245));
        return;
    }

    x = rect.x + (rect.w - 26) / 2;
    y = gui_taskbar_icon_y(rect, 22);
    title = minimized ? gui_rgb(95, 125, 175) : gui_rgb(86, 130, 210);
    body = minimized ? gui_rgb(28, 36, 55) : gui_rgb(32, 44, 70);
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

static int gui_clock_padding_x(void) {
    uint32_t pad = font_scale_value(6);
    if (pad < 6U) {
        pad = 6U;
    }
    return (int)pad;
}

static int gui_clock_text_height(void) {
    uint32_t text_h = font_get_ascii_height(font_get_default());

    if (text_h == 0U) {
        text_h = font_get_line_height(font_get_default());
    }
    return (int)text_h;
}

static int gui_clock_widget_height(void) {
    uint32_t text_h = (uint32_t)gui_clock_text_height();
    uint32_t vertical_pad = font_scale_value(8);
    uint32_t h = text_h + vertical_pad;

    if (h < 18U) {
        h = 18U;
    }
    if (h > GUI_TASKBAR_HEIGHT - 4U) {
        h = GUI_TASKBAR_HEIGHT - 4U;
    }
    return (int)h;
}

static gui_rect_t gui_get_clock_rect(const char *time_str) {
    int padding_x = gui_clock_padding_x();
    int text_w = (int)font_measure_text_width(font_get_default(), time_str);
    int h = gui_clock_widget_height();
    int w = text_w + padding_x * 2;
    int min_w = (int)font_measure_text_width(font_get_default(), "00:00:00") + padding_x * 2;

    if (w < min_w) {
        w = min_w;
    }
    if (w > (int)g_gui.width - 12) {
        w = (int)g_gui.width - 12;
    }

    return (gui_rect_t){
        (int)g_gui.width - w - 6,
        gui_taskbar_tray_y(h),
        w,
        h,
    };
}

static gui_rect_t gui_get_network_widget_rect(const gui_rect_t *clock_rect) {
    int h = gui_clock_widget_height();
    int w = h + 12;
    int gap = 4;

    if (w < 30) {
        w = 30;
    }

    return (gui_rect_t){
        clock_rect->x - w - gap,
        gui_taskbar_tray_y(h),
        w,
        h,
    };
}

static gui_rect_t gui_get_notification_widget_rect(const gui_rect_t *network_rect) {
    int h = gui_clock_widget_height();
    int w = h + 12;
    int gap = 4;

    if (w < 30) {
        w = 30;
    }

    return (gui_rect_t){
        network_rect->x - w - gap,
        gui_taskbar_tray_y(h),
        w,
        h,
    };
}

typedef enum gui_tray_network_kind {
    GUI_TRAY_NETWORK_NONE = 0,
    GUI_TRAY_NETWORK_WIRED,
    GUI_TRAY_NETWORK_WIRELESS,
} gui_tray_network_kind_t;

static gui_tray_network_kind_t gui_get_tray_network_state(int *ok) {
    net_device_info_t info;
    int connected;
    if (ok) *ok = 0;
    if (gui_get_primary_net_info(&info) != 0) {
        return GUI_TRAY_NETWORK_NONE;
    }
    connected = ((info.flags & NET_DEVICE_FLAG_UP) != 0) &&
                ((info.flags & NET_DEVICE_FLAG_LINK_UP) != 0) &&
                info.ip != 0;
    if (ok) *ok = connected;
    if (info.flags & NET_DEVICE_FLAG_WIRELESS) {
        return GUI_TRAY_NETWORK_WIRELESS;
    }
    return GUI_TRAY_NETWORK_WIRED;
}

int gui_tray_network_is_wireless(void) {
    int ok;
    return gui_get_tray_network_state(&ok) == GUI_TRAY_NETWORK_WIRELESS;
}

static void gui_draw_taskbar_network_error(int x, int y, uint32_t color) {
    gui_raw_line(x, y, x + 6, y + 6, color);
    gui_raw_line(x + 6, y, x, y + 6, color);
}

static void gui_draw_taskbar_network_icon(gui_rect_t net_rect) {
    int gx = net_rect.x + (net_rect.w - 15) / 2;
    int gy = gui_taskbar_icon_y(net_rect, 12);
    int ok = 0;
    gui_tray_network_kind_t kind = gui_get_tray_network_state(&ok);
    uint32_t nc = ok ? gui_rgb(120, 220, 255) : gui_rgb(255, 120, 120);
    uint32_t dim = ok ? gui_rgb(75, 95, 125) : gui_rgb(125, 75, 75);

    gui_raw_fill_rect(net_rect.x, net_rect.y, net_rect.w, net_rect.h,
                      gui_taskbar_icon_hovered(net_rect) ? gui_rgb(48, 58, 78) : gui_rgb(36, 44, 60));
    gui_raw_line(net_rect.x, net_rect.y, net_rect.x + net_rect.w - 1, net_rect.y, gui_rgb(80, 92, 120));
    gui_raw_line(net_rect.x, net_rect.y + net_rect.h - 1, net_rect.x + net_rect.w - 1,
                 net_rect.y + net_rect.h - 1, gui_rgb(12, 16, 24));
    gui_raw_line(net_rect.x, net_rect.y, net_rect.x, net_rect.y + net_rect.h - 1, gui_rgb(80, 92, 120));
    gui_raw_line(net_rect.x + net_rect.w - 1, net_rect.y, net_rect.x + net_rect.w - 1,
                 net_rect.y + net_rect.h - 1, gui_rgb(12, 16, 24));

    if (kind == GUI_TRAY_NETWORK_WIRELESS) {
        gui_raw_line(gx + 1, gy + 8, gx + 7, gy + 2, dim);
        gui_raw_line(gx + 13, gy + 8, gx + 7, gy + 2, dim);
        gui_raw_line(gx + 3, gy + 10, gx + 7, gy + 6, nc);
        gui_raw_line(gx + 11, gy + 10, gx + 7, gy + 6, nc);
        gui_raw_fill_rect(gx + 6, gy + 10, 3, 2, nc);
    } else if (kind == GUI_TRAY_NETWORK_WIRED) {
        gui_raw_fill_rect(gx + 2, gy + 2, 11, 7, nc);
        gui_raw_fill_rect(gx + 5, gy + 9, 5, 2, nc);
        gui_raw_line(gx + 1, gy + 1, gx + 13, gy + 1, dim);
        gui_raw_line(gx + 1, gy + 1, gx + 1, gy + 9, dim);
        gui_raw_line(gx + 13, gy + 1, gx + 13, gy + 9, dim);
    } else {
        gui_raw_fill_rect(gx, gy + 8, 3, 4, dim);
        gui_raw_fill_rect(gx + 6, gy + 5, 3, 7, dim);
        gui_raw_fill_rect(gx + 12, gy + 2, 3, 10, dim);
    }
    if (!ok) {
        gui_draw_taskbar_network_error(net_rect.x + net_rect.w - 9, net_rect.y + 4, gui_rgb(255, 100, 100));
    }
    g_network_widget_rect = net_rect;
}

static void gui_draw_taskbar_search_box(gui_rect_t r) {
    const char *text = g_gui.taskbar_search_text[0] ? g_gui.taskbar_search_text : i18n_t(I18N_KEY_TASKBAR_SEARCH);
    char clipped[GUI_TASKBAR_SEARCH_MAX + 1];
    uint32_t bg = g_gui.taskbar_search_focused ? gui_rgb(46, 56, 76) : gui_rgb(34, 40, 54);
    uint32_t fg = g_gui.taskbar_search_text[0] ? gui_rgb(230, 240, 255) : gui_rgb(150, 170, 195);
    int text_h = GUI_TEXT_LINE_H;
    int ty = r.y + (r.h - text_h) / 2;
    int max_chars;
    int cw = GUI_CHAR_W;
    if (cw <= 0) cw = 8;
    if (ty < r.y + 2) ty = r.y + 2;
    gui_raw_fill_rect(r.x, r.y, r.w, r.h, bg);
    gui_raw_line(r.x, r.y, r.x + r.w - 1, r.y, gui_rgb(82, 96, 126));
    gui_raw_line(r.x, r.y + r.h - 1, r.x + r.w - 1, r.y + r.h - 1, gui_rgb(12, 16, 24));
    gui_raw_line(r.x, r.y, r.x, r.y + r.h - 1, gui_rgb(82, 96, 126));
    gui_raw_line(r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h - 1, gui_rgb(12, 16, 24));

    /* magnifier glyph */
    {
        int gy = gui_taskbar_icon_y(r, 12);
        gui_raw_line(r.x + 6, gy + 0, r.x + 10, gy + 0, gui_rgb(170, 190, 220));
        gui_raw_line(r.x + 5, gy + 1, r.x + 5, gy + 4, gui_rgb(170, 190, 220));
        gui_raw_line(r.x + 11, gy + 1, r.x + 11, gy + 4, gui_rgb(170, 190, 220));
        gui_raw_line(r.x + 6, gy + 5, r.x + 10, gy + 5, gui_rgb(170, 190, 220));
        gui_raw_line(r.x + 10, gy + 5, r.x + 14, gy + 9, gui_rgb(170, 190, 220));
    }

    max_chars = gui_utf8_prefix_for_width(text, (uint32_t)strlen(text), r.w - 28);
    if (max_chars + 1 > (int)sizeof(clipped)) max_chars = (int)sizeof(clipped) - 1;
    memcpy(clipped, text, (size_t)max_chars);
    clipped[max_chars] = 0;
    gui_draw_text(r.x + 22, ty, clipped, fg);
    if (g_gui.taskbar_search_focused) {
        int caret_x = r.x + 22 + (int)font_measure_text_width(font_get_default(), g_gui.taskbar_search_text);
        if (caret_x > r.x + r.w - 6) caret_x = r.x + r.w - 6;
        gui_raw_line(caret_x, ty, caret_x, ty + text_h - 1, gui_rgb(230, 240, 255));
    }
}

static int taskbar_search_str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static const char *taskbar_search_path_ext(const char *path) {
    const char *dot = 0;
    const char *p = path;
    while (p && *p) {
        if (*p == '/') dot = 0;
        else if (*p == '.') dot = p;
        p++;
    }
    return dot ? dot + 1 : "";
}

static gui_icon_id_t taskbar_search_result_icon(const gui_taskbar_search_result_t *r) {
    const char *ext;
    if (!r) return GUI_ICON_FILE_GENERIC;
    if (r->is_dir) return GUI_ICON_FOLDER;
    if (r->is_executable) return GUI_ICON_FILE_EXEC;
    ext = taskbar_search_path_ext(r->path);
    if (!*ext) return GUI_ICON_FILE_GENERIC;
    if (taskbar_search_str_ieq(ext, "c") || taskbar_search_str_ieq(ext, "h") ||
        taskbar_search_str_ieq(ext, "cpp") || taskbar_search_str_ieq(ext, "hpp") ||
        taskbar_search_str_ieq(ext, "js") || taskbar_search_str_ieq(ext, "ts") ||
        taskbar_search_str_ieq(ext, "py") || taskbar_search_str_ieq(ext, "go") ||
        taskbar_search_str_ieq(ext, "rs") || taskbar_search_str_ieq(ext, "asm")) return GUI_ICON_FILE_CODE;
    if (taskbar_search_str_ieq(ext, "md")) return GUI_ICON_FILE_MARKUP;
    if (taskbar_search_str_ieq(ext, "txt") || taskbar_search_str_ieq(ext, "log") ||
        taskbar_search_str_ieq(ext, "readme")) return GUI_ICON_FILE_TEXT;
    if (taskbar_search_str_ieq(ext, "sh") || taskbar_search_str_ieq(ext, "bash")) return GUI_ICON_FILE_SHELL;
    if (taskbar_search_str_ieq(ext, "conf") || taskbar_search_str_ieq(ext, "cfg") ||
        taskbar_search_str_ieq(ext, "ini") || taskbar_search_str_ieq(ext, "json") ||
        taskbar_search_str_ieq(ext, "yaml") || taskbar_search_str_ieq(ext, "yml") ||
        taskbar_search_str_ieq(ext, "toml")) return GUI_ICON_FILE_CONFIG;
    if (taskbar_search_str_ieq(ext, "png") || taskbar_search_str_ieq(ext, "jpg") ||
        taskbar_search_str_ieq(ext, "jpeg") || taskbar_search_str_ieq(ext, "bmp") ||
        taskbar_search_str_ieq(ext, "gif") || taskbar_search_str_ieq(ext, "ico")) return GUI_ICON_FILE_IMAGE;
    if (taskbar_search_str_ieq(ext, "zip") || taskbar_search_str_ieq(ext, "tar") ||
        taskbar_search_str_ieq(ext, "gz") || taskbar_search_str_ieq(ext, "bz2") ||
        taskbar_search_str_ieq(ext, "xz") || taskbar_search_str_ieq(ext, "7z")) return GUI_ICON_FILE_ARCHIVE;
    if (taskbar_search_str_ieq(ext, "elf") || taskbar_search_str_ieq(ext, "exe") ||
        taskbar_search_str_ieq(ext, "bin") || taskbar_search_str_ieq(ext, "o") ||
        taskbar_search_str_ieq(ext, "a") || taskbar_search_str_ieq(ext, "so")) return GUI_ICON_FILE_EXEC;
    return GUI_ICON_FILE_GENERIC;
}

static void gui_draw_taskbar_search_results(void) {
    uint32_t i;
    gui_rect_t box = g_taskbar_search_rect;
    gui_list_view_t list;
    int row_h = GUI_TEXT_LINE_H + 8;
    int panel_h;
    int panel_y;
    int max_chars;
    int text_x_pad = 30;
    if (!g_gui.taskbar_search_focused || !g_gui.taskbar_search_text[0]) {
        g_gui.taskbar_search_results_rect.x = 0;
        g_gui.taskbar_search_results_rect.y = 0;
        g_gui.taskbar_search_results_rect.w = 0;
        g_gui.taskbar_search_results_rect.h = 0;
        return;
    }
    panel_h = (int)g_gui.taskbar_search_result_count * row_h;
    if (panel_h <= 0) panel_h = row_h;
    panel_y = box.y - panel_h - 6;
    if (panel_y < 0) panel_y = 0;
    g_gui.taskbar_search_results_rect.x = box.x;
    g_gui.taskbar_search_results_rect.y = panel_y;
    g_gui.taskbar_search_results_rect.w = box.w + 180;
    if (g_gui.taskbar_search_results_rect.x + g_gui.taskbar_search_results_rect.w > (int)g_gui.width) {
        g_gui.taskbar_search_results_rect.w = (int)g_gui.width - g_gui.taskbar_search_results_rect.x - 4;
    }
    g_gui.taskbar_search_results_rect.h = panel_h;

    gui_raw_fill_rect(g_gui.taskbar_search_results_rect.x, g_gui.taskbar_search_results_rect.y,
                      g_gui.taskbar_search_results_rect.w, g_gui.taskbar_search_results_rect.h,
                      gui_rgb(28, 34, 48));
    gui_raw_line(g_gui.taskbar_search_results_rect.x, g_gui.taskbar_search_results_rect.y,
                 g_gui.taskbar_search_results_rect.x + g_gui.taskbar_search_results_rect.w - 1,
                 g_gui.taskbar_search_results_rect.y, gui_rgb(86, 100, 130));
    gui_raw_line(g_gui.taskbar_search_results_rect.x, g_gui.taskbar_search_results_rect.y,
                 g_gui.taskbar_search_results_rect.x,
                 g_gui.taskbar_search_results_rect.y + g_gui.taskbar_search_results_rect.h - 1,
                 gui_rgb(86, 100, 130));
    gui_raw_line(g_gui.taskbar_search_results_rect.x + g_gui.taskbar_search_results_rect.w - 1,
                 g_gui.taskbar_search_results_rect.y,
                 g_gui.taskbar_search_results_rect.x + g_gui.taskbar_search_results_rect.w - 1,
                 g_gui.taskbar_search_results_rect.y + g_gui.taskbar_search_results_rect.h - 1,
                 gui_rgb(10, 14, 22));
    gui_raw_line(g_gui.taskbar_search_results_rect.x,
                 g_gui.taskbar_search_results_rect.y + g_gui.taskbar_search_results_rect.h - 1,
                 g_gui.taskbar_search_results_rect.x + g_gui.taskbar_search_results_rect.w - 1,
                 g_gui.taskbar_search_results_rect.y + g_gui.taskbar_search_results_rect.h - 1,
                 gui_rgb(10, 14, 22));

    max_chars = g_gui.taskbar_search_results_rect.w - text_x_pad - 8;
    if (max_chars < 1) max_chars = 1;
    if (g_gui.taskbar_search_result_count == 0) {
        gui_draw_text(g_gui.taskbar_search_results_rect.x + 10,
                      g_gui.taskbar_search_results_rect.y + 4,
                      "No files found", gui_rgb(160, 178, 205));
        return;
    }

    list.rect = g_gui.taskbar_search_results_rect;
    list.row_h = row_h;
    list.selected_row = g_gui.taskbar_search_selected;
    list.bg = gui_rgb(28, 34, 48);
    list.alt_bg = gui_rgb(31, 38, 54);
    list.selected_bg = gui_rgb(50, 64, 92);
    list.border = gui_rgb(38, 46, 64);
    list.selected_border = gui_rgb(90, 118, 170);

    for (i = 0; i < g_gui.taskbar_search_result_count; i++) {
        gui_taskbar_search_result_t *r = &g_gui.taskbar_search_results[i];
        gui_rect_t row_rect;
        gui_rect_t cell_rect;
        char line[GUI_TASKBAR_SEARCH_PATH_LEN];
        uint32_t k = 0;
        uint32_t j = 0;
        uint32_t text_color;
        if (!r->used) continue;
        row_rect = gui_list_view_row_rect(&list, (int)i);
        cell_rect = row_rect;
        cell_rect.x += 4;
        cell_rect.y += 2;
        cell_rect.w -= 8;
        cell_rect.h -= 4;
        gui_list_view_draw_row(&list, (int)i);
        k = gui_utf8_prefix_for_width(r->path, (uint32_t)strlen(r->path), max_chars);
        if (k + 1 > sizeof(line)) k = sizeof(line) - 1;
        memcpy(line, r->path, k);
        line[k] = 0;
        text_color = r->is_dir ? gui_rgb(170, 220, 255) :
                     (r->is_executable ? gui_rgb(255, 210, 170) : gui_rgb(230, 240, 255));
        gui_draw_file_icon_cell(&cell_rect, line, taskbar_search_result_icon(r),
                                (int)i == g_gui.taskbar_search_selected, 0,
                                text_color);
    }
}

static void gui_draw_taskbar(void) {
    uint32_t i;
    gui_taskbar_layout_t layout;
    int bx;
    gui_taskbar_get_layout(&layout);
    bx = layout.first_window_x;

    /* 娴犺濮熼弽蹇氬剹閺咁垱铆鐠恒劌鍙忕仦蹇擃啍鎼达讣绱濋柆顔荤秶娴犺濮熼弽蹇撲箯閸欏厖琚辨笟褎婀紒妯哄煑閻ㄥ嫰绮︽惔鏇礉闁灝鍘ゆ竟浣虹剨娑撳锟?     * 閸︺劌鐪虫稉顓濇崲閸斺剝鐖锕€褰告稉銈勬櫠瑜般垺鍨氱拹顖溾敍閸忋劌鐫嗛惃鍕潒鐟欏浜ｉ幀褑鎽戠痪瑁も偓鍌氭禈閺嶅洣缍呯純顔荤矝閹稿鐪虫稉顓炵鐏炩偓锟?*/
    gui_raw_fill_rect(0, layout.bar.y, (int)g_gui.width, layout.bar.h, gui_rgb(24, 28, 38));
    /* 閸忋劌顔旀惔鏇㈠劥闂冩潙濂栨潏瑙勬暪锟?*/
    gui_raw_fill_rect(0, layout.bar.y + layout.bar.h - 1, (int)g_gui.width, 1, gui_rgb(10, 13, 20));

    g_gui.desktop_start_button_rect = layout.start_button;
    gui_draw_taskbar_start_icon(layout.start_button);

    g_taskbar_search_rect = layout.search_box;
    gui_draw_taskbar_search_box(layout.search_box);

    gui_draw_taskbar_terminal_icon(layout.terminal_button);
    gui_draw_taskbar_sticky_icon(layout.sticky_button);

    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        gui_rect_t button;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!gui_taskbar_should_list_window(w)) continue;
        button.x = bx;
        button.y = layout.item_y;
        button.w = gui_taskbar_button_width(w);
        button.h = layout.item_h;
        if (button.x + button.w > layout.bar.x + layout.bar.w - 8) button.w = layout.bar.x + layout.bar.w - 8 - button.x;
        if (button.w <= 0) break;
        gui_draw_taskbar_window_icon(button, w);
        bx += gui_taskbar_button_width(w) + 6;
    }

    /* clock display pinned to bottom-right of the screen (independent of taskbar position) */
    {
        uint32_t ms = sched_time_ms();
        uint32_t total_sec = ms / 1000u;
        uint32_t hours = (total_sec / 3600u) % 24u;
        uint32_t mins  = (total_sec / 60u) % 60u;
        uint32_t secs  = total_sec % 60u;
        char clk[16];
        int p = 0;
        gui_rect_t clock_rect;
        gui_rect_t notif_rect;
        int padding_x = gui_clock_padding_x();
        int text_h = gui_clock_text_height();
        int text_y;
        clk[p++] = (char)('0' + (hours / 10) % 10);
        clk[p++] = (char)('0' + hours % 10);
        clk[p++] = ':';
        clk[p++] = (char)('0' + (mins / 10) % 10);
        clk[p++] = (char)('0' + mins % 10);
        clk[p++] = ':';
        clk[p++] = (char)('0' + (secs / 10) % 10);
        clk[p++] = (char)('0' + secs % 10);
        clk[p] = 0;

        gui_rect_t net_rect;
        clock_rect = gui_get_clock_rect(clk);
        net_rect = gui_get_network_widget_rect(&clock_rect);
        notif_rect = gui_get_notification_widget_rect(&net_rect);
        text_y = gui_taskbar_text_y(clock_rect, text_h);

        gui_raw_fill_rect(clock_rect.x, clock_rect.y, clock_rect.w, clock_rect.h, gui_rgb(36, 44, 60));
        gui_raw_line(clock_rect.x, clock_rect.y, clock_rect.x + clock_rect.w - 1, clock_rect.y, gui_rgb(80, 92, 120));
        gui_raw_line(clock_rect.x, clock_rect.y + clock_rect.h - 1, clock_rect.x + clock_rect.w - 1,
                     clock_rect.y + clock_rect.h - 1, gui_rgb(12, 16, 24));
        gui_raw_line(clock_rect.x, clock_rect.y, clock_rect.x, clock_rect.y + clock_rect.h - 1, gui_rgb(80, 92, 120));
        gui_raw_line(clock_rect.x + clock_rect.w - 1, clock_rect.y, clock_rect.x + clock_rect.w - 1,
                     clock_rect.y + clock_rect.h - 1, gui_rgb(12, 16, 24));
        gui_draw_text(clock_rect.x + padding_x, text_y, clk, gui_rgb(220, 240, 255));

        gui_draw_taskbar_network_icon(net_rect);

        {
            char cbuf[8];
            int n = (int)g_notif_count;
            int cp = 0;
            int glyph_y = gui_taskbar_icon_y(notif_rect, 8);
            int count_text_y = gui_taskbar_text_y(notif_rect, text_h);
            if (n > 99) n = 99;
            if (n >= 10) cbuf[cp++] = (char)('0' + (n / 10) % 10);
            cbuf[cp++] = (char)('0' + n % 10);
            cbuf[cp] = 0;

            gui_raw_fill_rect(notif_rect.x, notif_rect.y, notif_rect.w, notif_rect.h, gui_rgb(36, 44, 60));
            gui_raw_line(notif_rect.x, notif_rect.y, notif_rect.x + notif_rect.w - 1, notif_rect.y, gui_rgb(80, 92, 120));
            gui_raw_line(notif_rect.x, notif_rect.y + notif_rect.h - 1, notif_rect.x + notif_rect.w - 1,
                         notif_rect.y + notif_rect.h - 1, gui_rgb(12, 16, 24));
            gui_raw_line(notif_rect.x, notif_rect.y, notif_rect.x, notif_rect.y + notif_rect.h - 1, gui_rgb(80, 92, 120));
            gui_raw_line(notif_rect.x + notif_rect.w - 1, notif_rect.y, notif_rect.x + notif_rect.w - 1,
                         notif_rect.y + notif_rect.h - 1, gui_rgb(12, 16, 24));
            {
                int gx = notif_rect.x + 4;
                int gy = glyph_y;
                uint32_t bc = (n > 0) ? gui_rgb(255, 210, 90) : gui_rgb(160, 180, 210);
                gui_raw_fill_rect(gx + 1, gy,     3, 1, bc);
                gui_raw_fill_rect(gx,     gy + 1, 5, 4, bc);
                gui_raw_fill_rect(gx + 1, gy + 5, 3, 1, bc);
                gui_raw_fill_rect(gx + 2, gy + 6, 1, 1, bc);
            }
            gui_draw_text(notif_rect.x + 12, count_text_y, cbuf,
                          (n > 0) ? gui_rgb(255, 220, 120) : gui_rgb(200, 220, 240));
            g_notif_widget_rect = notif_rect;
        }
    }
    gui_draw_taskbar_search_results();
}

static void gui_draw_wallpaper_day(int width, int taskbar_top) {
    int y;
    int x;
    int horizon = taskbar_top * 62 / 100;
    uint32_t sky_top = gui_rgb(120, 180, 230);
    uint32_t sky_mid = gui_rgb(190, 220, 240);
    uint32_t sun = gui_rgb(255, 230, 130);
    uint32_t ground = gui_rgb(95, 145, 80);

    for (y = 0; y < taskbar_top; y++) {
        uint32_t amount = taskbar_top > 1 ? (uint32_t)(y * 255 / taskbar_top) : 0u;
        uint32_t color = gui_mix_rgb(sky_top, sky_mid, amount);
        if (y >= horizon) {
            uint32_t g_amt = taskbar_top > horizon ? (uint32_t)((y - horizon) * 255 / (taskbar_top - horizon)) : 0u;
            color = gui_mix_rgb(gui_rgb(140, 180, 110), ground, g_amt);
        }
        gui_raw_fill_rect(0, y, width, 1, color);
    }
    {
        int cx = width * 3 / 4;
        int cy = horizon - 60;
        int r = 28;
        int dx, dy;
        for (dy = -r; dy <= r; dy++) {
            for (dx = -r; dx <= r; dx++) {
                if (dx * dx + dy * dy <= r * r) {
                    gui_raw_put_pixel(cx + dx, cy + dy, sun);
                }
            }
        }
    }
    for (x = 60; x < width; x += 180) {
        int cy = 60 + ((x / 180) & 1) * 20;
        int i;
        for (i = 0; i < 50; i++) {
            gui_raw_put_pixel(x + i, cy, gui_rgb(250, 250, 252));
            gui_raw_put_pixel(x + i, cy + 1, gui_rgb(245, 245, 250));
        }
        for (i = 8; i < 42; i++) {
            gui_raw_put_pixel(x + i, cy - 1, gui_rgb(250, 250, 252));
        }
    }
}

static void gui_draw_wallpaper_solid(int width, int taskbar_top) {
    uint32_t bg = gui_rgb(45, 75, 110);
    gui_raw_fill_rect(0, 0, width, taskbar_top, bg);
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

    if (g_gui.wallpaper_theme == 1) {
        gui_draw_wallpaper_day(width, taskbar_top);
        return;
    }
    if (g_gui.wallpaper_theme == 2) {
        gui_draw_wallpaper_solid(width, taskbar_top);
        return;
    }

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


static void gui_render_scene(void) {
    uint32_t i;
    gui_draw_wallpaper();
    gui_desktop_draw();
    gui_draw_taskbar();

    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        if (idx < GUI_MAX_WINDOWS) gui_draw_window(&g_gui.windows[idx]);
    }

    /* Start menu is a desktop shell overlay.  It must stay above all normal
     * windows regardless of window activation/z-order, while the context menu
     * remains the topmost transient popup. */
    gui_desktop_draw_start_menu();
    gui_ctxmenu_draw();
}

void gui_render(void) {
    uint32_t i;
    uint32_t dirty_count;
    gui_rect_t dirty_rects[GUI_MAX_DIRTY_RECTS];
    if (!g_gui.initialized) return;
    g_gui.frame_counter++;
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
    static uint32_t clk_last_sec = 0xFFFFFFFFu;
    uint32_t now_sec;
    if (!g_gui.initialized) return;
    gui_poll_mouse();
    gui_process_events();
    browser_load_tick();
    gui_nettool_tick();
    gui_terminal_drain_output_queue();
    gui_terminal_tick_cursor();
    /* clock tick: invalidate taskbar clock area once per second */
    now_sec = sched_time_ms() / 1000u;
    if (now_sec != clk_last_sec) {
        clk_last_sec = now_sec;
        /* clock lives at bottom-right of the screen, not inside the taskbar rect */
        if (g_gui.width > 0 && g_gui.height > 0) {
            char time_buf[9];
            gui_rect_t clock_rect;
            uint32_t hours = (now_sec / 3600u) % 24u;
            uint32_t mins = (now_sec / 60u) % 60u;
            uint32_t secs = now_sec % 60u;
            time_buf[0] = (char)('0' + (hours / 10u) % 10u);
            time_buf[1] = (char)('0' + hours % 10u);
            time_buf[2] = ':';
            time_buf[3] = (char)('0' + (mins / 10u) % 10u);
            time_buf[4] = (char)('0' + mins % 10u);
            time_buf[5] = ':';
            time_buf[6] = (char)('0' + (secs / 10u) % 10u);
            time_buf[7] = (char)('0' + secs % 10u);
            time_buf[8] = 0;
            clock_rect = gui_get_clock_rect(time_buf);
            gui_invalidate_rect(clock_rect.x, clock_rect.y, clock_rect.w, clock_rect.h);
        }
    }
    if (gui_has_dirty()) {
        static uint32_t last_render_ms = 0;
        uint32_t now_ms = sched_time_ms();
        if ((uint32_t)(now_ms - last_render_ms) >= 16u) {
            last_render_ms = now_ms;
            gui_render();
        }
    }
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
    w1 = gui_create_app_window(app, 70, 70, 380, 230, i18n_t(I18N_KEY_WIN_CONTROL_CENTER));
    if (w1) {
        gui_add_label(w1, 18, 22, 300, 18, i18n_t(I18N_KEY_DEMO_WELCOME));
        gui_add_panel(w1, 18, 52, 335, 48, gui_rgb(210, 225, 245));
        gui_add_label(w1, 28, 66, 300, 18, i18n_t(I18N_KEY_DEMO_DRAG_HINT));
        gui_add_textbox(w1, 18, 108, 260, 26, "edit me");
        gui_add_button(w1, 18, 150, 120, 28, i18n_t(I18N_KEY_DEMO_BTN_CLICK), gui_demo_button, 0);
        gui_add_button(w1, 150, 150, 120, 28, i18n_t(I18N_KEY_DEMO_BTN_MINIMIZE), gui_demo_button, 0);
    }
    w2 = gui_create_app_window(app, 500, 120, 330, 170, i18n_t(I18N_KEY_WIN_ABOUT));
    if (w2) {
        gui_add_label(w2, 18, 24, 260, 18, i18n_t(I18N_KEY_DEMO_MVP));
        gui_add_label(w2, 18, 48, 260, 18, i18n_t(I18N_KEY_DEMO_FRAMEBUFFER));
        gui_add_button(w2, 18, 90, 100, 28, i18n_t(I18N_KEY_BTN_OK), gui_demo_button, 0);
    }
#if GUI_DEBUG_LOG
    gui_terminal_write("\n[GUI] demo app started\n> ");
#endif
    return (w1 || w2) ? 0 : -1;
}

/* === About / Recycle Bin simple windows === */

static gui_window_t *g_about_win = 0;
static gui_window_t *g_recycle_win = 0;
static gui_window_t *g_notif_win = 0;

#define GUI_NOTIF_MAX        16
#define GUI_NOTIF_TEXT_LEN   80

typedef struct {
    int used;
    char text[GUI_NOTIF_TEXT_LEN];
} gui_notif_entry_t;

static gui_notif_entry_t g_notif_log[GUI_NOTIF_MAX];
static uint32_t g_notif_count = 0;
static uint32_t g_notif_unread = 0;

void gui_notify(const char *text) {
    uint32_t i, j;
    if (!text) return;
    /* shift if full */
    if (g_notif_count >= GUI_NOTIF_MAX) {
        for (i = 0; i + 1 < GUI_NOTIF_MAX; i++) g_notif_log[i] = g_notif_log[i + 1];
        g_notif_count = GUI_NOTIF_MAX - 1;
    }
    g_notif_log[g_notif_count].used = 1;
    for (j = 0; j + 1 < GUI_NOTIF_TEXT_LEN && text[j]; j++) {
        g_notif_log[g_notif_count].text[j] = text[j];
    }
    g_notif_log[g_notif_count].text[j] = 0;
    g_notif_count++;
    g_notif_unread++;
}

static void about_on_close(gui_window_t *win, void *ud) {
    (void)win; (void)ud;
    g_about_win = 0;
}

static void about_on_ok(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (g_about_win) {
        gui_window_t *win = g_about_win;
        g_about_win = 0;
        gui_window_set_on_close(win, 0, 0);
        gui_destroy_window(win);
        gui_render();
    }
}

void gui_about_open(void) {
    gui_widget_t *btn;
    if (g_about_win) {
        gui_window_set_on_close(g_about_win, 0, 0);
        gui_destroy_window(g_about_win);
        g_about_win = 0;
    }
    g_about_win = gui_create_window(180, 140, 360, 200, i18n_t(I18N_KEY_WIN_ABOUT));
    if (!g_about_win) return;
    gui_window_set_on_close(g_about_win, about_on_close, 0);
    gui_add_label(g_about_win, 16, 50,  328, 16, i18n_t(I18N_KEY_ABOUT_TAGLINE));
    gui_add_label(g_about_win, 16, 74,  328, 16, i18n_t(I18N_KEY_ABOUT_VERSION));
    gui_add_label(g_about_win, 16, 98,  328, 16, i18n_t(I18N_KEY_ABOUT_BUILD));
    gui_add_label(g_about_win, 16, 122, 328, 16, i18n_t(I18N_KEY_ABOUT_LICENSE));
    btn = gui_add_button(g_about_win, 140, 152, 80, 28, i18n_t(I18N_KEY_BTN_OK), about_on_ok, 0);
    (void)btn;
    gui_render();
}

static void recycle_on_close(gui_window_t *win, void *ud) {
    (void)win; (void)ud;
    g_recycle_win = 0;
}

void gui_recycle_open(void) {
    int win_w = 420;
    int win_h = 240;

    if (g_recycle_win) {
        gui_window_set_on_close(g_recycle_win, 0, 0);
        gui_destroy_window(g_recycle_win);
        g_recycle_win = 0;
    }
    g_recycle_win = gui_create_window(140, 120, win_w, win_h, i18n_t(I18N_KEY_WIN_RECYCLE_BIN));
    if (!g_recycle_win) return;
    gui_window_set_on_close(g_recycle_win, recycle_on_close, 0);
    gui_render();
}



static const char *gui_settings_language_name(void) {
    return (i18n_current() == I18N_LOCALE_ZH) ? i18n_t(I18N_KEY_SETTINGS_LANGUAGE_CHINESE)
                                             : i18n_t(I18N_KEY_SETTINGS_LANGUAGE_ENGLISH);
}

static void settings_on_close(gui_window_t *win, void *ud) {
    (void)win;
    (void)ud;
    g_settings_win = 0;
}

static void settings_toggle_language_dropdown(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    g_settings_language_dropdown_open = !g_settings_language_dropdown_open;
    gui_settings_build(0);
}

static void settings_apply_language_en(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    g_settings_language_dropdown_open = 0;
    i18n_set_locale(I18N_LOCALE_EN);
    gui_desktop_refresh_i18n_labels();
    gui_settings_build(1);
}

static void settings_apply_language_zh(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    g_settings_language_dropdown_open = 0;
    i18n_set_locale(I18N_LOCALE_ZH);
    gui_desktop_refresh_i18n_labels();
    gui_settings_build(1);
}

static void settings_apply_font_slider(gui_widget_t *w, void *ud) {
    (void)ud;
    if (!w) return;
    if (w->value <= 0) font_set_size(FONT_SIZE_SMALL);
    else if (w->value >= 2) font_set_size(FONT_SIZE_LARGE);
    else font_set_size(FONT_SIZE_MEDIUM);
    gui_invalidate_all();
}

static void network_refresh(gui_widget_t *w, void *ud) {
    net_device_info_t info;
    (void)w;
    (void)ud;
    if (gui_get_primary_net_info(&info) == 0) net_refresh_device_status(info.name);
    gui_network_build(1);
}

static void network_toggle_admin(gui_widget_t *w, void *ud) {
    net_device_info_t info;
    (void)ud;
    if (!w) return;
    if (gui_get_primary_net_info(&info) == 0) net_set_device_admin_up(info.name, w->value ? 1 : 0);
    gui_network_build(1);
}

static void network_dhcp(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    dhcp_start();
    (void)net_config_save_dhcp();
    gui_network_build(1);
}

void gui_settings_row_init(gui_settings_row_t *row,
                           gui_window_t *window,
                           int x,
                           int y,
                           int width,
                           int label_h,
                           int button_h,
                           int button_w,
                           int gap) {
    if (!row) return;
    row->window = window;
    row->x = x;
    row->y = y;
    row->width = width;
    row->label_h = label_h;
    row->button_h = button_h;
    row->button_w = button_w;
    row->gap = gap;
}

void gui_settings_row_label(const gui_settings_row_t *row, const char *label) {
    if (!row || !row->window || !label) return;
    gui_add_label(row->window, row->x, row->y, row->width, row->label_h, label);
}

gui_widget_t *gui_settings_row_toggle(const gui_settings_row_t *row,
                                             const char *text,
                                             int checked,
                                             gui_widget_callback_t cb,
                                             void *ud) {
    int w;
    if (!row || !row->window) return 0;
    w = row->button_w * 2 + row->gap;
    return gui_add_toggle(row->window, row->x, row->y, w, row->button_h, text, checked, cb, ud);
}

gui_widget_t *gui_settings_row_button(const gui_settings_row_t *row,
                                             const char *text,
                                             gui_widget_callback_t cb,
                                             void *ud) {
    int w;
    if (!row || !row->window) return 0;
    w = row->button_w * 2 + row->gap;
    return gui_add_button(row->window, row->x, row->y, w, row->button_h, text, cb, ud);
}

gui_widget_t *gui_settings_row_slider(const gui_settings_row_t *row,
                                             int min,
                                             int max,
                                             int value,
                                             int step,
                                             gui_widget_callback_t cb,
                                             void *ud) {
    int w;
    if (!row || !row->window) return 0;
    w = row->button_w * 3 + row->gap * 2;
    return gui_add_slider(row->window, row->x, row->y, w, row->button_h, min, max, value, step, cb, ud);
}

void gui_settings_row_slider_labels(const gui_settings_row_t *row,
                                           const char *left,
                                           const char *middle,
                                           const char *right) {
    if (!row || !row->window) return;
    gui_add_label(row->window, row->x, row->y + row->button_h, row->button_w, row->label_h, left);
    gui_add_label(row->window, row->x + row->button_w + row->gap, row->y + row->button_h, row->button_w, row->label_h, middle);
    gui_add_label(row->window, row->x + (row->button_w + row->gap) * 2, row->y + row->button_h, row->button_w, row->label_h, right);
}

static int settings_parse_ipv4(const char *text, uint32_t *out) {
    uint32_t parts[4];
    int part = 0;
    uint32_t value = 0;
    int has_digit = 0;
    const char *p = text;
    if (!text || !out) return -1;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10u + (uint32_t)(*p - '0');
            if (value > 255u) return -1;
            has_digit = 1;
        } else if (*p == '.') {
            if (!has_digit || part >= 3) return -1;
            parts[part++] = value;
            value = 0;
            has_digit = 0;
        } else {
            return -1;
        }
        p++;
    }
    if (!has_digit || part != 3) return -1;
    parts[part] = value;
    *out = NET_IP4(parts[0], parts[1], parts[2], parts[3]);
    return 0;
}

static void network_apply_static(gui_widget_t *w, void *ud) {
    uint32_t ip;
    uint32_t mask;
    uint32_t gateway;
    uint32_t dns;
    (void)w;
    (void)ud;
    if (!g_network_ip_box || !g_network_mask_box || !g_network_gateway_box || !g_network_dns_box) return;
    if (settings_parse_ipv4(g_network_ip_box->text, &ip) != 0) return;
    if (settings_parse_ipv4(g_network_mask_box->text, &mask) != 0) return;
    if (settings_parse_ipv4(g_network_gateway_box->text, &gateway) != 0) return;
    if (settings_parse_ipv4(g_network_dns_box->text, &dns) != 0) return;
    net_config_ipv4(ip, mask, gateway, dns);
    (void)net_config_save_static(ip, mask, gateway, dns);
    gui_network_build(1);
}

static void gui_settings_build(int show_notice) {
    const font_renderer_t *font = font_get_default();
    int line_h = (int)font_get_line_height(font);
    int margin = (int)font_scale_value(14);
    int row_h = line_h + (int)font_scale_value(14);
    int button_h = line_h + (int)font_scale_value(12);
    int button_w = (int)font_scale_value(82);
    int gap = (int)font_scale_value(8);
    int win_w = (int)font_scale_value(640);
    int win_h = margin * 2 + row_h * 18 + 56;
    int x;
    int y;
    gui_settings_row_t row;

    if (win_w < 640) win_w = 640;
    if (win_h < 560) win_h = 560;

    if (g_settings_win) {
        gui_window_set_on_close(g_settings_win, 0, 0);
        gui_destroy_window(g_settings_win);
        g_settings_win = 0;
    }
    g_settings_win = gui_create_window(190, 70, win_w, win_h, i18n_t(I18N_KEY_WIN_SETTINGS));
    if (!g_settings_win) return;
    gui_window_set_on_close(g_settings_win, settings_on_close, 0);

    x = margin;
    y = 36;
    gui_settings_row_init(&row, g_settings_win, x, y, win_w - margin * 2, line_h + 4, button_h, button_w, gap);

    gui_settings_row_label(&row, i18n_t(I18N_KEY_SETTINGS_LANGUAGE));
    y += row_h;
    row.y = y;
    {
        char language_text[64];
        int dropdown_w = button_w * 2 + gap;
        strncpy(language_text, gui_settings_language_name(), sizeof(language_text) - 4);
        language_text[sizeof(language_text) - 4] = '\0';
        {
            uint32_t n = (uint32_t)strlen(language_text);
            if (n + 2 < sizeof(language_text)) {
                language_text[n] = ' ';
                language_text[n + 1] = 'v';
                language_text[n + 2] = '\0';
            }
        }
        gui_settings_row_button(&row, language_text, settings_toggle_language_dropdown, 0);
        if (g_settings_language_dropdown_open) {
            gui_add_button(g_settings_win, x, y + button_h + 2, dropdown_w, button_h, i18n_t(I18N_KEY_SETTINGS_LANGUAGE_ENGLISH), settings_apply_language_en, 0);
            gui_add_button(g_settings_win, x, y + (button_h + 2) * 2, dropdown_w, button_h, i18n_t(I18N_KEY_SETTINGS_LANGUAGE_CHINESE), settings_apply_language_zh, 0);
        }
    }

    y += row_h + gap;
    if (g_settings_language_dropdown_open) y += (button_h + 2) * 2;
    row.y = y;
    gui_settings_row_label(&row, i18n_t(I18N_KEY_SETTINGS_TEXT_SIZE));
    y += row_h;
    row.y = y;
    {
        int value = (font_get_size() == FONT_SIZE_SMALL) ? 0 : ((font_get_size() == FONT_SIZE_LARGE) ? 2 : 1);
        gui_settings_row_slider(&row, 0, 2, value, 1, settings_apply_font_slider, 0);
        gui_settings_row_slider_labels(&row,
                                       i18n_t(I18N_KEY_BTN_FONT_SMALL),
                                       i18n_t(I18N_KEY_BTN_FONT_MEDIUM),
                                       i18n_t(I18N_KEY_BTN_FONT_LARGE));
    }

    y += button_h + line_h + 4 + gap;
    row.y = y;
    gui_settings_row_label(&row, i18n_t(I18N_KEY_SETTINGS_NETWORK));
    y += row_h;
    row.y = y;
    gui_settings_row_button(&row, i18n_t(I18N_KEY_SETTINGS_NETWORK_DEVICE), settings_open_network, 0);

    if (show_notice) gui_notify(i18n_t(I18N_KEY_SETTINGS_APPLIED));
    gui_render();
}

void gui_settings_open(void) {
    gui_settings_build(0);
}

/* === Notification Center === */
static void notif_on_close(gui_window_t *win, void *ud) {
    (void)win; (void)ud;
    g_notif_win = 0;
}

static void notif_on_close_btn(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (g_notif_win) {
        gui_window_t *win = g_notif_win;
        g_notif_win = 0;
        gui_window_set_on_close(win, 0, 0);
        gui_destroy_window(win);
        gui_render();
    }
}

static void notif_on_clear(gui_widget_t *w, void *ud);

static void wifi_on_close(gui_window_t *win, void *ud) {
    (void)win;
    (void)ud;
    g_wifi_win = 0;
}

void gui_wifi_open(void) {
    net_wifi_network_info_t nets[NET_WIFI_MAX_RESULTS];
    uint32_t count;
    uint32_t i;
    int margin = (int)font_scale_value(16);
    int line_h = (int)font_get_line_height(font_get_default()) + 6;
    int row_h = line_h + 8;
    int win_w = (int)font_scale_value(420);
    int win_h = (int)font_scale_value(260);
    int x;
    int y;

    if (win_w < 360) win_w = 360;
    if (win_h < 220) win_h = 220;
    if (g_wifi_win) {
        gui_destroy_window(g_wifi_win);
        g_wifi_win = 0;
    }
    g_wifi_win = gui_create_window(230, 120, win_w, win_h, i18n_t(I18N_KEY_WIFI_AVAILABLE_NETWORKS));
    if (!g_wifi_win) return;
    gui_window_set_on_close(g_wifi_win, wifi_on_close, 0);

    x = margin;
    y = 42;
    count = net_scan_wifi(nets, NET_WIFI_MAX_RESULTS);
    if (count == 0) {
        gui_add_label(g_wifi_win, x, y, win_w - margin * 2, line_h + 6, i18n_t(I18N_KEY_WIFI_NO_NETWORKS));
        return;
    }

    for (i = 0; i < count && i < NET_WIFI_MAX_RESULTS; i++) {
        char line[96];
        int pos = 0;
        const char *ssid = nets[i].ssid[0] ? nets[i].ssid : "Wi-Fi";
        const char *state = nets[i].connected ? i18n_t(I18N_KEY_WIFI_CONNECTED) :
                            (nets[i].secured ? i18n_t(I18N_KEY_WIFI_SECURED) : i18n_t(I18N_KEY_WIFI_OPEN));
        pos = fp_str_append(line, pos, sizeof(line), ssid);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = gui_append_uint(line, pos, sizeof(line), nets[i].signal_percent);
        pos = fp_str_append(line, pos, sizeof(line), "%  ");
        pos = fp_str_append(line, pos, sizeof(line), state);
        (void)pos;
        gui_add_label(g_wifi_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;
        if (y + line_h > win_h - margin) break;
    }
}

static void gui_notif_open(void) {
    uint32_t i;
    char header[64];
    char body[GUI_WIDGET_TEXT_CAP];
    gui_widget_t *log_view;
    int pos;
    int body_pos;
    char nbuf[16];
    if (g_notif_win) {
        gui_window_set_on_close(g_notif_win, 0, 0);
        gui_destroy_window(g_notif_win);
        g_notif_win = 0;
    }
    g_notif_win = gui_create_window(120, 100, 420, 320, i18n_t(I18N_KEY_WIN_NOTIFICATIONS));
    if (!g_notif_win) return;
    gui_window_set_on_close(g_notif_win, notif_on_close, 0);

    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_NOTIF_TOTAL));
    pos = fp_str_append(header, pos, sizeof(header), ": ");
    fp_itoa((int)g_notif_count, nbuf);
    pos = fp_str_append(header, pos, sizeof(header), nbuf);
    pos = fp_str_append(header, pos, sizeof(header), "  ");
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_NOTIF_UNREAD));
    pos = fp_str_append(header, pos, sizeof(header), ": ");
    fp_itoa((int)g_notif_unread, nbuf);
    pos = fp_str_append(header, pos, sizeof(header), nbuf);
    (void)pos;
    gui_add_label(g_notif_win, 16, 36, 280, 16, header);

    gui_add_button(g_notif_win, 300, 32, 48, 22, i18n_t(I18N_KEY_BTN_CLEAR), notif_on_clear, 0);
    gui_add_button(g_notif_win, 354, 32, 48, 22, i18n_t(I18N_KEY_BTN_CLOSE), notif_on_close_btn, 0);

    body_pos = 0;
    if (g_notif_count > 0) {
        for (i = 0; i < g_notif_count && i < GUI_NOTIF_MAX; i++) {
            if (i > 0) body_pos = fp_str_append(body, body_pos, sizeof(body), "\n");
            body_pos = fp_str_append(body, body_pos, sizeof(body), g_notif_log[i].text);
        }
    }
    body[body_pos < (int)sizeof(body) ? body_pos : (int)sizeof(body) - 1] = 0;

    log_view = gui_add_textarea(g_notif_win, 16, 64, 388, 230, body);
    if (log_view) {
        gui_widget_set_textbox_flags(log_view,
            GUI_TEXTBOX_FLAG_READONLY | GUI_TEXTBOX_FLAG_MULTILINE | GUI_TEXTBOX_FLAG_WRAP);
        gui_widget_set_placeholder(log_view, i18n_t(I18N_KEY_WIN_NOTIFICATIONS));
    }

    g_notif_unread = 0;
    gui_render();
}

static void notif_on_clear(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    g_notif_count = 0;
    g_notif_unread = 0;
    gui_notif_open();   /* rebuild to refresh */
}


void fp_itoa(int n, char *buf) {
    char tmp[12];
    int i = 0, j = 0;
    if (n < 0) { buf[j++] = '-'; n = -n; }
    if (n == 0) tmp[i++] = '0';
    while (n > 0) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

int fp_str_append(char *dst, int pos, int cap, const char *src) {
    while (*src && pos < cap - 1) dst[pos++] = *src++;
    dst[pos] = 0;
    return pos;
}

int gui_append_uint(char *dst, int pos, int cap, uint32_t v) {
    char tmp[16];
    int i = 0;
    int j;
    if (cap <= 0) return pos;
    if (v == 0) return fp_str_append(dst, pos, cap, "0");
    while (v && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (j = i - 1; j >= 0; j--) {
        if (pos >= cap - 1) break;
        dst[pos++] = tmp[j];
    }
    dst[pos] = 0;
    return pos;
}

int gui_append_hex_byte(char *dst, int pos, int cap, uint8_t v) {
    static const char hex[] = "0123456789ABCDEF";
    if (pos < cap - 1) dst[pos++] = hex[(v >> 4) & 0x0f];
    if (pos < cap - 1) dst[pos++] = hex[v & 0x0f];
    if (cap > 0) dst[pos] = 0;
    return pos;
}

void gui_format_ipv4_inline(uint32_t ip, char *buf, int cap) {
    int pos = 0;
    if (!buf || cap <= 0) return;
    pos = gui_append_uint(buf, pos, cap, (ip >> 24) & 0xffu);
    pos = fp_str_append(buf, pos, cap, ".");
    pos = gui_append_uint(buf, pos, cap, (ip >> 16) & 0xffu);
    pos = fp_str_append(buf, pos, cap, ".");
    pos = gui_append_uint(buf, pos, cap, (ip >> 8) & 0xffu);
    pos = fp_str_append(buf, pos, cap, ".");
    pos = gui_append_uint(buf, pos, cap, ip & 0xffu);
    (void)pos;
}

void gui_format_mac_inline(const uint8_t mac[6], char *buf, int cap) {
    int pos = 0;
    int i;
    if (!buf || cap <= 0) return;
    for (i = 0; i < 6; i++) {
        if (i) pos = fp_str_append(buf, pos, cap, ":");
        pos = gui_append_hex_byte(buf, pos, cap, mac[i]);
    }
}

int gui_settings_append_field(char *dst, int pos, int cap, i18n_key_t key, const char *value) {
    pos = fp_str_append(dst, pos, cap, i18n_t(key));
    pos = fp_str_append(dst, pos, cap, ": ");
    pos = fp_str_append(dst, pos, cap, value ? value : "");
    return pos;
}


void gui_demo(void) {
    gui_app_t *app;
    if (!g_gui.initialized) return;
    app = gui_register_app("demo", "OpenOS Demo", gui_demo_app_entry, 0);
    if (app) gui_start_app(app);
    gui_render();
}

/* ============================================================
 * 锁屏专用底层原语（供 lockscreen.c 全屏自绘使用）
 *
 * 锁屏是桌面启动前的全屏门闸，不走窗口系统，因此需要直接
 * 访问 backbuffer 填充与 present。这里把内部静态原语以受控
 * 方式导出。
 * ============================================================ */

/* 返回当前屏幕宽高（像素） */
int gui_screen_width(void) {
    return (int)g_gui.width;
}

int gui_screen_height(void) {
    return (int)g_gui.height;
}

/* 全屏/矩形填充（写入 backbuffer，随后需调用 gui_screen_present） */
void gui_screen_fill_rect(int x, int y, int w, int h, uint32_t color) {
    gui_raw_fill_rect(x, y, w, h, color);
}

/* 画矩形边框（1px 线宽 * thickness） */
void gui_screen_draw_border(int x, int y, int w, int h, int thickness, uint32_t color) {
    int t;
    if (thickness < 1) thickness = 1;
    for (t = 0; t < thickness; t++) {
        gui_raw_fill_rect(x + t, y + t, w - 2 * t, 1, color);            /* top */
        gui_raw_fill_rect(x + t, y + h - 1 - t, w - 2 * t, 1, color);    /* bottom */
        gui_raw_fill_rect(x + t, y + t, 1, h - 2 * t, color);            /* left */
        gui_raw_fill_rect(x + w - 1 - t, y + t, 1, h - 2 * t, color);    /* right */
    }
}

/* 将 backbuffer 整屏刷到 framebuffer（present） */
void gui_screen_present(void) {
    gui_rect_t all;
    if (!gui_compositor_active()) return;
    all.x = 0;
    all.y = 0;
    all.w = (int)g_gui.width;
    all.h = (int)g_gui.height;
    gui_flush_rect(&all);
}
