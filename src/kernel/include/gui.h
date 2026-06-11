/* ============================================================
 * openos - Minimal GUI / Window System
 * ============================================================ */

#ifndef OPENOS_GUI_H
#define OPENOS_GUI_H

#include "types.h"

#define GUI_MAX_WINDOWS          16u
#define GUI_MAX_WIDGETS_PER_WIN  16u
#define GUI_TITLE_HEIGHT         22
#define GUI_BORDER_SIZE          2
#define GUI_CHAR_W               8
#define GUI_CHAR_H               8
#define GUI_TERM_COLS            128u
#define GUI_TERM_ROWS            48u
#define GUI_EVENT_QUEUE_SIZE     64u
#define GUI_MAX_DIRTY_RECTS      32u
#define GUI_TASKBAR_HEIGHT       24

#define GUI_WINDOW_FLAG_NONE      0x00000000u
#define GUI_WINDOW_FLAG_CLOSABLE  0x00000001u
#define GUI_WINDOW_FLAG_MINIMIZED 0x00000002u
#define GUI_WINDOW_FLAG_TERMINAL  0x00000004u
#define GUI_WINDOW_FLAG_MINIMIZABLE 0x00000008u

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
    GUI_WIDGET_TEXTBOX
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
    GUI_EVENT_KEY_DOWN
} gui_event_type_t;

typedef struct gui_window gui_window_t;
typedef struct gui_widget gui_widget_t;

typedef void (*gui_widget_callback_t)(gui_widget_t *widget, void *user_data);

typedef struct gui_event {
    gui_event_type_t type;
    int x;
    int y;
    int dx;
    int dy;
    uint8_t button;
    char key;
    gui_window_t *window;
    gui_widget_t *widget;
} gui_event_t;

struct gui_widget {
    uint32_t id;
    gui_widget_type_t type;
    gui_rect_t rect;
    char text[64];
    uint32_t bg_color;
    uint32_t fg_color;
    gui_widget_callback_t on_click;
    void *user_data;
    gui_window_t *owner;
    int visible;
    int enabled;
    int pressed;
    int focused;
    uint32_t cursor;
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
    gui_widget_t widgets[GUI_MAX_WIDGETS_PER_WIN];
    uint32_t widget_count;
};

typedef struct gui_terminal {
    gui_window_t *window;
    char cells[GUI_TERM_ROWS][GUI_TERM_COLS];
    uint32_t cols;
    uint32_t rows;
    uint32_t cursor_x;
    uint32_t cursor_y;
    int enabled;
    int input_focused;
    int dirty;
} gui_terminal_t;

typedef struct gui_system {
    int initialized;
    uint32_t width;
    uint32_t height;
    gui_color_scheme_t colors;
    gui_window_t windows[GUI_MAX_WINDOWS];
    uint32_t z_order[GUI_MAX_WINDOWS];
    uint32_t window_count;
    gui_window_t *active_window;
    gui_window_t *drag_window;
    gui_widget_t *pressed_widget;
    gui_widget_t *focused_widget;
    uint32_t next_window_id;
    uint32_t next_widget_id;

    gui_event_t events[GUI_EVENT_QUEUE_SIZE];
    uint32_t event_head;
    uint32_t event_tail;
    uint32_t event_count;

    int mouse_x;
    int mouse_y;
    uint8_t mouse_buttons;
    uint8_t last_mouse_buttons;
    int cursor_visible;

    uint32_t *backbuffer;
    uint32_t backbuffer_pixels;
    int double_buffered;
    gui_rect_t dirty_rects[GUI_MAX_DIRTY_RECTS];
    uint32_t dirty_count;
    int full_dirty;
    int clip_enabled;
    gui_rect_t clip_rect;

    gui_terminal_t terminal;
} gui_system_t;

void gui_init(void);
int gui_start(uint32_t width, uint32_t height);
int gui_is_ready(void);
void gui_shutdown_to_text_note(void);
void gui_set_cursor_visible(int visible);
int gui_is_cursor_visible(void);

const gui_system_t *gui_get_system(void);
void gui_print_info(void);
void gui_render(void);
void gui_demo(void);
void gui_poll(void);

gui_window_t *gui_create_window(int x, int y, int w, int h, const char *title);
void gui_destroy_window(gui_window_t *window);
void gui_show_window(gui_window_t *window);
void gui_hide_window(gui_window_t *window);
void gui_minimize_window(gui_window_t *window);
void gui_restore_window(gui_window_t *window);
void gui_set_active_window(gui_window_t *window);
void gui_bring_to_front(gui_window_t *window);

gui_widget_t *gui_add_label(gui_window_t *window, int x, int y, int w, int h, const char *text);
gui_widget_t *gui_add_button(gui_window_t *window, int x, int y, int w, int h, const char *text, gui_widget_callback_t cb, void *user_data);
gui_widget_t *gui_add_panel(gui_window_t *window, int x, int y, int w, int h, uint32_t color);
gui_widget_t *gui_add_textbox(gui_window_t *window, int x, int y, int w, int h, const char *text);

void gui_event_push(gui_event_t event);
int gui_event_pop(gui_event_t *event);
void gui_process_events(void);
void gui_post_key(char ch);
void gui_invalidate_rect(int x, int y, int w, int h);
void gui_invalidate_all(void);

void gui_terminal_init(void);
void gui_terminal_putc(char ch);
void gui_terminal_write(const char *text);
void gui_terminal_clear(void);
void gui_terminal_redraw(void);
void gui_terminal_on_input(char ch);
void gui_terminal_set_input_focus(int focused);

void gui_draw_text(int x, int y, const char *text, uint32_t color);
void gui_draw_char(int x, int y, char ch, uint32_t color);

#endif /* OPENOS_GUI_H */
