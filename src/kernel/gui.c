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
#include "fs/vfs.h"
#include "i18n.h"
#include "net/net.h"
#include "net/dhcp.h"
#include "net/dns.h"
#include "net/net_config.h"
#include "tls_parser.h"
#include "process.h"
extern int spawn_user_process(const char *path, char *const argv[]);
extern uint32_t sched_time_ms(void);

typedef enum browser_scheme {
    BROWSER_SCHEME_HTTP = 0,
    BROWSER_SCHEME_HTTPS = 1
} browser_scheme_t;

typedef enum browser_load_state {
    BROWSER_LOAD_IDLE = 0,
    BROWSER_LOAD_RESOLVING,
    BROWSER_LOAD_CONNECTING,
    BROWSER_LOAD_HTTP_SEND,
    BROWSER_LOAD_HTTP_RECV,
    BROWSER_LOAD_TLS_SEND,
    BROWSER_LOAD_TLS_RECV
} browser_load_state_t;

typedef struct browser_load_context {
    browser_load_state_t state;
    uint32_t state_started_ms;
    char url[64];
    char host[64];
    char path[128];
    uint16_t port;
    browser_scheme_t scheme;
    uint32_t ip;
    int conn;
    char request[256];
    uint8_t response[2048];
    int total;
    uint8_t tls_record[256];
    int tls_hello_len;
    uint8_t tls_hello[256];
} browser_load_context_t;

static void gui_desktop_run_action(uint32_t action);
static int  gui_taskbar_search_handle_key(int key);
static int  gui_is_enter_key(int key);
static int  browser_handle_address_enter(int key);
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
static void browser_load_start(void);
static void browser_load_tick(void);
static void browser_load_finish(const char *status);
static int  browser_str_starts_ci(const char *p, const char *prefix);
static int  browser_header_name_eq(const char *p, const char *name);
static void gui_taskbar_search_open_result(uint32_t index);
static int  gui_taskbar_search_result_index_at(int x, int y);
static void gui_taskbar_search_reset_results(void);
static void gui_taskbar_search_refresh_results(void);
static void gui_handle_mouse_right_down(int x, int y);
static void gui_ctxmenu_close(void);
static int  gui_ctxmenu_handle_click(int x, int y);
static void gui_ctxmenu_draw(void);
static int  gui_ctxmenu_is_open(void);
static void gui_file_preview_open(void);
static void gui_file_preview_render_list(void);
static void gui_file_preview_render_view(void);
static void gui_file_preview_render_edit(void);
static void gui_file_preview_rebuild(void);
static void gui_about_open(void);
static void gui_recycle_open(void);
static void gui_settings_open(void);
static void gui_network_open(void);
static void gui_wifi_open(void);
static int  gui_tray_network_is_wireless(void);
static void gui_notif_open(void);
static void gui_launcher_scan_bin(uint32_t start_index);
static void gui_notify(const char *text);
static int  fp_str_append(char *dst, int pos, int cap, const char *src);
static void fp_itoa(int n, char *buf);
static int gui_append_uint(char *dst, int pos, int cap, uint32_t v);
static void gui_format_ipv4_inline(uint32_t ip, char *buf, int cap);
static void gui_format_mac_inline(const uint8_t mac[6], char *buf, int cap);
static int gui_settings_append_field(char *dst, int pos, int cap, i18n_key_t key, const char *value);
void gui_terminal_redraw(void);
void gui_terminal_clear(void);
void gui_terminal_clear_selection(void);
int gui_terminal_copy_selection(void);

static gui_system_t g_gui;
static gui_accel_info_t g_gui_accel;
static gui_rect_t g_network_widget_rect;
static gui_rect_t g_taskbar_search_rect;
static gui_window_t *g_wifi_win;
static browser_load_context_t g_browser_load;

#define GUI_TASKBAR_SEARCH_MAX   63u
#define GUI_TASKBAR_SEARCH_W     180
#define GUI_TASKBAR_SEARCH_MIN_W 96

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
#define GUI_DESKTOP_ACTION_RECYCLE  6u
#define GUI_DESKTOP_ACTION_BROWSER  7u
#define GUI_DESKTOP_ACTION_THEME    8u
#define GUI_DESKTOP_ACTION_NOTIF    9u
#define GUI_DESKTOP_ACTION_SETTINGS 10u
#define GUI_DESKTOP_ACTION_LAUNCH_BIN_BASE 0x1000u  /* +index into binlist */
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
static void gui_browser_open(void);
#define GUI_BROWSER_CONTENT_LINES 12
#define GUI_BROWSER_LINKS_MAX GUI_BROWSER_CONTENT_LINES
static gui_widget_t *g_browser_address_box = 0;
static gui_widget_t *g_browser_content_lines[GUI_BROWSER_CONTENT_LINES];
static char g_browser_line_links[GUI_BROWSER_LINKS_MAX][128];
static gui_widget_t *g_browser_status_label = 0;
static gui_window_t *g_browser_win = 0;
static int gui_terminal_point_to_cell(int x, int y, uint32_t *col, uint32_t *row);
void gui_terminal_view_init(gui_terminal_view_t *view, uint32_t cols, uint32_t rows);
void gui_terminal_view_clear(gui_terminal_view_t *view);
void gui_terminal_view_make_layout(gui_rect_t viewport, int padding_x, int padding_y, gui_terminal_view_layout_t *layout);
int gui_terminal_view_point_to_cell(const gui_terminal_view_t *view, const gui_terminal_view_layout_t *layout, int x, int y, uint32_t *col, uint32_t *row);
void gui_terminal_view_begin_selection(gui_terminal_view_t *view, uint32_t col, uint32_t row);
void gui_terminal_view_update_selection(gui_terminal_view_t *view, uint32_t col, uint32_t row);
void gui_terminal_view_end_selection(gui_terminal_view_t *view);
int gui_terminal_view_cell_selected(const gui_terminal_view_t *view, uint32_t col, uint32_t row);
int gui_terminal_view_copy_selection(gui_terminal_view_t *view);
int gui_terminal_view_has_clipboard_text(const gui_terminal_view_t *view);
int gui_terminal_view_set_clipboard_text(gui_terminal_view_t *view, const char *text);
const char *gui_terminal_view_get_clipboard_text(const gui_terminal_view_t *view);
void gui_terminal_view_draw(const gui_terminal_view_t *view, const gui_terminal_view_layout_t *layout, int draw_cursor, uint32_t fg, uint32_t selection_bg, uint32_t selection_fg, uint32_t cursor_color);
void gui_terminal_view_scroll(gui_terminal_view_t *view);
static void gui_terminal_update_selection(uint32_t col, uint32_t row);
void gui_terminal_set_input_focus(int focused);
static void gui_raw_fill_rect(int x, int y, int w, int h, uint32_t color);
static int gui_terminal_set_clipboard_text(const char *text);
const char *gui_terminal_get_clipboard_text(void);

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

static void gui_raw_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
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
static void gui_update_start_menu_layout(void);
static void gui_start_menu_scroll_by(int delta_items);

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
static int gui_text_line_height_px(void);
static int gui_text_center_y(int top, int height);
static void gui_draw_file_icon(gui_icon_id_t id, int x, int y);
static void gui_draw_icon_button_face(const gui_rect_t *rect, int selected,
                                      int highlighted);
static void gui_draw_icon_button_frame(const gui_rect_t *rect, const char *label,
                                       int icon_w, int icon_h, int gap,
                                       int selected, int highlighted,
                                       uint32_t text_color,
                                       int *icon_x, int *icon_y);
static void gui_draw_file_icon_cell(const gui_rect_t *rect, const char *label,
                                    gui_icon_id_t icon, int selected,
                                    int highlighted, uint32_t text_color);

static uint32_t gui_text_visible_len_for_width(const char *text, uint32_t max_chars) {
    uint32_t n = 0;
    if (!text || max_chars == 0) return 0;
    while (text[n] && text[n] != '\n' && n < max_chars) n++;
    return n;
}

static uint32_t gui_text_len_until_break(const char *text) {
    uint32_t n = 0;
    if (!text) return 0;
    while (text[n] && text[n] != '\n') n++;
    return n;
}

static void gui_make_ellipsis_line(char *dst, uint32_t dst_size, const char *src, uint32_t max_chars, int use_ellipsis) {
    uint32_t i = 0;
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src || max_chars == 0) return;
    if (max_chars + 1 > dst_size) max_chars = dst_size - 1;
    while (src[i] && src[i] != '\n' && i < max_chars) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    if (use_ellipsis && src[i] && max_chars >= 3) {
        dst[max_chars - 3] = '.';
        dst[max_chars - 2] = '.';
        dst[max_chars - 1] = '.';
        dst[max_chars] = '\0';
    }
}

static int gui_draw_inline_icon(gui_icon_id_t icon, int x, int top, int height) {
    if (icon == GUI_ICON_NONE) return 0;
    gui_draw_file_icon(icon, x, top + (height - 14) / 2);
    return 14 + 4;
}

static int gui_label_aligned_x(const gui_widget_t *wg, int left, int width, uint32_t chars, int icon_offset) {
    int text_w = (int)chars * GUI_CHAR_W;
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
    uint32_t max_chars;
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
    max_chars = (uint32_t)(clip.w / GUI_CHAR_W);
    if (max_chars == 0) return;
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
        uint32_t draw_len;
        int multiline = (wg->label_flags & GUI_LABEL_FLAG_MULTILINE) != 0;
        int hard_wrap = multiline && src_len > max_chars;
        int truncated = src_len > max_chars;
        int y = ay + (int)(line * line_height);
        if (!multiline) y = gui_text_center_y(ay, wg->rect.h);
        if (y > ay + wg->rect.h - 1) break;
        if (hard_wrap) {
            consume_len = max_chars;
            truncated = 0;
        }
        gui_make_ellipsis_line(line_buf, sizeof(line_buf), p, max_chars,
                               truncated && (wg->label_flags & GUI_LABEL_FLAG_ELLIPSIS));
        draw_len = gui_text_visible_len_for_width(line_buf, max_chars);
        gui_draw_window_title_text(gui_label_aligned_x(wg, ax, wg->rect.w, draw_len, text_off), y, line_buf, color, &clip);
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

static int gui_text_line_height_px(void) {
    int h = (int)font_get_line_height(font_get_default());
    return h > 0 ? h : GUI_CHAR_H;
}

static int gui_text_glyph_height_px(void) {
    int ascii_h = (int)font_get_ascii_height(font_get_default());
    int unicode_h = (int)font_get_unicode_height();
    int h = ascii_h > unicode_h ? ascii_h : unicode_h;
    return h > 0 ? h : gui_text_line_height_px();
}

static int gui_text_center_y(int top, int height) {
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

static void gui_network_open(void) {
    gui_network_build(0);
}

static void settings_open_network(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    gui_network_open();
}

static void gui_settings_build(int show_notice);
static void gui_network_build(int show_notice);

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

static void gui_set_focused_widget(gui_widget_t *wg);

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
            wg->type == GUI_WIDGET_CONTEXTMENU || wg->type == GUI_WIDGET_DIALOG ||
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
            wg->type == GUI_WIDGET_MENUBAR || wg->type == GUI_WIDGET_CONTEXTMENU || wg->type == GUI_WIDGET_DIALOG || wg->type == GUI_WIDGET_TOAST || wg->type == GUI_WIDGET_TREEVIEW || wg->type == GUI_WIDGET_ICONVIEW ||
            (wg->type == GUI_WIDGET_LABEL && (wg->label_flags & GUI_LABEL_FLAG_COPYABLE)));
}

