/* ============================================================
 * openos - GUI 内部共享接口（gui.c 与子模块 gui_browser.c 等之间）
 *
 * 说明：以下符号原为 gui.c 内部 static，因拆分子模块需跨文件共享，
 *       改为非 static 并在此声明。仅供 GUI 各 .c 文件内部使用，
 *       不属于对外公开 API（对外 API 见 gui.h / gui_user.h）。
 * ============================================================ */
#ifndef OPENOS_GUI_INTERNAL_H
#define OPENOS_GUI_INTERNAL_H

#include "gui.h"
#include "types.h"

/* ---- gui.c 导出给子模块的共享全局状态 ---- */
extern gui_system_t g_gui;

/* ---- gui.c 导出给子模块的共享辅助函数 ---- */
uint32_t gui_rgb(uint8_t r, uint8_t g, uint8_t b);
void     gui_notify(const char *text);
int      gui_is_enter_key(int key);
int      gui_append_uint(char *dst, int pos, int cap, uint32_t v);
void     gui_terminal_show_prompt(void);
int      fp_str_append(char *dst, int pos, int cap, const char *src);

/* ---- gui_browser.c 导出给 gui.c 的浏览器/网络工具接口 ---- */
/* 网络工具异步状态机类型（gui.c 终端命令分支与 gui_browser.c 共享）*/
typedef enum {
    NT_TOOL_NONE = 0,
    NT_TOOL_PING,
    NT_TOOL_NSLOOKUP,
    NT_TOOL_WGET,
} nt_tool_t;

void gui_browser_open(void);
gui_window_t *gui_browser_window(void);   /* 返回浏览器窗口指针，供窗口关闭比较 */
void browser_load_tick(void);
int  browser_handle_address_enter(int key);
int  gui_nettool_start(nt_tool_t tool, const char *host, const char *path2);
void gui_nettool_tick(void);
int  gui_nettool_active(void);

/* ---- gui.c 导出给 gui_terminal.c 的绘制/辅助原语 ---- */
void gui_raw_fill_rect(int x, int y, int w, int h, uint32_t color);
void gui_set_clip_rect(const gui_rect_t *rect);
void gui_clear_clip_rect(void);
int  gui_net_alias_match(const char *c);
void gui_set_focused_widget(gui_widget_t *wg);
int  gui_str_eq(const char *a, const char *b);

/* ---- gui_terminal.c 导出给 gui.c 的终端视图/控制接口 ---- */
void gui_terminal_invalidate_cursor(void);
void gui_terminal_invalidate_body(void);
void gui_terminal_tick_cursor(void);
void gui_terminal_drain_output_queue(void);
int  gui_terminal_point_to_cell(int x, int y, uint32_t *col, uint32_t *row);
void gui_terminal_view_init(gui_terminal_view_t *view, uint32_t cols, uint32_t rows);
void gui_terminal_view_clear(gui_terminal_view_t *view);
void gui_terminal_view_make_layout(gui_rect_t viewport, int padding_x, int padding_y, gui_terminal_view_layout_t *layout);
int  gui_terminal_view_point_to_cell(const gui_terminal_view_t *view, const gui_terminal_view_layout_t *layout, int x, int y, uint32_t *col, uint32_t *row);
void gui_terminal_view_begin_selection(gui_terminal_view_t *view, uint32_t col, uint32_t row);
void gui_terminal_view_update_selection(gui_terminal_view_t *view, uint32_t col, uint32_t row);
void gui_terminal_view_end_selection(gui_terminal_view_t *view);
int  gui_terminal_view_cell_selected(const gui_terminal_view_t *view, uint32_t col, uint32_t row);
int  gui_terminal_view_copy_selection(gui_terminal_view_t *view);
int  gui_terminal_view_has_clipboard_text(const gui_terminal_view_t *view);
int  gui_terminal_view_set_clipboard_text(gui_terminal_view_t *view, const char *text);
const char *gui_terminal_view_get_clipboard_text(const gui_terminal_view_t *view);
void gui_terminal_view_draw(const gui_terminal_view_t *view, const gui_terminal_view_layout_t *layout, int draw_cursor, uint32_t fg, uint32_t selection_bg, uint32_t selection_fg, uint32_t cursor_color);
void gui_terminal_view_scroll(gui_terminal_view_t *view);
void gui_terminal_update_selection(uint32_t col, uint32_t row);
void gui_terminal_set_input_focus(int focused);
int  gui_terminal_set_clipboard_text(const char *text);
const char *gui_terminal_get_clipboard_text(void);


/* ---- Step1: gui.c 框架/工具 API 提升为共享（桌面 Shell 抽离前置）---- */
void fp_itoa(int n, char *buf);
void gui_about_open(void);
int gui_ascii_case_contains(const char *text, const char *query);
int gui_ascii_case_ends_with(const char *text, const char *suffix);
void gui_copy_cached_text(char *dst, uint32_t dst_size, const char *src);
void gui_draw_browser_icon_art(int x, int y, uint32_t color);
void gui_draw_folder_icon_art(int x, int y, uint32_t color);
void gui_draw_icon_button_frame(const gui_rect_t *rect, const char *label, int icon_w, int icon_h, int gap, int selected, int highlighted, uint32_t text_color, int *icon_x, int *icon_y);
void gui_draw_launcher_item(const gui_launcher_entry_t *entry, const gui_rect_t *rect, int selected, int highlighted);
void gui_file_preview_open(void);
void gui_file_preview_open_file(const char *path);
void gui_file_preview_open_path(const char *path);
void gui_make_ellipsis_line_px(char *dst, uint32_t dst_size, const char *src, uint32_t max_src_bytes, int max_width_px, int use_ellipsis);
void gui_network_open(void);
int gui_path_starts_with(const char *path, const char *prefix);
void gui_raw_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
void gui_raw_line(int x0, int y0, int x1, int y1, uint32_t color);
int gui_rect_contains(const gui_rect_t *r, int x, int y);
void gui_recycle_open(void);
void gui_set_wallpaper_theme(uint32_t theme);
void gui_settings_open(void);
int gui_string_equals(const char *a, const char *b);
uint32_t gui_text_len_until_break(const char *text);
int gui_text_line_height_px(void);
int gui_tray_network_is_wireless(void);
void gui_update_start_menu_layout(void);
uint32_t gui_utf8_prefix_for_width(const char *src, uint32_t max_bytes, int max_width_px);
uint32_t gui_utf8_step_bytes(const char *s);
void gui_wifi_open(void);
gui_window_t *gui_window_at(int x, int y);

/* === Sticky Note subsystem (gui_sticky.c) === */
/* Desktop note store shared between desktop layer (gui.c) and sticky notes */
#ifndef GUI_DESKTOP_NOTE_MAX_COUNT
#define GUI_DESKTOP_NOTE_MAX_COUNT 5
#define GUI_DESKTOP_NOTE_MAX_TEXT  96
typedef struct gui_desktop_note_store {
    char items[GUI_DESKTOP_NOTE_MAX_COUNT][GUI_DESKTOP_NOTE_MAX_TEXT + 1];
    int count;
} gui_desktop_note_store_t;
#endif
/* gui.c -> sticky: none. sticky -> gui.c(desktop): note store writer */
void gui_desktop_note_add(gui_desktop_note_store_t *store, const char *line, int len);
/* sticky -> external callers (gui.c desktop/start_menu): */
void gui_stickynote_open(void);
int sticky_export_to_desktop_store(gui_desktop_note_store_t *store);

#endif /* OPENOS_GUI_INTERNAL_H */
