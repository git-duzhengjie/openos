/* ============================================================
 * openos - User GUI ABI bridge
 * ============================================================ */

#ifndef OPENOS_GUI_USER_H
#define OPENOS_GUI_USER_H

#include "types.h"

typedef struct gui_window gui_window_t;
typedef struct gui_widget gui_widget_t;

#define GUI_USER_DRAW_FILL_RECT 1u
#define GUI_USER_DRAW_TEXT      2u
#define GUI_USER_DRAW_BLIT_RGBA32 3u
#define GUI_USER_DRAW_SCROLL    4u
#define GUI_USER_DRAW_PRESENT   5u

#define GUI_USER_EVENT_NONE         0u
#define GUI_USER_EVENT_BUTTON_CLICK 1u
#define GUI_USER_EVENT_KEY_DOWN     2u
#define GUI_USER_EVENT_TEXT_INPUT   3u
#define GUI_USER_EVENT_TEXT_CHANGED 4u
#define GUI_USER_EVENT_TEXT_SUBMIT  5u
#define GUI_USER_EVENT_FOCUS        6u
#define GUI_USER_EVENT_BLUR         7u
#define GUI_USER_EVENT_VALUE_CHANGED 8u

typedef struct gui_user_event {
    uint32_t owner_pid;
    uint32_t type;
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t key;
    int32_t button;
} gui_user_event_t;

typedef struct gui_user_widget_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    char text[256];
} gui_user_widget_request_t;

typedef struct gui_user_icon_button_request {
    uint32_t window_id;
    uint32_t icon;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    char text[256];
} gui_user_icon_button_request_t;

typedef struct gui_user_iconview_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t selected_index;
    uint32_t flags;
    char items[256];
} gui_user_iconview_request_t;

typedef struct gui_user_toolbar_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t flags;
    char items[256];
} gui_user_toolbar_request_t;

typedef struct gui_user_statusbar_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t flags;
    char text[256];
} gui_user_statusbar_request_t;

typedef struct gui_user_tabview_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t active_index;
    int32_t tab_index;
    uint32_t flags;
    char tabs[256];
} gui_user_tabview_request_t;

typedef struct gui_user_splitview_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t ratio;
    uint32_t flags;
} gui_user_splitview_request_t;

typedef struct gui_user_radio_request {
    uint32_t window_id;
    uint32_t group_id;
    uint32_t checked;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    char text[256];
} gui_user_radio_request_t;

typedef struct gui_user_select_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t selected_index;
    char items[256];
} gui_user_select_request_t;

typedef struct gui_user_tableview_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t selected_row;
    uint32_t flags;
    char columns[128];
    char rows[256];
} gui_user_tableview_request_t;

typedef struct gui_user_menubar_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t active_index;
    char menus[256];
} gui_user_menubar_request_t;

typedef struct gui_user_dialog_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t flags;
    char title[64];
    char message[256];
} gui_user_dialog_request_t;

typedef struct gui_user_contextmenu_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t selected_index;
    uint32_t disabled_mask;
    char items[256];
} gui_user_contextmenu_request_t;

typedef struct gui_user_treeview_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t selected_node;
    uint32_t flags;
    char nodes[256];
} gui_user_treeview_request_t;

typedef struct gui_user_text_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t cursor;
    int32_t flags;
    char text[256];
} gui_user_text_request_t;

typedef struct gui_user_panel_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t flags;
    uint32_t border_width;
    uint32_t padding;
} gui_user_panel_request_t;

typedef struct gui_user_slider_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t min;
    int32_t max;
    int32_t value;
    int32_t step;
} gui_user_slider_request_t;

typedef struct gui_user_progressbar_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t min;
    int32_t max;
    int32_t value;
    uint32_t flags;
} gui_user_progressbar_request_t;

typedef struct gui_user_imageview_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t flags;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t stride;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t pixels_user_ptr;
    uint32_t pixels_size;
} gui_user_imageview_request_t;

typedef struct gui_user_scrollbar_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    int32_t min;
    int32_t max;
    int32_t value;
    int32_t step;
} gui_user_scrollbar_request_t;

typedef struct gui_user_label_measure_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t max_width;
    int32_t out_width;
    int32_t out_height;
} gui_user_label_measure_request_t;

typedef struct gui_user_draw_request {
    uint32_t window_id;
    uint32_t widget_id;
    uint32_t op;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t pixels_user_ptr;
    uint32_t src_stride;
    int32_t src_x;
    int32_t src_y;
    char text[128];
} gui_user_draw_request_t;

