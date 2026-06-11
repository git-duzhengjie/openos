/* ============================================================
 * openos - Minimal GUI / Window System
 *
 * 支持：GUI 终端、PS/2 鼠标光标、事件队列、按钮点击�?
 *       窗口拖动/置顶/关闭/最小化、双缓冲渲染�?
 * ============================================================ */

#include "gui.h"
#include "framebuffer.h"
#include "mouse.h"
#include "font.h"
#include "serial.h"
#include "string.h"
#include "heap.h"

static gui_system_t g_gui;

static int gui_rect_contains(const gui_rect_t *r, int x, int y);
static gui_window_t *gui_top_window(void);
static void gui_set_hovered_widget(gui_widget_t *wg);
void gui_terminal_set_input_focus(int focused);

static uint32_t gui_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void gui_write_dec(uint32_t value) {
    char buf[11];
    int i = 0;
    if (value == 0) { serial_write("0"); return; }
    while (value > 0 && i < 10) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) serial_putc(buf[--i]);
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

static void gui_raw_put_pixel(int x, int y, uint32_t color) {
    if (!g_gui.initialized) return;
    if (x < 0 || y < 0 || x >= (int)g_gui.width || y >= (int)g_gui.height) return;
    if (g_gui.clip_enabled && !gui_rect_contains(&g_gui.clip_rect, x, y)) return;
    if (g_gui.double_buffered && g_gui.backbuffer) {
        g_gui.backbuffer[(uint32_t)y * g_gui.width + (uint32_t)x] = color;
    } else {
        framebuffer_put_pixel((uint32_t)x, (uint32_t)y, color);
    }
}

