/* ============================================================
 * openos - User GUI ABI bridge
 * ============================================================ */

#ifndef OPENOS_GUI_USER_H
#define OPENOS_GUI_USER_H

#include "types.h"

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

int gui_user_create_window(const char *title, int x, int y, int w, int h, uint32_t flags);
int gui_user_destroy_window(uint32_t window_id);
int gui_user_add_label(uint32_t window_id, int x, int y, int w, int h, const char *text);
int gui_user_add_button(uint32_t window_id, int x, int y, int w, int h, const char *text);
int gui_user_poll_event(gui_user_event_t *out_event);
int gui_user_set_text(uint32_t window_id, uint32_t widget_id, const char *text);
void gui_user_cleanup_process(uint32_t pid);

#endif /* OPENOS_GUI_USER_H */