typedef struct gui_user_toast_request {
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t flags;
    uint32_t duration_ms;
    char message[256];
} gui_user_toast_request_t;

typedef struct gui_user_window_info {
    uint32_t window_id;
    uint32_t owner_pid;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t flags;
    uint32_t focused;
} gui_user_window_info_t;

typedef struct gui_user_display_info {
    int32_t width;
    int32_t height;
    uint32_t dpi_x;
    uint32_t dpi_y;
    uint32_t scale_milli;
} gui_user_display_info_t;

int gui_user_create_window(const char *title, int x, int y, int w, int h, uint32_t flags);
int gui_user_destroy_window(uint32_t window_id);
int gui_user_add_label(uint32_t window_id, int x, int y, int w, int h, const char *text);
int gui_user_add_button(uint32_t window_id, int x, int y, int w, int h, const char *text);
int gui_user_add_icon_button(uint32_t window_id, int x, int y, int w, int h, const char *text, uint32_t icon);
int gui_user_add_iconview(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index, uint32_t flags);
int gui_user_set_iconview_items(uint32_t window_id, uint32_t widget_id, const char *items);
int gui_user_set_iconview_selected(uint32_t window_id, uint32_t widget_id, int selected_index);
int gui_user_get_iconview_selected(uint32_t window_id, uint32_t widget_id, int *out_selected_index);
int gui_user_add_toolbar(uint32_t window_id, int x, int y, int w, int h, const char *items, uint32_t flags);
int gui_user_set_toolbar_items(uint32_t window_id, uint32_t widget_id, const char *items);
int gui_user_add_statusbar(uint32_t window_id, int x, int y, int w, int h, const char *text, uint32_t flags);
int gui_user_set_statusbar_text(uint32_t window_id, uint32_t widget_id, const char *text);
int gui_user_set_statusbar_flags(uint32_t window_id, uint32_t widget_id, uint32_t flags);
int gui_user_add_tabview(uint32_t window_id, int x, int y, int w, int h, const char *tabs, int active_index, uint32_t flags);
int gui_user_set_tabview_tabs(uint32_t window_id, uint32_t widget_id, const char *tabs);
int gui_user_set_tabview_active(uint32_t window_id, uint32_t widget_id, int active_index);
int gui_user_get_tabview_active(uint32_t window_id, uint32_t widget_id, int *out_active_index);
int gui_user_close_tabview_tab(uint32_t window_id, uint32_t widget_id, int tab_index);
int gui_user_add_splitview(uint32_t window_id, int x, int y, int w, int h, int ratio, uint32_t flags);
int gui_user_set_splitview_ratio(uint32_t window_id, uint32_t widget_id, int ratio);
int gui_user_get_splitview_ratio(uint32_t window_id, uint32_t widget_id, int *out_ratio);
int gui_user_add_panel(uint32_t window_id, int x, int y, int w, int h, uint32_t color);
int gui_user_add_canvas(uint32_t window_id, int x, int y, int w, int h, uint32_t color);
int gui_user_add_toggle(uint32_t window_id, int x, int y, int w, int h, const char *text, int checked);
int gui_user_add_checkbox(uint32_t window_id, int x, int y, int w, int h, const char *text, int checked);
int gui_user_add_radiobutton(uint32_t window_id, int x, int y, int w, int h, const char *text, int group_id, int checked);
int gui_user_add_select(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index);
int gui_user_add_combobox(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index);
int gui_user_add_listview(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index, uint32_t flags);
int gui_user_add_tableview(uint32_t window_id, int x, int y, int w, int h, const char *columns, const char *rows, int selected_row, uint32_t flags);
int gui_user_add_menubar(uint32_t window_id, int x, int y, int w, int h, const char *menus, int active_index);
int gui_user_add_dialog(uint32_t window_id, int x, int y, int w, int h, const char *title, const char *message, uint32_t flags);
int gui_user_add_toast(uint32_t window_id, int x, int y, int w, int h, const char *message, uint32_t flags, uint32_t duration_ms);
int gui_user_add_contextmenu(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index, uint32_t disabled_mask);
int gui_user_add_treeview(uint32_t window_id, int x, int y, int w, int h, const char *nodes, int selected_node, uint32_t flags);
int gui_user_add_slider(uint32_t window_id, int x, int y, int w, int h, int min, int max, int value, int step);
int gui_user_add_scrollbar(uint32_t window_id, int x, int y, int w, int h, int min, int max, int value, int step);
int gui_user_add_scrollview(uint32_t window_id, int x, int y, int w, int h, int content_w, int content_h);
int gui_user_add_textbox(uint32_t window_id, int x, int y, int w, int h, const char *text);
int gui_user_add_textarea(uint32_t window_id, int x, int y, int w, int h, const char *text);
int gui_user_poll_event(gui_user_event_t *out_event);
void gui_user_post_key_event(gui_window_t *window, int key);
void gui_user_post_text_event(gui_widget_t *widget, uint32_t event_type);
void gui_user_post_value_event(gui_widget_t *widget);
void gui_user_widget_click_at(gui_widget_t *widget, int x, int y);
int gui_user_set_text(uint32_t window_id, uint32_t widget_id, const char *text);
int gui_user_set_text_cursor(uint32_t window_id, uint32_t widget_id, const char *text, int cursor);
int gui_user_get_text_cursor(uint32_t window_id, uint32_t widget_id, char *out_text, uint32_t out_size, int *out_cursor);
int gui_user_get_text(uint32_t window_id, uint32_t widget_id, char *out_text, uint32_t out_size);
int gui_user_set_text_placeholder(uint32_t window_id, uint32_t widget_id, const char *placeholder);
int gui_user_set_text_flags(uint32_t window_id, uint32_t widget_id, uint32_t flags);
int gui_user_get_text_flags(uint32_t window_id, uint32_t widget_id, uint32_t *flags);
int gui_user_set_icon(uint32_t window_id, uint32_t widget_id, uint32_t icon);
int gui_user_set_button_flags(uint32_t window_id, uint32_t widget_id, uint32_t flags);
int gui_user_get_button_flags(uint32_t window_id, uint32_t widget_id, uint32_t *flags);
int gui_user_set_label_options(uint32_t window_id, uint32_t widget_id, uint32_t flags, uint32_t align);
int gui_user_get_label_options(uint32_t window_id, uint32_t widget_id, uint32_t *flags, uint32_t *align);
int gui_user_measure_label(uint32_t window_id, uint32_t widget_id, int max_width, int *out_width, int *out_height);
int gui_user_set_panel_options(uint32_t window_id, uint32_t widget_id, uint32_t bg_color, uint32_t border_color, uint32_t flags, uint32_t border_width, uint32_t padding);
int gui_user_set_slider_value(uint32_t window_id, uint32_t widget_id, int value);
int gui_user_get_slider_value(uint32_t window_id, uint32_t widget_id, int *out_value);
int gui_user_set_slider_step(uint32_t window_id, uint32_t widget_id, int step);
int gui_user_get_slider_step(uint32_t window_id, uint32_t widget_id, int *out_step);
int gui_user_add_progressbar(uint32_t window_id, int x, int y, int w, int h, int min, int max, int value, uint32_t flags);
int gui_user_set_progressbar_value(uint32_t window_id, uint32_t widget_id, int value);
int gui_user_get_progressbar_value(uint32_t window_id, uint32_t widget_id, int *out_value);
int gui_user_set_progressbar_flags(uint32_t window_id, uint32_t widget_id, uint32_t flags);
int gui_user_add_spinner(uint32_t window_id, int x, int y, int w, int h, const char *text, uint32_t flags);
int gui_user_add_imageview(uint32_t window_id, int x, int y, int w, int h, uint32_t flags);
int gui_user_set_imageview_rgba(uint32_t window_id, uint32_t widget_id, const uint32_t *pixels, uint32_t width, uint32_t height, uint32_t flags);
int gui_user_set_imageview_bitmap(uint32_t window_id, uint32_t widget_id, const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t stride, uint32_t fg_color, uint32_t bg_color, uint32_t flags);
int gui_user_set_spinner_running(uint32_t window_id, uint32_t widget_id, int running);
int gui_user_set_spinner_text(uint32_t window_id, uint32_t widget_id, const char *text);
int gui_user_set_scrollbar_value(uint32_t window_id, uint32_t widget_id, int value);
int gui_user_get_scrollbar_value(uint32_t window_id, uint32_t widget_id, int *out_value);
int gui_user_set_scrollbar_step(uint32_t window_id, uint32_t widget_id, int step);
int gui_user_get_scrollbar_step(uint32_t window_id, uint32_t widget_id, int *out_step);
int gui_user_set_scrollview_offset(uint32_t window_id, uint32_t widget_id, int scroll_x, int scroll_y);
int gui_user_get_scrollview_offset(uint32_t window_id, uint32_t widget_id, int *out_scroll_x, int *out_scroll_y);
int gui_user_set_scrollview_content_size(uint32_t window_id, uint32_t widget_id, int content_w, int content_h);
int gui_user_get_scrollview_content_size(uint32_t window_id, uint32_t widget_id, int *out_content_w, int *out_content_h);
int gui_user_set_widget_parent(uint32_t window_id, uint32_t widget_id, uint32_t parent_widget_id);
int gui_user_set_toggle_checked(uint32_t window_id, uint32_t widget_id, int checked);
int gui_user_get_toggle_checked(uint32_t window_id, uint32_t widget_id, int *out_checked);
int gui_user_set_checkbox_checked(uint32_t window_id, uint32_t widget_id, int checked);
int gui_user_get_checkbox_checked(uint32_t window_id, uint32_t widget_id, int *out_checked);
int gui_user_set_radiobutton_checked(uint32_t window_id, uint32_t widget_id, int checked);
int gui_user_get_radiobutton_checked(uint32_t window_id, uint32_t widget_id, int *out_checked);
int gui_user_set_select_index(uint32_t window_id, uint32_t widget_id, int selected_index);
int gui_user_get_select_index(uint32_t window_id, uint32_t widget_id, int *out_selected_index);
int gui_user_set_select_items(uint32_t window_id, uint32_t widget_id, const char *items);
int gui_user_set_listview_index(uint32_t window_id, uint32_t widget_id, int selected_index);
int gui_user_get_listview_index(uint32_t window_id, uint32_t widget_id, int *out_selected_index);
int gui_user_set_listview_items(uint32_t window_id, uint32_t widget_id, const char *items);
int gui_user_set_tableview_row(uint32_t window_id, uint32_t widget_id, int selected_row);
int gui_user_get_tableview_row(uint32_t window_id, uint32_t widget_id, int *out_selected_row);
int gui_user_set_tableview_rows(uint32_t window_id, uint32_t widget_id, const char *rows);
int gui_user_set_menubar_active(uint32_t window_id, uint32_t widget_id, int active_index);
int gui_user_get_menubar_active(uint32_t window_id, uint32_t widget_id, int *out_active_index);
int gui_user_set_menubar_menus(uint32_t window_id, uint32_t widget_id, const char *menus);
int gui_user_set_contextmenu_index(uint32_t window_id, uint32_t widget_id, int selected_index);
int gui_user_get_contextmenu_index(uint32_t window_id, uint32_t widget_id, int *out_selected_index);
int gui_user_set_contextmenu_items(uint32_t window_id, uint32_t widget_id, const char *items);
int gui_user_set_contextmenu_disabled(uint32_t window_id, uint32_t widget_id, uint32_t disabled_mask);
int gui_user_show_contextmenu(uint32_t window_id, uint32_t widget_id, int x, int y);
int gui_user_hide_contextmenu(uint32_t window_id, uint32_t widget_id);
int gui_user_set_dialog_message(uint32_t window_id, uint32_t widget_id, const char *message);
int gui_user_show_dialog(uint32_t window_id, uint32_t widget_id);
int gui_user_hide_dialog(uint32_t window_id, uint32_t widget_id);
int gui_user_show_toast(uint32_t window_id, uint32_t widget_id, uint32_t duration_ms);
int gui_user_hide_toast(uint32_t window_id, uint32_t widget_id);
int gui_user_set_treeview_node(uint32_t window_id, uint32_t widget_id, int selected_node);
int gui_user_get_treeview_node(uint32_t window_id, uint32_t widget_id, int *out_selected_node);
int gui_user_set_treeview_nodes(uint32_t window_id, uint32_t widget_id, const char *nodes);
int gui_user_set_widget_enabled(uint32_t window_id, uint32_t widget_id, int enabled);
int gui_user_get_widget_enabled(uint32_t window_id, uint32_t widget_id, int *out_enabled);
int gui_user_draw(const gui_user_draw_request_t *request);
int gui_user_resize_window(uint32_t window_id, int w, int h);
int gui_user_get_window_info(uint32_t window_id, gui_user_window_info_t *out_info);
int gui_user_get_display_info(gui_user_display_info_t *out_info);
void gui_user_cleanup_process(uint32_t pid);

#endif /* OPENOS_GUI_USER_H */
