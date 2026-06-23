/* ============================================================
 * openos - User GUI ABI bridge
 * ============================================================ */

#include "gui_user.h"
#include "gui.h"
#include "process.h"
#include "string.h"

#define GUI_USER_TITLE_MAX 63u
#define GUI_USER_TEXT_MAX  255u
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

void gui_user_widget_click_at(gui_widget_t *widget, int x, int y) {
    gui_user_event_t event;
    if (!widget || !widget->owner || widget->owner->user_owner_pid == 0) return;
    event.owner_pid = widget->owner->user_owner_pid;
    event.type = GUI_USER_EVENT_BUTTON_CLICK;
    event.window_id = widget->owner->id;
    event.widget_id = widget->id;
    event.x = x;
    event.y = y;
    event.key = 0;
    event.button = 1;
    gui_user_push_event(&event);
}

static void gui_user_button_on_click(gui_widget_t *widget, void *user_data) {
    (void)user_data;
    if (!widget) return;
    gui_user_widget_click_at(widget, widget->rect.x, widget->rect.y);
}

void gui_user_post_value_event(gui_widget_t *widget) {
    gui_user_event_t event;
    if (!widget || !widget->owner || widget->owner->user_owner_pid == 0) return;
    event.owner_pid = widget->owner->user_owner_pid;
    event.type = GUI_USER_EVENT_VALUE_CHANGED;
    event.window_id = widget->owner->id;
    event.widget_id = widget->id;
    event.x = widget->value;
    event.y = 0;
    event.key = 0;
    event.button = 0;
    gui_user_push_event(&event);
}

void gui_user_post_key_event(gui_window_t *window, int key) {
    gui_user_event_t event;
    if (!window || window->user_owner_pid == 0 || key == 0) return;
    memset(&event, 0, sizeof(event));
    event.owner_pid = window->user_owner_pid;
    event.type = GUI_USER_EVENT_KEY_DOWN;
    event.window_id = window->id;
    gui_widget_t *focused = gui_get_focused_widget();
    event.widget_id = (focused && focused->owner == window) ? focused->id : 0;
    event.key = key;
    gui_user_push_event(&event);
}

