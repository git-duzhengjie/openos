/* ============================================================
 * openos - User GUI ABI bridge
 * ============================================================ */

#include "gui_user.h"
#include "gui.h"
#include "process.h"
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

static uint32_t gui_user_current_pid(void);

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

    uint32_t owner_pid = gui_user_current_pid();
    uint32_t count = (g_gui_user_event_tail + GUI_USER_EVENT_QUEUE_CAP - g_gui_user_event_head) % GUI_USER_EVENT_QUEUE_CAP;
    for (uint32_t i = 0; i < count; ++i) {
        gui_user_event_t candidate = g_gui_user_events[g_gui_user_event_head];
        g_gui_user_event_head = (g_gui_user_event_head + 1u) % GUI_USER_EVENT_QUEUE_CAP;
        if (candidate.owner_pid == owner_pid) {
            *event = candidate;
            return 1;
        }
        g_gui_user_events[g_gui_user_event_tail] = candidate;
        g_gui_user_event_tail = (g_gui_user_event_tail + 1u) % GUI_USER_EVENT_QUEUE_CAP;
    }
    return 0;
}

static void gui_user_button_on_click(gui_widget_t *widget, void *user_data) {
    gui_user_event_t event;
    (void)user_data;
    if (!widget || !widget->owner || widget->owner->user_owner_pid == 0) return;
    event.owner_pid = widget->owner->user_owner_pid;
    event.type = GUI_EVENT_BUTTON_CLICK;
    event.window_id = widget->owner->id;
    event.widget_id = widget->id;
    event.x = widget->rect.x;
    event.y = widget->rect.y;
    event.key = 0;
    event.button = 1;
    gui_user_push_event(&event);
}

static uint32_t gui_user_current_pid(void) {
    int pid = proc_current_pid();
    return pid > 0 ? (uint32_t)pid : 0;
}

static int gui_user_window_owned_by_current(gui_window_t *win) {
    uint32_t pid = gui_user_current_pid();
    return win && win->used && pid != 0 && win->user_owner_pid == pid;
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

    uint32_t owner_pid = gui_user_current_pid();
    if (owner_pid == 0) {
        return -1;
    }

    char safe_title[GUI_USER_TITLE_MAX + 1];
    gui_user_copy_text(safe_title, sizeof(safe_title), title ? title : "App");

    gui_window_t *win = gui_create_window(x, y, w, h, safe_title);
    if (!win) {
        return -1;
    }

    win->user_owner_pid = owner_pid;
    gui_show_window(win);
    gui_invalidate_all();
    return (int)win->id;
}

int gui_user_destroy_window(uint32_t window_id) {
    gui_window_t *win = gui_find_window(window_id);
    if (!gui_user_window_owned_by_current(win)) {
        return -1;
    }

    gui_destroy_window(win);
    gui_invalidate_all();
    return 0;
}

int gui_user_add_label(uint32_t window_id, int x, int y, int w, int h, const char *text) {
    gui_window_t *win = gui_find_window(window_id);
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
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
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
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

    out_event->owner_pid = gui_user_current_pid();
    out_event->type = GUI_EVENT_NONE;
    out_event->window_id = 0;
    out_event->widget_id = 0;
    out_event->x = 0;
    out_event->y = 0;
    out_event->key = 0;
    out_event->button = 0;
    return 0;
}

int gui_user_set_text(uint32_t window_id, uint32_t widget_id, const char *text) {
    gui_window_t *win = gui_find_window(window_id);
    if (!gui_user_window_owned_by_current(win) || !text) {
        return -1;
    }

    gui_widget_t *widget = gui_find_widget(win, widget_id);
    if (!widget) {
        return -1;
    }

    char safe_text[GUI_USER_TEXT_MAX + 1];
    gui_user_copy_text(safe_text, sizeof(safe_text), text);
    gui_widget_set_text(widget, safe_text);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_draw(const gui_user_draw_request_t *request) {
    if (!request) {
        return -1;
    }

    gui_window_t *win = gui_find_window(request->window_id);
    if (!gui_user_window_owned_by_current(win)) {
        return -1;
    }

    if (request->op == GUI_USER_DRAW_FILL_RECT) {
        return gui_window_fill_client_rect(win, request->x, request->y, request->w, request->h, request->bg_color);
    }

    if (request->op == GUI_USER_DRAW_TEXT) {
        char safe_text[129];
        gui_user_copy_text(safe_text, sizeof(safe_text), request->text);
        return gui_window_draw_client_text(win, request->x, request->y, safe_text, request->fg_color);
    }

    return -1;
}

void gui_user_cleanup_process(uint32_t pid) {
    if (pid == 0) {
        return;
    }

    uint32_t read = g_gui_user_event_head;
    uint32_t write = g_gui_user_event_head;
    while (read != g_gui_user_event_tail) {
        gui_user_event_t event = g_gui_user_events[read];
        read = (read + 1u) % GUI_USER_EVENT_QUEUE_CAP;
        if (event.owner_pid != pid) {
            g_gui_user_events[write] = event;
            write = (write + 1u) % GUI_USER_EVENT_QUEUE_CAP;
        }
    }
    g_gui_user_event_tail = write;

    gui_destroy_windows_by_user_owner(pid);
}
