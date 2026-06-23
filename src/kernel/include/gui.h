/* ============================================================
 * openos - Minimal GUI / Window System
 * ============================================================ */

#ifndef OPENOS_GUI_H
#define OPENOS_GUI_H

#include "types.h"
#include "font.h"
#include "framebuffer.h"

#define GUI_MAX_WINDOWS          16u
#define GUI_MAX_APPS             16u
#define GUI_MAX_WIDGETS_PER_WIN  64u
#define GUI_APP_NAME_LEN         32u
#define GUI_BORDER_SIZE          2
#define GUI_CHAR_W               ((int)font_get_ascii_width(font_get_default()))
#define GUI_CHAR_H               ((int)font_get_ascii_height(font_get_default()))
#define GUI_TEXT_LINE_H          ((int)font_get_line_height(font_get_default()))
#define GUI_TITLE_HEIGHT         ((GUI_TEXT_LINE_H + 4 < 22) ? 22 : (GUI_TEXT_LINE_H + 4))
#define GUI_TERM_COLS            128u
#define GUI_TERM_ROWS            48u
#define GUI_TERM_CLIPBOARD_SIZE  1024u
#define GUI_EVENT_QUEUE_SIZE     64u
#define GUI_MAX_DIRTY_RECTS      32u
#define GUI_TASKBAR_HEIGHT       40
#define GUI_TASKBAR_ICON_BUTTON_W 40
#define GUI_TASKBAR_START_W      GUI_TASKBAR_ICON_BUTTON_W
#define GUI_DESKTOP_MAX_ICONS    16u
#define GUI_DESKTOP_ICON_W       72
#define GUI_DESKTOP_ICON_H       64
#define GUI_DESKTOP_MENU_W       184
#define GUI_DESKTOP_MENU_H       144
#define GUI_LAUNCHER_MAX_APPS    24u
#define GUI_LAUNCHER_ITEM_H      24
#define GUI_TASKBAR_SEARCH_MAX_RESULTS 8u
#define GUI_TASKBAR_SEARCH_PATH_LEN 128u
#define GUI_WIDGET_TEXT_CAP      256u

#define GUI_LABEL_FLAG_ELLIPSIS   0x00000001u
#define GUI_LABEL_FLAG_MULTILINE  0x00000002u
#define GUI_LABEL_FLAG_SELECTABLE 0x00000004u
#define GUI_LABEL_FLAG_COPYABLE   0x00000008u
#define GUI_LABEL_ALIGN_LEFT      0u
#define GUI_LABEL_ALIGN_CENTER    1u
#define GUI_LABEL_ALIGN_RIGHT     2u

#define GUI_BUTTON_FLAG_DEFAULT   0x00000001u
#define GUI_BUTTON_FLAG_DANGER    0x00000002u
#define GUI_TOGGLE_FLAG_ON        0x00000004u
#define GUI_CHECKBOX_FLAG_CHECKED 0x00000008u
#define GUI_RADIOBUTTON_FLAG_CHECKED 0x00000010u

#define GUI_TEXTBOX_FLAG_READONLY  0x00000001u
#define GUI_TEXTBOX_FLAG_DISABLED  0x00000002u
#define GUI_TEXTBOX_FLAG_PASSWORD  0x00000004u
#define GUI_TEXTBOX_FLAG_MULTILINE 0x00000008u
#define GUI_TEXTBOX_FLAG_WRAP      0x00000010u

#define GUI_LISTVIEW_FLAG_MULTI_SELECT    0x00000001u
#define GUI_LISTVIEW_FLAG_SHOW_CHECKBOXES 0x00000002u

#define GUI_TABLEVIEW_FLAG_SHOW_HEADER     0x00000001u
#define GUI_TABLEVIEW_FLAG_GRID_LINES      0x00000002u
#define GUI_TABLEVIEW_FLAG_ROW_SELECT      0x00000004u
#define GUI_TABLEVIEW_FLAG_SORTABLE        0x00000008u
#define GUI_DIALOG_TYPE_MASK               0x0000000fu
#define GUI_DIALOG_TYPE_INFO               0x00000000u
#define GUI_DIALOG_TYPE_WARNING            0x00000001u
#define GUI_DIALOG_TYPE_ERROR              0x00000002u
#define GUI_DIALOG_TYPE_CONFIRM            0x00000003u
#define GUI_DIALOG_FLAG_CANCEL             0x00000010u
#define GUI_TREEVIEW_FLAG_SHOW_LINES        0x00000001u
#define GUI_TREEVIEW_FLAG_SHOW_ICONS        0x00000002u

