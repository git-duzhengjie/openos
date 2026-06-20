/* ============================================================
 * openos - User GUI ABI bridge
 * ============================================================ */

#include "gui_user.h"
#include "gui.h"
#include "string.h"

#define GUI_USER_TITLE_MAX 63u
#define GUI_USER_TEXT_MAX  63u
#define GUI_USER_MIN_W     80
#define GUI_USER_MIN_H     60
#define GUI_USER_MAX_W     1024
#define GUI_USER_MAX_H     768
#define GUI_USER_EVENT_QUEUE_CAP 32u

static gui_user_event_t g_gui_user_events[GUI_USER_EVENT_QUEUE_CAP];
static uint32_t g_gui_user_event_head;
static uint32_t g_gui_user_event_tail;

static int gui_user_event_queue_full(void) {
    return ((g_gui_user_event_tail + 1u) % GUI_USER_EVENT_QUEUE_CAP) == g_gui_user_event_head;
}

static int gui_user_event_queue_empty(void) {
    return g_gui_user_event_head == g_gui_user_event_tail;
}

static void gui_user_push_event(const gui_user_event_t *event) {
    if (!event) return;
    if (gui_user_event_queue_full()) {
        g_gui_user_event_head = (g_gui_user_event_head + 1u) % GUI_USER_EVENT_QUEUE_CAP;
    }
    g_gui_user_events[g_gui_user_event_tail] = *event;
    g_gui_user_event_tail = (g_gui_user_event_tail + 1u) % GUI_USER_EVENT_QUEUE_CAP;
}

static int gui_user_pop_event(gui_user_event_t *event) {
    if (!event || gui_user_event_queue_empty()) return 0;
    *event = g_gui_user_events[g_gui_user_event_head];
    g_gui_user_event_head = (g_gui_user_event_head + 1u) % GUI_USER_EVENT_QUEUE_CAP;
    return 1;
}

static void gui_user_button_on_click(gui_widget_t *widget, void *user_data) {
    gui_user_event_t event;
    (void)user_data;
    if (!widget || !widget->owner) return;
    event.type = GUI_EVENT_BUTTON_CLICK;
    event.window_id = widget->owner->id;
    event.widget_id = widget->id;
    event.x = widget->rect.x;
    event.y = widget->rect.y;
    event.key = 0;
    event.button = 1;
    gui_user_push_event(&event);
}

static void gui_user_copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    while (i + 1 < dst_size && src[i]) {
        char ch = src[i];
        dst[i] = ch;
        ++i;
    }
    dst[i] = '\0';
}

int gui_user_create_window(const char *title, int x, int y, int w, int h, uint32_t flags) {
    (void)flags;

    if (w < GUI_USER_MIN_W || h < GUI_USER_MIN_H || w > GUI_USER_MAX_W || h > GUI_USER_MAX_H) {
        return -1;
    }

    char safe_title[GUI_USER_TITLE_MAX + 1];
    gui_user_copy_text(safe_title, sizeof(safe_title), title ? title : "App");

    gui_window_t *win = gui_create_window(x, y, w, h, safe_title);
    if (!win) {
        return -1;
    }

    gui_show_window(win);
    gui_invalidate_all();
    return (int)win->id;
}

int gui_user_destroy_window(uint32_t window_id) {
    gui_window_t *win = gui_find_window(window_id);
    if (!win) {
        return -1;
    }

    gui_destroy_window(win);
    gui_invalidate_all();
    return 0;
}

int gui_user_add_label(uint32_t window_id, int x, int y, int w, int h, const char *text) {
    gui_window_t *win = gui_find_window(window_id);
    if (!win || w <= 0 || h <= 0) {
        return -1;
    }

    char safe_text[GUI_USER_TEXT_MAX + 1];
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "");

    gui_widget_t *widget = gui_add_label(win, x, y, w, h, safe_text);
    if (!widget) {
        return -1;
    }

    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_button(uint32_t window_id, int x, int y, int w, int h, const char *text) {
    gui_window_t *win = gui_find_window(window_id);
    if (!win || w <= 0 || h <= 0) {
        return -1;
    }

    char safe_text[GUI_USER_TEXT_MAX + 1];
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "Button");

    gui_widget_t *widget = gui_add_button(win, x, y, w, h, safe_text, gui_user_button_on_click, NULL);
    if (!widget) {
        return -1;
    }

    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_poll_event(gui_user_event_t *out_event) {
    if (!out_event) {
        return -1;
    }

    if (gui_user_pop_event(out_event)) {
        return 1;
    }

    out_event->type = GUI_EVENT_NONE;
    out_event->window_id = 0;
    out_event->widget_id = 0;
    out_event->x = 0;
    out_event->y = 0;
    out_event->key = 0;
    out_event->button = 0;
    return 0;
}