static void gui_raw_fill_rect(int x, int y, int w, int h, uint32_t color) {
    int yy, xx;
    if (w <= 0 || h <= 0) return;
    for (yy = y; yy < y + h; yy++) {
        for (xx = x; xx < x + w; xx++) {
            gui_raw_put_pixel(xx, yy, color);
        }
    }
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

static void gui_set_clip_rect(const gui_rect_t *rect) {
    gui_rect_t screen;
    gui_rect_t clipped;
    if (!rect) {
        g_gui.clip_enabled = 0;
        return;
    }
    screen.x = 0; screen.y = 0; screen.w = (int)g_gui.width; screen.h = (int)g_gui.height;
    if (gui_rect_intersect(rect, &screen, &clipped)) {
        g_gui.clip_rect = clipped;
        g_gui.clip_enabled = 1;
    } else {
        g_gui.clip_rect.x = 0; g_gui.clip_rect.y = 0; g_gui.clip_rect.w = 0; g_gui.clip_rect.h = 0;
        g_gui.clip_enabled = 1;
    }
}

static void gui_clear_clip_rect(void) {
    g_gui.clip_enabled = 0;
}

void gui_draw_char(int x, int y, char ch, uint32_t color) {
    int row, col;
    if ((uint8_t)ch < 32 || ch == ' ') return;
    for (row = 0; row < GUI_CHAR_H; row++) {
        uint8_t bits = font_get_glyph_row(font_get_default(), ch, row);
        for (col = 0; col < GUI_CHAR_W; col++) {
            if (bits & (0x80u >> col)) gui_raw_put_pixel(x + col, y + row, color);
        }
    }
}

void gui_draw_text(int x, int y, const char *text, uint32_t color) {
    int cx = x;
    if (!text) return;
    while (*text) {
        if (*text == '\n') {
            y += GUI_CHAR_H + 2;
            cx = x;
        } else {
            gui_draw_char(cx, y, *text, color);
            cx += GUI_CHAR_W;
        }
        text++;
    }
}

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
    r.x = w->rect.x + w->rect.w - GUI_TITLE_HEIGHT * 2 + 5;
    r.y = w->rect.y + 4;
    r.w = GUI_TITLE_HEIGHT - 8;
    r.h = GUI_TITLE_HEIGHT - 8;
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
        if (idx < GUI_MAX_WINDOWS && g_gui.windows[idx].used) return &g_gui.windows[idx];
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

static gui_widget_t *gui_widget_at(gui_window_t *w, int sx, int sy) {
    uint32_t i;
    if (!w) return 0;
    for (i = 0; i < w->widget_count; i++) {
        gui_widget_t *wg = &w->widgets[i];
        if (!wg->visible || !wg->enabled) continue;
        if (gui_rect_contains(&wg->rect, sx, sy)) return wg;
    }
    return 0;
}

static int gui_widget_can_focus(gui_widget_t *wg) {
    return wg && wg->visible && wg->enabled &&
           (wg->type == GUI_WIDGET_TEXTBOX || wg->type == GUI_WIDGET_BUTTON);
}

static int gui_widget_is_clickable(gui_widget_t *wg) {
    return wg && wg->visible && wg->enabled && wg->type == GUI_WIDGET_BUTTON;
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

static void gui_set_hovered_widget(gui_widget_t *wg) {
    if (wg && !gui_widget_is_clickable(wg)) wg = 0;
    if (g_gui.hovered_widget == wg) return;
    if (g_gui.hovered_widget) g_gui.hovered_widget->hovered = 0;
    g_gui.hovered_widget = wg;
    if (g_gui.hovered_widget) g_gui.hovered_widget->hovered = 1;
    gui_invalidate_all();
}

static gui_widget_t *gui_widget_at_screen(int x, int y) {
    gui_window_t *w = gui_window_at(x, y);
    if (!w) return 0;
    return gui_widget_at(w, x - w->rect.x - GUI_BORDER_SIZE, y - w->rect.y - GUI_TITLE_HEIGHT);
}

static void gui_set_focused_widget(gui_widget_t *wg) {
    if (g_gui.focused_widget == wg) return;
    if (g_gui.focused_widget) g_gui.focused_widget->focused = 0;
    g_gui.focused_widget = 0;
    if (gui_widget_can_focus(wg)) {
        if (wg->type == GUI_WIDGET_TEXTBOX) {
            uint32_t len = (uint32_t)strlen(wg->text);
            if (wg->cursor > len) wg->cursor = len;
        }
        wg->focused = 1;
        g_gui.focused_widget = wg;
        gui_terminal_set_input_focus(0);
    }
    gui_invalidate_all();
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

static void gui_textbox_on_key(gui_widget_t *wg, int key) {
    uint32_t len;
    uint32_t i;
    int changed = 1;
    if (!gui_widget_can_focus(wg)) return;
    len = (uint32_t)strlen(wg->text);
    if (wg->cursor > len) wg->cursor = len;

    if (key == GUI_KEY_BACKSPACE) {
        if (wg->cursor == 0 || len == 0) changed = 0;
        else {
            for (i = wg->cursor - 1; i < len; i++) wg->text[i] = wg->text[i + 1];
            wg->cursor--;
        }
    } else if (key == GUI_KEY_DELETE) {
        if (wg->cursor >= len) changed = 0;
        else {
            for (i = wg->cursor; i < len; i++) wg->text[i] = wg->text[i + 1];
        }
    } else if (key == GUI_KEY_LEFT) {
        if (wg->cursor > 0) wg->cursor--;
        else changed = 0;
    } else if (key == GUI_KEY_RIGHT) {
        if (wg->cursor < len) wg->cursor++;
        else changed = 0;
    } else if (key == GUI_KEY_HOME) {
        if (wg->cursor != 0) wg->cursor = 0;
        else changed = 0;
    } else if (key == GUI_KEY_END) {
        if (wg->cursor != len) wg->cursor = len;
        else changed = 0;
    } else if (key == GUI_KEY_ENTER || key == GUI_KEY_TAB) {
        changed = 0;
    } else if (key >= 32 && key <= 126 && len + 1 < sizeof(wg->text)) {
        for (i = len + 1; i > wg->cursor; i--) wg->text[i] = wg->text[i - 1];
        wg->text[wg->cursor++] = (char)key;
    } else {
        changed = 0;
    }
    if (changed && wg->owner) gui_invalidate_rect(wg->owner->rect.x, wg->owner->rect.y, wg->owner->rect.w, wg->owner->rect.h);
}

static void gui_draw_cursor(void) {
    int x = g_gui.mouse_x;
    int y = g_gui.mouse_y;
    uint32_t c = gui_rgb(255, 255, 255);
    uint32_t b = gui_rgb(0, 0, 0);
    gui_raw_line(x, y, x, y + 15, b);
    gui_raw_line(x, y, x + 10, y + 10, b);
    gui_raw_line(x, y + 15, x + 4, y + 11, b);
    gui_raw_line(x + 4, y + 11, x + 10, y + 10, b);
    gui_raw_line(x + 1, y + 2, x + 1, y + 12, c);
    gui_raw_line(x + 1, y + 2, x + 8, y + 9, c);
    gui_raw_line(x + 2, y + 12, x + 4, y + 10, c);
}

void gui_invalidate_rect(int x, int y, int w, int h) {
    gui_rect_t *r;
    if (!g_gui.initialized) return;
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)g_gui.width || y >= (int)g_gui.height) return;
    if (x + w > (int)g_gui.width) w = (int)g_gui.width - x;
    if (y + h > (int)g_gui.height) h = (int)g_gui.height - y;
    if (w <= 0 || h <= 0) return;
    if (g_gui.dirty_count >= GUI_MAX_DIRTY_RECTS) {
        g_gui.full_dirty = 1;
        return;
    }
    r = &g_gui.dirty_rects[g_gui.dirty_count++];
    r->x = x; r->y = y; r->w = w; r->h = h;
}

void gui_invalidate_all(void) {
    if (!g_gui.initialized) return;
    g_gui.full_dirty = 1;
    g_gui.dirty_count = 0;
}

static void gui_flush_rect(const gui_rect_t *r) {
    int x, y;
    if (!r) return;
    for (y = r->y; y < r->y + r->h; y++) {
        for (x = r->x; x < r->x + r->w; x++) {
            framebuffer_put_pixel((uint32_t)x, (uint32_t)y, g_gui.backbuffer[y * (int)g_gui.width + x]);
        }
    }
}

static void gui_flush_backbuffer(void) {
    uint32_t i;
    gui_rect_t all;
    if (!g_gui.double_buffered || !g_gui.backbuffer) return;
    if (!g_gui.full_dirty && g_gui.dirty_count == 0) return;
    if (g_gui.full_dirty) {
        all.x = 0; all.y = 0; all.w = (int)g_gui.width; all.h = (int)g_gui.height;
        gui_flush_rect(&all);
    } else {
        for (i = 0; i < g_gui.dirty_count; i++) gui_flush_rect(&g_gui.dirty_rects[i]);
    }
    g_gui.full_dirty = 0;
    g_gui.dirty_count = 0;
}

static void gui_draw_widget(gui_widget_t *wg) {
    uint32_t bg, fg;
    if (!wg || !wg->visible || !wg->owner) return;
    int ax = wg->owner->rect.x + GUI_BORDER_SIZE + wg->rect.x;
    int ay = wg->owner->rect.y + GUI_TITLE_HEIGHT + wg->rect.y;

    if (wg->type == GUI_WIDGET_LABEL) {
        gui_draw_text(ax, ay + 3, wg->text, wg->fg_color ? wg->fg_color : g_gui.colors.text_fg);
    } else if (wg->type == GUI_WIDGET_BUTTON) {
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
        gui_draw_text(ax + text_dx, ay + (wg->rect.h - GUI_CHAR_H) / 2 + text_dy, wg->text, fg);
    } else if (wg->type == GUI_WIDGET_PANEL) {
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, wg->bg_color);
    } else if (wg->type == GUI_WIDGET_TEXTBOX) {
        uint32_t border = wg->focused ? g_gui.colors.accent : g_gui.colors.button_border;
        uint32_t text_x = (uint32_t)(ax + 4);
        uint32_t text_y = (uint32_t)(ay + (wg->rect.h - GUI_CHAR_H) / 2);
        gui_raw_fill_rect(ax, ay, wg->rect.w, wg->rect.h, wg->bg_color ? wg->bg_color : gui_rgb(250, 250, 250));
        gui_raw_line(ax, ay, ax + wg->rect.w - 1, ay, border);
        gui_raw_line(ax, ay, ax, ay + wg->rect.h - 1, border);
        gui_raw_line(ax + wg->rect.w - 1, ay, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_raw_line(ax, ay + wg->rect.h - 1, ax + wg->rect.w - 1, ay + wg->rect.h - 1, border);
        gui_draw_text((int)text_x, (int)text_y, wg->text, wg->fg_color ? wg->fg_color : gui_rgb(20, 20, 20));
        if (wg->focused) {
            int cx = ax + 4 + (int)(wg->cursor * GUI_CHAR_W);
            if (cx < ax + wg->rect.w - 3) gui_raw_line(cx, ay + 4, cx, ay + wg->rect.h - 5, gui_rgb(20, 20, 20));
        }
    }
}

static void gui_draw_window(gui_window_t *w) {
    uint32_t i;
    uint32_t border;
    uint32_t title;
    if (!w || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;

    border = w->active ? g_gui.colors.accent : g_gui.colors.window_border;
    title = w->active ? g_gui.colors.title_bg : gui_rgb(50, 55, 68);

    gui_raw_fill_rect(w->rect.x, w->rect.y, w->rect.w, w->rect.h, border);
    gui_raw_fill_rect(w->rect.x + GUI_BORDER_SIZE, w->rect.y + GUI_BORDER_SIZE,
                      w->rect.w - GUI_BORDER_SIZE * 2, GUI_TITLE_HEIGHT - GUI_BORDER_SIZE,
                      title);
    gui_raw_fill_rect(w->rect.x + GUI_BORDER_SIZE, w->rect.y + GUI_TITLE_HEIGHT,
                      w->rect.w - GUI_BORDER_SIZE * 2, w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE,
                      w->bg_color ? w->bg_color : g_gui.colors.window_bg);

    gui_draw_text(w->rect.x + 8, w->rect.y + 7, w->title, g_gui.colors.title_fg);

    if (w->flags & GUI_WINDOW_FLAG_CLOSABLE) {
        gui_rect_t c = gui_close_rect(w);
        gui_raw_fill_rect(c.x, c.y, c.w, c.h, gui_rgb(160, 50, 55));
        gui_raw_line(c.x + 3, c.y + 3, c.x + c.w - 4, c.y + c.h - 4, gui_rgb(255,255,255));
        gui_raw_line(c.x + c.w - 4, c.y + 3, c.x + 3, c.y + c.h - 4, gui_rgb(255,255,255));
    }

    if (w->flags & GUI_WINDOW_FLAG_MINIMIZABLE) {
        gui_rect_t m = gui_min_rect(w);
        gui_raw_fill_rect(m.x, m.y, m.w, m.h, gui_rgb(80, 90, 105));
        gui_raw_line(m.x + 3, m.y + m.h - 4, m.x + m.w - 4, m.y + m.h - 4, gui_rgb(255,255,255));
    }

    {
        gui_rect_t client;
        client.x = w->rect.x + GUI_BORDER_SIZE;
        client.y = w->rect.y + GUI_TITLE_HEIGHT;
        client.w = w->rect.w - GUI_BORDER_SIZE * 2;
        client.h = w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE;
        gui_set_clip_rect(&client);
        for (i = 0; i < w->widget_count; i++) {
            gui_draw_widget(&w->widgets[i]);
        }
        gui_clear_clip_rect();
    }
}

void gui_event_push(gui_event_t event) {
    if (g_gui.event_count >= GUI_EVENT_QUEUE_SIZE) return;
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
}

void gui_set_active_window(gui_window_t *window) {
    uint32_t i;
    for (i = 0; i < GUI_MAX_WINDOWS; i++) g_gui.windows[i].active = 0;
    if (window && gui_window_index(window) >= 0) {
        gui_bring_to_front(window);
        g_gui.active_window = window;
        g_gui.active_window->active = 1;
    } else {
        g_gui.active_window = 0;
    }
    gui_invalidate_all();
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

    if (g_gui.active_window == window) g_gui.active_window = 0;
    if (g_gui.drag_window == window) g_gui.drag_window = 0;
    if (g_gui.pressed_widget && g_gui.pressed_widget->owner == window) g_gui.pressed_widget = 0;
    if (g_gui.hovered_widget && g_gui.hovered_widget->owner == window) g_gui.hovered_widget = 0;
    if (g_gui.focused_widget && g_gui.focused_widget->owner == window) g_gui.focused_widget = 0;
    memset(window, 0, sizeof(gui_window_t));

    gui_refresh_window_refs();
    g_gui.active_window = gui_top_window();
    if (g_gui.active_window) g_gui.active_window->active = 1;
    gui_invalidate_all();
}

void gui_minimize_window(gui_window_t *window) {
    if (!window) return;
    window->flags |= GUI_WINDOW_FLAG_MINIMIZED;
    if (g_gui.active_window == window) g_gui.active_window = 0;
    if (g_gui.focused_widget && g_gui.focused_widget->owner == window) gui_set_focused_widget(0);
    gui_invalidate_all();
}

void gui_restore_window(gui_window_t *window) {
    if (!window) return;
    window->flags &= ~GUI_WINDOW_FLAG_MINIMIZED;
    window->visible = 1;
    gui_set_active_window(window);
    gui_invalidate_all();
}

void gui_show_window(gui_window_t *window) { if (window) { window->visible = 1; gui_invalidate_all(); } }
void gui_hide_window(gui_window_t *window) { if (window) { window->visible = 0; gui_invalidate_all(); } }

static int gui_taskbar_terminal_button_at(int x, int y) {
    if (y < (int)g_gui.height - GUI_TASKBAR_HEIGHT) return 0;
    return x >= 8 && x < 8 + GUI_TASKBAR_START_W;
}

static gui_window_t *gui_taskbar_window_at(int x, int y) {
    uint32_t i;
    int bx = GUI_TASKBAR_START_W + 16;
    if (y < (int)g_gui.height - GUI_TASKBAR_HEIGHT) return 0;
    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        int bw;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!w->used || !w->visible || !(w->flags & GUI_WINDOW_FLAG_MINIMIZED)) continue;
        bw = 72 + (int)strlen(w->title) * GUI_CHAR_W;
        if (bw > 180) bw = 180;
        if (x >= bx && x < bx + bw) return w;
        bx += bw + 6;
    }
    return 0;
}