#define GUI_PANEL_FLAG_BORDER      0x00000001u
#define GUI_PANEL_FLAG_ROUNDED     0x00000002u
#define GUI_PANEL_FLAG_SHADOW      0x00000004u

#define GUI_WINDOW_FLAG_NONE      0x00000000u
#define GUI_WINDOW_FLAG_CLOSABLE  0x00000001u
#define GUI_WINDOW_FLAG_MINIMIZED 0x00000002u
#define GUI_WINDOW_FLAG_TERMINAL  0x00000004u
#define GUI_WINDOW_FLAG_MINIMIZABLE 0x00000008u
#define GUI_WINDOW_FLAG_RESIZABLE 0x00000010u
#define GUI_WINDOW_FLAG_MAXIMIZABLE 0x00000020u
#define GUI_WINDOW_FLAG_MAXIMIZED 0x00000040u

#define GUI_KEY_BACKSPACE  8
#define GUI_KEY_TAB        9
#define GUI_KEY_ENTER      13
#define GUI_KEY_SPACE      32
#define GUI_KEY_DELETE     0x101
#define GUI_KEY_LEFT       0x102
#define GUI_KEY_RIGHT      0x103
#define GUI_KEY_HOME       0x104
#define GUI_KEY_END        0x105
#define GUI_KEY_UP         0x106
#define GUI_KEY_DOWN       0x107
#define GUI_KEY_ALT_TAB    0x108
#define GUI_KEY_SUPER      0x109

typedef struct gui_rect {
    int x;
    int y;
    int w;
    int h;
} gui_rect_t;

typedef struct gui_color_scheme {
    uint32_t desktop_bg;
    uint32_t window_bg;
    uint32_t window_border;
    uint32_t title_bg;
    uint32_t title_fg;
    uint32_t text_fg;
    uint32_t button_bg;
    uint32_t button_border;
    uint32_t button_fg;
    uint32_t accent;
} gui_color_scheme_t;

typedef enum gui_widget_type {
    GUI_WIDGET_NONE = 0,
    GUI_WIDGET_LABEL,
    GUI_WIDGET_BUTTON,
    GUI_WIDGET_PANEL,
    GUI_WIDGET_TEXTBOX,
    GUI_WIDGET_SLIDER,
    GUI_WIDGET_TEXTAREA,
    GUI_WIDGET_CANVAS,
    GUI_WIDGET_ICON_BUTTON,
    GUI_WIDGET_TOGGLE,
    GUI_WIDGET_CHECKBOX,
    GUI_WIDGET_RADIOBUTTON,
    GUI_WIDGET_SELECT,
    GUI_WIDGET_COMBOBOX,
    GUI_WIDGET_LISTVIEW,
    GUI_WIDGET_TABLEVIEW,
    GUI_WIDGET_MENUBAR,
    GUI_WIDGET_CONTEXTMENU,
    GUI_WIDGET_DIALOG,
    GUI_WIDGET_TREEVIEW,
    GUI_WIDGET_SCROLLBAR,
    GUI_WIDGET_SCROLLVIEW
} gui_widget_type_t;

typedef enum gui_event_type {
    GUI_EVENT_NONE = 0,
    GUI_EVENT_MOUSE_MOVE,
    GUI_EVENT_MOUSE_DOWN,
    GUI_EVENT_MOUSE_UP,
    GUI_EVENT_BUTTON_CLICK,
    GUI_EVENT_WINDOW_FOCUS,
    GUI_EVENT_WINDOW_CLOSE,
    GUI_EVENT_WINDOW_MINIMIZE,
    GUI_EVENT_WINDOW_DRAG,
    GUI_EVENT_KEY_DOWN,
    GUI_EVENT_MOUSE_WHEEL,
    GUI_EVENT_FOCUS,
    GUI_EVENT_BLUR,
    GUI_EVENT_TEXT_CHANGED,
    GUI_EVENT_TEXT_SUBMIT,
    GUI_EVENT_VALUE_CHANGED
} gui_event_type_t;

typedef struct gui_window gui_window_t;
typedef struct gui_widget gui_widget_t;
typedef struct gui_app gui_app_t;