void gui_user_post_text_event(gui_widget_t *widget, uint32_t event_type) {
    gui_user_event_t event;
    if (!widget || !widget->owner || widget->owner->user_owner_pid == 0) return;
    if (event_type != GUI_EVENT_TEXT_CHANGED && event_type != GUI_EVENT_TEXT_SUBMIT &&
        event_type != GUI_EVENT_FOCUS && event_type != GUI_EVENT_BLUR) return;
    memset(&event, 0, sizeof(event));
    event.owner_pid = widget->owner->user_owner_pid;
    if (event_type == GUI_EVENT_TEXT_CHANGED) event.type = GUI_USER_EVENT_TEXT_CHANGED;
    else if (event_type == GUI_EVENT_TEXT_SUBMIT) event.type = GUI_USER_EVENT_TEXT_SUBMIT;
    else if (event_type == GUI_EVENT_FOCUS) event.type = GUI_USER_EVENT_FOCUS;
    else event.type = GUI_USER_EVENT_BLUR;
    event.window_id = widget->owner->id;
    event.widget_id = widget->id;
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

int gui_user_add_icon_button(uint32_t window_id, int x, int y, int w, int h, const char *text, uint32_t icon) {
    gui_window_t *win = gui_find_window(window_id);
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }

    char safe_text[GUI_USER_TEXT_MAX + 1];
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "");

    gui_widget_t *widget = gui_add_icon_button(win, x, y, w, h, safe_text, (gui_icon_id_t)icon, gui_user_button_on_click, NULL);
    if (!widget) {
        return -1;
    }

    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_toolbar(uint32_t window_id, int x, int y, int w, int h, const char *items, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    gui_user_copy_text(safe_items, sizeof(safe_items), items ? items : "");
    widget = gui_add_toolbar(win, x, y, w, h, safe_items, flags);
    if (!widget) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_set_toolbar_items(uint32_t window_id, uint32_t widget_id, const char *items) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TOOLBAR) return -1;
    gui_user_copy_text(safe_items, sizeof(safe_items), items ? items : "");
    if (gui_toolbar_set_items(widget, safe_items) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_add_statusbar(uint32_t window_id, int x, int y, int w, int h, const char *text, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_text[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "");
    widget = gui_add_statusbar(win, x, y, w, h, safe_text, flags);
    if (!widget) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_set_statusbar_text(uint32_t window_id, uint32_t widget_id, const char *text) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_text[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_STATUSBAR) return -1;
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "");
    if (gui_statusbar_set_text(widget, safe_text) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_statusbar_flags(uint32_t window_id, uint32_t widget_id, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_STATUSBAR) return -1;
    if (gui_statusbar_set_flags(widget, flags) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_add_tabview(uint32_t window_id, int x, int y, int w, int h, const char *tabs, int active_index, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_tabs[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    gui_user_copy_text(safe_tabs, sizeof(safe_tabs), tabs ? tabs : "");
    widget = gui_add_tabview(win, x, y, w, h, safe_tabs, active_index, flags, NULL, NULL);
    if (!widget) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_set_tabview_tabs(uint32_t window_id, uint32_t widget_id, const char *tabs) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_tabs[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TABVIEW) return -1;
    gui_user_copy_text(safe_tabs, sizeof(safe_tabs), tabs ? tabs : "");
    if (gui_tabview_set_tabs(widget, safe_tabs) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_tabview_active(uint32_t window_id, uint32_t widget_id, int active_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TABVIEW) return -1;
    if (gui_tabview_set_active(widget, active_index) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_tabview_active(uint32_t window_id, uint32_t widget_id, int *out_active_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || !out_active_index) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TABVIEW) return -1;
    return gui_tabview_get_active(widget, out_active_index);
}

int gui_user_close_tabview_tab(uint32_t window_id, uint32_t widget_id, int tab_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TABVIEW) return -1;
    if (gui_tabview_close_tab(widget, tab_index) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_add_groupbox(uint32_t window_id, int x, int y, int w, int h, const char *title, uint32_t bg_color, uint32_t border_color, uint32_t flags, uint32_t padding) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_title[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    gui_user_copy_text(safe_title, sizeof(safe_title), title ? title : "");
    widget = gui_add_groupbox(win, x, y, w, h, safe_title);
    if (!widget) return -1;
    gui_widget_set_groupbox_options(widget, safe_title, bg_color, border_color, flags, padding);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_set_groupbox_options(uint32_t window_id, uint32_t widget_id, const char *title, uint32_t bg_color, uint32_t border_color, uint32_t flags, uint32_t padding) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_title[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_GROUPBOX) return -1;
    gui_user_copy_text(safe_title, sizeof(safe_title), title ? title : widget->text);
    gui_widget_set_groupbox_options(widget, safe_title, bg_color, border_color, flags, padding);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_add_form(uint32_t window_id, int x, int y, int w, int h, const char *title, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_title[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    gui_user_copy_text(safe_title, sizeof(safe_title), title ? title : "Form");
    widget = gui_add_form(win, x, y, w, h, safe_title, flags);
    if (!widget) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_form_field(uint32_t window_id, uint32_t form_id, int row, const char *label, const char *value, const char *hint, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *form;
    gui_widget_t *input;
    char safe_label[129];
    char safe_value[129];
    char safe_hint[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    form = gui_find_widget(win, form_id);
    if (!form || form->type != GUI_WIDGET_GROUPBOX) return -1;
    gui_user_copy_text(safe_label, sizeof(safe_label), label ? label : "Label");
    gui_user_copy_text(safe_value, sizeof(safe_value), value ? value : "");
    gui_user_copy_text(safe_hint, sizeof(safe_hint), hint ? hint : "");
    input = gui_add_form_field(win, form, row, safe_label, safe_value, safe_hint, flags);
    if (!input) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)input->id;
}

int gui_user_add_form_submit(uint32_t window_id, uint32_t form_id, int row, const char *text) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *form;
    gui_widget_t *button;
    char safe_text[65];
    if (!gui_user_window_owned_by_current(win)) return -1;
    form = gui_find_widget(win, form_id);
    if (!form || form->type != GUI_WIDGET_GROUPBOX) return -1;
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "Submit");
    button = gui_add_form_submit(win, form, safe_text, row);
    if (!button) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)button->id;
}

int gui_user_add_splitview(uint32_t window_id, int x, int y, int w, int h, int ratio, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    widget = gui_add_splitview(win, x, y, w, h, ratio, flags, NULL, NULL);
    if (!widget) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_set_splitview_ratio(uint32_t window_id, uint32_t widget_id, int ratio) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SPLITVIEW) return -1;
    if (gui_splitview_set_ratio(widget, ratio) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_splitview_ratio(uint32_t window_id, uint32_t widget_id, int *out_ratio) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || !out_ratio) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SPLITVIEW) return -1;
    return gui_splitview_get_ratio(widget, out_ratio);
}

int gui_user_add_iconview(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    gui_user_copy_text(safe_items, sizeof(safe_items), items ? items : "");
    widget = gui_add_iconview(win, x, y, w, h, safe_items, selected_index, flags, NULL, NULL);
    if (!widget) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_set_iconview_items(uint32_t window_id, uint32_t widget_id, const char *items) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_ICONVIEW) return -1;
    gui_user_copy_text(safe_items, sizeof(safe_items), items ? items : "");
    if (gui_iconview_set_items(widget, safe_items) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_iconview_selected(uint32_t window_id, uint32_t widget_id, int selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_ICONVIEW) return -1;
    old_value = widget->value;
    if (gui_iconview_set_selected(widget, selected_index) < 0) return -1;
    if (old_value != widget->value) gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_iconview_selected(uint32_t window_id, uint32_t widget_id, int *out_selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_selected_index || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_ICONVIEW) return -1;
    return gui_iconview_get_selected(widget, out_selected_index);
}

int gui_user_add_panel(uint32_t window_id, int x, int y, int w, int h, uint32_t color) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    widget = gui_add_panel(win, x, y, w, h, color);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_canvas(uint32_t window_id, int x, int y, int w, int h, uint32_t color) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    widget = gui_add_canvas(win, x, y, w, h, color);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_toggle(uint32_t window_id, int x, int y, int w, int h, const char *text, int checked) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_text[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "");
    widget = gui_add_toggle(win, x, y, w, h, safe_text, checked, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_checkbox(uint32_t window_id, int x, int y, int w, int h, const char *text, int checked) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_text[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "");
    widget = gui_add_checkbox(win, x, y, w, h, safe_text, checked, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_radiobutton(uint32_t window_id, int x, int y, int w, int h, const char *text, int group_id, int checked) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_text[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "");
    widget = gui_add_radiobutton(win, x, y, w, h, safe_text, group_id, checked, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_select(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    gui_user_copy_text(safe_items, sizeof(safe_items), items ? items : "");
    widget = gui_add_select(win, x, y, w, h, safe_items, selected_index, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_combobox(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    gui_user_copy_text(safe_items, sizeof(safe_items), items ? items : "");
    widget = gui_add_combobox(win, x, y, w, h, safe_items, selected_index, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_slider(uint32_t window_id, int x, int y, int w, int h, int min, int max, int value, int step) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    widget = gui_add_slider(win, x, y, w, h, min, max, value, step, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_progressbar(uint32_t window_id, int x, int y, int w, int h, int min, int max, int value, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    widget = gui_add_progressbar(win, x, y, w, h, min, max, value, flags);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_imageview(uint32_t window_id, int x, int y, int w, int h, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    widget = gui_add_imageview(win, x, y, w, h, flags);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_set_imageview_rgba(uint32_t window_id, uint32_t widget_id, const uint32_t *pixels, uint32_t width, uint32_t height, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (gui_imageview_set_rgba(widget, pixels, width, height, flags) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_imageview_bitmap(uint32_t window_id, uint32_t widget_id, const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t stride, uint32_t fg_color, uint32_t bg_color, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (gui_imageview_set_bitmap(widget, pixels, width, height, stride, fg_color, bg_color, flags) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_add_scrollbar(uint32_t window_id, int x, int y, int w, int h, int min, int max, int value, int step) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    widget = gui_add_scrollbar(win, x, y, w, h, min, max, value, step, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_scrollview(uint32_t window_id, int x, int y, int w, int h, int content_w, int content_h) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    widget = gui_add_scrollview(win, x, y, w, h, content_w, content_h);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_textbox(uint32_t window_id, int x, int y, int w, int h, const char *text) {
    gui_window_t *win = gui_find_window(window_id);
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }

    char safe_text[GUI_USER_TEXT_MAX + 1];
    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "");

    gui_widget_t *widget = gui_add_textbox(win, x, y, w, h, safe_text);
    if (!widget) {
        return -1;
    }

    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_textarea(uint32_t window_id, int x, int y, int w, int h, const char *text) {
    gui_window_t *win = gui_find_window(window_id);
    char safe_text[GUI_USER_TEXT_MAX + 1];
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }

    gui_user_copy_text(safe_text, sizeof(safe_text), text ? text : "");
    widget = gui_add_textarea(win, x, y, w, h, safe_text);
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
    out_event->type = GUI_USER_EVENT_NONE;
    out_event->window_id = 0;
    out_event->widget_id = 0;
    out_event->x = 0;
    out_event->y = 0;
    out_event->key = 0;
    out_event->button = 0;
    return 0;
}


int gui_user_add_listview(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    gui_user_copy_text(safe_items, sizeof(safe_items), items ? items : "");
    widget = gui_add_listview(win, x, y, w, h, safe_items, selected_index, flags, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_add_tableview(uint32_t window_id, int x, int y, int w, int h, const char *columns, const char *rows, int selected_row, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_columns[GUI_USER_TEXT_MAX + 1];
    char safe_rows[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    gui_user_copy_text(safe_columns, sizeof(safe_columns), columns ? columns : "");
    gui_user_copy_text(safe_rows, sizeof(safe_rows), rows ? rows : "");
    widget = gui_add_tableview(win, x, y, w, h, safe_columns, safe_rows, selected_row, flags, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}


int gui_user_add_menubar(uint32_t window_id, int x, int y, int w, int h, const char *menus, int active_index) {
    gui_window_t *win = gui_find_window(window_id);
    char safe_menus[256];
    gui_widget_t *wg;
    if (!gui_user_window_owned_by_current(win)) return -1;
    gui_user_copy_text(safe_menus, sizeof(safe_menus), menus);
    wg = gui_add_menubar(win, x, y, w, h, safe_menus, active_index, NULL, NULL);
    return wg ? (int)wg->id : -1;
}

int gui_user_add_dialog(uint32_t window_id, int x, int y, int w, int h, const char *title, const char *message, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    char safe_title[64];
    char safe_message[GUI_USER_TEXT_MAX + 1];
    gui_widget_t *wg;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    gui_user_copy_text(safe_title, sizeof(safe_title), title ? title : "Dialog");
    gui_user_copy_text(safe_message, sizeof(safe_message), message ? message : "");
    wg = gui_add_dialog(win, x, y, w, h, safe_title, safe_message, flags, NULL, NULL);
    if (!wg) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)wg->id;
}

int gui_user_set_dialog_message(uint32_t window_id, uint32_t widget_id, const char *message) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_message[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_DIALOG) return -1;
    gui_user_copy_text(safe_message, sizeof(safe_message), message ? message : "");
    if (gui_dialog_set_message(widget, safe_message) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_show_dialog(uint32_t window_id, uint32_t widget_id) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (gui_dialog_show(widget) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_hide_dialog(uint32_t window_id, uint32_t widget_id) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (gui_dialog_hide(widget) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_add_toast(uint32_t window_id, int x, int y, int w, int h, const char *message, uint32_t flags, uint32_t duration_ms) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_message[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    gui_user_copy_text(safe_message, sizeof(safe_message), message ? message : "");
    widget = gui_add_toast(win, x, y, w, h, safe_message, flags, duration_ms, NULL, NULL);
    if (!widget) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_show_toast(uint32_t window_id, uint32_t widget_id, uint32_t duration_ms) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (gui_toast_show(widget, duration_ms) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_hide_toast(uint32_t window_id, uint32_t widget_id) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (gui_toast_hide(widget) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_add_treeview(uint32_t window_id, int x, int y, int w, int h, const char *nodes, int selected_node, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_nodes[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) {
        return -1;
    }
    gui_user_copy_text(safe_nodes, sizeof(safe_nodes), nodes ? nodes : "");
    widget = gui_add_treeview(win, x, y, w, h, safe_nodes, selected_node, flags, NULL, NULL);
    if (!widget) {
        return -1;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_set_text_cursor(uint32_t window_id, uint32_t widget_id, const char *text, int cursor) {
    gui_window_t *win = gui_find_window(window_id);
    char safe_text[GUI_USER_TEXT_MAX + 1];
    gui_widget_t *widget;
    uint32_t len;
    if (!gui_user_window_owned_by_current(win) || !text) {
        return -1;
    }

    widget = gui_find_widget(win, widget_id);
    if (!widget) {
        return -1;
    }

    gui_user_copy_text(safe_text, sizeof(safe_text), text);
    gui_widget_set_text(widget, safe_text);
    if (widget->type == GUI_WIDGET_TEXTBOX && cursor >= 0) {
        len = (uint32_t)strlen(widget->text);
        widget->cursor = (uint32_t)cursor;
        if (widget->cursor > len) widget->cursor = len;
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_text(uint32_t window_id, uint32_t widget_id, const char *text) {
    return gui_user_set_text_cursor(window_id, widget_id, text, -1);
}

int gui_user_get_text_cursor(uint32_t window_id, uint32_t widget_id, char *out_text, uint32_t out_size, int *out_cursor) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_text || out_size == 0 || !gui_user_window_owned_by_current(win)) {
        return -1;
    }
    widget = gui_find_widget(win, widget_id);
    if (!widget) {
        return -1;
    }
    gui_user_copy_text(out_text, out_size, widget->text);
    if (out_cursor) {
        uint32_t len = (uint32_t)strlen(widget->text);
        if (widget->cursor > len) widget->cursor = len;
        *out_cursor = (int)widget->cursor;
    }
    return 0;
}

int gui_user_get_text(uint32_t window_id, uint32_t widget_id, char *out_text, uint32_t out_size) {
    return gui_user_get_text_cursor(window_id, widget_id, out_text, out_size, 0);
}

int gui_user_set_text_placeholder(uint32_t window_id, uint32_t widget_id, const char *placeholder) {
    gui_window_t *win = gui_find_window(window_id);
    char safe_text[GUI_USER_TEXT_MAX + 1];
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || !placeholder) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || (widget->type != GUI_WIDGET_TEXTBOX && widget->type != GUI_WIDGET_TEXTAREA)) return -1;
    gui_user_copy_text(safe_text, sizeof(safe_text), placeholder);
    gui_widget_set_placeholder(widget, safe_text);
    return 0;
}

int gui_user_set_text_flags(uint32_t window_id, uint32_t widget_id, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || (widget->type != GUI_WIDGET_TEXTBOX && widget->type != GUI_WIDGET_TEXTAREA)) return -1;
    gui_widget_set_textbox_flags(widget, flags);
    return 0;
}

int gui_user_get_text_flags(uint32_t window_id, uint32_t widget_id, uint32_t *flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!flags || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || (widget->type != GUI_WIDGET_TEXTBOX && widget->type != GUI_WIDGET_TEXTAREA)) return -1;
    *flags = gui_widget_get_textbox_flags(widget);
    return 0;
}

int gui_user_set_icon(uint32_t window_id, uint32_t widget_id, uint32_t icon) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || (widget->type != GUI_WIDGET_BUTTON && widget->type != GUI_WIDGET_ICON_BUTTON)) return -1;
    gui_widget_set_icon(widget, (gui_icon_id_t)icon);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_button_flags(uint32_t window_id, uint32_t widget_id, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || (widget->type != GUI_WIDGET_BUTTON && widget->type != GUI_WIDGET_ICON_BUTTON)) return -1;
    gui_widget_set_button_flags(widget, flags);
    return 0;
}

int gui_user_get_button_flags(uint32_t window_id, uint32_t widget_id, uint32_t *flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!flags || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || (widget->type != GUI_WIDGET_BUTTON && widget->type != GUI_WIDGET_ICON_BUTTON)) return -1;
    *flags = gui_widget_get_button_flags(widget);
    return 0;
}

int gui_user_set_label_options(uint32_t window_id, uint32_t widget_id, uint32_t flags, uint32_t align) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_LABEL) return -1;
    gui_widget_set_label_options(widget, flags, align);
    return 0;
}

int gui_user_get_label_options(uint32_t window_id, uint32_t widget_id, uint32_t *flags, uint32_t *align) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!flags || !align || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_LABEL) return -1;
    *flags = gui_widget_get_label_flags(widget);
    *align = gui_widget_get_label_align(widget);
    return 0;
}

int gui_user_measure_label(uint32_t window_id, uint32_t widget_id, int max_width, int *out_width, int *out_height) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_width || !out_height || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_LABEL) return -1;
    return gui_widget_measure_label(widget, max_width, out_width, out_height);
}

int gui_user_set_panel_options(uint32_t window_id, uint32_t widget_id, uint32_t bg_color, uint32_t border_color, uint32_t flags, uint32_t border_width, uint32_t padding) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_PANEL) return -1;
    gui_widget_set_panel_options(widget, bg_color, border_color, flags, border_width, padding);
    return 0;
}

int gui_user_set_toggle_checked(uint32_t window_id, uint32_t widget_id, int checked) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TOGGLE) return -1;
    old_value = widget->value;
    if (gui_toggle_set_checked(widget, checked) < 0) return -1;
    if (widget->value != old_value) gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_toggle_checked(uint32_t window_id, uint32_t widget_id, int *out_checked) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_checked || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TOGGLE) return -1;
    *out_checked = gui_toggle_get_checked(widget);
    return 0;
}

int gui_user_set_checkbox_checked(uint32_t window_id, uint32_t widget_id, int checked) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_CHECKBOX) return -1;
    old_value = widget->value;
    if (gui_checkbox_set_checked(widget, checked) < 0) return -1;
    if (widget->value != old_value) gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_checkbox_checked(uint32_t window_id, uint32_t widget_id, int *out_checked) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_checked || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_CHECKBOX) return -1;
    *out_checked = gui_checkbox_get_checked(widget);
    return 0;
}

int gui_user_set_radiobutton_checked(uint32_t window_id, uint32_t widget_id, int checked) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_RADIOBUTTON) return -1;
    old_value = widget->value;
    if (gui_radiobutton_set_checked(widget, checked) < 0) return -1;
    if (widget->value != old_value) gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_radiobutton_checked(uint32_t window_id, uint32_t widget_id, int *out_checked) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_checked || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_RADIOBUTTON) return -1;
    *out_checked = gui_radiobutton_get_checked(widget);
    return 0;
}

int gui_user_set_select_index(uint32_t window_id, uint32_t widget_id, int selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || (widget->type != GUI_WIDGET_SELECT && widget->type != GUI_WIDGET_COMBOBOX)) return -1;
    old_value = widget->value;
    if (gui_select_set_selected(widget, selected_index) < 0) return -1;
    widget->step = 0;
    if (widget->value != old_value) gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_select_index(uint32_t window_id, uint32_t widget_id, int *out_selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_selected_index || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || (widget->type != GUI_WIDGET_SELECT && widget->type != GUI_WIDGET_COMBOBOX)) return -1;
    return gui_select_get_selected(widget, out_selected_index);
}

int gui_user_set_select_items(uint32_t window_id, uint32_t widget_id, const char *items) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || (widget->type != GUI_WIDGET_SELECT && widget->type != GUI_WIDGET_COMBOBOX)) return -1;
    gui_user_copy_text(safe_items, sizeof(safe_items), items ? items : "");
    if (gui_select_set_items(widget, safe_items) < 0) return -1;
    widget->step = 0;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}


int gui_user_set_listview_index(uint32_t window_id, uint32_t widget_id, int selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_LISTVIEW) return -1;
    old_value = widget->value;
    if (gui_listview_set_selected(widget, selected_index) < 0) return -1;
    if (widget->value != old_value) gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_listview_index(uint32_t window_id, uint32_t widget_id, int *out_selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_selected_index || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_LISTVIEW) return -1;
    return gui_listview_get_selected(widget, out_selected_index);
}