static int gui_widget_is_hoverable(gui_widget_t *wg) {
    return wg && wg->visible && wg->enabled &&
           (wg->type == GUI_WIDGET_BUTTON || wg->type == GUI_WIDGET_ICON_BUTTON ||
            wg->type == GUI_WIDGET_TOGGLE || wg->type == GUI_WIDGET_CHECKBOX ||
            wg->type == GUI_WIDGET_RADIOBUTTON || wg->type == GUI_WIDGET_SELECT ||
            wg->type == GUI_WIDGET_COMBOBOX ||
            wg->type == GUI_WIDGET_LISTVIEW || wg->type == GUI_WIDGET_TABLEVIEW ||
            wg->type == GUI_WIDGET_MENUBAR || wg->type == GUI_WIDGET_CONTEXTMENU || wg->type == GUI_WIDGET_DIALOG || wg->type == GUI_WIDGET_TOAST ||
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

static void gui_set_focused_widget(gui_widget_t *wg) {
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

    if (key == 3) {
        return gui_text_widget_copy_selection(wg);
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
    } else if (key == GUI_KEY_ENTER) {
        if (wg->type == GUI_WIDGET_TEXTAREA) {
            text_changed = gui_text_widget_insert_text(wg, "\n");
        } else {
            if (wg->owner && wg->owner->user_owner_pid != 0) gui_user_post_text_event(wg, GUI_EVENT_TEXT_SUBMIT);
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
        if (text_changed && wg->owner->user_owner_pid != 0) gui_user_post_text_event(wg, GUI_EVENT_TEXT_CHANGED);
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
    width = 18 + (int)strlen(label) * (int)GUI_CHAR_W;
    if (shortcut[0]) width += 10 + (int)strlen(shortcut) * (int)GUI_CHAR_W;
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


static int gui_contextmenu_row_height(void) { return (int)GUI_CHAR_H + 8; }

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
    int w = (int)strlen(label) * (int)GUI_CHAR_W + 18;
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

static void gui_draw_statusbar_text_cell(int x, int y, int w, int h, const char *text, uint32_t color, int align) {
    gui_rect_t clip;
    int tx;
    int text_w;
    if (!text || !text[0] || w <= 8) return;
    clip.x = x + 4;
    clip.y = y + 2;
    clip.w = w - 8;
    clip.h = h - 4;
    text_w = (int)strlen(text) * (int)GUI_CHAR_W;
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
            text_dx += gui_draw_inline_icon(wg->icon, ax + text_dx,
                                            ay + text_dy, wg->rect.h);
            {
                gui_rect_t clip = { ax + text_dx, ay + 2, wg->rect.w - text_dx - 3, wg->rect.h - 4 };
                gui_draw_window_title_text(ax + text_dx, gui_text_center_y(ay, wg->rect.h) + text_dy,
                                           wg->text, fg, &clip);
            }
        }
    } else if (wg->type == GUI_WIDGET_TOOLBAR) {
        gui_draw_toolbar_widget(wg, ax, ay);
    } else if (wg->type == GUI_WIDGET_STATUSBAR) {
        gui_draw_statusbar_widget(wg, ax, ay);
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
                gui_draw_window_title_text(ax + wg->rect.w / 2 - ((int)strlen(pct) * (int)GUI_CHAR_W) / 2, gui_text_center_y(ay, wg->rect.h), pct, wg->enabled ? g_gui.colors.text_fg : gui_rgb(125, 130, 140), &clip);
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
                int sx = cx + iw - 8 - (int)strlen(shortcut) * (int)GUI_CHAR_W;
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
                int sw = (int)strlen(shortcut) * (int)GUI_CHAR_W;
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
    if (bar_w < GUI_TASKBAR_START_W * 2 + 6 + padding * 2) {
        bar_w = GUI_TASKBAR_START_W * 2 + 6 + padding * 2;
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
    layout->first_window_x = layout->terminal_button.x + layout->terminal_button.w + 6;
    layout->item_h = GUI_TASKBAR_HEIGHT - 6;
    layout->item_y = gui_taskbar_item_y();
}

static int gui_taskbar_terminal_button_at(int x, int y) {
    gui_taskbar_layout_t layout;
    gui_taskbar_get_layout(&layout);
    return gui_rect_contains(&layout.terminal_button, x, y);
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
    gui_taskbar_invalidate_icon_hover_change(g_network_widget_rect, old_x, old_y, new_x, new_y);
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
    if (col >= g_gui.terminal.view.cols || row >= g_gui.terminal.view.rows) return;
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
    if (row >= g_gui.terminal.view.rows) return;
    if (col >= g_gui.terminal.view.cols) col = g_gui.terminal.view.cols - 1;
    x = w->rect.x + 6 + (int)col * GUI_CHAR_W;
    y = w->rect.y + GUI_TITLE_HEIGHT + 5 + (int)row * (GUI_CHAR_H + 1) + GUI_CHAR_H;
    gui_invalidate_rect(x - 1, y - 1, GUI_CHAR_W + 2, 3);
}

void gui_terminal_view_make_layout(gui_rect_t viewport, int padding_x, int padding_y, gui_terminal_view_layout_t *layout) {
    if (!layout) return;
    layout->x = viewport.x + padding_x;
    layout->y = viewport.y + padding_y;
    layout->cell_w = GUI_CHAR_W;
    layout->cell_h = GUI_CHAR_H + 1;
    layout->char_h = GUI_CHAR_H;
    layout->clip_rect = viewport;
}

int gui_terminal_view_point_to_cell(const gui_terminal_view_t *view, const gui_terminal_view_layout_t *layout, int x, int y, uint32_t *col, uint32_t *row) {
    int rel_x;
    int rel_y;
    uint32_t c;
    uint32_t r;
    if (!view || !layout || !col || !row || view->cols == 0 || view->rows == 0) return 0;
    if (layout->cell_w <= 0 || layout->cell_h <= 0) return 0;
    if (x < layout->x || y < layout->y) return 0;
    rel_x = x - layout->x;
    rel_y = y - layout->y;
    c = (uint32_t)(rel_x / layout->cell_w);
    r = (uint32_t)(rel_y / layout->cell_h);
    if (c >= view->cols || r >= view->rows) return 0;
    *col = c;
    *row = r;
    return 1;
}

static int gui_terminal_point_to_cell(int x, int y, uint32_t *col, uint32_t *row) {
    gui_window_t *w = g_gui.terminal.window;
    gui_rect_t viewport;
    gui_terminal_view_layout_t layout;
    if (!w || !g_gui.terminal.enabled || !col || !row) return 0;
    if (!w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return 0;
    viewport.x = w->rect.x;
    viewport.y = w->rect.y + GUI_TITLE_HEIGHT;
    viewport.w = w->rect.w;
    viewport.h = w->rect.h - GUI_TITLE_HEIGHT;
    gui_terminal_view_make_layout(viewport, 6, 5, &layout);
    return gui_terminal_view_point_to_cell(&g_gui.terminal.view, &layout, x, y, col, row);
}

void gui_terminal_view_begin_selection(gui_terminal_view_t *view, uint32_t col, uint32_t row) {
    if (!view || view->cols == 0 || view->rows == 0) return;
    if (col >= view->cols) col = view->cols - 1;
    if (row >= view->rows) row = view->rows - 1;
    view->selecting = 1;
    view->has_selection = 0;
    view->selection_anchor_x = col;
    view->selection_anchor_y = row;
    view->selection_start_x = col;
    view->selection_start_y = row;
    view->selection_end_x = col;
    view->selection_end_y = row;
}

void gui_terminal_view_update_selection(gui_terminal_view_t *view, uint32_t col, uint32_t row) {
    uint32_t a;
    uint32_t b;
    if (!view || view->cols == 0 || view->rows == 0) return;
    if (col >= view->cols) col = view->cols - 1;
    if (row >= view->rows) row = view->rows - 1;
    a = view->selection_anchor_y * view->cols + view->selection_anchor_x;
    b = row * view->cols + col;
    if (b < a) {
        view->selection_start_x = col;
        view->selection_start_y = row;
        view->selection_end_x = view->selection_anchor_x;
        view->selection_end_y = view->selection_anchor_y;
    } else {
        view->selection_start_x = view->selection_anchor_x;
        view->selection_start_y = view->selection_anchor_y;
        view->selection_end_x = col;
        view->selection_end_y = row;
    }
    view->has_selection = (a != b) ? 1 : 0;
}

void gui_terminal_view_end_selection(gui_terminal_view_t *view) {
    if (!view) return;
    view->selecting = 0;
}

static void gui_terminal_update_selection(uint32_t col, uint32_t row) {
    gui_terminal_view_update_selection(&g_gui.terminal.view, col, row);
    gui_terminal_invalidate_body();
}

int gui_terminal_view_cell_selected(const gui_terminal_view_t *view, uint32_t col, uint32_t row) {
    uint32_t p, s, e;
    if (!view || !view->has_selection || view->cols == 0) return 0;
    p = row * view->cols + col;
    s = view->selection_start_y * view->cols + view->selection_start_x;
    e = view->selection_end_y * view->cols + view->selection_end_x;
    return (p >= s && p <= e) ? 1 : 0;
}

void gui_terminal_view_clear_selection(gui_terminal_view_t *view) {
    if (!view || (!view->has_selection && !view->selecting)) return;
    view->selecting = 0;
    view->has_selection = 0;
}

void gui_terminal_clear_selection(void) {
    gui_terminal_view_t *view = &g_gui.terminal.view;
    if (!view->has_selection && !view->selecting) return;
    gui_terminal_view_clear_selection(view);
    gui_terminal_invalidate_body();
}

int gui_terminal_view_copy_selection(gui_terminal_view_t *view) {
    uint32_t r, c;
    uint32_t len = 0;
    uint32_t copied = 0;
    if (!view || !view->has_selection || view->cols == 0) return 0;
    for (r = view->selection_start_y; r <= view->selection_end_y && r < view->rows; r++) {
        uint32_t start_c = 0;
        uint32_t end_c = view->cols - 1;
        int last_non_space = -1;
        if (r == view->selection_start_y) start_c = view->selection_start_x;
        if (r == view->selection_end_y) end_c = view->selection_end_x;
        for (c = start_c; c <= end_c && c < view->cols; c++) {
            if (view->cells[r][c] != ' ') last_non_space = (int)c;
        }
        if (last_non_space < (int)start_c && r != view->selection_end_y) {
            if (len + 1 < GUI_TERM_CLIPBOARD_SIZE) view->clipboard[len++] = '\n';
            continue;
        }
        if (last_non_space >= (int)start_c) {
            for (c = start_c; c <= (uint32_t)last_non_space && c < view->cols; c++) {
                if (len + 1 >= GUI_TERM_CLIPBOARD_SIZE) break;
                view->clipboard[len++] = view->cells[r][c];
                copied = 1;
            }
        }
        if (r != view->selection_end_y && len + 1 < GUI_TERM_CLIPBOARD_SIZE) {
            view->clipboard[len++] = '\n';
        }
        if (len + 1 >= GUI_TERM_CLIPBOARD_SIZE) break;
    }
    view->clipboard[len] = '\0';
    view->clipboard_len = len;
    return copied || len > 0;
}

int gui_terminal_copy_selection(void) {
    if (!g_gui.initialized) return 0;
    return gui_terminal_view_copy_selection(&g_gui.terminal.view);
}

int gui_terminal_view_has_clipboard_text(const gui_terminal_view_t *view) {
    return (view && view->clipboard_len > 0 && view->clipboard[0] != '\0') ? 1 : 0;
}

int gui_terminal_view_set_clipboard_text(gui_terminal_view_t *view, const char *text) {
    uint32_t len = 0;
    if (!view) return 0;
    if (!text) text = "";
    while (text[len] && len + 1 < GUI_TERM_CLIPBOARD_SIZE) {
        view->clipboard[len] = text[len];
        len++;
    }
    view->clipboard[len] = '\0';
    view->clipboard_len = len;
    return len > 0;
}

const char *gui_terminal_view_get_clipboard_text(const gui_terminal_view_t *view) {
    return gui_terminal_view_has_clipboard_text(view) ? view->clipboard : "";
}

int gui_terminal_has_clipboard_text(void) {
    return gui_terminal_view_has_clipboard_text(&g_gui.terminal.view);
}

static int gui_terminal_set_clipboard_text(const char *text) {
    return gui_terminal_view_set_clipboard_text(&g_gui.terminal.view, text);
}

const char *gui_terminal_get_clipboard_text(void) {
    return gui_terminal_view_get_clipboard_text(&g_gui.terminal.view);
}

__attribute__((optimize("no-jump-tables")))
static void gui_handle_mouse_down(int x, int y) {
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
    int search_idx;
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
        w->rect.x = x - w->drag_offset_x;
        w->rect.y = y - w->drag_offset_y;
        if (w->rect.x < 0) w->rect.x = 0;
        if (w->rect.y < 0) w->rect.y = 0;
        if (w->rect.x + w->rect.w > (int)g_gui.width) w->rect.x = (int)g_gui.width - w->rect.w;
        if (w->rect.y + w->rect.h > (int)g_gui.height - GUI_TASKBAR_HEIGHT) w->rect.y = (int)g_gui.height - GUI_TASKBAR_HEIGHT - w->rect.h;
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
        w->rect.w = nw;
        w->rect.h = nh;
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
            } else if (ev.key == GUI_KEY_TAB) {
                gui_focus_next_widget();
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       (g_gui.focused_widget->type == GUI_WIDGET_TEXTBOX ||
                        g_gui.focused_widget->type == GUI_WIDGET_TEXTAREA)) {
                gui_textbox_on_key(g_gui.focused_widget, ev.key);
            } else if (browser_handle_address_enter(ev.key)) {
                /* Browser address bar consumed Enter. */
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
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused) {
                /* Focused widgets consume keys that they do not handle. */
            } else {
                /* Terminal input must flow through shell_run(), not GUI key events. */
            }
        } else if (ev.type == GUI_EVENT_MOUSE_DOWN) {
            if (ev.button & 1u) gui_handle_mouse_down(ev.x, ev.y);
            else if (ev.button & 2u) gui_handle_mouse_right_down(ev.x, ev.y);
        } else if (ev.type == GUI_EVENT_MOUSE_UP) {
            if (ev.button & 1u) gui_handle_mouse_up(ev.x, ev.y);
        } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
            gui_handle_mouse_move(ev.x, ev.y);
        } else if (ev.type == GUI_EVENT_MOUSE_WHEEL) {
            gui_widget_t *wheel_widget = gui_widget_at_screen(ev.x, ev.y);
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
            gui_cursor_present_fast();
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

    gui_terminal_init();
    gui_desktop_init();
    gui_notify(i18n_t(I18N_KEY_NOTIFY_WELCOME));
    gui_notify(i18n_t(I18N_KEY_NOTIFY_THEME_TIP));
    gui_render();
    return 0;
}

static void gui_copy_cached_text(char *dst, uint32_t dst_size, const char *src) {
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

static int gui_string_equals(const char *a, const char *b) {
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

static void gui_draw_folder_icon_art(int x, int y, uint32_t color) {
    gui_raw_fill_rect(x + 2, y + 6, 11, 6, gui_rgb(255, 220, 105));
    gui_raw_fill_rect(x, y + 11, 28, 20, color);
    gui_raw_fill_rect(x + 2, y + 15, 24, 14, gui_rgb(255, 211, 86));
    gui_raw_line(x, y + 11, x + 27, y + 11, gui_rgb(255, 238, 160));
    gui_raw_line(x, y + 11, x, y + 30, gui_rgb(255, 238, 160));
    gui_raw_line(x + 27, y + 11, x + 27, y + 30, gui_rgb(130, 90, 24));
    gui_raw_line(x, y + 30, x + 27, y + 30, gui_rgb(130, 90, 24));
}

static void gui_draw_browser_icon_art(int x, int y, uint32_t color) {
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

static void gui_draw_icon_button_frame(const gui_rect_t *rect, const char *label,
                                       int icon_w, int icon_h, int gap,
                                       int selected, int highlighted,
                                       uint32_t text_color,
                                       int *icon_x, int *icon_y) {
    int top_pad;
    int text_w;
    int text_x;
    int text_y;

    if (!rect) return;
    gui_draw_icon_button_face(rect, selected, highlighted);
    if (icon_w < 0) icon_w = 0;
    if (icon_h < 0) icon_h = 0;
    if (gap < 0) gap = 0;
    top_pad = (rect->h - (icon_h + gap + GUI_CHAR_H)) / 2;
    if (top_pad < 0) top_pad = 0;
    if (icon_x) *icon_x = rect->x + (rect->w - icon_w) / 2;
    if (icon_y) *icon_y = rect->y + top_pad;
    if (label && label[0]) {
        text_w = (int)font_measure_text_width(font_get_default(), label);
        text_x = rect->x + (rect->w - text_w) / 2;
        if (text_x < rect->x) text_x = rect->x;
        text_y = rect->y + top_pad + icon_h + gap;
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

static void gui_start_menu_clamp_scroll(void) {
    int max = gui_start_menu_max_scroll();
    if (g_gui.desktop_start_menu_scroll < 0) g_gui.desktop_start_menu_scroll = 0;
    if (g_gui.desktop_start_menu_scroll > max) g_gui.desktop_start_menu_scroll = max;
}

static void gui_update_start_menu_layout(void) {
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
    max_h = (int)g_gui.height - GUI_TASKBAR_HEIGHT - 12;
    if (max_h < header_h + item_h + 12) max_h = header_h + item_h + 12;
    if (desired_h > max_h) desired_h = max_h;
    if (desired_h < header_h + item_h + 12 && g_gui.launcher_app_count > 0) {
        desired_h = header_h + item_h + 12;
    }
    if (line_h > 0 && desired_h < line_h + item_h + 28) {
        desired_h = line_h + item_h + 28;
    }

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

static void gui_draw_launcher_item(const gui_launcher_entry_t *entry,
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
        int track_x = r->x + r->w - 12;
        int track_y = r->y + header_h;
        int track_h = r->h - header_h - 8;
        int thumb_h;
        int thumb_y;
        int max_scroll = gui_start_menu_max_scroll();
        gui_raw_fill_rect(track_x, track_y, 6, track_h, gui_rgb(18, 24, 36));
        thumb_h = (visible * track_h) / (int)g_gui.launcher_app_count;
        if (thumb_h < 12) thumb_h = 12;
        if (thumb_h > track_h) thumb_h = track_h;
        thumb_y = track_y;
        if (max_scroll > 0 && track_h > thumb_h) {
            thumb_y += (g_gui.desktop_start_menu_scroll * (track_h - thumb_h)) / max_scroll;
        }
        gui_raw_fill_rect(track_x + 1, thumb_y, 4, thumb_h, gui_rgb(112, 146, 198));
    }
}

static void gui_desktop_draw(void) {
    uint32_t i;
    /* center the 3-line welcome block on screen (above taskbar) */
    const char *line0 = i18n_t(I18N_KEY_BANNER_LINE0);
    const char *line1 = i18n_t(I18N_KEY_BANNER_LINE1);
    const char *line2 = i18n_t(I18N_KEY_BANNER_LINE2);
    const int line_gap = 12;                       /* extra gap between lines */
    const int line_step = GUI_CHAR_H + line_gap;   /* full line stride */
    int block_h;
    int avail_h;
    int top_y;
    int w0, w1, w2;
    int x0, x1, x2;

    if (!g_gui.desktop_enabled) return;

    block_h = GUI_CHAR_H + line_step * 2;          /* 3 lines */
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

static int gui_ascii_case_contains(const char *text, const char *query) {
    const char *p;
    if (!query || !query[0]) return 1;
    if (!text) return 0;
    for (p = text; *p; p++) {
        if (gui_ascii_case_equal_prefix(p, query)) return 1;
    }
    return 0;
}

static int gui_ascii_case_ends_with(const char *text, const char *suffix) {
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

static int gui_path_starts_with(const char *path, const char *prefix) {
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

static void gui_taskbar_search_copy(char *dst, uint32_t dst_len, const char *src) {
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

static void gui_file_preview_open_path(const char *path);
static void gui_file_preview_open_file(const char *path);

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

static int gui_is_enter_key(int key) {
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

static void gui_set_wallpaper_theme(uint32_t theme) {
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
        if (gui_start_menu_item_at(x, y, &launcher_index)) {
            g_gui.desktop_start_menu_open = 0;
            gui_launcher_launch(launcher_index);
            gui_invalidate_all();
            return 1;
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
    menu->item_count = 0;
    menu->w = GUI_CTXMENU_W;
    menu->item_h = GUI_CTXMENU_ITEM_H;
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
        gui_draw_text(menu->x + 12, item_y + 5, item->label, item->enabled ? fg : dim);
        if (item->shortcut[0]) {
            int sx = menu->x + menu->w - 12 - (int)font_measure_text_width(font_get_default(), item->shortcut);
            gui_draw_text(sx, item_y + 5, item->shortcut, item->enabled ? hint : dim);
        }
        if (item->has_submenu) {
            gui_draw_text(menu->x + menu->w - 16, item_y + 5, ">", item->enabled ? hint : dim);
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
    gui_ctxmenu_add_ex("Default", "", 0, 1, 0);
    gui_ctxmenu_add_ex("Day",     "", 1, 1, 0);
    gui_ctxmenu_add_ex("Solid",   "", 2, 1, 0);
    gui_ctxmenu_open_at(x, y, gui_ctxmenu_wallpaper_action, 0);
}

/* Handlers for desktop right-click menu */
static void gui_ctxmenu_desktop_action(int id, void *user) {
    (void)user;
    switch (id) {
        case 1: gui_file_preview_open(); break;        /* Open Files */
        case 2: gui_desktop_run_action(GUI_DESKTOP_ACTION_TERMINAL); break;
        case 3: gui_ctxmenu_open_wallpaper_submenu(g_ctxmenu.x + g_ctxmenu.w - 8, g_ctxmenu.y + 58); break;
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
    gui_ctxmenu_open_at(x, y, gui_ctxmenu_desktop_action, 0);
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
            key == GUI_KEY_ENTER || key == GUI_KEY_TAB) return 1;
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
    if (!widget || (widget->type != GUI_WIDGET_TEXTBOX && widget->type != GUI_WIDGET_TEXTAREA)) return;
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
    widget->button_flags = flags & (GUI_BUTTON_FLAG_DEFAULT | GUI_BUTTON_FLAG_DANGER);
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

void gui_widget_focus(gui_widget_t *widget) {
    gui_set_focused_widget(widget);
}

void gui_terminal_view_clear(gui_terminal_view_t *view) {
    uint32_t r, c;
    if (!view) return;
    for (r = 0; r < GUI_TERM_ROWS; r++) {
        for (c = 0; c < GUI_TERM_COLS; c++) {
            view->cells[r][c] = ' ';
        }
    }
    view->cursor_x = 0;
    view->cursor_y = 0;
    view->selecting = 0;
    view->has_selection = 0;
}

void gui_terminal_view_init(gui_terminal_view_t *view, uint32_t cols, uint32_t rows) {
    if (!view) return;
    view->cols = cols;
    view->rows = rows;
    if (view->cols > GUI_TERM_COLS) view->cols = GUI_TERM_COLS;
    if (view->rows > GUI_TERM_ROWS) view->rows = GUI_TERM_ROWS;
    view->clipboard_len = 0;
    view->clipboard[0] = '\0';
    gui_terminal_view_clear(view);
}

void gui_terminal_clear(void) {
    if (!g_gui.initialized || !g_gui.terminal.enabled) return;
    gui_terminal_view_clear(&g_gui.terminal.view);
    g_gui.terminal.dirty = 1;
    gui_terminal_invalidate_body();
}

void gui_terminal_init(void) {
    gui_window_t *term;
    uint32_t cols;
    uint32_t rows;
    if (g_gui.terminal.window) return;
    term = gui_create_window(24, 420, (int)g_gui.width - 48, (int)g_gui.height - 448, i18n_t(I18N_KEY_WIN_TERMINAL));
    if (!term) return;
    term->flags |= GUI_WINDOW_FLAG_TERMINAL;
    term->bg_color = gui_rgb(10, 14, 22);
    g_gui.terminal.window = term;
    cols = (uint32_t)((term->rect.w - 12) / GUI_CHAR_W);
    rows = (uint32_t)((term->rect.h - GUI_TITLE_HEIGHT - 10) / (GUI_CHAR_H + 1));
    gui_terminal_view_init(&g_gui.terminal.view, cols, rows);
    g_gui.terminal.enabled = 1;
    g_gui.terminal.input_focused = 1;
    g_gui.terminal.cursor_visible = 1;
    g_gui.terminal.cursor_blink_ticks = 0;
    gui_terminal_clear();
    gui_terminal_write("TERMINAL ready. Keyboard input is routed here.\n> ");
}

void gui_terminal_view_scroll(gui_terminal_view_t *view) {
    uint32_t r;
    if (!view || view->rows == 0 || view->cols == 0) return;
    for (r = 1; r < view->rows; r++) {
        memcpy(view->cells[r - 1], view->cells[r], view->cols);
    }
    memset(view->cells[view->rows - 1], ' ', view->cols);
    if (view->cursor_y > 0) view->cursor_y--;
}

static void gui_terminal_scroll(void) {
    gui_terminal_view_scroll(&g_gui.terminal.view);
    gui_terminal_invalidate_body();
}

static void gui_terminal_view_handle_carriage_return(gui_terminal_view_t *view) {
    if (!view) return;
    /* CR returns to the beginning of the current line. It must not scroll or move down. */
    view->cursor_x = 0;
}

static void gui_terminal_view_handle_line_feed(gui_terminal_view_t *view, int *body_dirty) {
    if (!view || !body_dirty) return;
    /* LF advances to the next display line. Keep the existing console convention of column 0. */
    view->cursor_x = 0;
    view->cursor_y++;
    if (view->cursor_y >= view->rows) {
        gui_terminal_view_scroll(view);
        *body_dirty = 1;
    }
}

static void gui_terminal_view_handle_backspace(gui_terminal_view_t *view) {
    if (!view || view->cols == 0) return;
    if (view->cursor_x > 0) {
        view->cursor_x--;
    } else if (view->cursor_y > 0) {
        view->cursor_y--;
        view->cursor_x = view->cols - 1;
    }
}

int gui_terminal_view_putc(gui_terminal_view_t *view, char ch, int *body_dirty) {
    int dirty = 0;
    if (!view || view->cols == 0 || view->rows == 0) return 0;
    if (ch == '\n') {
        gui_terminal_view_handle_line_feed(view, &dirty);
    } else if (ch == '\r') {
        gui_terminal_view_handle_carriage_return(view);
    } else if (ch == '\b') {
        gui_terminal_view_handle_backspace(view);
    } else {
        if (view->cursor_x >= view->cols) {
            gui_terminal_view_handle_line_feed(view, &dirty);
        }
        if (view->cursor_y >= view->rows) {
            gui_terminal_view_scroll(view);
            dirty = 1;
        }
        view->cells[view->cursor_y][view->cursor_x] = ch;
        view->cursor_x++;
    }
    if (view->cursor_y >= view->rows) {
        gui_terminal_view_scroll(view);
        dirty = 1;
    }
    if (body_dirty) *body_dirty = dirty;
    return 1;
}

void gui_terminal_view_write(gui_terminal_view_t *view, const char *text) {
    if (!text) return;
    while (*text) {
        gui_terminal_view_putc(view, *text++, 0);
    }
}

void gui_terminal_putc(char ch) {
    gui_terminal_view_t *view = &g_gui.terminal.view;
    uint32_t old_x, old_y;
    int body_dirty = 0;
    if (!g_gui.initialized || !g_gui.terminal.enabled) return;
    if (view->cols == 0 || view->rows == 0) return;
    if (view->has_selection && ch != 0x03 && ch != 0x16) gui_terminal_clear_selection();

    old_x = view->cursor_x;
    old_y = view->cursor_y;
    gui_terminal_invalidate_cursor_at(old_x, old_y);
    gui_terminal_view_putc(view, ch, &body_dirty);
    if (body_dirty) {
        gui_terminal_invalidate_body();
    } else {
        gui_terminal_invalidate_cell(old_x, old_y);
        gui_terminal_invalidate_cursor_at(view->cursor_x, view->cursor_y);
    }
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

void gui_terminal_view_draw(const gui_terminal_view_t *view, const gui_terminal_view_layout_t *layout, int draw_cursor, uint32_t fg, uint32_t selection_bg, uint32_t selection_fg, uint32_t cursor_color) {
    uint32_t r, c;
    uint32_t r_start = 0, r_end;
    uint32_t c_start = 0, c_end;
    if (!view || !layout || view->cols == 0 || view->rows == 0) return;
    r_end = view->rows;
    c_end = view->cols;
    {
        gui_rect_t clip = layout->clip_rect;
        gui_set_clip_rect(&clip);
    }
    if (g_gui.clip_enabled && g_gui.clip_rect.w <= 0) {
        gui_clear_clip_rect();
        return;
    }
    if (g_gui.clip_enabled) {
        int left = g_gui.clip_rect.x - layout->x;
        int top = g_gui.clip_rect.y - layout->y;
        int right = g_gui.clip_rect.x + g_gui.clip_rect.w - layout->x + layout->cell_w - 1;
        int bottom = g_gui.clip_rect.y + g_gui.clip_rect.h - layout->y + layout->char_h;
        if (left > 0) c_start = (uint32_t)(left / layout->cell_w);
        if (top > 0) r_start = (uint32_t)(top / layout->cell_h);
        if (right > 0) c_end = (uint32_t)(right / layout->cell_w);
        if (bottom > 0) r_end = (uint32_t)(bottom / layout->cell_h);
        if (c_end > view->cols) c_end = view->cols;
        if (r_end > view->rows) r_end = view->rows;
        if (c_start > c_end) c_start = c_end;
        if (r_start > r_end) r_start = r_end;
    }
    for (r = r_start; r < r_end; r++) {
        for (c = c_start; c < c_end; c++) {
            char ch = view->cells[r][c];
            int px = layout->x + (int)c * layout->cell_w;
            int py = layout->y + (int)r * layout->cell_h;
            int selected = gui_terminal_view_cell_selected(view, c, r);
            if (selected) {
                gui_raw_fill_rect(px, py, layout->cell_w, layout->cell_h, selection_bg);
            }
            if (ch != ' ') {
                gui_draw_char(px, py, ch, selected ? selection_fg : fg);
            }
        }
    }
    if (draw_cursor && view->cursor_x < view->cols && view->cursor_y < view->rows) {
        gui_raw_fill_rect(layout->x + (int)view->cursor_x * layout->cell_w,
                          layout->y + (int)view->cursor_y * layout->cell_h + layout->char_h,
                          layout->cell_w - 1, 1, cursor_color);
    }
    gui_clear_clip_rect();
}

void gui_terminal_redraw(void) {
    gui_window_t *w = g_gui.terminal.window;
    gui_rect_t client;
    gui_terminal_view_layout_t layout;
    if (!w || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;
    client.x = w->rect.x + GUI_BORDER_SIZE;
    client.y = w->rect.y + GUI_TITLE_HEIGHT;
    client.w = w->rect.w - GUI_BORDER_SIZE * 2;
    client.h = w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE;
    gui_terminal_view_make_layout(client, 6 - GUI_BORDER_SIZE, 5, &layout);
    gui_terminal_view_draw(&g_gui.terminal.view, &layout,
                           g_gui.terminal.input_focused && g_gui.terminal.cursor_visible,
                           gui_rgb(185, 255, 185),
                           gui_rgb(70, 105, 180),
                           gui_rgb(255, 255, 255),
                           gui_rgb(185, 255, 185));
}

static void gui_terminal_invalidate_cursor(void) {
    gui_terminal_invalidate_cursor_at(g_gui.terminal.view.cursor_x, g_gui.terminal.view.cursor_y);
}

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

static gui_window_t *fp_window;

static void gui_draw_taskbar_window_icon(gui_rect_t rect, gui_window_t *w) {
    int hover = gui_taskbar_icon_hovered(rect);
    int minimized = w && ((w->flags & GUI_WINDOW_FLAG_MINIMIZED) != 0);
    int x;
    int y;
    uint32_t title;
    uint32_t body;
    uint32_t border = gui_rgb(205, 225, 255);
    uint32_t shadow = gui_rgb(60, 76, 110);

    if (w == fp_window) {
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
    if (w == g_browser_win) {
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

static int gui_tray_network_is_wireless(void) {
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

    max_chars = (r.w - 28) / cw;
    if (max_chars < 1) max_chars = 1;
    strncpy(clipped, text, (size_t)max_chars);
    clipped[max_chars] = 0;
    gui_draw_text(r.x + 22, ty, clipped, fg);
    if (g_gui.taskbar_search_focused) {
        int caret_x = r.x + 22 + (int)g_gui.taskbar_search_len * cw;
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
    int cw = GUI_CHAR_W > 0 ? GUI_CHAR_W : 8;
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

    max_chars = (g_gui.taskbar_search_results_rect.w - text_x_pad - 8) / cw;
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
        while (r->path[j] && k + 1 < sizeof(line) && (int)k < max_chars) {
            line[k++] = r->path[j++];
        }
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

static void gui_notify(const char *text) {
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

static void gui_about_open(void) {
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

static void gui_recycle_open(void) {
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

static void browser_on_close(gui_window_t *win, void *ud) {
    uint32_t i;
    (void)win;
    (void)ud;
    browser_load_finish(0);
    g_browser_win = 0;
    g_browser_address_box = 0;
    g_browser_status_label = 0;
    for (i = 0; i < GUI_BROWSER_CONTENT_LINES; i++) g_browser_content_lines[i] = 0;
}

static void browser_set_widget_text(gui_widget_t *widget, const char *text) {
    uint32_t i;
    if (!widget || !text) return;
    for (i = 0; i < sizeof(widget->text) - 1u && text[i]; i++) widget->text[i] = text[i];
    widget->text[i] = '\0';
}

static void browser_set_status(const char *text) {
    browser_set_widget_text(g_browser_status_label, text);
    if (text) gui_notify(text);
    gui_render();
}

static void browser_clear_content(void) {
    uint32_t i;
    for (i = 0; i < GUI_BROWSER_CONTENT_LINES; i++) {
        browser_set_widget_text(g_browser_content_lines[i], "");
        g_browser_line_links[i][0] = '\0';
        if (g_browser_content_lines[i]) {
            g_browser_content_lines[i]->type = GUI_WIDGET_LABEL;
            g_browser_content_lines[i]->on_click = 0;
            g_browser_content_lines[i]->user_data = 0;
            g_browser_content_lines[i]->fg_color = gui_rgb(20, 30, 45);
        }
    }
}

static int browser_copy_until(char *out, uint32_t cap, const char **pp, char stop) {
    uint32_t len = 0;
    const char *p;
    if (!out || cap == 0 || !pp || !*pp) return -1;
    p = *pp;
    while (*p && *p != stop) {
        if (len + 1u >= cap) return -1;
        out[len++] = *p++;
    }
    out[len] = '\0';
    *pp = p;
    return 0;
}

static int browser_parse_url(const char *url, char *host, uint32_t host_cap,
                             char *path, uint32_t path_cap, uint16_t *port,
                             browser_scheme_t *scheme) {
    const char *p;
    const char *host_start;
    uint32_t len = 0;
    uint32_t port_num = 0;
    browser_scheme_t parsed_scheme = BROWSER_SCHEME_HTTP;
    if (!url || !host || !path || !port || host_cap == 0 || path_cap == 0) return -1;
    p = url;
    if (browser_str_starts_ci(p, "http://")) {
        parsed_scheme = BROWSER_SCHEME_HTTP;
        p += 7;
    } else if (browser_str_starts_ci(p, "https://")) {
        parsed_scheme = BROWSER_SCHEME_HTTPS;
        p += 8;
    }
    host_start = p;
    while (*p && *p != '/' && *p != ':') {
        if (len + 1u >= host_cap) return -1;
        host[len++] = *p++;
    }
    host[len] = '\0';
    if (p == host_start) return -1;
    *port = (parsed_scheme == BROWSER_SCHEME_HTTPS) ? 443u : 80u;
    if (*p == ':') {
        p++;
        while (*p >= '0' && *p <= '9') {
            port_num = port_num * 10u + (uint32_t)(*p - '0');
            if (port_num > 65535u) return -1;
            p++;
        }
        if (port_num == 0) return -1;
        *port = (uint16_t)port_num;
    }
    if (*p == '/') {
        if (browser_copy_until(path, path_cap, &p, '\0') != 0) return -1;
    } else {
        path[0] = '/';
        path[1] = '\0';
    }
    if (scheme) *scheme = parsed_scheme;
    return 0;
}

static uint32_t browser_render_text_at(const char *text, uint32_t start_line) {
    uint32_t line = start_line;
    uint32_t col = 0;
    char linebuf[64];
    const char *p;
    if (line >= GUI_BROWSER_CONTENT_LINES) return line;
    if (!text || !*text) return line;
    linebuf[0] = '\0';
    p = text;
    while (*p && line < GUI_BROWSER_CONTENT_LINES) {
        char ch = *p++;
        if (ch == '\r') continue;
        if (ch == '\n' || col >= 58u) {
            linebuf[col] = '\0';
            browser_set_widget_text(g_browser_content_lines[line], linebuf);
            line++;
            col = 0;
            linebuf[0] = '\0';
            if (ch == '\n') continue;
            if (line >= GUI_BROWSER_CONTENT_LINES) break;
        }
        if ((unsigned char)ch < 32u) ch = ' ';
        linebuf[col++] = ch;
        linebuf[col] = '\0';
    }
    if (line < GUI_BROWSER_CONTENT_LINES && col > 0) {
        browser_set_widget_text(g_browser_content_lines[line], linebuf);
        line++;
    }
    return line;
}

static int browser_str_starts_ci(const char *p, const char *prefix) {
    uint32_t i = 0;
    if (!p || !prefix) return 0;
    while (prefix[i]) {
        char a = p[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        i++;
    }
    return 1;
}

static int browser_html_tag_break(const char *tag) {
    while (*tag == ' ' || *tag == '\t' || *tag == '/') tag++;
    return browser_str_starts_ci(tag, "br") || browser_str_starts_ci(tag, "p") ||
           browser_str_starts_ci(tag, "div") || browser_str_starts_ci(tag, "section") ||
           browser_str_starts_ci(tag, "article") || browser_str_starts_ci(tag, "header") ||
           browser_str_starts_ci(tag, "footer") || browser_str_starts_ci(tag, "main") ||
           browser_str_starts_ci(tag, "nav") || browser_str_starts_ci(tag, "blockquote") ||
           browser_str_starts_ci(tag, "ul") || browser_str_starts_ci(tag, "ol") ||
           browser_str_starts_ci(tag, "li") || browser_str_starts_ci(tag, "tr") ||
           browser_str_starts_ci(tag, "table") || browser_str_starts_ci(tag, "h1") ||
           browser_str_starts_ci(tag, "h2") || browser_str_starts_ci(tag, "h3") ||
           browser_str_starts_ci(tag, "h4") || browser_str_starts_ci(tag, "h5") ||
           browser_str_starts_ci(tag, "h6") || browser_str_starts_ci(tag, "title") ||
           browser_str_starts_ci(tag, "pre");
}

static int browser_html_tag_is_pre(const char *tag) {
    while (*tag == ' ' || *tag == '\t' || *tag == '/') tag++;
    return browser_str_starts_ci(tag, "pre") || browser_str_starts_ci(tag, "code");
}

static int browser_html_tag_is_list_item(const char *tag) {
    while (*tag == ' ' || *tag == '\t' || *tag == '/') tag++;
    return browser_str_starts_ci(tag, "li");
}

static char browser_decode_entity(const char **pp) {
    const char *p = *pp;
    if (browser_str_starts_ci(p, "amp;")) { *pp = p + 4; return '&'; }
    if (browser_str_starts_ci(p, "lt;")) { *pp = p + 3; return '<'; }
    if (browser_str_starts_ci(p, "gt;")) { *pp = p + 3; return '>'; }
    if (browser_str_starts_ci(p, "quot;")) { *pp = p + 5; return '"'; }
    if (browser_str_starts_ci(p, "apos;")) { *pp = p + 5; return '\''; }
    if (browser_str_starts_ci(p, "#39;")) { *pp = p + 4; return '\''; }
    if (browser_str_starts_ci(p, "nbsp;")) { *pp = p + 5; return ' '; }
    if (browser_str_starts_ci(p, "copy;")) { *pp = p + 5; return 'c'; }
    if (browser_str_starts_ci(p, "reg;")) { *pp = p + 4; return 'r'; }
    if (browser_str_starts_ci(p, "mdash;")) { *pp = p + 6; return '-'; }
    if (browser_str_starts_ci(p, "ndash;")) { *pp = p + 6; return '-'; }
    if (browser_str_starts_ci(p, "hellip;")) { *pp = p + 7; return '.'; }
    if (browser_str_starts_ci(p, "lsquo;")) { *pp = p + 6; return '\''; }
    if (browser_str_starts_ci(p, "rsquo;")) { *pp = p + 6; return '\''; }
    if (browser_str_starts_ci(p, "ldquo;")) { *pp = p + 6; return '"'; }
    if (browser_str_starts_ci(p, "rdquo;")) { *pp = p + 6; return '"'; }
    if (p[0] == '#') {
        uint32_t value = 0;
        const char *q = p + 1;
        int hex = 0;
        if (*q == 'x' || *q == 'X') { hex = 1; q++; }
        while (*q && *q != ';') {
            char c = *q;
            uint32_t digit;
            if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
            else if (hex && c >= 'a' && c <= 'f') digit = 10u + (uint32_t)(c - 'a');
            else if (hex && c >= 'A' && c <= 'F') digit = 10u + (uint32_t)(c - 'A');
            else return '&';
            value = value * (hex ? 16u : 10u) + digit;
            if (value > 255u) return '?';
            q++;
        }
        if (*q == ';' && value > 0) {
            *pp = q + 1;
            return (value < 32u) ? ' ' : (char)value;
        }
    }
    return '&';
}

static int browser_html_tag_name_eq(const char *tag, const char *name) {
    uint32_t i = 0;
    if (!tag || !name) return 0;
    while (*tag == ' ' || *tag == '\t' || *tag == '\r' || *tag == '\n') tag++;
    if (*tag == '/') tag++;
    while (name[i]) {
        char a = tag[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        i++;
    }
    return tag[i] == '\0' || tag[i] == '>' || tag[i] == ' ' || tag[i] == '\t' ||
           tag[i] == '\r' || tag[i] == '\n' || tag[i] == '/';
}

static int browser_html_hidden_tag(const char *tag) {
    return browser_html_tag_name_eq(tag, "head") || browser_html_tag_name_eq(tag, "style") ||
           browser_html_tag_name_eq(tag, "script") || browser_html_tag_name_eq(tag, "svg") ||
           browser_html_tag_name_eq(tag, "noscript") || browser_html_tag_name_eq(tag, "template");
}

static const char *browser_html_skip_hidden(const char *p, const char *tag) {
    const char *q = p;
    const char *name = 0;
    if (browser_html_tag_name_eq(tag, "head")) name = "head";
    else if (browser_html_tag_name_eq(tag, "style")) name = "style";
    else if (browser_html_tag_name_eq(tag, "script")) name = "script";
    else if (browser_html_tag_name_eq(tag, "svg")) name = "svg";
    else if (browser_html_tag_name_eq(tag, "noscript")) name = "noscript";
    else if (browser_html_tag_name_eq(tag, "template")) name = "template";
    if (!name) return p;
    while (*q) {
        if (q[0] == '<' && q[1] == '/' && browser_html_tag_name_eq(q + 2, name)) {
            while (*q && *q != '>') q++;
            if (*q == '>') q++;
            return q;
        }
        q++;
    }
    return q;
}

static const char *browser_html_find_body_start(const char *html) {
    const char *p = html;
    if (!p) return html;
    while (*p) {
        if (*p == '<' && browser_str_starts_ci(p + 1, "body")) {
            while (*p && *p != '>') p++;
            return (*p == '>') ? p + 1 : p;
        }
        p++;
    }
    return html;
}

static void browser_html_to_text(const char *html, char *out, uint32_t cap) {
    uint32_t n = 0;
    int last_space = 1;
    int preserve_ws = 0;
    const char *p = html;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!html) return;
    p = browser_html_find_body_start(html);
    while (*p && n + 1u < cap) {
        char ch = *p++;
        if (ch == '<') {
            const char *tag_start = p;
            const char *tag_end = p;
            int is_close = (*tag_start == '/');
            while (*tag_end && *tag_end != '>') tag_end++;
            if (!*tag_end) break;
            if (!is_close && browser_html_hidden_tag(tag_start)) {
                p = browser_html_skip_hidden(tag_end + 1, tag_start);
                continue;
            }
            if (browser_html_tag_break(tag_start) && n > 0 && out[n - 1] != '\n') {
                while (n > 0 && out[n - 1] == ' ') n--;
                if (n > 0 && n + 1u < cap) out[n++] = '\n';
                last_space = 1;
            }
            if (!is_close && browser_html_tag_is_list_item(tag_start) && n + 3u < cap) {
                out[n++] = '-';
                out[n++] = ' ';
                last_space = 0;
            }
            if (browser_html_tag_is_pre(tag_start)) preserve_ws = !is_close;
            p = tag_end + 1;
            continue;
        }
        if (ch == '&') ch = browser_decode_entity(&p);
        if (preserve_ws) {
            if (ch == '\r') continue;
            if (ch == '\t') ch = ' ';
            if ((unsigned char)ch < 32u && ch != '\n') continue;
            out[n++] = ch;
            last_space = (ch == ' ' || ch == '\n');
            continue;
        }
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        if ((unsigned char)ch < 32u) continue;
        if (ch == ' ') {
            if (last_space) continue;
            last_space = 1;
        } else {
            last_space = 0;
        }
        out[n++] = ch;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\n')) n--;
    out[n] = '\0';
}

static int browser_text_contains_ci(const char *p, const char *needle, uint32_t max_scan) {
    uint32_t i = 0;
    if (!p || !needle || !needle[0]) return 0;
    while (p[i] && i < max_scan) {
        if (browser_str_starts_ci(p + i, needle)) return 1;
        i++;
    }
    return 0;
}

static const char *browser_skip_text_prefix(const char *p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p ? p : "";
}

static int browser_body_looks_html(const char *body) {
    const char *p = browser_skip_text_prefix(body);
    if (!p || !*p) return 0;
    if (browser_str_starts_ci(p, "<!doctype") || browser_str_starts_ci(p, "<html") ||
        browser_str_starts_ci(p, "<head") || browser_str_starts_ci(p, "<body") ||
        browser_str_starts_ci(p, "<style") || browser_str_starts_ci(p, "</style") ||
        browser_str_starts_ci(p, "</head")) return 1;
    return browser_text_contains_ci(p, "<html", 512u) ||
           browser_text_contains_ci(p, "<body", 512u) ||
           browser_text_contains_ci(p, "<div", 512u) ||
           browser_text_contains_ci(p, "<p", 512u) ||
           browser_text_contains_ci(p, "<h1", 512u) ||
           browser_text_contains_ci(p, "<title", 512u);
}

static int browser_response_is_html(char *response, const char *body) {
    const char *p = response;
    while (p && *p && body && p < body) {
        if (browser_header_name_eq(p, "Content-Type")) {
            const char *v = p;
            while (*v && *v != ':' && *v != '\n') v++;
            if (*v == ':') v++;
            while (*v && *v != '\r' && *v != '\n') {
                if (browser_str_starts_ci(v, "text/html") || browser_str_starts_ci(v, "application/xhtml")) return 1;
                v++;
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return browser_body_looks_html(body);
}

static void browser_copy_url(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void browser_current_origin(char *origin, uint32_t cap) {
    char url[64];
    char host[64];
    char path[128];
    uint16_t port;
    browser_scheme_t scheme = BROWSER_SCHEME_HTTP;
    int pos = 0;
    uint32_t i;
    char portbuf[8];
    uint16_t default_port;
    if (!origin || cap == 0) return;
    origin[0] = '\0';
    browser_copy_url(url, sizeof(url), g_browser_address_box ? g_browser_address_box->text : "http://example.com/");
    if (browser_parse_url(url, host, sizeof(host), path, sizeof(path), &port, &scheme) != 0) return;
    default_port = (scheme == BROWSER_SCHEME_HTTPS) ? 443u : 80u;
    pos = fp_str_append(origin, pos, (int)cap, scheme == BROWSER_SCHEME_HTTPS ? "https://" : "http://");
    pos = fp_str_append(origin, pos, (int)cap, host);
    if (port != default_port) {
        for (i = 0; i < sizeof(portbuf); i++) portbuf[i] = '\0';
        i = sizeof(portbuf) - 1u;
        do {
            portbuf[--i] = (char)('0' + (port % 10u));
            port /= 10u;
        } while (port && i > 0);
        pos = fp_str_append(origin, pos, (int)cap, ":");
        pos = fp_str_append(origin, pos, (int)cap, &portbuf[i]);
    }
    (void)path;
}

static void browser_resolve_link_url(const char *href, char *out, uint32_t cap) {
    char origin[96];
    int pos = 0;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!href || !*href || href[0] == '#' || browser_str_starts_ci(href, "mailto:")) return;
    if (browser_str_starts_ci(href, "http://") || browser_str_starts_ci(href, "https://")) {
        browser_copy_url(out, cap, href);
        return;
    }
    browser_current_origin(origin, sizeof(origin));
    if (!origin[0]) return;
    pos = fp_str_append(out, pos, (int)cap, origin);
    if (href[0] == '/') {
        pos = fp_str_append(out, pos, (int)cap, href);
    } else {
        pos = fp_str_append(out, pos, (int)cap, "/");
        pos = fp_str_append(out, pos, (int)cap, href);
    }
    (void)pos;
}

static const char *browser_find_attr_value(const char *tag, const char *attr) {
    uint32_t attr_len = (uint32_t)strlen(attr);
    const char *p = tag;
    while (p && *p && *p != '>') {
        if (browser_str_starts_ci(p, attr) && p[attr_len] == '=') {
            p += attr_len + 1u;
            if (*p == '\"' || *p == '\'') p++;
            return p;
        }
        p++;
    }
    return 0;
}

static void browser_copy_attr(char *dst, uint32_t cap, const char *value) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!value) return;
    while (value[i] && value[i] != '\"' && value[i] != '\'' && value[i] != '>' &&
           value[i] != ' ' && value[i] != '\t' && i + 1u < cap) {
        dst[i] = value[i];
        i++;
    }
    dst[i] = '\0';
}

static void browser_extract_anchor_text(const char *start, const char *end, char *out, uint32_t cap) {
    uint32_t n = 0;
    int in_tag = 0;
    if (!out || cap == 0) return;
    out[0] = '\0';
    while (start && start < end && *start && n + 1u < cap) {
        char ch = *start++;
        if (in_tag) {
            if (ch == '>') in_tag = 0;
            continue;
        }
        if (ch == '<') {
            in_tag = 1;
            continue;
        }
        if (ch == '&') ch = browser_decode_entity(&start);
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        if ((unsigned char)ch < 32u) continue;
        out[n++] = ch;
    }
    out[n] = '\0';
}

static void browser_link_on_click(gui_widget_t *w, void *ud) {
    const char *url = (const char *)ud;
    (void)w;
    if (!url || !*url || !g_browser_address_box) return;
    browser_set_widget_text(g_browser_address_box, url);
    g_browser_address_box->cursor = (uint32_t)strlen(g_browser_address_box->text);
    browser_load_start();
}

static void browser_set_line_link(uint32_t line, const char *label, const char *url) {
    gui_widget_t *wg;
    if (line >= GUI_BROWSER_LINKS_MAX) return;
    wg = g_browser_content_lines[line];
    browser_copy_url(g_browser_line_links[line], sizeof(g_browser_line_links[line]), url);
    browser_set_widget_text(wg, label && *label ? label : url);
    if (wg) {
        wg->type = GUI_WIDGET_BUTTON;
        wg->on_click = browser_link_on_click;
        wg->user_data = (void *)g_browser_line_links[line];
        wg->bg_color = gui_rgb(236, 238, 244);
        wg->fg_color = gui_rgb(20, 90, 190);
    }
}

static uint32_t browser_render_links(const char *html, uint32_t start_line) {
    const char *p = html;
    uint32_t line = start_line;
    while (p && *p && line < GUI_BROWSER_CONTENT_LINES) {
        const char *a = p;
        const char *href_v;
        const char *tag_end;
        const char *close;
        char href[128];
        char url[128];
        char label[64];
        char display[64];
        int pos = 0;
        while (*a && !browser_str_starts_ci(a, "<a")) a++;
        if (!*a) break;
        tag_end = a;
        while (*tag_end && *tag_end != '>') tag_end++;
        if (*tag_end != '>') break;
        href_v = browser_find_attr_value(a, "href");
        if (!href_v || href_v > tag_end) {
            p = tag_end + 1;
            continue;
        }
        browser_copy_attr(href, sizeof(href), href_v);
        browser_resolve_link_url(href, url, sizeof(url));
        close = tag_end + 1;
        while (*close && !browser_str_starts_ci(close, "</a")) close++;
        browser_extract_anchor_text(tag_end + 1, close, label, sizeof(label));
        if (url[0]) {
            pos = fp_str_append(display, pos, sizeof(display), "> ");
            pos = fp_str_append(display, pos, sizeof(display), label[0] ? label : url);
            (void)pos;
            browser_set_line_link(line++, display, url);
        }
        p = *close ? close + 3 : tag_end + 1;
    }
    return line;
}

static uint32_t browser_render_body_text_at(const char *body, uint32_t start_line) {
    if (!body || !*body) {
        if (start_line < GUI_BROWSER_CONTENT_LINES) browser_set_widget_text(g_browser_content_lines[start_line++], "HTTP response has no body.");
        return start_line;
    }
    return browser_render_text_at(body, start_line);
}

static uint32_t browser_render_body_at(const char *body, uint32_t start_line) {
    static char html_text[1024];
    if (!body || !*body) return browser_render_body_text_at(body, start_line);
    if (browser_body_looks_html(body)) {
        browser_html_to_text(body, html_text, sizeof(html_text));
        return browser_render_body_text_at(html_text, start_line);
    }
    return browser_render_body_text_at(body, start_line);
}

static void browser_render_body(const char *body) {
    browser_clear_content();
    (void)browser_render_body_at(body, 0);
}

static int browser_header_name_eq(const char *p, const char *name) {
    uint32_t i = 0;
    if (!p || !name) return 0;
    while (name[i]) {
        char a = p[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        i++;
    }
    return p[i] == ':';
}

static void browser_copy_header_line(char *out, uint32_t cap, const char *line) {
    uint32_t i = 0;
    if (!out || cap == 0) return;
    if (!line) {
        out[0] = '\0';
        return;
    }
    while (line[i] && line[i] != '\r' && line[i] != '\n' && i + 1u < cap) {
        out[i] = line[i];
        i++;
    }
    out[i] = '\0';
}

static uint32_t browser_render_response_summary(char *response, const char *body) {
    const char *p;
    uint32_t line = 0;
    char status[64];
    char header[64];
    browser_clear_content();
    if (browser_response_is_html(response, body)) {
        static char html_text[1024];
        browser_html_to_text(body, html_text, sizeof(html_text));
        line = browser_render_body_text_at(html_text, line);
        return browser_render_links(body, line);
    }
    browser_copy_header_line(status, sizeof(status), response);
    if (status[0]) browser_set_widget_text(g_browser_content_lines[line++], status);
    p = response;
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
    while (*p && p < body && line + 1u < GUI_BROWSER_CONTENT_LINES) {
        if (*p == '\r' || *p == '\n') break;
        if (browser_header_name_eq(p, "Content-Type") ||
            browser_header_name_eq(p, "Content-Length") ||
            browser_header_name_eq(p, "Location") ||
            browser_header_name_eq(p, "Server")) {
            browser_copy_header_line(header, sizeof(header), p);
            browser_set_widget_text(g_browser_content_lines[line++], header);
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    if (line < GUI_BROWSER_CONTENT_LINES) browser_set_widget_text(g_browser_content_lines[line++], "");
    return browser_render_body_at(body, line);
}

static const char *browser_find_body(char *response) {
    char *p;
    if (!response) return "";
    for (p = response; p[0] && p[1] && p[2] && p[3]; p++) {
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') return p + 4;
        if (p[0] == '\n' && p[1] == '\n') return p + 2;
    }
    return response;
}


static int browser_tls_append_u8(uint8_t *buf, uint32_t cap, uint32_t *pos, uint8_t v) {
    if (!buf || !pos || *pos >= cap) return -1;
    buf[(*pos)++] = v;
    return 0;
}

static int browser_tls_append_u16(uint8_t *buf, uint32_t cap, uint32_t *pos, uint16_t v) {
    if (browser_tls_append_u8(buf, cap, pos, (uint8_t)(v >> 8)) != 0) return -1;
    return browser_tls_append_u8(buf, cap, pos, (uint8_t)(v & 0xffu));
}

static int browser_tls_append_bytes(uint8_t *buf, uint32_t cap, uint32_t *pos, const uint8_t *src, uint32_t len) {
    uint32_t i;
    if (!buf || !pos || !src || *pos + len > cap) return -1;
    for (i = 0; i < len; i++) buf[(*pos)++] = src[i];
    return 0;
}

static int browser_tls_build_client_hello(const char *host, uint8_t *out, uint32_t cap) {
    static const uint8_t ciphers[] = {
        0xc0, 0x2f, /* TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 */
        0xc0, 0x30, /* TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 */
        0xc0, 0x13, /* TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA */
        0xc0, 0x14, /* TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA */
        0x00, 0x9c, /* TLS_RSA_WITH_AES_128_GCM_SHA256 */
        0x00, 0x9d, /* TLS_RSA_WITH_AES_256_GCM_SHA384 */
        0x00, 0x2f, /* TLS_RSA_WITH_AES_128_CBC_SHA */
        0x00, 0xff  /* TLS_EMPTY_RENEGOTIATION_INFO_SCSV */
    };
    static const uint8_t groups[] = {0x00, 0x17, 0x00, 0x18, 0x00, 0x19};
    static const uint8_t ec_points[] = {0x00};
    static const uint8_t sigalgs[] = {0x04, 0x01, 0x05, 0x01, 0x02, 0x01, 0x04, 0x03, 0x05, 0x03};
    static const uint8_t alpn_http11[] = {0x00, 0x08, 0x07, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    uint32_t p = 0;
    uint32_t record_len_pos;
    uint32_t handshake_len_pos;
    uint32_t handshake_start;
    uint32_t ext_len_pos;
    uint32_t ext_start;
    uint32_t sni_len = 0;
    uint32_t i;
    uint32_t seed = sched_time_ms();
    while (host && host[sni_len] && sni_len < 63u) sni_len++;
    if (!out || cap < 128u || sni_len == 0u) return -1;

    if (browser_tls_append_u8(out, cap, &p, 0x16) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 0x0301) != 0) return -1;
    record_len_pos = p;
    if (browser_tls_append_u16(out, cap, &p, 0) != 0) return -1;

    if (browser_tls_append_u8(out, cap, &p, 0x01) != 0) return -1;
    handshake_len_pos = p;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;
    handshake_start = p;

    if (browser_tls_append_u16(out, cap, &p, 0x0303) != 0) return -1;
    for (i = 0; i < 32u; i++) {
        seed = seed * 1103515245u + 12345u + i;
        if (browser_tls_append_u8(out, cap, &p, (uint8_t)(seed >> 16)) != 0) return -1;
    }
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sizeof(ciphers)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, ciphers, sizeof(ciphers)) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 1) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;

    ext_len_pos = p;
    if (browser_tls_append_u16(out, cap, &p, 0) != 0) return -1;
    ext_start = p;

    if (browser_tls_append_u16(out, cap, &p, 0x0000) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(5u + sni_len)) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(3u + sni_len)) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0x00) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sni_len) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, (const uint8_t *)host, sni_len) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x000a) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(2u + sizeof(groups))) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sizeof(groups)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, groups, sizeof(groups)) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x000b) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(1u + sizeof(ec_points))) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, (uint8_t)sizeof(ec_points)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, ec_points, sizeof(ec_points)) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x000d) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(2u + sizeof(sigalgs))) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sizeof(sigalgs)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, sigalgs, sizeof(sigalgs)) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x0010) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sizeof(alpn_http11)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, alpn_http11, sizeof(alpn_http11)) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x0016) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 0) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x0017) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 0) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0xff01) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 1) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x002b) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 3) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 2) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 0x0303) != 0) return -1;

    out[ext_len_pos] = (uint8_t)((p - ext_start) >> 8);
    out[ext_len_pos + 1u] = (uint8_t)((p - ext_start) & 0xffu);
    out[handshake_len_pos] = (uint8_t)((p - handshake_start) >> 16);
    out[handshake_len_pos + 1u] = (uint8_t)((p - handshake_start) >> 8);
    out[handshake_len_pos + 2u] = (uint8_t)((p - handshake_start) & 0xffu);
    out[record_len_pos] = (uint8_t)((p - 5u) >> 8);
    out[record_len_pos + 1u] = (uint8_t)((p - 5u) & 0xffu);
    return (int)p;
}

static void browser_append_hex4(char *dst, int *pos, int cap, uint16_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[5];
    tmp[0] = hex[(v >> 12) & 0xfu];
    tmp[1] = hex[(v >> 8) & 0xfu];
    tmp[2] = hex[(v >> 4) & 0xfu];
    tmp[3] = hex[v & 0xfu];
    tmp[4] = '\0';
    *pos = fp_str_append(dst, *pos, cap, tmp);
}

static void browser_render_https_probe(const char *host, const uint8_t *record, uint32_t len) {
    char line[160];
    int pos;
    tls_parser_summary_t summary;
    int parsed_records;
    uint8_t i;

    parsed_records = tls_parse_records(record, len, &summary);
    browser_clear_content();
    browser_set_widget_text(g_browser_content_lines[0], "HTTPS/TLS handshake summary.");

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Host: ");
    pos = fp_str_append(line, pos, sizeof(line), host ? host : "");
    browser_set_widget_text(g_browser_content_lines[1], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "TLS records parsed: ");
    pos = gui_append_uint(line, pos, sizeof(line), parsed_records > 0 ? (uint32_t)parsed_records : 0u);
    pos = fp_str_append(line, pos, sizeof(line), " type: ");
    pos = fp_str_append(line, pos, sizeof(line), tls_record_type_name(summary.record_type));
    browser_set_widget_text(g_browser_content_lines[2], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Record TLS version: 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.record_version);
    pos = fp_str_append(line, pos, sizeof(line), " length: ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.record_length);
    browser_set_widget_text(g_browser_content_lines[3], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Handshake list: ");
    for (i = 0; i < summary.handshake_count && i < TLS_PARSER_MAX_HANDSHAKES; i++) {
        if (i > 0) pos = fp_str_append(line, pos, sizeof(line), ", ");
        pos = fp_str_append(line, pos, sizeof(line), tls_handshake_type_name(summary.handshake_types[i]));
    }
    if (summary.handshake_count == 0u) pos = fp_str_append(line, pos, sizeof(line), "none");
    browser_set_widget_text(g_browser_content_lines[4], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Server: 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.server_version);
    pos = fp_str_append(line, pos, sizeof(line), " cipher: 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.cipher_suite);
    pos = fp_str_append(line, pos, sizeof(line), " extLen: ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.extensions_length);
    browser_set_widget_text(g_browser_content_lines[5], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Certificates: ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.certificate_count);
    pos = fp_str_append(line, pos, sizeof(line), " bytes: ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.certificate_bytes);
    browser_set_widget_text(g_browser_content_lines[6], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "ECDHE: curveType ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.key_exchange_curve_type);
    pos = fp_str_append(line, pos, sizeof(line), " namedCurve 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.key_exchange_named_curve);
    pos = fp_str_append(line, pos, sizeof(line), " pubKeyLen ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.key_exchange_public_key_length);
    browser_set_widget_text(g_browser_content_lines[7], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "ECDHE signature: alg 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.key_exchange_signature_algorithm);
    pos = fp_str_append(line, pos, sizeof(line), " sigLen ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.key_exchange_signature_length);
    browser_set_widget_text(g_browser_content_lines[8], line);

    if (summary.alert_level || summary.alert_description) {
        pos = 0;
        line[0] = '\0';
        pos = fp_str_append(line, pos, sizeof(line), "TLS alert level: ");
        pos = gui_append_uint(line, pos, sizeof(line), summary.alert_level);
        pos = fp_str_append(line, pos, sizeof(line), " description: ");
        pos = gui_append_uint(line, pos, sizeof(line), summary.alert_description);
        browser_set_widget_text(g_browser_content_lines[9], line);
    } else {
        browser_set_widget_text(g_browser_content_lines[9], "Next: derive ECDHE secret and TLS traffic keys.");
    }
    browser_set_widget_text(g_browser_content_lines[10], "HTTPS pages still need cipher/decrypt + Finished verify.");
}

static void browser_load_reset_connection(void) {
    if (g_browser_load.state != BROWSER_LOAD_IDLE && g_browser_load.conn >= 0) {
        net_tcp_close(g_browser_load.conn);
    }
    g_browser_load.conn = -1;
}

static void browser_load_finish(const char *status) {
    browser_load_reset_connection();
    g_browser_load.state = BROWSER_LOAD_IDLE;
    if (status) browser_set_status(status);
}

static int browser_load_timed_out(uint32_t timeout_ms) {
    return (uint32_t)(sched_time_ms() - g_browser_load.state_started_ms) >= timeout_ms;
}

static void browser_load_set_state(browser_load_state_t state) {
    g_browser_load.state = state;
    g_browser_load.state_started_ms = sched_time_ms();
}

static void browser_load_begin_connect(void) {
    uint32_t local_port = (g_browser_load.scheme == BROWSER_SCHEME_HTTPS ? 45000u : 43000u) +
                          (sched_time_ms() % 2000u);
    g_browser_load.conn = net_tcp_open(0, (uint16_t)local_port,
                                       g_browser_load.ip, g_browser_load.port, 1);
    if (g_browser_load.conn < 0) {
        browser_set_widget_text(g_browser_content_lines[0],
                                g_browser_load.scheme == BROWSER_SCHEME_HTTPS ?
                                "HTTPS TCP connection failed." : "TCP connection failed.");
        browser_load_finish("Connection failed");
        return;
    }
    browser_load_set_state(BROWSER_LOAD_CONNECTING);
}

static void browser_load_start(void) {
    int parse_result;

    browser_load_reset_connection();
    memset(&g_browser_load, 0, sizeof(g_browser_load));
    g_browser_load.conn = -1;

    browser_clear_content();
    browser_set_status("Loading...");
    browser_copy_url(g_browser_load.url, sizeof(g_browser_load.url),
                     g_browser_address_box ? g_browser_address_box->text : "http://example.com/");
    parse_result = browser_parse_url(g_browser_load.url,
                                     g_browser_load.host, sizeof(g_browser_load.host),
                                     g_browser_load.path, sizeof(g_browser_load.path),
                                     &g_browser_load.port, &g_browser_load.scheme);
    if (parse_result != 0) {
        browser_set_widget_text(g_browser_content_lines[0], "Invalid URL. Use http://host/path or https://host/path.");
        browser_load_finish("Invalid URL");
        return;
    }

    if (net_parse_ipv4(g_browser_load.host, &g_browser_load.ip) == 0) {
        browser_load_begin_connect();
        return;
    }
    if (dns_query_a(g_browser_load.host) != 0) {
        browser_set_widget_text(g_browser_content_lines[0], "DNS lookup failed or host is unreachable.");
        browser_load_finish("DNS failed");
        return;
    }
    browser_load_set_state(BROWSER_LOAD_RESOLVING);
}

static void browser_load_send_http(void) {
    int pos = 0;
    g_browser_load.request[0] = '\0';
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request), "GET ");
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request), g_browser_load.path);
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request), " HTTP/1.0\r\nHost: ");
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request), g_browser_load.host);
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request),
                        "\r\nConnection: close\r\nUser-Agent: OpenOSBrowser/0.1\r\n\r\n");
    (void)pos;
    if (net_tcp_send(g_browser_load.conn, (const uint8_t *)g_browser_load.request,
                     (uint16_t)strlen(g_browser_load.request)) != 0) {
        browser_set_widget_text(g_browser_content_lines[0], "Failed to send HTTP request.");
        browser_load_finish("Send failed");
        return;
    }
    g_browser_load.total = 0;
    browser_load_set_state(BROWSER_LOAD_HTTP_RECV);
}

static void browser_load_send_tls(void) {
    g_browser_load.tls_hello_len = browser_tls_build_client_hello(g_browser_load.host,
                                                                  g_browser_load.tls_hello,
                                                                  sizeof(g_browser_load.tls_hello));
    if (g_browser_load.tls_hello_len <= 0) {
        browser_set_widget_text(g_browser_content_lines[0], "Failed to build TLS ClientHello.");
        browser_load_finish("TLS setup failed");
        return;
    }
    if (net_tcp_send(g_browser_load.conn, g_browser_load.tls_hello,
                     (uint16_t)g_browser_load.tls_hello_len) != 0) {
        browser_set_widget_text(g_browser_content_lines[0], "Failed to send TLS ClientHello.");
        browser_load_finish("TLS send failed");
        return;
    }
    browser_load_set_state(BROWSER_LOAD_TLS_RECV);
}

static void browser_load_tick(void) {
    int state;
    int got;

    if (g_browser_load.state == BROWSER_LOAD_IDLE) return;
    net_poll();

    if (g_browser_load.state == BROWSER_LOAD_RESOLVING) {
        dns_state_t dns_state = dns_get_state();
        if (dns_state == DNS_STATE_RESOLVED) {
            g_browser_load.ip = dns_get_last_result();
            if (!g_browser_load.ip) {
                browser_set_widget_text(g_browser_content_lines[0], "DNS lookup returned no address.");
                browser_load_finish("DNS failed");
                return;
            }
            browser_load_begin_connect();
            return;
        }
        if (dns_state == DNS_STATE_FAILED || browser_load_timed_out(3000u)) {
            browser_set_widget_text(g_browser_content_lines[0], "DNS lookup failed or timed out.");
            browser_load_finish("DNS failed");
        }
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_CONNECTING) {
        state = net_tcp_state(g_browser_load.conn);
        if (state == NET_TCP_STATE_ESTABLISHED) {
            browser_load_set_state(g_browser_load.scheme == BROWSER_SCHEME_HTTPS ?
                                   BROWSER_LOAD_TLS_SEND : BROWSER_LOAD_HTTP_SEND);
        } else if (state < 0 || state == NET_TCP_STATE_CLOSED || browser_load_timed_out(5000u)) {
            browser_set_widget_text(g_browser_content_lines[0],
                                    g_browser_load.scheme == BROWSER_SCHEME_HTTPS ?
                                    "HTTPS TCP connection failed or timed out." :
                                    "TCP connection failed or timed out.");
            browser_load_finish("Connection failed");
        }
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_HTTP_SEND) {
        browser_load_send_http();
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_HTTP_RECV) {
        got = net_tcp_recv(g_browser_load.conn,
                           g_browser_load.response + g_browser_load.total,
                           (uint16_t)(sizeof(g_browser_load.response) - 1u - (uint32_t)g_browser_load.total));
        if (got > 0) {
            g_browser_load.total += got;
            g_browser_load.state_started_ms = sched_time_ms();
            if (g_browser_load.total >= (int)sizeof(g_browser_load.response) - 1) {
                g_browser_load.response[g_browser_load.total] = 0;
                (void)browser_render_response_summary((char *)g_browser_load.response,
                                                       browser_find_body((char *)g_browser_load.response));
                browser_load_finish("Done");
            }
            return;
        }
        state = net_tcp_state(g_browser_load.conn);
        if (state == NET_TCP_STATE_CLOSED || state == NET_TCP_STATE_CLOSE_WAIT) {
            g_browser_load.response[g_browser_load.total] = 0;
            if (g_browser_load.total <= 0) {
                browser_clear_content();
                browser_set_widget_text(g_browser_content_lines[0], "No HTTP response received before connection closed.");
                browser_load_finish("Timeout");
            } else {
                (void)browser_render_response_summary((char *)g_browser_load.response,
                                                       browser_find_body((char *)g_browser_load.response));
                browser_load_finish("Done");
            }
            return;
        }
        if (browser_load_timed_out(7000u)) {
            browser_clear_content();
            browser_set_widget_text(g_browser_content_lines[0], "No HTTP response received before timeout.");
            browser_load_finish("Timeout");
        }
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_TLS_SEND) {
        browser_load_send_tls();
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_TLS_RECV) {
        got = net_tcp_recv(g_browser_load.conn, g_browser_load.tls_record, sizeof(g_browser_load.tls_record));
        if (got > 0) {
            browser_render_https_probe(g_browser_load.host, g_browser_load.tls_record, (uint32_t)got);
            browser_load_finish("HTTPS TLS response received");
            return;
        }
        state = net_tcp_state(g_browser_load.conn);
        if (state == NET_TCP_STATE_CLOSED || state == NET_TCP_STATE_CLOSE_WAIT || browser_load_timed_out(6000u)) {
            browser_set_widget_text(g_browser_content_lines[0], "No TLS response received.");
            browser_load_finish("TLS no response");
        }
    }
}

static void browser_on_nav(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    browser_load_start();
}

static int browser_handle_address_enter(int key) {
    if (!g_browser_address_box || g_gui.focused_widget != g_browser_address_box) return 0;
    if (!gui_is_enter_key(key)) return 0;
    browser_load_start();
    return 1;
}

static void gui_browser_open(void) {
    int win_w = 620;
    int win_h = 420;
    uint32_t i;

    if (g_browser_win) {
        browser_load_finish(0);
        gui_window_set_on_close(g_browser_win, 0, 0);
        gui_destroy_window(g_browser_win);
        g_browser_win = 0;
        g_browser_address_box = 0;
        g_browser_status_label = 0;
        for (i = 0; i < GUI_BROWSER_CONTENT_LINES; i++) g_browser_content_lines[i] = 0;
    }

    g_browser_win = gui_create_window(120, 86, win_w, win_h, i18n_t(I18N_KEY_WIN_BROWSER));
    if (!g_browser_win) return;
    gui_window_set_on_close(g_browser_win, browser_on_close, 0);

    gui_add_button(g_browser_win, 14, 18, 34, 26, i18n_t(I18N_KEY_BROWSER_BACK), browser_on_nav, 0);
    gui_add_button(g_browser_win, 54, 18, 34, 26, i18n_t(I18N_KEY_BROWSER_FORWARD), browser_on_nav, 0);
    gui_add_button(g_browser_win, 94, 18, 70, 26, i18n_t(I18N_KEY_BROWSER_REFRESH), browser_on_nav, 0);
    gui_add_label(g_browser_win, 176, 24, 62, 16, i18n_t(I18N_KEY_BROWSER_ADDRESS));
    g_browser_address_box = gui_add_textbox(g_browser_win, 238, 18, 282, 26, "http://example.com/");
    gui_add_button(g_browser_win, 530, 18, 62, 26, i18n_t(I18N_KEY_BROWSER_GO), browser_on_nav, 0);

    gui_add_panel(g_browser_win, 14, 56, win_w - 28, win_h - 104, gui_rgb(246, 249, 253));
    g_browser_content_lines[0] = gui_add_label(g_browser_win, 34, 82, win_w - 68, 18, i18n_t(I18N_KEY_BROWSER_HOME_TITLE));
    g_browser_content_lines[1] = gui_add_label(g_browser_win, 34, 104, win_w - 68, 18, i18n_t(I18N_KEY_BROWSER_HOME_HINT));
    g_browser_content_lines[2] = gui_add_label(g_browser_win, 34, 126, win_w - 68, 18, i18n_t(I18N_KEY_BROWSER_STATUS_PLACEHOLDER));
    for (i = 3; i < GUI_BROWSER_CONTENT_LINES; i++) {
        g_browser_content_lines[i] = gui_add_label(g_browser_win, 34, 82 + (int)i * 22, win_w - 68, 18, "");
    }
    g_browser_status_label = gui_add_label(g_browser_win, 14, win_h - 34, win_w - 28, 16, i18n_t(I18N_KEY_BROWSER_STATUS_READY));

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

static void gui_settings_open(void) {
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

static void gui_wifi_open(void) {
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

/* === File Preview (enhanced) === */

#define GUI_FP_MAX_PATH        256
#define GUI_FP_MAX_NAME        64
#define GUI_FP_MAX_ENTRIES     256
#define GUI_FP_LIST_PER_PAGE   8
#define GUI_FP_VIEW_MAX_LINES  18
#define GUI_FP_VIEW_LINE_CHARS 56
#define GUI_FP_VIEW_BUF_SIZE   8192
#define GUI_FP_EDIT_MAX_LINES  16
#define GUI_FP_EDIT_LINE_CHARS 56

/* enhanced list layout: name | mtime | type | size */
#define GUI_FP_COL_NAME_X      28   /* after 18px icon + 4px gap */
#define GUI_FP_COL_MTIME_X     200
#define GUI_FP_COL_TYPE_X      300
#define GUI_FP_COL_SIZE_X      350
#define GUI_FP_ROW_HEIGHT      20
#define GUI_FP_ICON_SIZE       14


static char          fp_path[GUI_FP_MAX_PATH];
static int           fp_page = 0;
static int           fp_mode = 0;            /* 0=list, 1=view, 2=edit */
static char          fp_view_name[GUI_FP_MAX_NAME];
static int           fp_view_line_offset = 0; /* first visible line index in view mode */
static int           fp_view_total_lines = 0; /* cached total wrapped line count */
static gui_widget_t *fp_edit_textarea = 0;
static gui_widget_t *fp_edit_status = 0;
static int           fp_sort_key  = 0;        /* 0=name, 1=mtime, 2=size */
static int           fp_sort_desc = 0;        /* 0=asc, 1=desc */
static int           fp_sorted_idx[GUI_FP_MAX_ENTRIES];
static int           fp_sorted_count = 0;

/* Prompt overlay: 0=hidden, 1=new file, 2=new dir, 3=rename, 4=confirm delete */
static int           fp_prompt_mode = 0;
static char          fp_prompt_buf[GUI_FP_MAX_NAME];
static int           fp_prompt_len = 0;
static char          fp_prompt_target[GUI_FP_MAX_NAME]; /* original name for rename / delete */
static char          fp_status[80] = {0};
static int           fp_selected = -1;             /* index within current page of selected row */
static int           fp_last_click_global = -1;    /* double-click target in global item index */
static uint32_t      fp_last_click_frame = 0;
static gui_widget_t *fp_prompt_textbox = 0;        /* prompt input textbox */

static int fp_scale_i(int value) {
    return (int)font_scale_value((uint32_t)value);
}

static int fp_line_h(void) {
    int h = (int)font_get_line_height(font_get_default());
    return h > 0 ? h : 10;
}

static int fp_text_row_h(void) {
    return fp_line_h() + fp_scale_i(4);
}

static int fp_button_h(void) {
    int h = fp_line_h() + fp_scale_i(10);
    return h < 22 ? 22 : h;
}

static int fp_panel_gap(void) {
    int gap = fp_scale_i(8);
    return gap < 6 ? 6 : gap;
}

static int fp_view_window_h(void) {
    int h = fp_scale_i(500);
    if (h < 430) h = 430;
    if (h > 560) h = 560;
    return h;
}

static int fp_edit_window_h(void) {
    int h = fp_scale_i(520);
    if (h < 440) h = 440;
    if (h > 580) h = 580;
    return h;
}

static int fp_view_visible_lines(void) {
    int title = fp_line_h() + fp_scale_i(8);
    int nav = fp_button_h();
    int footer = fp_button_h() + fp_panel_gap();
    int available = fp_view_window_h() - GUI_TITLE_HEIGHT - fp_scale_i(16) - title - nav - footer;
    int row_h = fp_text_row_h();
    int lines = available / (row_h > 0 ? row_h : 1);
    if (lines < 4) lines = 4;
    if (lines > GUI_FP_VIEW_MAX_LINES) lines = GUI_FP_VIEW_MAX_LINES;
    return lines;
}

static int fp_edit_visible_lines(void) {
    int title = fp_line_h() + fp_scale_i(8);
    int status = fp_line_h() + fp_panel_gap();
    int footer = fp_button_h() + fp_panel_gap();
    int available = fp_edit_window_h() - GUI_TITLE_HEIGHT - fp_scale_i(16) - title - status - footer;
    int row_h = fp_text_row_h();
    int lines = available / (row_h > 0 ? row_h : 1);
    if (lines < 4) lines = 4;
    if (lines > GUI_FP_EDIT_MAX_LINES) lines = GUI_FP_EDIT_MAX_LINES;
    return lines;
}

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

static int gui_append_uint(char *dst, int pos, int cap, uint32_t v) {
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

static int gui_append_hex_byte(char *dst, int pos, int cap, uint8_t v) {
    static const char hex[] = "0123456789ABCDEF";
    if (pos < cap - 1) dst[pos++] = hex[(v >> 4) & 0x0f];
    if (pos < cap - 1) dst[pos++] = hex[v & 0x0f];
    if (cap > 0) dst[pos] = 0;
    return pos;
}

static void gui_format_ipv4_inline(uint32_t ip, char *buf, int cap) {
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

static void gui_format_mac_inline(const uint8_t mac[6], char *buf, int cap) {
    int pos = 0;
    int i;
    if (!buf || cap <= 0) return;
    for (i = 0; i < 6; i++) {
        if (i) pos = fp_str_append(buf, pos, cap, ":");
        pos = gui_append_hex_byte(buf, pos, cap, mac[i]);
    }
}

static int gui_settings_append_field(char *dst, int pos, int cap, i18n_key_t key, const char *value) {
    pos = fp_str_append(dst, pos, cap, i18n_t(key));
    pos = fp_str_append(dst, pos, cap, ": ");
    pos = fp_str_append(dst, pos, cap, value ? value : "");
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

/* lowercase compare last suffix; returns 1 if match */
static int fp_str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static const char *fp_ext(const char *name) {
    const char *dot = 0;
    const char *p = name;
    while (*p) { if (*p == '.') dot = p; p++; }
    return dot ? dot + 1 : "";
}

static gui_icon_id_t fp_pick_icon(const dentry_t *e) {
    const char *ext;
    if (!e) return GUI_ICON_FILE_GENERIC;
    if (fp_entry_is_dir(e)) return GUI_ICON_FOLDER;
    ext = fp_ext(e->name);
    if (!*ext) return GUI_ICON_FILE_GENERIC;
    if (fp_str_ieq(ext, "c") || fp_str_ieq(ext, "h") ||
        fp_str_ieq(ext, "cpp") || fp_str_ieq(ext, "hpp") ||
        fp_str_ieq(ext, "js") || fp_str_ieq(ext, "ts") ||
        fp_str_ieq(ext, "py") || fp_str_ieq(ext, "go") ||
        fp_str_ieq(ext, "rs") || fp_str_ieq(ext, "asm")) return GUI_ICON_FILE_CODE;
    if (fp_str_ieq(ext, "md")) return GUI_ICON_FILE_MARKUP;
    if (fp_str_ieq(ext, "txt") || fp_str_ieq(ext, "log") ||
        fp_str_ieq(ext, "readme")) return GUI_ICON_FILE_TEXT;
    if (fp_str_ieq(ext, "sh") || fp_str_ieq(ext, "bash")) return GUI_ICON_FILE_SHELL;
    if (fp_str_ieq(ext, "conf") || fp_str_ieq(ext, "cfg") ||
        fp_str_ieq(ext, "ini") || fp_str_ieq(ext, "json") ||
        fp_str_ieq(ext, "yaml") || fp_str_ieq(ext, "yml") ||
        fp_str_ieq(ext, "toml")) return GUI_ICON_FILE_CONFIG;
    if (fp_str_ieq(ext, "png") || fp_str_ieq(ext, "jpg") ||
        fp_str_ieq(ext, "jpeg") || fp_str_ieq(ext, "bmp") ||
        fp_str_ieq(ext, "gif") || fp_str_ieq(ext, "ico")) return GUI_ICON_FILE_IMAGE;
    if (fp_str_ieq(ext, "zip") || fp_str_ieq(ext, "tar") ||
        fp_str_ieq(ext, "gz") || fp_str_ieq(ext, "bz2") ||
        fp_str_ieq(ext, "xz") || fp_str_ieq(ext, "7z")) return GUI_ICON_FILE_ARCHIVE;
    if (fp_str_ieq(ext, "elf") || fp_str_ieq(ext, "exe") ||
        fp_str_ieq(ext, "bin") || fp_str_ieq(ext, "o") ||
        fp_str_ieq(ext, "a") || fp_str_ieq(ext, "so")) return GUI_ICON_FILE_EXEC;
    return GUI_ICON_FILE_GENERIC;
}

static const char *fp_type_label(const dentry_t *e) {
    const char *ext;
    if (!e) return "";
    if (fp_entry_is_dir(e)) return i18n_t(I18N_KEY_FILE_TYPE_FOLDER);
    ext = fp_ext(e->name);
    if (!*ext) return i18n_t(I18N_KEY_FILE_TYPE_FILE);
    if (fp_str_ieq(ext, "c") || fp_str_ieq(ext, "h")) return i18n_t(I18N_KEY_FILE_TYPE_C_SOURCE);
    if (fp_str_ieq(ext, "md")) return i18n_t(I18N_KEY_FILE_TYPE_MARKDOWN);
    if (fp_str_ieq(ext, "txt") || fp_str_ieq(ext, "log")) return i18n_t(I18N_KEY_FILE_TYPE_TEXT);
    if (fp_str_ieq(ext, "sh") || fp_str_ieq(ext, "bash")) return i18n_t(I18N_KEY_FILE_TYPE_SHELL);
    if (fp_str_ieq(ext, "json")) return "JSON";
    if (fp_str_ieq(ext, "conf") || fp_str_ieq(ext, "cfg") ||
        fp_str_ieq(ext, "ini") || fp_str_ieq(ext, "toml")) return i18n_t(I18N_KEY_FILE_TYPE_CONFIG);
    if (fp_str_ieq(ext, "yaml") || fp_str_ieq(ext, "yml")) return "YAML";
    if (fp_str_ieq(ext, "png") || fp_str_ieq(ext, "jpg") ||
        fp_str_ieq(ext, "jpeg") || fp_str_ieq(ext, "bmp") ||
        fp_str_ieq(ext, "gif") || fp_str_ieq(ext, "ico")) return i18n_t(I18N_KEY_FILE_TYPE_IMAGE);
    if (fp_str_ieq(ext, "zip") || fp_str_ieq(ext, "tar") ||
        fp_str_ieq(ext, "gz") || fp_str_ieq(ext, "bz2") ||
        fp_str_ieq(ext, "xz") || fp_str_ieq(ext, "7z")) return i18n_t(I18N_KEY_FILE_TYPE_ARCHIVE);
    if (fp_str_ieq(ext, "elf") || fp_str_ieq(ext, "exe") ||
        fp_str_ieq(ext, "bin") || fp_str_ieq(ext, "o") ||
        fp_str_ieq(ext, "a") || fp_str_ieq(ext, "so")) return i18n_t(I18N_KEY_FILE_TYPE_EXEC);
    return i18n_t(I18N_KEY_FILE_TYPE_FILE);
}

/* format file size like "1.2K", "3M" etc */
static void fp_format_size(uint32_t bytes, char *out) {
    char tmp[12];
    int i = 0, j = 0;
    uint32_t n;
    const char *unit = "B";
    if (bytes < 1024) {
        n = bytes;
        unit = "B";
    } else if (bytes < 1024u * 1024u) {
        n = bytes / 1024u;
        unit = "K";
    } else if (bytes < 1024u * 1024u * 1024u) {
        n = bytes / (1024u * 1024u);
        unit = "M";
    } else {
        n = bytes / (1024u * 1024u * 1024u);
        unit = "G";
    }
    if (n == 0) tmp[i++] = '0';
    while (n > 0) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i > 0) out[j++] = tmp[--i];
    while (*unit) out[j++] = *unit++;
    out[j] = 0;
}

static void fp_pad2(char *out, int *pos, int v) {
    out[(*pos)++] = (char)('0' + ((v / 10) % 10));
    out[(*pos)++] = (char)('0' + (v % 10));
}

/* format mtime like "2026-06-17 16:44" or "--" if not set */
static void fp_format_mtime(const vfs_time_t *t, char *out) {
    int pos = 0;
    if (!t || t->year == 0) {
        out[0] = '-'; out[1] = '-'; out[2] = 0;
        return;
    }
    out[pos++] = (char)('0' + ((t->year / 1000) % 10));
    out[pos++] = (char)('0' + ((t->year / 100) % 10));
    out[pos++] = (char)('0' + ((t->year / 10) % 10));
    out[pos++] = (char)('0' + (t->year % 10));
    out[pos++] = '-';
    fp_pad2(out, &pos, t->month);
    out[pos++] = '-';
    fp_pad2(out, &pos, t->day);
    out[pos++] = ' ';
    fp_pad2(out, &pos, t->hour);
    out[pos++] = ':';
    fp_pad2(out, &pos, t->minute);
    out[pos] = 0;
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

static int fp_row_height(void) {
    int text_h = gui_text_line_height_px();
    int h = text_h + 6;
    if (h < GUI_FP_ROW_HEIGHT) h = GUI_FP_ROW_HEIGHT;
    return h;
}

static int fp_list_per_page(void) {
    int text_h = gui_text_line_height_px();
    int label_h = text_h + 2;
    int button_h = text_h + 6;
    int header_h;
    int row_h = fp_row_height();
    int path_y = 28;
    int nav_y;
    int header_y;
    int sep_y;
    int list_y;
    int prompt_h;
    int usable_h;
    int window_h;
    int rows;

    if (button_h < 20) button_h = 20;
    header_h = button_h;
    nav_y = path_y + label_h + 4;
    header_y = nav_y + button_h + 4;
    sep_y = header_y + header_h + 1;
    list_y = sep_y + 3;
    prompt_h = (fp_prompt_mode == 0) ? label_h : button_h;
    window_h = fp_window ? fp_window->rect.h : 430;

    /* Leave room for toolbar, status/prompt controls and a bottom margin.
     * New File/New Directory/Rename prompts add a textbox plus OK/Cancel
     * buttons at the bottom, so fixed list height can push them outside the
     * window when the default font is scaled up. */
    usable_h = window_h - list_y - button_h - prompt_h - 30;
    if (usable_h < row_h * 3) usable_h = row_h * 3;

    rows = usable_h / row_h;
    if (rows < 3) rows = 3;
    if (rows > GUI_FP_LIST_PER_PAGE) rows = GUI_FP_LIST_PER_PAGE;
    return rows;
}

typedef struct fp_list_layout {
    int x;
    int w;
    int name_x;
    int name_w;
    int mtime_x;
    int mtime_w;
    int type_x;
    int type_w;
    int size_x;
    int size_w;
    int sep_name;
    int sep_mtime;
    int sep_type;
} fp_list_layout_t;

typedef void (*gui_table_sort_handler_t)(gui_widget_t *w, void *ud);
typedef void (*gui_table_row_handler_t)(gui_widget_t *w, void *ud);

typedef struct gui_table_view_column {
    int x;
    int w;
    int sep_x;
    const char *title;
    int sort_key;
    int sortable;
} gui_table_view_column_t;

typedef struct gui_table_view {
    gui_window_t *window;
    int x;
    int y;
    int w;
    int header_h;
    int row_y;
    int row_h;
    int rows;
    int sort_key;
    int sort_desc;
    int selected_row;
    int column_count;
    gui_table_view_column_t columns[4];
    gui_table_sort_handler_t on_sort;
    gui_table_row_handler_t on_row;
} gui_table_view_t;

static int gui_text_width_px(const char *text) {
    int w;
    if (!text) return 0;
    w = (int)font_measure_text_width(font_get_default(), text);
    return w > 0 ? w : (int)strlen(text) * GUI_CHAR_W;
}

static int fp_button_width_for(const char *text, int min_w) {
    int w = gui_text_width_px(text) + 18;
    return w < min_w ? min_w : w;
}

static int fp_default_window_w(void) {
    int pad = fp_scale_i(8);
    int icon_gap = fp_scale_i(24);
    int col_gap = fp_scale_i(8);
    int name_w;
    int mtime_w;
    int type_w;
    int size_w;
    int w;
    if (pad < 6) pad = 6;
    if (icon_gap < 24) icon_gap = 24;
    if (col_gap < 6) col_gap = 6;

    name_w = gui_text_width_px("hello-long-name.txt") + icon_gap + col_gap;
    mtime_w = gui_text_width_px("2026-06-19 23:59") + col_gap * 2;
    type_w = gui_text_width_px(i18n_t(I18N_KEY_FILE_TYPE_FOLDER)) + col_gap * 2;
    size_w = gui_text_width_px("9999K") + col_gap * 2;

    if (name_w < fp_scale_i(160)) name_w = fp_scale_i(160);
    if (mtime_w < fp_scale_i(156)) mtime_w = fp_scale_i(156);
    if (type_w < fp_scale_i(76)) type_w = fp_scale_i(76);
    if (size_w < fp_scale_i(58)) size_w = fp_scale_i(58);

    w = pad * 2 + icon_gap + name_w + mtime_w + type_w + size_w;
    if (w < 560) w = 560;
    if (w > 900) w = 900;
    return w;
}

static void fp_compute_list_layout(fp_list_layout_t *l) {
    int inner_w;
    int pad;
    int icon_gap;
    int col_gap;
    int name_min;
    int mtime_min;
    int type_min;
    int size_min;
    int fixed_w;
    int remaining;
    if (!l) return;

    pad = fp_scale_i(8);
    if (pad < 6) pad = 6;
    col_gap = fp_scale_i(8);
    if (col_gap < 6) col_gap = 6;
    icon_gap = fp_scale_i(24);
    if (icon_gap < 24) icon_gap = 24;

    l->x = pad;
    inner_w = fp_window ? fp_window->rect.w - pad * 2 : fp_default_window_w() - pad * 2;
    if (inner_w < fp_scale_i(320)) inner_w = fp_scale_i(320);
    l->w = inner_w;

    name_min = gui_text_width_px(i18n_t(I18N_KEY_FILE_COL_NAME)) + icon_gap + col_gap;
    mtime_min = gui_text_width_px("2026-06-19 23:59") + col_gap * 2;
    type_min = gui_text_width_px(i18n_t(I18N_KEY_FILE_TYPE_FOLDER)) + col_gap * 2;
    size_min = gui_text_width_px(i18n_t(I18N_KEY_FILE_COL_SIZE)) + col_gap * 2;
    if (name_min < fp_scale_i(132)) name_min = fp_scale_i(132);
    if (mtime_min < fp_scale_i(156)) mtime_min = fp_scale_i(156);
    if (type_min < fp_scale_i(76)) type_min = fp_scale_i(76);
    if (size_min < fp_scale_i(58)) size_min = fp_scale_i(58);

    fixed_w = icon_gap + mtime_min + type_min + size_min;
    remaining = inner_w - fixed_w;
    if (remaining < name_min) {
        remaining = name_min;
        l->w = fixed_w + remaining;
    }

    l->name_x = l->x + col_gap / 2;
    l->name_w = remaining;
    l->sep_name = l->x + icon_gap + l->name_w;
    l->mtime_x = l->sep_name + col_gap;
    l->mtime_w = mtime_min - col_gap;
    l->sep_mtime = l->mtime_x + l->mtime_w + col_gap / 2;
    l->type_x = l->sep_mtime + col_gap;
    l->type_w = type_min - col_gap;
    l->sep_type = l->type_x + l->type_w + col_gap / 2;
    l->size_x = l->sep_type + col_gap;
    l->size_w = l->x + l->w - l->size_x - col_gap / 2;
    if (l->size_w < size_min / 2) l->size_w = size_min / 2;
}

static void gui_table_view_draw_header(gui_table_view_t *table) {
    int i;
    if (!table || !table->window) return;
    gui_add_button(table->window, table->columns[0].x, table->y + 2,
                   table->columns[0].w, table->header_h - 4,
                   table->columns[0].title, table->on_sort, (void*)(intptr_t)table->columns[0].sort_key);
    for (i = 1; i < table->column_count; i++) {
        const gui_table_view_column_t *col = &table->columns[i];
        gui_add_button(table->window, col->x, table->y + 2,
                       col->w, table->header_h - 4,
                       col->title, col->sortable ? table->on_sort : 0,
                       (void*)(intptr_t)col->sort_key);
        gui_raw_line(col->sep_x, table->y + 4, col->sep_x, table->y + table->header_h - 6,
                     gui_rgb(170, 185, 205));
    }
    gui_raw_line(table->x, table->y + table->header_h - 1,
                 table->x + table->w, table->y + table->header_h - 1,
                 gui_rgb(160, 175, 195));
}

static void gui_table_view_draw_row(gui_table_view_t *table, int row, int global_index) {
    uint32_t bg;
    uint32_t border;
    int y;
    if (!table || !table->window) return;
    y = table->row_y + row * table->row_h;
    bg = (row == table->selected_row) ? gui_rgb(215, 232, 255) :
         ((row & 1) ? gui_rgb(248, 250, 252) : gui_rgb(240, 244, 248));
    border = (row == table->selected_row) ? gui_rgb(90, 130, 190) : gui_rgb(210, 218, 228);
    gui_raw_fill_rect(table->x, y, table->w, table->row_h - 1, bg);
    gui_raw_line(table->x, y + table->row_h - 1, table->x + table->w, y + table->row_h - 1, border);
    gui_add_button(table->window, table->x, y, table->w, table->row_h - 1,
                   "", table->on_row, (void*)(intptr_t)global_index);
}

static void gui_table_view_draw_text_cell(gui_table_view_t *table, int col, int y, const char *text, uint32_t color) {
    if (!table || col < 0 || col >= table->column_count) return;
    gui_draw_text(table->columns[col].x, y, text, color);
}

static int fp_total_pages(void) {
    int n = fp_total_items();
    int per_page = fp_list_per_page();
    if (n <= 0) return 1;
    return (n + per_page - 1) / per_page;
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

/* string compare, case-insensitive ------------------------------- */
static int fp_str_cmp_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* vfs_time compare: returns negative/0/positive -------------------- */
static int fp_time_cmp(const vfs_time_t *a, const vfs_time_t *b) {
    if (a->year   != b->year)   return (int)a->year   - (int)b->year;
    if (a->month  != b->month)  return (int)a->month  - (int)b->month;
    if (a->day    != b->day)    return (int)a->day    - (int)b->day;
    if (a->hour   != b->hour)   return (int)a->hour   - (int)b->hour;
    if (a->minute != b->minute) return (int)a->minute - (int)b->minute;
    return (int)a->second - (int)b->second;
}

/* compare two real entries by current sort key -------------------- */
static int fp_compare_real(int ia, int ib) {
    dentry_t *ea = fp_get_real_entry(ia);
    dentry_t *eb = fp_get_real_entry(ib);
    int da, db, r = 0;
    if (!ea || !eb) return 0;
    /* directories always come before files */
    da = fp_entry_is_dir(ea) ? 1 : 0;
    db = fp_entry_is_dir(eb) ? 1 : 0;
    if (da != db) return db - da;  /* dir(1) before file(0) */
    switch (fp_sort_key) {
    case 1: /* mtime */
        if (ea->inode && eb->inode) {
            r = fp_time_cmp(&ea->inode->mtime, &eb->inode->mtime);
        }
        if (r == 0) r = fp_str_cmp_ci(ea->name, eb->name);
        break;
    case 2: /* size */
        if (ea->inode && eb->inode) {
            if (ea->inode->size < eb->inode->size) r = -1;
            else if (ea->inode->size > eb->inode->size) r = 1;
            else r = 0;
        }
        if (r == 0) r = fp_str_cmp_ci(ea->name, eb->name);
        break;
    default: /* name */
        r = fp_str_cmp_ci(ea->name, eb->name);
        break;
    }
    return fp_sort_desc ? -r : r;
}

/* build sorted index over real entries (excludes leading "..") --- */
static void fp_build_sorted_index(void) {
    int n = fp_count_entries();
    int i, j;
    if (n > GUI_FP_MAX_ENTRIES) n = GUI_FP_MAX_ENTRIES;
    for (i = 0; i < n; i++) fp_sorted_idx[i] = i;
    /* simple insertion sort, stable, fine for n <= 256 */
    for (i = 1; i < n; i++) {
        int key = fp_sorted_idx[i];
        j = i - 1;
        while (j >= 0 && fp_compare_real(fp_sorted_idx[j], key) > 0) {
            fp_sorted_idx[j + 1] = fp_sorted_idx[j];
            j--;
        }
        fp_sorted_idx[j + 1] = key;
    }
    fp_sorted_count = n;
}

/* fetch the Nth real entry under current sort order --------------- */
static dentry_t *fp_get_sorted_real_entry(int target) {
    if (target < 0 || target >= fp_sorted_count) return 0;
    return fp_get_real_entry(fp_sorted_idx[target]);
}

/* callbacks ------------------------------------------------------ */
static void fp_on_back(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    fp_mode = 0;
    fp_page = 0;
    fp_view_line_offset = 0;
    fp_view_total_lines = 0;
    gui_file_preview_rebuild();
}

static void fp_on_view_up(gui_widget_t *w, void *ud) {
    int visible = fp_view_visible_lines();
    (void)w; (void)ud;
    if (fp_view_line_offset > 0) {
        fp_view_line_offset -= visible;
        if (fp_view_line_offset < 0) fp_view_line_offset = 0;
        gui_file_preview_rebuild();
    }
}

static void fp_on_view_down(gui_widget_t *w, void *ud) {
    int visible = fp_view_visible_lines();
    (void)w; (void)ud;
    if (fp_view_line_offset + visible < fp_view_total_lines) {
        fp_view_line_offset += visible;
        gui_file_preview_rebuild();
    }
}

static void fp_on_edit_enter(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    fp_mode = 2;
    gui_file_preview_rebuild();
}

static void fp_on_edit_cancel(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    fp_mode = 1;
    gui_file_preview_rebuild();
}

static void fp_on_edit_save(gui_widget_t *w, void *ud) {
    char full[GUI_FP_MAX_PATH];
    const char *src;
    int total = 0;
    int fd;
    int written = 0;
    (void)w; (void)ud;

    src = fp_edit_textarea ? fp_edit_textarea->text : "";
    while (src[total]) total++;

    fp_path_join(fp_path, fp_view_name, full, sizeof(full));
    fd = vfs_open(full, O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        if (fp_edit_status) gui_widget_set_text(fp_edit_status, "Save failed: open");
        gui_render();
        return;
    }
    while (written < total) {
        int n = vfs_write(fd, src + written, total - written);
        if (n <= 0) break;
        written += n;
    }
    vfs_close(fd);
    if (written < total) {
        if (fp_edit_status) gui_widget_set_text(fp_edit_status, "Save failed: write");
        gui_render();
        return;
    }
    /* success: return to view mode */
    {
        char msg[GUI_NOTIF_TEXT_LEN];
        int mp = 0;
        mp = fp_str_append(msg, mp, sizeof(msg), i18n_t(I18N_KEY_STATUS_SAVED_PREFIX));
        mp = fp_str_append(msg, mp, sizeof(msg), fp_view_name);
        (void)mp;
        gui_notify(msg);
    }
    fp_mode = 1;
    fp_view_line_offset = 0;
    gui_file_preview_rebuild();
}

static void fp_clear_entry_click_state(void) {
    fp_selected = -1;
    fp_last_click_global = -1;
    fp_last_click_frame = 0;
}

static void fp_on_prev(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (fp_page > 0) {
        fp_page--;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
    }
}

static void fp_on_next(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (fp_page + 1 < fp_total_pages()) {
        fp_page++;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
    }
}

static void fp_open_entry_at_global(int global_index) {
    int real_index;
    dentry_t *e;

    /* item 0 in non-root is the ".." shortcut */
    if (!fp_is_root() && global_index == 0) {
        fp_path_pop();
        fp_page = 0;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
        return;
    }

    real_index = fp_is_root() ? global_index : (global_index - 1);
    e = fp_get_sorted_real_entry(real_index);
    if (!e) return;

    if (fp_entry_is_dir(e)) {
        fp_path_push(e->name);
        fp_page = 0;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
    } else {
        int i = 0;
        while (e->name[i] && i < GUI_FP_MAX_NAME - 1) {
            fp_view_name[i] = e->name[i];
            i++;
        }
        fp_view_name[i] = 0;
        fp_mode = 1;
        fp_view_line_offset = 0;
        fp_view_total_lines = 0;
        fp_last_click_global = -1;
        fp_last_click_frame = 0;
        gui_file_preview_rebuild();
    }
}

static void fp_on_entry(gui_widget_t *w, void *ud) {
    int slot = (int)(intptr_t)ud;
    int global_index;
    int is_double_click;
    (void)w;

    /* Single click selects only. Double click opens the selected item. */
    fp_selected = slot;
    global_index = fp_page * fp_list_per_page() + slot;
    is_double_click = (fp_last_click_global == global_index &&
                       fp_last_click_frame != 0 &&
                       (g_gui.frame_counter - fp_last_click_frame) < 18);
    fp_last_click_global = global_index;
    fp_last_click_frame = g_gui.frame_counter;

    if (is_double_click) {
        fp_open_entry_at_global(global_index);
    } else {
        gui_file_preview_rebuild();
    }
}

/* sort column click: same column -> toggle direction; new column -> reset to asc */
static void fp_on_sort(gui_widget_t *w, void *ud) {
    int key = (int)(intptr_t)ud;
    (void)w;
    if (key == fp_sort_key) {
        fp_sort_desc = !fp_sort_desc;
    } else {
        fp_sort_key = key;
        fp_sort_desc = 0;
    }
    fp_page = 0;
    gui_file_preview_rebuild();
}

/* ---- File operation helpers ---------------------------------- */
static void fp_status_set(const char *s) {
    int i;
    for (i = 0; i < 79 && s[i]; i++) fp_status[i] = s[i];
    fp_status[i] = 0;
}

static void fp_join_full(const char *name, char *out, int out_sz) {
    int i = 0, j;
    while (i < out_sz - 1 && fp_path[i]) { out[i] = fp_path[i]; i++; }
    if (i > 0 && out[i-1] != '/' && i < out_sz - 1) out[i++] = '/';
    for (j = 0; name[j] && i < out_sz - 1; j++) out[i++] = name[j];
    out[i] = 0;
}

static int fp_name_exists(const char *name) {
    dentry_t *e;
    int i = 0;
    if (!name[0]) return 1;
    while ((e = vfs_readdir(fp_path, i++)) != 0) {
        const char *n = e->name;
        int k;
        for (k = 0; n[k] && name[k] && n[k] == name[k]; k++) {}
        if (n[k] == 0 && name[k] == 0) return 1;
    }
    return 0;
}

static int fp_name_is_valid(const char *name) {
    int i;
    if (!name[0]) return 0;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) return 0;
    for (i = 0; name[i]; i++) {
        char c = name[i];
        if (c == '/' || c == 0 || (unsigned char)c < 32) return 0;
    }
    return 1;
}

static void fp_prompt_open(int mode, const char *initial) {
    int i;
    fp_prompt_mode = mode;
    fp_prompt_len = 0;
    fp_prompt_buf[0] = 0;
    fp_prompt_target[0] = 0;
    if (initial) {
        for (i = 0; i < GUI_FP_MAX_NAME - 1 && initial[i]; i++) {
            fp_prompt_buf[i] = initial[i];
            fp_prompt_target[i] = initial[i];
        }
        fp_prompt_buf[i] = 0;
        fp_prompt_target[i] = 0;
        fp_prompt_len = i;
    }
    gui_file_preview_rebuild();
}

static void fp_prompt_close(void) {
    fp_prompt_mode = 0;
    fp_prompt_len = 0;
    fp_prompt_buf[0] = 0;
    fp_prompt_target[0] = 0;
    gui_file_preview_rebuild();
}

static void fp_action_new_file(const char *name) {
    char full[GUI_FP_MAX_PATH];
    int fd;
    if (!fp_name_is_valid(name)) { fp_status_set(i18n_t(I18N_KEY_STATUS_INVALID_NAME)); return; }
    if (fp_name_exists(name))    { fp_status_set(i18n_t(I18N_KEY_STATUS_ALREADY_EXISTS)); return; }
    fp_join_full(name, full, sizeof(full));
    fd = vfs_open(full, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { fp_status_set(i18n_t(I18N_KEY_STATUS_CREATE_FAILED)); return; }
    vfs_close(fd);
    fp_status_set(i18n_t(I18N_KEY_STATUS_FILE_CREATED));
    gui_notify(i18n_t(I18N_KEY_STATUS_FILE_CREATED));
}

static void fp_action_new_dir(const char *name) {
    char full[GUI_FP_MAX_PATH];
    if (!fp_name_is_valid(name)) { fp_status_set(i18n_t(I18N_KEY_STATUS_INVALID_NAME)); return; }
    if (fp_name_exists(name))    { fp_status_set(i18n_t(I18N_KEY_STATUS_ALREADY_EXISTS)); return; }
    fp_join_full(name, full, sizeof(full));
    if (vfs_mkdir(full, 0755) < 0) { fp_status_set(i18n_t(I18N_KEY_STATUS_MKDIR_FAILED)); return; }
    fp_status_set(i18n_t(I18N_KEY_STATUS_DIR_CREATED));
    gui_notify(i18n_t(I18N_KEY_STATUS_DIR_CREATED));
}

static void fp_action_rename(const char *old_name, const char *new_name) {
    char src[GUI_FP_MAX_PATH];
    char dst[GUI_FP_MAX_PATH];
    if (!fp_name_is_valid(new_name)) { fp_status_set(i18n_t(I18N_KEY_STATUS_INVALID_NAME)); return; }
    if (fp_name_exists(new_name))    { fp_status_set(i18n_t(I18N_KEY_STATUS_TARGET_EXISTS)); return; }
    fp_join_full(old_name, src, sizeof(src));
    fp_join_full(new_name, dst, sizeof(dst));
    if (vfs_rename(src, dst) < 0) { fp_status_set(i18n_t(I18N_KEY_STATUS_RENAME_FAILED)); return; }
    fp_status_set(i18n_t(I18N_KEY_STATUS_RENAMED));
    gui_notify(i18n_t(I18N_KEY_STATUS_RENAMED));
}

static void fp_action_delete(const char *name, int is_dir) {
    char full[GUI_FP_MAX_PATH];
    int r;
    fp_join_full(name, full, sizeof(full));
    r = is_dir ? vfs_rmdir(full) : vfs_unlink(full);
    if (r < 0) { fp_status_set(is_dir ? i18n_t(I18N_KEY_STATUS_RMDIR_FAILED) : i18n_t(I18N_KEY_STATUS_DELETE_FAILED)); return; }
    fp_status_set(i18n_t(I18N_KEY_STATUS_DELETED));
    gui_notify(i18n_t(I18N_KEY_STATUS_DELETED));
}

/* Detect if fp_prompt_target refers to a directory in current dir */
static int fp_target_is_dir(void) {
    int i;
    dentry_t *e;
    for (i = 0; ; i++) {
        e = vfs_readdir(fp_path, i);
        if (!e) break;
        if (e->name[0]) {
            const char *a = e->name;
            const char *b = fp_prompt_target;
            int k;
            for (k = 0; a[k] && b[k] && a[k] == b[k]; k++) {}
            if (a[k] == 0 && b[k] == 0) {
                return (e->inode && (e->inode->mode & FS_DIR)) ? 1 : 0;
            }
        }
    }
    return 0;
}

static void fp_prompt_submit(void) {
    int mode = fp_prompt_mode;
    /* sync textbox content -> fp_prompt_buf */
    if (fp_prompt_textbox && (mode == 1 || mode == 2 || mode == 3)) {
        int i;
        for (i = 0; i < (int)sizeof(fp_prompt_buf) - 1 && fp_prompt_textbox->text[i]; i++) {
            fp_prompt_buf[i] = fp_prompt_textbox->text[i];
        }
        fp_prompt_buf[i] = 0;
        fp_prompt_len = i;
    }
    if (mode == 4) {
        /* delete confirm: target already set */
        if (fp_prompt_target[0]) fp_action_delete(fp_prompt_target, fp_target_is_dir());
        fp_prompt_close();
        return;
    }
    if (!fp_prompt_buf[0]) { fp_prompt_close(); return; }
    if      (mode == 1) fp_action_new_file(fp_prompt_buf);
    else if (mode == 2) fp_action_new_dir(fp_prompt_buf);
    else if (mode == 3) fp_action_rename(fp_prompt_target, fp_prompt_buf);
    fp_prompt_close();
}

/* Toolbar callbacks ---------------------------------------------- */
static void fp_get_selected_name(char *out, int out_sz) {
    int gidx, real_index;
    dentry_t *e;
    int i;
    out[0] = 0;
    if (fp_selected < 0) return;
    gidx = fp_page * fp_list_per_page() + fp_selected;
    if (gidx >= fp_total_items()) return;
    if (!fp_is_root() && gidx == 0) return; /* '..' not selectable */
    real_index = fp_is_root() ? gidx : (gidx - 1);
    e = fp_get_sorted_real_entry(real_index);
    if (!e || !e->name[0]) return;
    for (i = 0; e->name[i] && i < out_sz - 1; i++) out[i] = e->name[i];
    out[i] = 0;
}

static void fp_on_tb_new_file(gui_widget_t *w, void *ud) { (void)w; (void)ud; fp_prompt_open(1, ""); gui_file_preview_rebuild(); }
static void fp_on_tb_new_dir(gui_widget_t *w, void *ud)  { (void)w; (void)ud; fp_prompt_open(2, ""); gui_file_preview_rebuild(); }
static void fp_on_tb_rename(gui_widget_t *w, void *ud) {
    char name[GUI_FP_MAX_NAME];
    (void)w; (void)ud;
    fp_get_selected_name(name, sizeof(name));
    fp_prompt_open(3, name);
    if (!name[0]) fp_status_set(i18n_t(I18N_KEY_STATUS_ENTER_TARGET));
    gui_file_preview_rebuild();
}
static void fp_on_tb_delete(gui_widget_t *w, void *ud) {
    char name[GUI_FP_MAX_NAME];
    (void)w; (void)ud;
    fp_get_selected_name(name, sizeof(name));
    if (!name[0]) { fp_status_set(i18n_t(I18N_KEY_STATUS_CLICK_FILE_FIRST)); gui_file_preview_rebuild(); return; }
    fp_prompt_open(4, name);
    gui_file_preview_rebuild();
}
static void fp_on_tb_refresh(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    fp_status_set(i18n_t(I18N_KEY_STATUS_REFRESHED));
    gui_file_preview_rebuild();
}
static void fp_on_prompt_ok(gui_widget_t *w, void *ud)     { (void)w; (void)ud; fp_prompt_submit(); gui_file_preview_rebuild(); }
static void fp_on_prompt_cancel(gui_widget_t *w, void *ud) { (void)w; (void)ud; fp_prompt_close(); gui_file_preview_rebuild(); }

/* render list mode ---------------------------------------------- */
static void gui_file_preview_render_list(void) {
    char header[GUI_FP_MAX_PATH + 16];
    char pageinfo[32];
    char buf[12];
    gui_widget_t *btn;
    gui_widget_t *lbl;
    int y, slot, total_pages, total_items, base;
    int pos;
    int text_h, label_h, button_h, header_h, row_h, list_count;
    int path_y, nav_y, page_y, header_y, sep_y, list_y, toolbar_y, status_y;
    fp_list_layout_t layout;
    gui_table_view_t table;

    if (!fp_window) return;

    /* build sorted index for current directory */
    fp_build_sorted_index();

    text_h = gui_text_line_height_px();
    label_h = text_h + 2;
    button_h = text_h + 6;
    if (button_h < 20) button_h = 20;
    header_h = button_h;
    row_h = fp_row_height();
    list_count = fp_list_per_page();
    path_y = 28;
    nav_y = path_y + label_h + 4;
    page_y = gui_text_center_y(nav_y, button_h);
    header_y = nav_y + button_h + 4;
    sep_y = header_y + header_h + 1;
    list_y = sep_y + 3;
    toolbar_y = list_y + list_count * row_h + 8;
    status_y = toolbar_y + button_h + 6;
    fp_compute_list_layout(&layout);
    table.window = fp_window;
    table.x = layout.x;
    table.y = header_y;
    table.w = layout.w;
    table.header_h = header_h;
    table.row_y = list_y;
    table.row_h = row_h;
    table.rows = list_count;
    table.sort_key = fp_sort_key;
    table.sort_desc = fp_sort_desc;
    table.selected_row = fp_selected;
    table.column_count = 4;
    table.on_sort = fp_on_sort;
    table.on_row = fp_on_entry;
    table.columns[0].x = layout.x;
    table.columns[0].w = layout.sep_name - layout.x;
    table.columns[0].sep_x = layout.sep_name;
    table.columns[0].title = 0;
    table.columns[0].sort_key = 0;
    table.columns[0].sortable = 1;
    table.columns[1].x = layout.mtime_x - 4;
    table.columns[1].w = layout.sep_mtime - layout.mtime_x + 4;
    table.columns[1].sep_x = layout.sep_name;
    table.columns[1].title = 0;
    table.columns[1].sort_key = 1;
    table.columns[1].sortable = 1;
    table.columns[2].x = layout.type_x;
    table.columns[2].w = layout.type_w;
    table.columns[2].sep_x = layout.sep_mtime;
    table.columns[2].title = 0;
    table.columns[2].sort_key = -1;
    table.columns[2].sortable = 0;
    table.columns[3].x = layout.size_x - 4;
    table.columns[3].w = layout.x + layout.w - layout.size_x + 4;
    table.columns[3].sep_x = layout.sep_type;
    table.columns[3].title = 0;
    table.columns[3].sort_key = 2;
    table.columns[3].sortable = 1;

    /* path header */
    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_HEADER_PATH));
    pos = fp_str_append(header, pos, sizeof(header), fp_path);
    (void)pos;
    gui_add_label(fp_window, layout.x, path_y, layout.w, label_h, header);

    /* nav buttons */
    {
        int prev_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_PREV), 60);
        int next_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_NEXT), 60);
        int nav_gap = 8;
        btn = gui_add_button(fp_window, layout.x, nav_y, prev_w, button_h, i18n_t(I18N_KEY_BTN_PREV), fp_on_prev, 0);
        (void)btn;
        btn = gui_add_button(fp_window, layout.x + prev_w + nav_gap, nav_y, next_w, button_h, i18n_t(I18N_KEY_BTN_NEXT), fp_on_next, 0);
        (void)btn;
    }

    total_pages = fp_total_pages();
    total_items = fp_total_items();
    pos = 0;
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), i18n_t(I18N_KEY_PAGE));
    fp_itoa(fp_page + 1, buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), i18n_t(I18N_KEY_PAGE_OF));
    fp_itoa(total_pages, buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), i18n_t(I18N_KEY_PAGE_OPEN_PAREN));
    fp_itoa(total_items, buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), buf);
    pos = fp_str_append(pageinfo, pos, sizeof(pageinfo), i18n_t(I18N_KEY_PAGE_ITEMS));
    (void)pos;
    {
        int prev_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_PREV), 60);
        int next_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_NEXT), 60);
        int nav_gap = 8;
        int page_x = layout.x + prev_w + next_w + nav_gap * 2;
        int page_w = layout.x + layout.w - page_x;
        if (page_w < 80) page_w = 80;
        gui_add_label(fp_window, page_x, page_y, page_w, label_h, pageinfo);
    }

    /* toolbar: New File | New Dir | Rename | Delete | Refresh */
    {
        int tx = layout.x;
        int gap = 5;
        int w_new_file = fp_button_width_for(i18n_t(I18N_KEY_BTN_NEW_FILE), 84);
        int w_new_dir = fp_button_width_for(i18n_t(I18N_KEY_BTN_NEW_DIR), 84);
        int w_rename = fp_button_width_for(i18n_t(I18N_KEY_BTN_RENAME), 72);
        int w_delete = fp_button_width_for(i18n_t(I18N_KEY_BTN_DELETE), 72);
        int w_refresh = fp_button_width_for(i18n_t(I18N_KEY_BTN_REFRESH), 84);
        btn = gui_add_button(fp_window, tx, toolbar_y, w_new_file, button_h, i18n_t(I18N_KEY_BTN_NEW_FILE), fp_on_tb_new_file, 0);
        if (btn) { btn->bg_color = gui_rgb(220, 235, 220); }
        tx += w_new_file + gap;
        btn = gui_add_button(fp_window, tx, toolbar_y, w_new_dir, button_h, i18n_t(I18N_KEY_BTN_NEW_DIR),  fp_on_tb_new_dir, 0);
        if (btn) { btn->bg_color = gui_rgb(220, 235, 220); }
        tx += w_new_dir + gap;
        btn = gui_add_button(fp_window, tx, toolbar_y, w_rename, button_h, i18n_t(I18N_KEY_BTN_RENAME),   fp_on_tb_rename, 0);
        tx += w_rename + gap;
        btn = gui_add_button(fp_window, tx, toolbar_y, w_delete, button_h, i18n_t(I18N_KEY_BTN_DELETE),   fp_on_tb_delete, 0);
        if (btn) { btn->bg_color = gui_rgb(245, 220, 220); }
        tx += w_delete + gap;
        btn = gui_add_button(fp_window, tx, toolbar_y, w_refresh, button_h, i18n_t(I18N_KEY_BTN_REFRESH),  fp_on_tb_refresh, 0);
    }

    /* status / prompt area at the very bottom */
    if (fp_prompt_mode == 0) {
        if (fp_status[0]) {
            lbl = gui_add_label(fp_window, layout.x, status_y, layout.w, label_h, fp_status);
            if (lbl) lbl->fg_color = gui_rgb(80, 80, 120);
        }
    } else {
        char promptlabel[80];
        const char *title_s = "";
        int pp = 0;
        if      (fp_prompt_mode == 1) title_s = i18n_t(I18N_KEY_PROMPT_NEW_FILE);
        else if (fp_prompt_mode == 2) title_s = i18n_t(I18N_KEY_PROMPT_NEW_DIR);
        else if (fp_prompt_mode == 3) title_s = i18n_t(I18N_KEY_PROMPT_RENAME);
        else if (fp_prompt_mode == 4) title_s = i18n_t(I18N_KEY_PROMPT_DELETE_CONFIRM);
        while (title_s[pp] && pp < 60) { promptlabel[pp] = title_s[pp]; pp++; }
        promptlabel[pp] = 0;
        {
            int ok_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_OK), 56);
            int cancel_w = fp_button_width_for(i18n_t(I18N_KEY_BTN_CANCEL), 72);
            int prompt_w = gui_text_width_px(promptlabel) + 10;
            int buttons_w = ok_w + cancel_w + 8;
            int field_x;
            int field_w;
            if (prompt_w < 160) prompt_w = 160;
            if (prompt_w > layout.w / 3) prompt_w = layout.w / 3;
            field_x = layout.x + prompt_w + 8;
            field_w = layout.x + layout.w - field_x - buttons_w - 12;
            if (field_w < 120) field_w = 120;
            lbl = gui_add_label(fp_window, layout.x, status_y, prompt_w, label_h, promptlabel);
            if (lbl) lbl->fg_color = gui_rgb(40, 40, 100);
            if (fp_prompt_mode == 4) {
                /* delete confirm: show name as readonly label */
                lbl = gui_add_label(fp_window, field_x, status_y, field_w, label_h, fp_prompt_target);
                if (lbl) lbl->fg_color = gui_rgb(180, 60, 60);
            } else {
                fp_prompt_textbox = gui_add_textbox(fp_window, field_x, status_y - 2, field_w, button_h, fp_prompt_buf);
                if (fp_prompt_textbox) {
                    /* sync displayed text into buf via length */
                    fp_prompt_len = 0;
                    while (fp_prompt_buf[fp_prompt_len]) fp_prompt_len++;
                }
            }
            btn = gui_add_button(fp_window, layout.x + layout.w - buttons_w, status_y - 2, ok_w, button_h, i18n_t(I18N_KEY_BTN_OK),     fp_on_prompt_ok, 0);
            if (btn) btn->bg_color = gui_rgb(200, 230, 200);
            btn = gui_add_button(fp_window, layout.x + layout.w - cancel_w, status_y - 2, cancel_w, button_h, i18n_t(I18N_KEY_BTN_CANCEL), fp_on_prompt_cancel, 0);
            if (btn) btn->bg_color = gui_rgb(240, 220, 220);
        }
    }

    /* column header row: clickable sort buttons | separators */
    {
        char hname[32], hmod[32], htype[24], hsize[24];
        const char *arrow_no = "  ";
        const char *suf_n, *suf_m, *suf_t, *suf_s;
        uint32_t hdr_fg = gui_rgb(80, 80, 90);
        uint32_t hdr_bg = gui_rgb(232, 232, 240);
        uint32_t sel_bg = gui_rgb(208, 220, 244);
        uint32_t sep_color = gui_rgb(200, 200, 210);
        /* ascii arrows: ^ down, v up; use ASCII to avoid font issues */
        suf_n = (fp_sort_key == 0) ? (fp_sort_desc ? " v" : " ^") : arrow_no;
        suf_m = (fp_sort_key == 1) ? (fp_sort_desc ? " v" : " ^") : arrow_no;
        suf_t = arrow_no;
        suf_s = (fp_sort_key == 2) ? (fp_sort_desc ? " v" : " ^") : arrow_no;

        hname[0] = 0;
        hmod[0] = 0;
        htype[0] = 0;
        hsize[0] = 0;
        fp_str_append(hname, 0, (int)sizeof(hname), i18n_t(I18N_KEY_FILE_COL_NAME));
        fp_str_append(hmod, 0, (int)sizeof(hmod), i18n_t(I18N_KEY_FILE_COL_MODIFIED));
        fp_str_append(htype, 0, (int)sizeof(htype), i18n_t(I18N_KEY_FILE_COL_TYPE));
        fp_str_append(hsize, 0, (int)sizeof(hsize), i18n_t(I18N_KEY_FILE_COL_SIZE));
        fp_str_append(hname, (int)strlen(hname), (int)sizeof(hname), suf_n);
        fp_str_append(hmod, (int)strlen(hmod), (int)sizeof(hmod), suf_m);
        fp_str_append(htype, (int)strlen(htype), (int)sizeof(htype), suf_t);
        fp_str_append(hsize, (int)strlen(hsize), (int)sizeof(hsize), suf_s);

        table.columns[0].title = hname;
        table.columns[1].title = hmod;
        table.columns[2].title = htype;
        table.columns[3].title = hsize;
        gui_table_view_draw_header(&table);
        (void)hdr_bg;
        (void)sel_bg;
        (void)sep_color;
        (void)hdr_fg;
    }

    /* entries */
    y = list_y;
    base = fp_page * list_count;
    for (slot = 0; slot < list_count; slot++) {
        char line[80];
        char sizebuf[12];
        char mtimebuf[24];
        const char *type_str;
        const char *display_name;
        int gidx = base + slot;
        int real_index;
        dentry_t *e = 0;
        gui_icon_id_t icon;
        uint32_t fsize = 0;
        const vfs_time_t *mt = 0;
        int p = 0;
        int col;
        int selected;
        int hover;
        int is_parent = 0;

        if (gidx >= total_items) break;
        selected = (slot == fp_selected);
        hover = 0;

        if (!fp_is_root() && gidx == 0) {
            is_parent = 1;
            icon = GUI_ICON_UPDIR;
            display_name = "..";
            type_str = i18n_t(I18N_KEY_TYPE_UP);
            mtimebuf[0] = '-'; mtimebuf[1] = '-'; mtimebuf[2] = 0;
            sizebuf[0] = '-'; sizebuf[1] = '-'; sizebuf[2] = 0;
        } else {
            real_index = fp_is_root() ? gidx : (gidx - 1);
            e = fp_get_sorted_real_entry(real_index);
            if (!e) break;
            icon = fp_pick_icon(e);
            display_name = e->name;
            type_str = fp_type_label(e);
            if (e->inode) {
                fsize = e->inode->size;
                mt = &e->inode->mtime;
            }
            if (fp_entry_is_dir(e)) {
                sizebuf[0] = '-'; sizebuf[1] = '-'; sizebuf[2] = 0;
            } else {
                fp_format_size(fsize, sizebuf);
            }
            fp_format_mtime(mt, mtimebuf);
        }

        /* One click target spans the row; individual columns are clipped labels.
         * This avoids the old fixed-character table string overlapping when the
         * default font is scaled up or CJK glyphs are wider than ASCII glyphs. */
        line[0] = 0;
        gui_table_view_draw_row(&table, slot, slot);
        (void)line;

        {
            gui_rect_t icon_cell;
            int bottom_pad = font_scale_value(2);
            int label_h = gui_text_glyph_height_px();
            int label_y;
            if (bottom_pad < 1) bottom_pad = 1;
            if (label_h > row_h - 2) label_h = row_h - 2;
            if (label_h < 1) label_h = 1;
            label_y = y + row_h - label_h - bottom_pad;
            if (label_y < y + 1) label_y = y + 1;

            /* File rows intentionally place text close to the lower edge.
             * This matches the visual baseline of the bitmap font better than
             * mathematical vertical centering in the tall row background. */
            icon_cell.x = layout.name_x;
            icon_cell.y = y + 1;
            icon_cell.w = layout.name_w;
            icon_cell.h = row_h - 2;
            gui_draw_file_icon_cell(&icon_cell, display_name, icon,
                                    selected, hover,
                                    gui_rgb(24, 28, 34));
            gui_table_view_draw_text_cell(&table, 1, label_y, mtimebuf, gui_rgb(42, 46, 54));
            gui_table_view_draw_text_cell(&table, 2, label_y, type_str, gui_rgb(42, 46, 54));
            gui_table_view_draw_text_cell(&table, 3, label_y, sizebuf, gui_rgb(42, 46, 54));
        }

        y += row_h;
        (void)is_parent;
        (void)p;
        (void)col;
    }
}