void gui_post_key_code(int key) {
    gui_event_t ev;
    if (!g_gui.initialized || !key) return;
    ev.type = GUI_EVENT_KEY_DOWN;
    ev.x = 0; ev.y = 0; ev.dx = 0; ev.dy = 0;
    ev.button = 0; ev.key = key;
    ev.window = g_gui.active_window;
    ev.widget = 0;
    gui_event_push(ev);
}

void gui_post_key(char ch) {
    gui_post_key_code((int)(unsigned char)ch);
}

__attribute__((optimize("no-jump-tables")))
void gui_process_events(void) {
    gui_event_t ev;
    while (gui_event_pop(&ev)) {
        if (ev.type == GUI_EVENT_KEY_DOWN) {
            if (ev.key == GUI_KEY_TAB) {
                gui_focus_next_widget();
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_TEXTBOX) {
                gui_textbox_on_key(g_gui.focused_widget, ev.key);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused &&
                       g_gui.focused_widget->type == GUI_WIDGET_BUTTON &&
                       (ev.key == GUI_KEY_ENTER || ev.key == GUI_KEY_SPACE)) {
                gui_button_activate(g_gui.focused_widget);
            } else if (g_gui.focused_widget && g_gui.focused_widget->focused) {
                /* Focused widgets consume keys that they do not handle. */
            } else if (ev.key >= 0 && ev.key <= 255) {
                gui_terminal_on_input((char)ev.key);
            }
        } else if (ev.type == GUI_EVENT_MOUSE_DOWN) {
            if (gui_taskbar_terminal_button_at(ev.x, ev.y)) {
                gui_terminal_open();
                continue;
            }
            gui_window_t *tw = gui_taskbar_window_at(ev.x, ev.y);
            if (tw) {
                gui_restore_window(tw);
                continue;
            }
            gui_window_t *w = gui_window_at(ev.x, ev.y);
            if (w) {
                if (w == g_gui.terminal.window) gui_terminal_set_input_focus(1);
                gui_set_active_window(w);
                gui_rect_t close = gui_close_rect(w);
                gui_rect_t minr = gui_min_rect(w);
                if ((w->flags & GUI_WINDOW_FLAG_CLOSABLE) && gui_rect_contains(&close, ev.x, ev.y)) {
                    ev.type = GUI_EVENT_WINDOW_CLOSE;
                    ev.window = w;
                    gui_event_push(ev);
                } else if ((w->flags & GUI_WINDOW_FLAG_MINIMIZABLE) && gui_rect_contains(&minr, ev.x, ev.y)) {
                    ev.type = GUI_EVENT_WINDOW_MINIMIZE;
                    ev.window = w;
                    gui_event_push(ev);
                } else {
                    int sx = ev.x - w->rect.x - GUI_BORDER_SIZE;
                    int sy = ev.y - w->rect.y - GUI_TITLE_HEIGHT;
                    gui_widget_t *wg = gui_widget_at(w, sx, sy);
                    if (gui_widget_is_clickable(wg)) {
                        gui_set_focused_widget(wg);
                        wg->pressed = 1;
                        g_gui.pressed_widget = wg;
                        gui_invalidate_all();
                    } else if (gui_widget_can_focus(wg)) {
                        gui_set_focused_widget(wg);
                    } else {
                        gui_set_focused_widget(0);
                        gui_rect_t tr = gui_title_rect(w);
                        if (gui_rect_contains(&tr, ev.x, ev.y)) {
                            w->dragging = 1;
                            w->drag_offset_x = ev.x - w->rect.x;
                            w->drag_offset_y = ev.y - w->rect.y;
                            g_gui.drag_window = w;
                        }
                    }
                }
            } else {
                gui_set_focused_widget(0);
            }
        } else if (ev.type == GUI_EVENT_MOUSE_UP) {
            if (g_gui.drag_window) {
                g_gui.drag_window->dragging = 0;
                g_gui.drag_window = 0;
            }
            if (g_gui.pressed_widget) {
                gui_widget_t *wg = g_gui.pressed_widget;
                int still_inside = 0;
                gui_widget_t *under = gui_widget_at_screen(ev.x, ev.y);
                wg->pressed = 0;
                g_gui.pressed_widget = 0;
                still_inside = (under == wg && gui_widget_is_clickable(wg));
                gui_invalidate_all();
                if (still_inside) {
                    gui_button_activate(wg);
                }
            }
        } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
            gui_set_hovered_widget(gui_widget_at_screen(ev.x, ev.y));
            if (g_gui.drag_window && g_gui.drag_window->dragging) {
                gui_window_t *w = g_gui.drag_window;
                w->rect.x = ev.x - w->drag_offset_x;
                w->rect.y = ev.y - w->drag_offset_y;
                if (w->rect.x < 0) w->rect.x = 0;
                if (w->rect.y < 0) w->rect.y = 0;
                if (w->rect.x + w->rect.w > (int)g_gui.width) w->rect.x = (int)g_gui.width - w->rect.w;
                if (w->rect.y + w->rect.h > (int)g_gui.height - GUI_TASKBAR_HEIGHT) w->rect.y = (int)g_gui.height - GUI_TASKBAR_HEIGHT - w->rect.h;
                gui_invalidate_all();
            } else {
                gui_invalidate_rect(ev.x - 18, ev.y - 18, 36, 36);
            }
        } else if (ev.type == GUI_EVENT_BUTTON_CLICK) {
            if (ev.widget && gui_widget_is_clickable(ev.widget)) {
                /* 按钮点击功能已预留，当前优先保证点击不崩溃，后续再添加具体功能 */
                serial_write("[GUI] button clicked, no crash ✅\n");
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
    gui_event_t ev;
    if (!g_gui.initialized) return;

    mouse_snapshot_and_clear_delta(&ms);
    if (!ms.present) return;

    if (ms.x < 0) ms.x = 0;
    if (ms.y < 0) ms.y = 0;
    if (ms.x > (int)g_gui.width - 1) ms.x = (int)g_gui.width - 1;
    if (ms.y > (int)g_gui.height - 1) ms.y = (int)g_gui.height - 1;

    if (ms.x != g_gui.mouse_x || ms.y != g_gui.mouse_y) {
        gui_invalidate_rect(g_gui.mouse_x - 2, g_gui.mouse_y - 2, 22, 22);
        gui_invalidate_rect(ms.x - 2, ms.y - 2, 22, 22);
        memset(&ev, 0, sizeof(ev));
        ev.type = GUI_EVENT_MOUSE_MOVE;
        ev.x = ms.x;
        ev.y = ms.y;
        ev.dx = ms.dx;
        ev.dy = ms.dy;
        ev.button = ms.buttons;
        ev.window = 0;
        ev.widget = 0;
        ev.key = 0;
        gui_event_push(ev);
    }

    if ((ms.buttons & 1) && !(g_gui.last_mouse_buttons & 1)) {
        memset(&ev, 0, sizeof(ev));
        ev.type = GUI_EVENT_MOUSE_DOWN;
        ev.x = ms.x; ev.y = ms.y; ev.button = 1;
        gui_event_push(ev);
    } else if (!(ms.buttons & 1) && (g_gui.last_mouse_buttons & 1)) {
        memset(&ev, 0, sizeof(ev));
        ev.type = GUI_EVENT_MOUSE_UP;
        ev.x = ms.x; ev.y = ms.y; ev.button = 1;
        gui_event_push(ev);
    }

    g_gui.mouse_x = ms.x;
    g_gui.mouse_y = ms.y;
    g_gui.last_mouse_buttons = ms.buttons;
}

void gui_init(void) {
    memset(&g_gui, 0, sizeof(g_gui));
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
    g_gui.mouse_x = 512;
    g_gui.mouse_y = 384;
    /* Default off avoids two cursors in QEMU/VM windows:
     * host cursor + OpenOS software cursor. Use `cursor on` to show it.
     */
    g_gui.cursor_visible = 0;
    serial_write("[OK] GUI object pool\n");
}

int gui_start(uint32_t width, uint32_t height) {
    const framebuffer_info_t *info;
    uint32_t pixels;
    if (!framebuffer_is_available()) return -1;
    if (framebuffer_set_mode(width, height, 32) != 0) return -1;
    info = framebuffer_get_info();
    if (!info || !info->mode_set) return -1;

    g_gui.width = info->width;
    g_gui.height = info->height;
    g_gui.initialized = 1;
    g_gui.mouse_x = (int)(g_gui.width / 2);
    g_gui.mouse_y = (int)(g_gui.height / 2);
    mouse_set_bounds((int)g_gui.width, (int)g_gui.height);
    mouse_set_position(g_gui.mouse_x, g_gui.mouse_y);

    pixels = g_gui.width * g_gui.height;
    if (!g_gui.backbuffer || g_gui.backbuffer_pixels < pixels) {
        g_gui.backbuffer = (uint32_t *)kmalloc(pixels * sizeof(uint32_t));
        g_gui.backbuffer_pixels = g_gui.backbuffer ? pixels : 0;
    }
    g_gui.double_buffered = g_gui.backbuffer ? 1 : 0;

    gui_terminal_init();
    gui_render();
    return 0;
}

static void gui_create_welcome_window(void) {
    gui_window_t *welcome;

    welcome = gui_create_window(92, 84, 470, 210, "Welcome to OpenOS");
    if (!welcome) return;

    gui_add_label(welcome, 22, 26, 390, 18, "OpenOS desktop is ready.");
    gui_add_label(welcome, 22, 54, 410, 18, "Click TERMINAL on the taskbar");
    gui_add_label(welcome, 22, 78, 420, 18, "to open the command line tool.");
    gui_add_label(welcome, 22, 118, 420, 18, "Tip: use cursor on/off for software mouse.");
}

int gui_start_desktop(void) {
    if (g_gui.initialized) {
        gui_terminal_minimize();
        gui_create_welcome_window();
        gui_render();
        return 0;
    }

    if (gui_start(1024, 768) != 0) return -1;
    gui_terminal_write("\n[GUI] desktop started. Click TERMINAL on the taskbar to open command line.\n");
    gui_terminal_minimize();
    gui_create_welcome_window();
    gui_render();
    return 0;
}

int gui_is_ready(void) { return g_gui.initialized; }

int gui_has_focused_widget(void) {
    return g_gui.initialized && g_gui.focused_widget && g_gui.focused_widget->focused;
}

int gui_should_capture_key_code(int key) {
    gui_widget_t *wg;

    if (!g_gui.initialized || !g_gui.focused_widget || !g_gui.focused_widget->focused) return 0;

    wg = g_gui.focused_widget;
    if (!wg->visible || !wg->enabled) return 0;

    if (key == GUI_KEY_TAB) return 1;

    if (wg->type == GUI_WIDGET_TEXTBOX) return 1;

    if (wg->type == GUI_WIDGET_BUTTON) {
        return key == GUI_KEY_ENTER || key == GUI_KEY_SPACE;
    }

    return 0;
}

void gui_shutdown_to_text_note(void) { serial_write("[GUI] text mode restore is not implemented yet\n"); }

void gui_set_cursor_visible(int visible) {
    g_gui.cursor_visible = visible ? 1 : 0;
    if (g_gui.initialized) gui_render();
}

int gui_is_cursor_visible(void) { return g_gui.cursor_visible; }

const gui_system_t *gui_get_system(void) { return &g_gui; }

void gui_print_info(void) {
    serial_write("[GUI] ready="); gui_write_dec((uint32_t)g_gui.initialized);
    serial_write(" size="); gui_write_dec(g_gui.width); serial_write("x"); gui_write_dec(g_gui.height);
    serial_write(" windows="); gui_write_dec(g_gui.window_count);
    serial_write(" active="); gui_write_dec(g_gui.active_window ? g_gui.active_window->id : 0);
    serial_write(" events="); gui_write_dec(g_gui.event_count);
    serial_write(" dblbuf="); gui_write_dec((uint32_t)g_gui.double_buffered);
    serial_write(" cursor="); gui_write_dec((uint32_t)g_gui.cursor_visible);
    serial_write(" mouse="); gui_write_dec((uint32_t)g_gui.mouse_x); serial_write(","); gui_write_dec((uint32_t)g_gui.mouse_y);
    serial_write("\n");
}

gui_window_t *gui_create_window(int x, int y, int w, int h, const char *title) {
    gui_window_t *win;
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
    gui_copy_text(win->title, title, sizeof(win->title));
    win->bg_color = g_gui.colors.window_bg;
    win->flags = GUI_WINDOW_FLAG_CLOSABLE | GUI_WINDOW_FLAG_MINIMIZABLE;
    win->visible = 1;
    g_gui.z_order[g_gui.window_count++] = idx;
    gui_set_active_window(win);
    return win;
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

gui_widget_t *gui_add_panel(gui_window_t *window, int x, int y, int w, int h, uint32_t color) {
    gui_widget_t *wg = gui_alloc_widget(window, GUI_WIDGET_PANEL, x, y, w, h, "");
    if (wg) wg->bg_color = color;
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
    if (widget->owner) gui_invalidate_rect(widget->owner->rect.x, widget->owner->rect.y, widget->owner->rect.w, widget->owner->rect.h);
}

void gui_terminal_clear(void) {
    uint32_t r, c;
    for (r = 0; r < GUI_TERM_ROWS; r++) for (c = 0; c < GUI_TERM_COLS; c++) g_gui.terminal.cells[r][c] = ' ';
    g_gui.terminal.cursor_x = 0;
    g_gui.terminal.cursor_y = 0;
    g_gui.terminal.dirty = 1;
}

void gui_terminal_init(void) {
    gui_window_t *term;
    if (g_gui.terminal.window) return;
    term = gui_create_window(24, 420, (int)g_gui.width - 48, (int)g_gui.height - 448, "OpenOS GUI Terminal");
    if (!term) return;
    term->flags |= GUI_WINDOW_FLAG_TERMINAL;
    term->bg_color = gui_rgb(10, 14, 22);
    g_gui.terminal.window = term;
    g_gui.terminal.cols = (uint32_t)((term->rect.w - 12) / GUI_CHAR_W);
    g_gui.terminal.rows = (uint32_t)((term->rect.h - GUI_TITLE_HEIGHT - 10) / (GUI_CHAR_H + 1));
    if (g_gui.terminal.cols > GUI_TERM_COLS) g_gui.terminal.cols = GUI_TERM_COLS;
    if (g_gui.terminal.rows > GUI_TERM_ROWS) g_gui.terminal.rows = GUI_TERM_ROWS;
    g_gui.terminal.enabled = 1;
    g_gui.terminal.input_focused = 1;
    gui_terminal_clear();
    gui_terminal_write("OpenOS GUI terminal ready. Keyboard input is routed here.\n> ");
}

static void gui_terminal_scroll(void) {
    uint32_t r;
    for (r = 1; r < g_gui.terminal.rows; r++) {
        memcpy(g_gui.terminal.cells[r - 1], g_gui.terminal.cells[r], g_gui.terminal.cols);
    }
    memset(g_gui.terminal.cells[g_gui.terminal.rows - 1], ' ', g_gui.terminal.cols);
    if (g_gui.terminal.cursor_y > 0) g_gui.terminal.cursor_y--;
}

void gui_terminal_putc(char ch) {
    if (!g_gui.initialized || !g_gui.terminal.enabled) return;
    if (ch == '\r') return;
    if (ch == '\n') {
        g_gui.terminal.cursor_x = 0;
        g_gui.terminal.cursor_y++;
    } else if (ch == '\b') {
        if (g_gui.terminal.cursor_x > 0) {
            g_gui.terminal.cursor_x--;
            g_gui.terminal.cells[g_gui.terminal.cursor_y][g_gui.terminal.cursor_x] = ' ';
        }
    } else {
        if (g_gui.terminal.cursor_x >= g_gui.terminal.cols) {
            g_gui.terminal.cursor_x = 0;
            g_gui.terminal.cursor_y++;
        }
        if (g_gui.terminal.cursor_y >= g_gui.terminal.rows) gui_terminal_scroll();
        g_gui.terminal.cells[g_gui.terminal.cursor_y][g_gui.terminal.cursor_x++] = ch;
    }
    if (g_gui.terminal.cursor_y >= g_gui.terminal.rows) gui_terminal_scroll();
    g_gui.terminal.dirty = 1;
    if (g_gui.terminal.window) gui_invalidate_rect(g_gui.terminal.window->rect.x, g_gui.terminal.window->rect.y, g_gui.terminal.window->rect.w, g_gui.terminal.window->rect.h);
}

void gui_terminal_set_input_focus(int focused) {
    g_gui.terminal.input_focused = focused ? 1 : 0;
}

void gui_terminal_open(void) {
    extern void kernel_start_shell_thread(void);
    if (!g_gui.initialized || !g_gui.terminal.window) return;
    g_gui.terminal.enabled = 1;
    g_gui.terminal.input_focused = 1;
    gui_restore_window(g_gui.terminal.window);
    gui_set_active_window(g_gui.terminal.window);
    gui_invalidate_rect(g_gui.terminal.window->rect.x, g_gui.terminal.window->rect.y,
                        g_gui.terminal.window->rect.w, g_gui.terminal.window->rect.h);
    kernel_start_shell_thread();
}

void gui_terminal_minimize(void) {
    if (!g_gui.initialized || !g_gui.terminal.window) return;
    gui_minimize_window(g_gui.terminal.window);
    g_gui.terminal.input_focused = 0;
}

void gui_terminal_on_input(char ch) {
    if (!g_gui.initialized || !g_gui.terminal.enabled || !g_gui.terminal.input_focused) return;
    gui_terminal_putc(ch);
}

void gui_terminal_write(const char *text) {
    if (!text) return;
    while (*text) gui_terminal_putc(*text++);
}

void gui_terminal_redraw(void) {
    uint32_t r, c;
    gui_window_t *w = g_gui.terminal.window;
    int ox, oy;
    if (!w || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;
    ox = w->rect.x + 6;
    oy = w->rect.y + GUI_TITLE_HEIGHT + 5;
    {
        gui_rect_t client;
        client.x = w->rect.x + GUI_BORDER_SIZE;
        client.y = w->rect.y + GUI_TITLE_HEIGHT;
        client.w = w->rect.w - GUI_BORDER_SIZE * 2;
        client.h = w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE;
        gui_set_clip_rect(&client);
    }
    for (r = 0; r < g_gui.terminal.rows; r++) {
        for (c = 0; c < g_gui.terminal.cols; c++) {
            char ch = g_gui.terminal.cells[r][c];
            if (ch != ' ') gui_draw_char(ox + (int)c * GUI_CHAR_W, oy + (int)r * (GUI_CHAR_H + 1), ch, gui_rgb(185, 255, 185));
        }
    }
    gui_raw_fill_rect(ox + (int)g_gui.terminal.cursor_x * GUI_CHAR_W,
                      oy + (int)g_gui.terminal.cursor_y * (GUI_CHAR_H + 1) + GUI_CHAR_H,
                      GUI_CHAR_W - 1, 1, gui_rgb(185, 255, 185));
    gui_clear_clip_rect();
}

static void gui_draw_taskbar(void) {
    uint32_t i;
    int bx = GUI_TASKBAR_START_W + 16;
    int y = (int)g_gui.height - GUI_TASKBAR_HEIGHT;
    gui_raw_fill_rect(0, y, (int)g_gui.width, GUI_TASKBAR_HEIGHT, gui_rgb(24, 28, 38));
    gui_raw_fill_rect(8, y + 3, GUI_TASKBAR_START_W, GUI_TASKBAR_HEIGHT - 6, gui_rgb(64, 92, 150));
    gui_raw_line(8, y + 3, 8 + GUI_TASKBAR_START_W - 1, y + 3, gui_rgb(170, 205, 255));
    gui_raw_line(8, y + 3, 8, y + GUI_TASKBAR_HEIGHT - 4, gui_rgb(170, 205, 255));
    gui_raw_line(8 + GUI_TASKBAR_START_W - 1, y + 3, 8 + GUI_TASKBAR_START_W - 1, y + GUI_TASKBAR_HEIGHT - 4, gui_rgb(12, 18, 30));
    gui_raw_line(8, y + GUI_TASKBAR_HEIGHT - 4, 8 + GUI_TASKBAR_START_W - 1, y + GUI_TASKBAR_HEIGHT - 4, gui_rgb(12, 18, 30));
    gui_draw_text(20, y + 8, "TERMINAL", gui_rgb(255,255,255));
    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        gui_window_t *w;
        int bw;
        if (idx >= GUI_MAX_WINDOWS) continue;
        w = &g_gui.windows[idx];
        if (!w->used || !w->visible || !(w->flags & GUI_WINDOW_FLAG_MINIMIZED)) continue;
        bw = 72 + (int)strlen(w->title) * GUI_CHAR_W;
        if (bw > 180) bw = 180;
        gui_raw_fill_rect(bx, y + 3, bw, GUI_TASKBAR_HEIGHT - 6, gui_rgb(52, 68, 96));
        gui_raw_line(bx, y + 3, bx + bw - 1, y + 3, gui_rgb(135, 170, 230));
        gui_raw_line(bx, y + 3, bx, y + GUI_TASKBAR_HEIGHT - 4, gui_rgb(135, 170, 230));
        gui_raw_line(bx + bw - 1, y + 3, bx + bw - 1, y + GUI_TASKBAR_HEIGHT - 4, gui_rgb(20, 24, 35));
        gui_raw_line(bx, y + GUI_TASKBAR_HEIGHT - 4, bx + bw - 1, y + GUI_TASKBAR_HEIGHT - 4, gui_rgb(20, 24, 35));
        gui_draw_text(bx + 8, y + 8, w->title, gui_rgb(245,245,255));
        bx += bw + 6;
    }
}

void gui_render(void) {
    uint32_t i;
    if (!g_gui.initialized) return;
    gui_raw_fill_rect(0, 0, (int)g_gui.width, (int)g_gui.height, g_gui.colors.desktop_bg);
    gui_draw_taskbar();

    for (i = 0; i < g_gui.window_count; i++) {
        uint32_t idx = g_gui.z_order[i];
        if (idx < GUI_MAX_WINDOWS) gui_draw_window(&g_gui.windows[idx]);
    }
    gui_terminal_redraw();
    if (g_gui.cursor_visible) gui_draw_cursor();
    gui_flush_backbuffer();
}

void gui_poll(void) {
    if (!g_gui.initialized) return;
    gui_poll_mouse();
    gui_process_events();
    gui_render();
}

static void gui_demo_button(gui_widget_t *widget, void *user_data) {
    (void)widget;
    (void)user_data;
    gui_terminal_write("\n[GUI] button clicked\n> ");
}

void gui_demo(void) {
    gui_window_t *w1;
    gui_window_t *w2;
    if (!g_gui.initialized) return;
    w1 = gui_create_window(70, 70, 380, 230, "OpenOS Control Center");
    if (w1) {
        gui_add_label(w1, 18, 22, 300, 18, "Welcome to OpenOS GUI");
        gui_add_panel(w1, 18, 52, 335, 48, gui_rgb(210, 225, 245));
        gui_add_label(w1, 28, 66, 300, 18, "Window drag, focus and buttons enabled.");
        gui_add_textbox(w1, 18, 108, 260, 26, "edit me");
        gui_add_button(w1, 18, 150, 120, 28, "Click", gui_demo_button, 0);
        gui_add_button(w1, 150, 150, 120, 28, "Minimize", gui_demo_button, 0);
    }
    w2 = gui_create_window(500, 120, 330, 170, "About OpenOS");
    if (w2) {
        gui_add_label(w2, 18, 24, 260, 18, "openos GUI framework MVP");
        gui_add_label(w2, 18, 48, 260, 18, "Framebuffer + windows + events");
        gui_add_button(w2, 18, 90, 100, 28, "OK", gui_demo_button, 0);
    }
    gui_terminal_write("\n[GUI] demo windows created\n> ");
    gui_render();
}