int gui_user_set_listview_items(uint32_t window_id, uint32_t widget_id, const char *items) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_LISTVIEW) return -1;
    gui_user_copy_text(safe_items, sizeof(safe_items), items ? items : "");
    if (gui_listview_set_items(widget, safe_items) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_tableview_row(uint32_t window_id, uint32_t widget_id, int selected_row) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TABLEVIEW) return -1;
    old_value = widget->value;
    if (gui_tableview_set_selected(widget, selected_row) < 0) return -1;
    if (widget->value != old_value) gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_tableview_row(uint32_t window_id, uint32_t widget_id, int *out_selected_row) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_selected_row || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TABLEVIEW) return -1;
    return gui_tableview_get_selected(widget, out_selected_row);
}

int gui_user_set_tableview_rows(uint32_t window_id, uint32_t widget_id, const char *rows) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_rows[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TABLEVIEW) return -1;
    gui_user_copy_text(safe_rows, sizeof(safe_rows), rows ? rows : "");
    if (gui_tableview_set_rows(widget, safe_rows) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}


int gui_user_set_menubar_active(uint32_t window_id, uint32_t widget_id, int active_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    return gui_menubar_set_active(widget, active_index);
}

int gui_user_get_menubar_active(uint32_t window_id, uint32_t widget_id, int *out_active_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_active_index || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    return gui_menubar_get_active(widget, out_active_index);
}