typedef void (*gui_widget_callback_t)(gui_widget_t *widget, void *user_data);
typedef int (*gui_app_entry_t)(gui_app_t *app, void *user_data);

typedef struct gui_event {
    gui_event_type_t type;
    int x;
    int y;
    int dx;
    int dy;
    uint8_t button;
    int key;
    gui_window_t *window;
    gui_widget_t *widget;
} gui_event_t;

typedef enum gui_icon_id {
    GUI_ICON_NONE = 0,
    GUI_ICON_FOLDER,
    GUI_ICON_UPDIR,
    GUI_ICON_FILE_GENERIC,
    GUI_ICON_FILE_TEXT,
    GUI_ICON_FILE_CODE,
    GUI_ICON_FILE_CONFIG,
    GUI_ICON_FILE_SHELL,
    GUI_ICON_FILE_EXEC,
    GUI_ICON_FILE_IMAGE,
    GUI_ICON_FILE_ARCHIVE,
    GUI_ICON_FILE_MARKUP,
    GUI_ICON_COUNT
} gui_icon_id_t;

struct gui_widget {
    uint32_t id;
    gui_widget_type_t type;
    gui_rect_t rect;
    char text[GUI_WIDGET_TEXT_CAP];
    char placeholder[GUI_WIDGET_TEXT_CAP];
    uint32_t bg_color;
    uint32_t fg_color;
    gui_widget_callback_t on_click;
    void *user_data;
    gui_window_t *owner;
    uint32_t parent_id;
    int visible;
    int enabled;
    int pressed;
    int hovered;
    int focused;
    uint32_t cursor;
    uint32_t text_scroll;
    uint32_t selection_anchor;
    uint32_t selection_start;
    uint32_t selection_end;
    int text_selecting;
    uint32_t textbox_flags;
    uint32_t button_flags;
    uint32_t label_flags;
    uint32_t label_align;
    uint32_t panel_flags;
    uint32_t panel_border_color;
    uint32_t panel_border_width;
    uint32_t panel_padding;
    int min_value;
    int max_value;
    int value;
    int step;
    int group_id;
    int table_sort_column;
    int table_sort_ascending;
    gui_icon_id_t icon;
};

struct gui_window {
    uint32_t id;
    int used;
    gui_rect_t rect;
    char title[64];
    uint32_t bg_color;
    uint32_t flags;
    int visible;
    int active;
    int dragging;
    int drag_offset_x;
    int drag_offset_y;
    int resizing;
    int resize_start_mx;
    int resize_start_my;
    int resize_start_w;
    int resize_start_h;
    gui_rect_t saved_rect;
    uint32_t last_title_click_frame;
    uint32_t user_owner_pid;
    gui_app_t *owner_app;
    void (*on_close)(struct gui_window *win, void *user_data);
    void *close_user_data;
    gui_widget_t widgets[GUI_MAX_WIDGETS_PER_WIN];
    uint32_t widget_count;
};

struct gui_app {
    uint32_t id;
    int used;
    int running;
    char name[GUI_APP_NAME_LEN];
    char title[64];
    gui_app_entry_t entry;
    void *user_data;
    gui_window_t *main_window;
    uint32_t window_count;
};

typedef struct gui_terminal_view {
    char cells[GUI_TERM_ROWS][GUI_TERM_COLS];
    uint32_t cols;
    uint32_t rows;
    uint32_t cursor_x;
    uint32_t cursor_y;
    int selecting;
    int has_selection;
    uint32_t selection_anchor_x;
    uint32_t selection_anchor_y;
    uint32_t selection_start_x;
    uint32_t selection_start_y;
    uint32_t selection_end_x;
    uint32_t selection_end_y;
    char clipboard[GUI_TERM_CLIPBOARD_SIZE];
    uint32_t clipboard_len;
} gui_terminal_view_t;

typedef struct gui_terminal_view_layout {
    int x;
    int y;
    int cell_w;
    int cell_h;
    int char_h;
    int cols;
    int rows;
    gui_rect_t clip_rect;
} gui_terminal_view_layout_t;

typedef struct gui_terminal {
    gui_window_t *window;
    gui_terminal_view_t view;
    int enabled;
    int input_focused;
    int dirty;
    int cursor_visible;
    uint32_t cursor_blink_ticks;
} gui_terminal_t;

typedef struct gui_settings_row {
    gui_window_t *window;
    int x;
    int y;
    int width;
    int label_h;
    int button_h;
    int button_w;
    int gap;
} gui_settings_row_t;