/* Returns 1 if name ends with .md / .MD */
static int fp_is_markdown(const char *name) {
    int n = 0;
    while (name[n]) n++;
    if (n < 3) return 0;
    if (name[n-3] == '.' &&
        (name[n-2] == 'm' || name[n-2] == 'M') &&
        (name[n-1] == 'd' || name[n-1] == 'D')) return 1;
    return 0;
}

/* Format a single raw line as markdown.
 * Inputs: src (NUL-terminated). Outputs: dst (caller buffer, dst_sz), *out_color.
 * Returns number of chars written (excluding NUL). */
static int fp_md_format_line(const char *src, char *dst, int dst_sz, uint32_t *out_color) {
    int sp = 0, dp = 0;
    int level = 0;
    int bullet = 0;
    int rule = 0;
    int code = 0;
    int quote = 0;
    int i, k, n;

    /* count leading spaces */
    while (src[sp] == ' ' && sp < 8) sp++;

    /* heading */
    if (src[sp] == '#') {
        k = sp;
        while (src[k] == '#' && level < 6) { level++; k++; }
        if (src[k] == ' ' || src[k] == 0) {
            /* skip spaces */
            while (src[k] == ' ') k++;
            /* emit prefix */
            for (i = 0; i < level && dp < dst_sz - 1; i++) dst[dp++] = '#';
            if (dp < dst_sz - 1) dst[dp++] = ' ';
            while (src[k] && dp < dst_sz - 1) dst[dp++] = src[k++];
            dst[dp] = 0;
            *out_color = (level == 1) ? gui_rgb(180, 60, 40) :
                         (level == 2) ? gui_rgb(150, 80, 40) :
                                        gui_rgb(120, 100, 40);
            return dp;
        }
    }

    /* horizontal rule: --- *** ___ */
    if ((src[sp] == '-' || src[sp] == '*' || src[sp] == '_')) {
        char ch = src[sp];
        n = 0;
        for (k = sp; src[k] == ch; k++) n++;
        if (n >= 3 && (src[k] == 0 || src[k] == ' ')) rule = 1;
    }
    if (rule) {
        for (i = 0; i < 50 && dp < dst_sz - 1; i++) dst[dp++] = '-';
        dst[dp] = 0;
        *out_color = gui_rgb(120, 120, 120);
        return dp;
    }

    /* unordered bullet: - or * followed by space */
    if ((src[sp] == '-' || src[sp] == '*' || src[sp] == '+') && src[sp+1] == ' ') {
        bullet = 1;
        k = sp + 2;
        if (dp < dst_sz - 1) dst[dp++] = ' ';
        if (dp < dst_sz - 1) dst[dp++] = ' ';
        if (dp < dst_sz - 1) dst[dp++] = (char)0x95 & 0x7f; /* fallback dot */
        dp--;
        if (dp < dst_sz - 1) dst[dp++] = '*';
        if (dp < dst_sz - 1) dst[dp++] = ' ';
        while (src[k] && dp < dst_sz - 1) dst[dp++] = src[k++];
        dst[dp] = 0;
        *out_color = gui_rgb(40, 100, 40);
        return dp;
    }

    /* blockquote: > */
    if (src[sp] == '>') {
        quote = 1;
        k = sp + 1;
        if (src[k] == ' ') k++;
        if (dp < dst_sz - 1) dst[dp++] = '|';
        if (dp < dst_sz - 1) dst[dp++] = ' ';
        while (src[k] && dp < dst_sz - 1) dst[dp++] = src[k++];
        dst[dp] = 0;
        *out_color = gui_rgb(100, 100, 140);
        return dp;
    }

    /* code block fence ``` */
    if (src[sp] == '`' && src[sp+1] == '`' && src[sp+2] == '`') {
        for (i = 0; i < 40 && dp < dst_sz - 1; i++) dst[dp++] = '=';
        dst[dp] = 0;
        *out_color = gui_rgb(80, 80, 80);
        return dp;
    }

    /* default: strip simple emphasis * and _ */
    k = 0;
    while (src[k] && dp < dst_sz - 1) {
        char c = src[k];
        if ((c == '*' || c == '_') &&
            (k == 0 || src[k-1] != '\\')) {
            k++;
            continue;
        }
        if (c == '`') { k++; continue; }
        dst[dp++] = c;
        k++;
    }
    dst[dp] = 0;
    *out_color = gui_rgb(40, 40, 40);
    (void)bullet; (void)quote; (void)code;
    return dp;
}