int gui_user_set_menubar_menus(uint32_t window_id, uint32_t widget_id, const char *menus) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_menus[256];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    gui_user_copy_text(safe_menus, sizeof(safe_menus), menus);
    return gui_menubar_set_menus(widget, safe_menus);
}


int gui_user_add_contextmenu(uint32_t window_id, int x, int y, int w, int h, const char *items, int selected_index, uint32_t disabled_mask) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *wg;
    char safe_items[256];
    if (!gui_user_window_owned_by_current(win)) return -1;
    gui_user_copy_text(safe_items, sizeof(safe_items), items);
    wg = gui_add_contextmenu(win, x, y, w, h, safe_items, selected_index, disabled_mask, 0, 0);
    if (!wg) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)wg->id;
}

int gui_user_set_contextmenu_index(uint32_t window_id, uint32_t widget_id, int selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_CONTEXTMENU) return -1;
    old_value = widget->value;
    if (gui_contextmenu_set_selected(widget, selected_index) < 0) return -1;
    if (widget->value != old_value) gui_user_post_value_event(widget);
    return 0;
}

int gui_user_get_contextmenu_index(uint32_t window_id, uint32_t widget_id, int *out_selected_index) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_selected_index || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    return gui_contextmenu_get_selected(widget, out_selected_index);
}