typedef struct gui_compositor_info {
    int enabled;
    int active;
    int double_buffered;
    uint32_t width;
    uint32_t height;
    uint32_t backbuffer_pixels;
    uint32_t dirty_count;
    uint32_t dirty_rect_capacity;
    int full_dirty;
    uint32_t flush_generation;
} gui_compositor_info_t;

typedef struct gui_accel_info {
    int enabled;
    uint32_t rect_fills;
    uint32_t rect_fill_pixels;
    uint32_t backbuffer_fast_fills;
    uint32_t framebuffer_fast_fills;
    uint32_t blits;
    uint32_t blit_pixels;
    uint32_t backbuffer_fast_blits;
    uint32_t framebuffer_fast_blits;
    uint32_t rect_copies;
    uint32_t rect_copy_pixels;
    uint32_t flush_rects;
    uint32_t flush_pixels;
    uint32_t flush_rows;
    framebuffer_backend_type_t backend;
    uint32_t backend_caps;
    uint32_t alpha_pixels;
    uint32_t icon_quality_passes;
} gui_accel_info_t;

typedef struct gui_desktop_icon {
    int used;
    gui_rect_t rect;
    char label[32];
    uint32_t color;
    uint32_t action;
} gui_desktop_icon_t;

typedef struct gui_desktop_info {
    int enabled;
    int start_menu_open;
    uint32_t icon_count;
    gui_rect_t taskbar_rect;
    gui_rect_t start_button_rect;
    gui_rect_t start_menu_rect;
} gui_desktop_info_t;

typedef struct gui_launcher_entry {
    int used;
    char name[GUI_APP_NAME_LEN];
    char title[64];
    uint32_t action;
    uint32_t color;
    char path[64];  /* if non-empty and action >= LAUNCH_BIN_BASE, exec this path */
} gui_launcher_entry_t;

typedef struct gui_launcher_info {
    int enabled;
    uint32_t app_count;
    gui_rect_t menu_rect;
} gui_launcher_info_t;

typedef struct gui_taskbar_search_result {
    int used;
    int is_dir;
    int is_executable;
    char name[GUI_APP_NAME_LEN];
    char path[GUI_TASKBAR_SEARCH_PATH_LEN];
} gui_taskbar_search_result_t;

typedef struct gui_system {
    int initialized;
    uint32_t width;
    uint32_t height;
    gui_color_scheme_t colors;
    gui_window_t windows[GUI_MAX_WINDOWS];
    uint32_t z_order[GUI_MAX_WINDOWS];
    uint32_t window_count;
    gui_app_t apps[GUI_MAX_APPS];
    gui_app_t *active_app;
    gui_app_t *launching_app;
    gui_window_t *active_window;
    gui_window_t *drag_window;
    gui_widget_t *pressed_widget;
    gui_widget_t *slider_widget;
    gui_widget_t *scrollbar_widget;
    gui_widget_t *hovered_widget;
    gui_widget_t *focused_widget;
    gui_widget_t *text_select_widget;
    uint32_t next_window_id;
    uint32_t next_widget_id;
    uint32_t next_app_id;

    gui_event_t events[GUI_EVENT_QUEUE_SIZE];
    uint32_t event_head;
    uint32_t event_tail;
    uint32_t event_count;

    int mouse_x;
    int mouse_y;
    uint8_t mouse_buttons;
    uint8_t last_mouse_buttons;
    int cursor_visible;
    int cursor_drawn;
    int cursor_fb_x;
    int cursor_fb_y;
    uint32_t *backbuffer;
    uint32_t backbuffer_pixels;
    int double_buffered;
    int compositor_enabled;
    gui_rect_t dirty_rects[GUI_MAX_DIRTY_RECTS];
    uint32_t dirty_count;
    uint32_t dirty_rect_capacity;
    int full_dirty;
    uint32_t flush_generation;
    int clip_enabled;
    gui_rect_t clip_rect;
    int render_clip_enabled;
    gui_rect_t render_clip_rect;

    int desktop_enabled;
    int desktop_start_menu_open;
    int desktop_start_menu_scroll;
    gui_rect_t desktop_taskbar_rect;
    gui_rect_t desktop_start_button_rect;
    gui_rect_t desktop_start_menu_rect;
    gui_desktop_icon_t desktop_icons[GUI_DESKTOP_MAX_ICONS];
    uint32_t desktop_icon_count;
    int launcher_enabled;
    gui_launcher_entry_t launcher_entries[GUI_LAUNCHER_MAX_APPS];
    uint32_t launcher_app_count;
    int taskbar_search_focused;
    uint32_t taskbar_search_len;
    char taskbar_search_text[64];
    gui_taskbar_search_result_t taskbar_search_results[GUI_TASKBAR_SEARCH_MAX_RESULTS];
    uint32_t taskbar_search_result_count;
    int taskbar_search_selected;
    gui_rect_t taskbar_search_results_rect;
    uint32_t wallpaper_theme;
    uint32_t frame_counter;

    gui_terminal_t terminal;
} gui_system_t;

