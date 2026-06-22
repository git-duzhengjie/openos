/* ============================================================
 * openos - User GUI ABI bridge
 * ============================================================ */

#ifndef OPENOS_GUI_USER_H
#define OPENOS_GUI_USER_H

#include "types.h"

typedef struct gui_window gui_window_t;

#define GUI_USER_DRAW_FILL_RECT 1u
#define GUI_USER_DRAW_TEXT      2u
#define GUI_USER_DRAW_BLIT_RGBA32 3u
#define GUI_USER_DRAW_SCROLL    4u
#define GUI_USER_DRAW_PRESENT   5u

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
    char text[64];
} gui_user_widget_request_t;

typedef struct gui_user_draw_request {
    uint32_t window_id;
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
int gui_user_poll_event(gui_user_event_t *out_event);
void gui_user_post_key_event(gui_window_t *window, int key);
int gui_user_set_text(uint32_t window_id, uint32_t widget_id, const char *text);
int gui_user_draw(const gui_user_draw_request_t *request);
int gui_user_resize_window(uint32_t window_id, int w, int h);
int gui_user_get_window_info(uint32_t window_id, gui_user_window_info_t *out_info);
int gui_user_get_display_info(gui_user_display_info_t *out_info);
void gui_user_cleanup_process(uint32_t pid);

#endif /* OPENOS_GUI_USER_H */