int gui_user_set_contextmenu_items(uint32_t window_id, uint32_t widget_id, const char *items) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_items[256];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    gui_user_copy_text(safe_items, sizeof(safe_items), items);
    return gui_contextmenu_set_items(widget, safe_items);
}

int gui_user_set_contextmenu_disabled(uint32_t window_id, uint32_t widget_id, uint32_t disabled_mask) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    return gui_contextmenu_set_disabled_mask(widget, disabled_mask);
}

int gui_user_show_contextmenu(uint32_t window_id, uint32_t widget_id, int x, int y) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    return gui_contextmenu_show(widget, x, y);
}

int gui_user_hide_contextmenu(uint32_t window_id, uint32_t widget_id) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    return gui_contextmenu_hide(widget);
}

int gui_user_set_treeview_node(uint32_t window_id, uint32_t widget_id, int selected_node) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TREEVIEW) return -1;
    old_value = widget->value;
    if (gui_treeview_set_selected(widget, selected_node) < 0) return -1;
    if (widget->value != old_value) gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_treeview_node(uint32_t window_id, uint32_t widget_id, int *out_selected_node) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_selected_node || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TREEVIEW) return -1;
    return gui_treeview_get_selected(widget, out_selected_node);
}

int gui_user_set_treeview_nodes(uint32_t window_id, uint32_t widget_id, const char *nodes) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    char safe_nodes[GUI_USER_TEXT_MAX + 1];
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_TREEVIEW) return -1;
    gui_user_copy_text(safe_nodes, sizeof(safe_nodes), nodes ? nodes : "");
    if (gui_treeview_set_nodes(widget, safe_nodes) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_widget_enabled(uint32_t window_id, uint32_t widget_id, int enabled) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget) return -1;
    gui_widget_set_enabled(widget, enabled);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_widget_enabled(uint32_t window_id, uint32_t widget_id, int *out_enabled) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_enabled || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget) return -1;
    *out_enabled = gui_widget_get_enabled(widget);
    return 0;
}