void gui_init(void);
int gui_start(uint32_t width, uint32_t height);
int gui_is_ready(void);
int gui_has_focused_widget(void);
gui_widget_t *gui_get_focused_widget(void);
void gui_shutdown_to_text_note(void);
void gui_set_cursor_visible(int visible);
int gui_is_cursor_visible(void);

const gui_system_t *gui_get_system(void);
void gui_get_compositor_info(gui_compositor_info_t *info);
void gui_get_accel_info(gui_accel_info_t *info);
int gui_get_desktop_info(gui_desktop_info_t *info);
int gui_get_launcher_info(gui_launcher_info_t *info);
int gui_launcher_launch(uint32_t index);
int gui_accel_is_enabled(void);
int gui_compositor_is_active(void);
void gui_set_compositor_enabled(int enabled);
void gui_compositor_flush(void);
void gui_print_info(void);
void gui_render(void);
void gui_demo(void);
int gui_start_desktop(void);
void gui_poll(void);

gui_app_t *gui_register_app(const char *name, const char *title, gui_app_entry_t entry, void *user_data);
int gui_start_app(gui_app_t *app);
void gui_exit_app(gui_app_t *app);
void gui_destroy_windows_by_user_owner(uint32_t owner_pid);
int gui_window_fill_client_rect(gui_window_t *win, int x, int y, int w, int h, uint32_t color);
int gui_window_draw_client_text(gui_window_t *win, int x, int y, const char *text, uint32_t color);
int gui_window_blit_client_rgba32(gui_window_t *win, int x, int y, int w, int h, const uint32_t *pixels, uint32_t src_stride);
int gui_window_scroll_client_rect(gui_window_t *win, int dst_x, int dst_y, int src_x, int src_y, int w, int h);
int gui_window_present_client(gui_window_t *win);
gui_app_t *gui_get_active_app(void);
gui_app_t *gui_get_window_app(gui_window_t *window);
gui_window_t *gui_create_app_window(gui_app_t *app, int x, int y, int w, int h, const char *title);
gui_window_t *gui_create_window(int x, int y, int w, int h, const char *title);
gui_window_t *gui_find_window(uint32_t window_id);
void gui_destroy_window(gui_window_t *window);
void gui_window_set_on_close(gui_window_t *window,
                             void (*cb)(gui_window_t *win, void *user_data),
                             void *user_data);
void gui_show_window(gui_window_t *window);
void gui_hide_window(gui_window_t *window);
void gui_minimize_window(gui_window_t *window);
void gui_restore_window(gui_window_t *window);
void gui_toggle_maximize_window(gui_window_t *window);
void gui_set_active_window(gui_window_t *window);
void gui_bring_to_front(gui_window_t *window);
gui_window_t *gui_get_window_at(int x, int y);
void gui_cycle_windows(void);