/* render view mode ---------------------------------------------- */
static void gui_file_preview_render_view(void) {
    char header[GUI_FP_MAX_PATH + 16];
    char status[64];
    char full[GUI_FP_MAX_PATH];
    char buf[GUI_FP_VIEW_BUF_SIZE + 1];
    char line[GUI_FP_VIEW_LINE_CHARS + 1];
    int fd, total, i, pos, lines, n;
    int line_pos;
    int x, y, content_w;
    int line_index;
    int total_lines;
    int spos;
    int margin;
    int gap;
    int label_h;
    int row_h;
    int button_h;
    int header_y;
    int nav_y;
    int body_y;
    int visible_lines;

    if (!fp_window) return;

    margin = fp_scale_i(8);
    if (margin < 8) margin = 8;
    gap = fp_panel_gap();
    label_h = fp_line_h() + fp_scale_i(4);
    row_h = fp_text_row_h();
    button_h = fp_button_h();
    visible_lines = fp_view_visible_lines();
    content_w = fp_window->rect.w - margin * 2;
    if (content_w < 120) content_w = 120;
    header_y = GUI_TITLE_HEIGHT + gap;
    nav_y = header_y + label_h + gap;
    body_y = nav_y + button_h + gap;

    /* header */
    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_HEADER_FILE));
    pos = fp_str_append(header, pos, sizeof(header), fp_view_name);
    (void)pos;
    gui_add_label(fp_window, margin, header_y, content_w, label_h, header);

    x = margin;
    gui_add_button(fp_window, x, nav_y, fp_scale_i(72), button_h, i18n_t(I18N_KEY_BTN_BACK), fp_on_back, 0);
    x += fp_scale_i(72) + gap;
    gui_add_button(fp_window, x, nav_y, fp_scale_i(40), button_h, "^", fp_on_view_up, 0);
    x += fp_scale_i(40) + gap;
    gui_add_button(fp_window, x, nav_y, fp_scale_i(40), button_h, "v", fp_on_view_down, 0);
    gui_add_button(fp_window, fp_window->rect.w - margin - fp_scale_i(76), nav_y,
                   fp_scale_i(76), button_h, i18n_t(I18N_KEY_BTN_EDIT), fp_on_edit_enter, 0);

    /* load file content */
    fp_path_join(fp_path, fp_view_name, full, sizeof(full));
    fd = vfs_open(full, O_RDONLY, 0);
    if (fd < 0) {
        gui_add_label(fp_window, margin, body_y, content_w, label_h,
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

    /* first pass: count total wrapped lines */
    total_lines = 0;
    {
        int cnt_pos = 0;
        int j;
        for (j = 0; j <= total; j++) {
            char c = (j < total) ? buf[j] : '\n';
            if (c == '\n') { total_lines++; cnt_pos = 0; }
            else {
                cnt_pos++;
                if (cnt_pos >= GUI_FP_VIEW_LINE_CHARS) { total_lines++; cnt_pos = 0; }
            }
        }
    }
    fp_view_total_lines = total_lines;
    if (fp_view_line_offset >= total_lines) {
        fp_view_line_offset = total_lines > visible_lines
                              ? total_lines - visible_lines : 0;
    }
    if (fp_view_line_offset < 0) fp_view_line_offset = 0;

    /* status label: "Line a-b / total" */
    {
        char nbuf[16];
        int last = fp_view_line_offset + visible_lines;
        if (last > total_lines) last = total_lines;
        spos = 0;
        spos = fp_str_append(status, spos, sizeof(status), i18n_t(I18N_KEY_LINE));
        fp_itoa(total_lines > 0 ? fp_view_line_offset + 1 : 0, nbuf);
        spos = fp_str_append(status, spos, sizeof(status), nbuf);
        spos = fp_str_append(status, spos, sizeof(status), i18n_t(I18N_KEY_LINE_DASH));
        fp_itoa(last, nbuf);
        spos = fp_str_append(status, spos, sizeof(status), nbuf);
        spos = fp_str_append(status, spos, sizeof(status), i18n_t(I18N_KEY_LINE_OF));
        fp_itoa(total_lines, nbuf);
        spos = fp_str_append(status, spos, sizeof(status), nbuf);
        (void)spos;
        gui_add_label(fp_window, margin + fp_scale_i(172), nav_y + (button_h - label_h) / 2,
                      fp_scale_i(190), label_h, status);
    }

    /* second pass: walk and emit visible lines */
    y = body_y;
    lines = 0;
    line_index = 0;
    i = 0;
    line_pos = 0;
    while (i <= total && lines < visible_lines) {
        char c = (i < total) ? buf[i] : '\n';
        int flush = 0;

        if (c == '\n') {
            flush = 1;
        } else if (line_pos >= GUI_FP_VIEW_LINE_CHARS) {
            flush = 1;
        }

        if (flush) {
            line[line_pos] = 0;
            if (line_index >= fp_view_line_offset) {
                if (fp_is_markdown(fp_view_name)) {
                    char md_line[GUI_FP_VIEW_LINE_CHARS + 16];
                    uint32_t color = gui_rgb(40, 40, 40);
                    gui_widget_t *mlbl;
                    fp_md_format_line(line, md_line, (int)sizeof(md_line), &color);
                    mlbl = gui_add_label(fp_window, margin, y, content_w, row_h, md_line);
                    if (mlbl) mlbl->fg_color = color;
                } else {
                    gui_add_label(fp_window, margin, y, content_w, row_h, line);
                }
                y += row_h;
                lines++;
            }
            line_index++;
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
}

/* clear fp_window pointer when window is closed by user (X button) */
static void fp_on_window_close(gui_window_t *win, void *ud) {
    (void)win;
    (void)ud;
    fp_window = 0;
}

/* render edit mode ---------------------------------------------- */
static void gui_file_preview_render_edit(void) {
    int margin, gap, label_h, button_h;
    int header_y, nav_y, body_y;
    int content_w;
    char full[GUI_FP_MAX_PATH];
    char header[128];
    char buf[GUI_WIDGET_TEXT_CAP];
    int fd, n, total;
    int pos;

    if (!fp_window) return;

    margin = fp_scale_i(8);
    if (margin < 8) margin = 8;
    gap = fp_panel_gap();
    label_h = fp_line_h() + fp_scale_i(4);
    button_h = fp_button_h();
    content_w = fp_window->rect.w - margin * 2;
    if (content_w < 120) content_w = 120;
    header_y = GUI_TITLE_HEIGHT + gap;
    nav_y = header_y + label_h + gap;
    body_y = nav_y + button_h + gap;

    fp_edit_textarea = 0;
    fp_edit_status = 0;

    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_HEADER_EDIT));
    pos = fp_str_append(header, pos, sizeof(header), fp_view_name);
    (void)pos;
    gui_add_label(fp_window, margin, header_y, content_w, label_h, header);

    gui_add_button(fp_window, margin, nav_y, fp_scale_i(72), button_h,
                   i18n_t(I18N_KEY_BTN_SAVE), fp_on_edit_save, 0);
    gui_add_button(fp_window, margin + fp_scale_i(72) + gap, nav_y,
                   fp_scale_i(82), button_h, i18n_t(I18N_KEY_BTN_CANCEL), fp_on_edit_cancel, 0);
    fp_edit_status = gui_add_label(fp_window, margin + fp_scale_i(172),
                                   nav_y + (button_h - label_h) / 2,
                                   fp_window->rect.w - margin * 2 - fp_scale_i(172), label_h, "");

    fp_path_join(fp_path, fp_view_name, full, sizeof(full));
    fd = vfs_open(full, O_RDONLY, 0);
    total = 0;
    if (fd >= 0) {
        while (total < (int)sizeof(buf) - 1) {
            n = vfs_read(fd, buf + total, (int)sizeof(buf) - 1 - total);
            if (n <= 0) break;
            total += n;
        }
        vfs_close(fd);
    }
    buf[total] = 0;

    fp_edit_textarea = gui_add_textarea(fp_window, margin, body_y,
                                        content_w,
                                        fp_window->rect.h - body_y - margin,
                                        buf);
    if (fp_edit_textarea) {
        gui_widget_set_textbox_flags(fp_edit_textarea,
            GUI_TEXTBOX_FLAG_MULTILINE | GUI_TEXTBOX_FLAG_WRAP);
        gui_widget_set_placeholder(fp_edit_textarea, i18n_t(I18N_KEY_WIN_FILE_EDITOR));
    }
}

/* rebuild and open ---------------------------------------------- */
static void gui_file_preview_rebuild(void) {
    const char *title;
    int win_x = 60;
    int win_y = 60;
    int win_w = fp_default_window_w();
    int win_h = 430;
    int had_window = 0;

    switch (fp_mode) {
        case 0:  title = i18n_t(I18N_KEY_WIN_FILES); break;
        case 1:  title = i18n_t(I18N_KEY_WIN_FILE_VIEWER); break;
        case 2:  title = i18n_t(I18N_KEY_WIN_FILE_EDITOR); break;
        default: title = i18n_t(I18N_KEY_WIN_FILES); break;
    }

    if (fp_mode == 1) win_h = fp_view_window_h();
    else if (fp_mode == 2) win_h = fp_edit_window_h();

    if (fp_window) {
        had_window = 1;
        win_x = fp_window->rect.x;
        win_y = fp_window->rect.y;
        win_w = fp_window->rect.w;
        win_h = fp_window->rect.h;

        /* avoid firing close hook (it would null fp_window prematurely) */
        gui_window_set_on_close(fp_window, 0, 0);
        gui_destroy_window(fp_window);
        fp_window = 0;
    }

    if (had_window) {
        int min_w = fp_default_window_w();
        if (win_w < min_w) win_w = min_w;
        if (win_h < 260) win_h = 260;
    }

    fp_window = gui_create_window(win_x, win_y, win_w, win_h, title);
    if (!fp_window) return;
    gui_window_set_on_close(fp_window, fp_on_window_close, 0);
    if (fp_mode == 0) {
        gui_file_preview_render_list();
    } else if (fp_mode == 1) {
        gui_file_preview_render_view();
    } else {
        gui_file_preview_render_edit();
    }
    gui_render();
}

static void gui_file_preview_open_file(const char *path) {
    char parent[GUI_FP_MAX_PATH];
    char name[GUI_FP_MAX_NAME];
    uint32_t i;
    int slash = -1;
    if (!path || !path[0]) return;
    for (i = 0; path[i] && i < GUI_FP_MAX_PATH - 1; i++) {
        if (path[i] == '/') slash = (int)i;
    }
    if (slash <= 0) {
        parent[0] = '/';
        parent[1] = 0;
        gui_taskbar_search_copy(name, sizeof(name), path[0] == '/' ? path + 1 : path);
    } else {
        uint32_t n = 0;
        while (n < (uint32_t)slash && n + 1 < sizeof(parent)) {
            parent[n] = path[n];
            n++;
        }
        parent[n] = 0;
        gui_taskbar_search_copy(name, sizeof(name), path + slash + 1);
    }

    gui_taskbar_search_copy(fp_path, sizeof(fp_path), parent);
    gui_taskbar_search_copy(fp_view_name, sizeof(fp_view_name), name);
    fp_mode = 1;
    fp_selected = -1;
    fp_page = 0;
    fp_view_line_offset = 0;
    fp_view_total_lines = 0;
    fp_last_click_global = -1;
    fp_last_click_frame = 0;
    fp_clear_entry_click_state();
    gui_file_preview_rebuild();
}

static void gui_file_preview_open_path(const char *path) {
    char parent[GUI_FP_MAX_PATH];
    char name[GUI_FP_MAX_NAME];
    dentry_t *entry = 0;
    uint32_t i;
    uint32_t entry_index = 0;
    int slash = -1;
    int is_dir = 0;
    if (!path || !path[0]) {
        gui_file_preview_open();
        return;
    }
    if (gui_string_equals(path, "/")) {
        fp_path_set_root();
        fp_selected = -1;
        fp_mode = 0;
        fp_page = 0;
        fp_clear_entry_click_state();
        gui_file_preview_rebuild();
        return;
    }
    for (i = 0; path[i] && i < GUI_FP_MAX_PATH - 1; i++) {
        if (path[i] == '/') slash = (int)i;
    }
    if (slash <= 0) {
        parent[0] = '/';
        parent[1] = 0;
        gui_taskbar_search_copy(name, sizeof(name), path[0] == '/' ? path + 1 : path);
    } else {
        uint32_t n = 0;
        while (n < (uint32_t)slash && n + 1 < sizeof(parent)) {
            parent[n] = path[n];
            n++;
        }
        parent[n] = 0;
        gui_taskbar_search_copy(name, sizeof(name), path + slash + 1);
    }
    for (i = 0; i < 128; i++) {
        dentry_t *e = vfs_readdir(parent, i);
        if (!e) break;
        if (gui_string_equals(e->name, name)) {
            entry = e;
            entry_index = i;
            break;
        }
    }
    if (entry && fp_entry_is_dir(entry)) is_dir = 1;
    if (is_dir) {
        gui_taskbar_search_copy(fp_path, sizeof(fp_path), path);
        fp_selected = -1;
    } else {
        int global_index;
        gui_taskbar_search_copy(fp_path, sizeof(fp_path), parent);
        global_index = (fp_is_root() ? (int)entry_index : (int)entry_index + 1);
        fp_page = global_index / fp_list_per_page();
        fp_selected = global_index - fp_page * fp_list_per_page();
    }
    fp_mode = 0;
    fp_clear_entry_click_state();
    if (!is_dir && entry) {
        int global_index = (fp_is_root() ? (int)entry_index : (int)entry_index + 1);
        fp_page = global_index / fp_list_per_page();
        fp_selected = global_index - fp_page * fp_list_per_page();
    } else {
        fp_page = 0;
    }
    gui_file_preview_rebuild();
}

static void gui_file_preview_open(void) {
    if (fp_path[0] == 0) fp_path_set_root();
    fp_mode = 0;
    fp_page = 0;
    fp_clear_entry_click_state();
    gui_file_preview_rebuild();
}

void gui_demo(void) {
    gui_app_t *app;
    if (!g_gui.initialized) return;
    app = gui_register_app("demo", "OpenOS Demo", gui_demo_app_entry, 0);
    if (app) gui_start_app(app);
    gui_render();
}