int gui_user_set_slider_value(uint32_t window_id, uint32_t widget_id, int value) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SLIDER) return -1;
    if (widget->min_value > widget->max_value) return -1;
    old_value = widget->value;
    if (gui_slider_set_value(widget, value) < 0) return -1;
    if (widget->value == old_value) return 0;
    gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_slider_value(uint32_t window_id, uint32_t widget_id, int *out_value) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_value || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SLIDER) return -1;
    return gui_slider_get_value(widget, out_value);
}

int gui_user_set_slider_step(uint32_t window_id, uint32_t widget_id, int step) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SLIDER) return -1;
    old_value = widget->value;
    if (gui_slider_set_step(widget, step) < 0) return -1;
    if (widget->value != old_value) {
        gui_user_post_value_event(widget);
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_slider_step(uint32_t window_id, uint32_t widget_id, int *out_step) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_step || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SLIDER) return -1;
    return gui_slider_get_step(widget, out_step);
}

int gui_user_set_progressbar_value(uint32_t window_id, uint32_t widget_id, int value) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_PROGRESSBAR) return -1;
    old_value = widget->value;
    if (gui_progressbar_set_value(widget, value) < 0) return -1;
    if (widget->value != old_value) {
        gui_user_post_value_event(widget);
        gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    }
    return 0;
}