gui_widget_t *gui_add_label(gui_window_t *window, int x, int y, int w, int h, const char *text);
gui_widget_t *gui_add_button(gui_window_t *window, int x, int y, int w, int h, const char *text, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_panel(gui_window_t *window, int x, int y, int w, int h, uint32_t color);
gui_widget_t *gui_add_textbox(gui_window_t *window, int x, int y, int w, int h, const char *text);
gui_widget_t *gui_add_textarea(gui_window_t *window, int x, int y, int w, int h, const char *text);
gui_widget_t *gui_add_canvas(gui_window_t *window, int x, int y, int w, int h, uint32_t color);
gui_widget_t *gui_add_icon_button(gui_window_t *window, int x, int y, int w, int h, const char *text, gui_icon_id_t icon, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_toggle(gui_window_t *window, int x, int y, int w, int h, const char *text, int checked, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_checkbox(gui_window_t *window, int x, int y, int w, int h, const char *text, int checked, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_radiobutton(gui_window_t *window, int x, int y, int w, int h, const char *text, int group_id, int checked, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_select(gui_window_t *window, int x, int y, int w, int h, const char *items, int selected_index, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_combobox(gui_window_t *window, int x, int y, int w, int h, const char *items, int selected_index, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_listview(gui_window_t *window, int x, int y, int w, int h, const char *items, int selected_index, uint32_t flags, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_tableview(gui_window_t *window, int x, int y, int w, int h, const char *columns, const char *rows, int selected_row, uint32_t flags, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_menubar(gui_window_t *window, int x, int y, int w, int h, const char *menus, int active_index, gui_widget_callback_t cb, void *user_data);
int gui_menubar_set_menus(gui_widget_t *widget, const char *menus);
int gui_menubar_set_active(gui_widget_t *widget, int active_index);
int gui_menubar_get_active(gui_widget_t *widget, int *out_active_index);
gui_widget_t *gui_add_dialog(gui_window_t *window, int x, int y, int w, int h, const char *title, const char *message, uint32_t flags, gui_widget_callback_t cb, void *user_data);
int gui_dialog_set_message(gui_widget_t *widget, const char *message);
int gui_dialog_show(gui_widget_t *widget);
int gui_dialog_hide(gui_widget_t *widget);
gui_widget_t *gui_add_contextmenu(gui_window_t *window, int x, int y, int w, int h, const char *items, int selected_index, uint32_t disabled_mask, gui_widget_callback_t cb, void *user_data);
int gui_contextmenu_set_items(gui_widget_t *widget, const char *items);
int gui_contextmenu_set_selected(gui_widget_t *widget, int selected_index);
int gui_contextmenu_get_selected(gui_widget_t *widget, int *out_selected_index);
int gui_contextmenu_set_disabled_mask(gui_widget_t *widget, uint32_t disabled_mask);
int gui_contextmenu_show(gui_widget_t *widget, int x, int y);
int gui_contextmenu_hide(gui_widget_t *widget);
gui_widget_t *gui_add_treeview(gui_window_t *window, int x, int y, int w, int h, const char *nodes, int selected_node, uint32_t flags, gui_widget_callback_t cb, void *user_data);
int gui_select_set_selected(gui_widget_t *widget, int selected_index);
int gui_select_get_selected(gui_widget_t *widget, int *out_selected_index);
int gui_select_set_items(gui_widget_t *widget, const char *items);
int gui_listview_set_selected(gui_widget_t *widget, int selected_index);
int gui_listview_get_selected(gui_widget_t *widget, int *out_selected_index);
int gui_listview_set_items(gui_widget_t *widget, const char *items);
gui_widget_t *gui_add_slider(gui_window_t *window, int x, int y, int w, int h, int min, int max, int value, int step, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_scrollbar(gui_window_t *window, int x, int y, int w, int h, int min, int max, int value, int step, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_scrollview(gui_window_t *window, int x, int y, int w, int h, int content_w, int content_h);
gui_widget_t *gui_find_widget(gui_window_t *window, uint32_t id);
int gui_widget_set_parent(gui_widget_t *widget, gui_widget_t *parent);
void gui_widget_set_enabled(gui_widget_t *widget, int enabled);
void gui_widget_set_visible(gui_widget_t *widget, int visible);
int gui_widget_get_enabled(const gui_widget_t *widget);
void gui_widget_set_text(gui_widget_t *widget, const char *text);
void gui_widget_set_placeholder(gui_widget_t *widget, const char *placeholder);
void gui_widget_set_textbox_flags(gui_widget_t *widget, uint32_t flags);
uint32_t gui_widget_get_textbox_flags(const gui_widget_t *widget);
const char *gui_widget_get_text(const gui_widget_t *widget);
void gui_widget_set_button_flags(gui_widget_t *widget, uint32_t flags);
uint32_t gui_widget_get_button_flags(const gui_widget_t *widget);
void gui_widget_set_label_options(gui_widget_t *widget, uint32_t flags, uint32_t align);
uint32_t gui_widget_get_label_flags(const gui_widget_t *widget);
uint32_t gui_widget_get_label_align(const gui_widget_t *widget);
int gui_widget_measure_label(const gui_widget_t *widget, int max_width, int *out_width, int *out_height);
int gui_widget_fill_rect(gui_widget_t *widget, int x, int y, int w, int h, uint32_t color);
int gui_widget_draw_text(gui_widget_t *widget, int x, int y, const char *text, uint32_t color);
int gui_widget_blit_rgba32(gui_widget_t *widget, int x, int y, int w, int h, const uint32_t *pixels, uint32_t src_stride);
int gui_widget_scroll_rect(gui_widget_t *widget, int dst_x, int dst_y, int src_x, int src_y, int w, int h);
int gui_widget_present(gui_widget_t *widget);
void gui_widget_set_panel_options(gui_widget_t *widget, uint32_t bg_color, uint32_t border_color, uint32_t flags, uint32_t border_width, uint32_t padding);
int gui_toggle_set_checked(gui_widget_t *widget, int checked);
int gui_toggle_get_checked(const gui_widget_t *widget);
int gui_checkbox_set_checked(gui_widget_t *widget, int checked);
int gui_checkbox_get_checked(const gui_widget_t *widget);
int gui_radiobutton_set_checked(gui_widget_t *widget, int checked);
int gui_radiobutton_get_checked(const gui_widget_t *widget);
int gui_tableview_set_rows(gui_widget_t *widget, const char *rows);
int gui_tableview_set_selected(gui_widget_t *widget, int selected_row);
int gui_tableview_get_selected(gui_widget_t *widget, int *out_selected_row);
int gui_treeview_set_nodes(gui_widget_t *widget, const char *nodes);
int gui_treeview_set_selected(gui_widget_t *widget, int selected_node);
int gui_treeview_get_selected(gui_widget_t *widget, int *out_selected_node);
int gui_slider_set_value(gui_widget_t *widget, int value);
int gui_slider_get_value(gui_widget_t *widget, int *out_value);
int gui_slider_set_step(gui_widget_t *widget, int step);
int gui_slider_get_step(gui_widget_t *widget, int *out_step);
int gui_scrollbar_set_value(gui_widget_t *widget, int value);
int gui_scrollbar_get_value(gui_widget_t *widget, int *out_value);
int gui_scrollbar_set_step(gui_widget_t *widget, int step);
int gui_scrollbar_get_step(gui_widget_t *widget, int *out_step);
int gui_scrollview_set_offset(gui_widget_t *widget, int scroll_x, int scroll_y);
int gui_scrollview_get_offset(gui_widget_t *widget, int *out_scroll_x, int *out_scroll_y);
int gui_scrollview_set_content_size(gui_widget_t *widget, int content_w, int content_h);
int gui_scrollview_get_content_size(gui_widget_t *widget, int *out_content_w, int *out_content_h);
void gui_widget_set_colors(gui_widget_t *widget, uint32_t bg_color, uint32_t fg_color);
void gui_widget_set_on_click(gui_widget_t *widget, gui_widget_callback_t cb, void *user_data);
void gui_widget_set_icon(gui_widget_t *widget, gui_icon_id_t icon);
void gui_widget_focus(gui_widget_t *widget);

void gui_event_push(gui_event_t event);
int gui_event_pop(gui_event_t *event);
void gui_process_events(void);
void gui_post_key(char ch);
void gui_post_key_code(int key);
int gui_should_capture_key_code(int key);
void gui_invalidate_rect(int x, int y, int w, int h);
void gui_invalidate_all(void);
int gui_blit_rgba32(int x, int y, int w, int h, const uint32_t *pixels, uint32_t src_stride);
int gui_copy_rect(int dst_x, int dst_y, int src_x, int src_y, int w, int h);

void gui_terminal_init(void);
void gui_terminal_putc(char ch);
void gui_terminal_write(const char *text);
void gui_terminal_enqueue_output(const char *text);
void gui_terminal_clear(void);
void gui_terminal_redraw(void);
void gui_terminal_on_input(char ch);
void gui_terminal_set_input_focus(int focused);
int gui_terminal_is_active(void);
void gui_terminal_open(void);
void gui_terminal_minimize(void);
int gui_terminal_copy_selection(void);
int gui_terminal_has_clipboard_text(void);
const char *gui_terminal_get_clipboard_text(void);
void gui_terminal_clear_selection(void);

void gui_draw_text(int x, int y, const char *text, uint32_t color);
void gui_draw_char(int x, int y, char ch, uint32_t color);

#endif /* OPENOS_GUI_H */