int gui_user_get_progressbar_value(uint32_t window_id, uint32_t widget_id, int *out_value) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_value || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_PROGRESSBAR) return -1;
    return gui_progressbar_get_value(widget, out_value);
}

int gui_user_set_progressbar_flags(uint32_t window_id, uint32_t widget_id, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_PROGRESSBAR) return -1;
    if (gui_progressbar_set_flags(widget, flags) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_add_spinner(uint32_t window_id, int x, int y, int w, int h, const char *text, uint32_t flags) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win) || w <= 0 || h <= 0) return -1;
    widget = gui_add_spinner(win, x, y, w, h, text ? text : "", flags);
    if (!widget) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return (int)widget->id;
}

int gui_user_set_spinner_running(uint32_t window_id, uint32_t widget_id, int running) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SPINNER) return -1;
    if (gui_spinner_set_running(widget, running) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_spinner_text(uint32_t window_id, uint32_t widget_id, const char *text) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SPINNER) return -1;
    if (gui_spinner_set_text(widget, text ? text : "") < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_set_scrollbar_value(uint32_t window_id, uint32_t widget_id, int value) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SCROLLBAR) return -1;
    if (widget->min_value > widget->max_value) return -1;
    old_value = widget->value;
    if (gui_scrollbar_set_value(widget, value) < 0) return -1;
    if (widget->value == old_value) return 0;
    gui_user_post_value_event(widget);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_scrollbar_value(uint32_t window_id, uint32_t widget_id, int *out_value) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_value || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SCROLLBAR) return -1;
    return gui_scrollbar_get_value(widget, out_value);
}

int gui_user_set_scrollbar_step(uint32_t window_id, uint32_t widget_id, int step) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    int old_value;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SCROLLBAR) return -1;
    old_value = widget->value;
    if (gui_scrollbar_set_step(widget, step) < 0) return -1;
    if (widget->value != old_value) {
        gui_user_post_value_event(widget);
    }
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_scrollbar_step(uint32_t window_id, uint32_t widget_id, int *out_step) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_step || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SCROLLBAR) return -1;
    return gui_scrollbar_get_step(widget, out_step);
}

int gui_user_set_scrollview_offset(uint32_t window_id, uint32_t widget_id, int scroll_x, int scroll_y) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SCROLLVIEW) return -1;
    if (gui_scrollview_set_offset(widget, scroll_x, scroll_y) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_scrollview_offset(uint32_t window_id, uint32_t widget_id, int *out_scroll_x, int *out_scroll_y) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_scroll_x || !out_scroll_y || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SCROLLVIEW) return -1;
    return gui_scrollview_get_offset(widget, out_scroll_x, out_scroll_y);
}

int gui_user_set_scrollview_content_size(uint32_t window_id, uint32_t widget_id, int content_w, int content_h) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SCROLLVIEW) return -1;
    if (gui_scrollview_set_content_size(widget, content_w, content_h) < 0) return -1;
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_scrollview_content_size(uint32_t window_id, uint32_t widget_id, int *out_content_w, int *out_content_h) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    if (!out_content_w || !out_content_h || !gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget || widget->type != GUI_WIDGET_SCROLLVIEW) return -1;
    return gui_scrollview_get_content_size(widget, out_content_w, out_content_h);
}

int gui_user_set_widget_parent(uint32_t window_id, uint32_t widget_id, uint32_t parent_widget_id) {
    gui_window_t *win = gui_find_window(window_id);
    gui_widget_t *widget;
    gui_widget_t *parent = 0;
    if (!gui_user_window_owned_by_current(win)) return -1;
    widget = gui_find_widget(win, widget_id);
    if (!widget) return -1;
    if (parent_widget_id) {
        parent = gui_find_widget(win, parent_widget_id);
        if (!parent) return -1;
    }
    return gui_widget_set_parent(widget, parent);
}

int gui_user_draw(const gui_user_draw_request_t *request) {
    if (!request) {
        return -1;
    }

    gui_window_t *win = gui_find_window(request->window_id);
    if (!gui_user_window_owned_by_current(win)) {
        return -1;
    }

    gui_widget_t *canvas = 0;
    if (request->widget_id) {
        canvas = gui_find_widget(win, request->widget_id);
        if (!canvas || canvas->type != GUI_WIDGET_CANVAS) return -1;
    }

    if (request->op == GUI_USER_DRAW_FILL_RECT) {
        if (canvas) return gui_widget_fill_rect(canvas, request->x, request->y, request->w, request->h, request->bg_color);
        return gui_window_fill_client_rect(win, request->x, request->y, request->w, request->h, request->bg_color);
    }

    if (request->op == GUI_USER_DRAW_TEXT) {
        char safe_text[129];
        gui_user_copy_text(safe_text, sizeof(safe_text), request->text);
        if (canvas) return gui_widget_draw_text(canvas, request->x, request->y, safe_text, request->fg_color);
        return gui_window_draw_client_text(win, request->x, request->y, safe_text, request->fg_color);
    }

    if (request->op == GUI_USER_DRAW_BLIT_RGBA32) {
        if (canvas) return gui_widget_blit_rgba32(canvas, request->x, request->y, request->w, request->h,
                                                 (const uint32_t *)request->pixels_user_ptr, request->src_stride);
        return gui_window_blit_client_rgba32(win, request->x, request->y, request->w, request->h,
                                            (const uint32_t *)request->pixels_user_ptr, request->src_stride);
    }

    if (request->op == GUI_USER_DRAW_SCROLL) {
        if (canvas) return gui_widget_scroll_rect(canvas, request->x, request->y, request->src_x, request->src_y,
                                                 request->w, request->h);
        return gui_window_scroll_client_rect(win, request->x, request->y, request->src_x, request->src_y,
                                             request->w, request->h);
    }

    if (request->op == GUI_USER_DRAW_PRESENT) {
        if (canvas) return gui_widget_present(canvas);
        return gui_window_present_client(win);
    }

    return -1;
}

int gui_user_resize_window(uint32_t window_id, int w, int h) {
    gui_window_t *win = gui_find_window(window_id);
    if (!gui_user_window_owned_by_current(win)) {
        return -1;
    }
    if (w < GUI_USER_MIN_W || h < GUI_USER_MIN_H || w > GUI_USER_MAX_W || h > GUI_USER_MAX_H) {
        return -1;
    }

    int old_x = win->rect.x;
    int old_y = win->rect.y;
    int old_w = win->rect.w;
    int old_h = win->rect.h;
    win->rect.w = w;
    win->rect.h = h;
    gui_invalidate_rect(old_x, old_y, old_w, old_h);
    gui_invalidate_rect(win->rect.x, win->rect.y, win->rect.w, win->rect.h);
    return 0;
}

int gui_user_get_window_info(uint32_t window_id, gui_user_window_info_t *out_info) {
    gui_window_t *win = gui_find_window(window_id);
    if (!out_info || !gui_user_window_owned_by_current(win)) {
        return -1;
    }

    out_info->window_id = win->id;
    out_info->owner_pid = win->user_owner_pid;
    out_info->x = win->rect.x;
    out_info->y = win->rect.y;
    out_info->w = win->rect.w;
    out_info->h = win->rect.h;
    out_info->flags = win->flags;
    out_info->focused = win->active ? 1u : 0u;
    return 0;
}

int gui_user_get_display_info(gui_user_display_info_t *out_info) {
    const gui_system_t *gui;
    if (!out_info || !gui_is_ready()) {
        return -1;
    }

    gui = gui_get_system();
    if (!gui || gui->width == 0 || gui->height == 0) {
        return -1;
    }

    out_info->width = (int32_t)gui->width;
    out_info->height = (int32_t)gui->height;
    out_info->dpi_x = 96u;
    out_info->dpi_y = 96u;
    out_info->scale_milli = 1000u;
    return 0;
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
