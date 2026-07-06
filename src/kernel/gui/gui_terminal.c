/* gui_terminal.c — GUI 终端子系统（从 gui.c 拆分） */
#include "gui.h"
#include "gui_user.h"
#include "framebuffer.h"
#include "mouse.h"
#include "usb_tablet.h"
#include "font.h"
#include "serial.h"
#include "string.h"
#include "heap.h"
#include "input_buffer.h"
#include "core/fs/vfs.h"
#include "i18n.h"
#include "net/net.h"
#include "net/dhcp.h"
#include "net/dns.h"
#include "net/net_config.h"
#include "tls_parser.h"
#include "process.h"
#include "gui_internal.h"
#include <stdint.h>

extern int ramfs_snapshot_save(void);
extern int ramfs_snapshot_load(void);
extern int  arch_x86_64_usermode_launch_path(const char *path, int argc, const char **argv, int envc, const char **envp);
extern void arch_x86_64_fd_set_stdout_mirror(void (*sink)(char c));
void gui_terminal_set_capture(int on);

#define GUI_TERMINAL_OUTPUT_QUEUE_SIZE 4096u

#define GUI_TERMINAL_OUTPUT_DRAIN_LIMIT 128u

static volatile uint32_t g_terminal_out_head = 0;

static volatile uint32_t g_terminal_out_tail = 0;

static char g_terminal_out_queue[GUI_TERMINAL_OUTPUT_QUEUE_SIZE];


void gui_terminal_invalidate_body(void) {
    gui_window_t *w = g_gui.terminal.window;
    if (!g_gui.initialized || !g_gui.terminal.enabled || !w ||
        !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) {
        return;
    }
    gui_invalidate_rect(w->rect.x + GUI_BORDER_SIZE,
                        w->rect.y + GUI_TITLE_HEIGHT,
                        w->rect.w - GUI_BORDER_SIZE * 2,
                        w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE);
}

static void gui_terminal_invalidate_cell(uint32_t col, uint32_t row) {
    gui_window_t *w = g_gui.terminal.window;
    int x, y;
    if (!g_gui.initialized || !g_gui.terminal.enabled || !w ||
        !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) {
        return;
    }
    if (col >= g_gui.terminal.view.cols || row >= g_gui.terminal.view.rows) return;
    x = w->rect.x + 6 + (int)col * GUI_CHAR_W;
    y = w->rect.y + GUI_TITLE_HEIGHT + 5 + (int)row * (GUI_CHAR_H + 1);
    gui_invalidate_rect(x - 1, y - 1, GUI_CHAR_W + 2, GUI_CHAR_H + 3);
}

static void gui_terminal_invalidate_cursor_at(uint32_t col, uint32_t row) {
    gui_window_t *w = g_gui.terminal.window;
    int x, y;
    if (!g_gui.initialized || !g_gui.terminal.enabled || !w ||
        !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) {
        return;
    }
    if (row >= g_gui.terminal.view.rows) return;
    if (col >= g_gui.terminal.view.cols) col = g_gui.terminal.view.cols - 1;
    x = w->rect.x + 6 + (int)col * GUI_CHAR_W;
    y = w->rect.y + GUI_TITLE_HEIGHT + 5 + (int)row * (GUI_CHAR_H + 1) + GUI_CHAR_H;
    gui_invalidate_rect(x - 1, y - 1, GUI_CHAR_W + 2, 3);
}

void gui_terminal_view_make_layout(gui_rect_t viewport, int padding_x, int padding_y, gui_terminal_view_layout_t *layout) {
    if (!layout) return;
    layout->x = viewport.x + padding_x;
    layout->y = viewport.y + padding_y;
    layout->cell_w = GUI_CHAR_W;
    layout->cell_h = GUI_CHAR_H + 1;
    layout->char_h = GUI_CHAR_H;
    layout->clip_rect = viewport;
}

int gui_terminal_view_point_to_cell(const gui_terminal_view_t *view, const gui_terminal_view_layout_t *layout, int x, int y, uint32_t *col, uint32_t *row) {
    int rel_x;
    int rel_y;
    uint32_t c;
    uint32_t r;
    if (!view || !layout || !col || !row || view->cols == 0 || view->rows == 0) return 0;
    if (layout->cell_w <= 0 || layout->cell_h <= 0) return 0;
    if (x < layout->x || y < layout->y) return 0;
    rel_x = x - layout->x;
    rel_y = y - layout->y;
    c = (uint32_t)(rel_x / layout->cell_w);
    r = (uint32_t)(rel_y / layout->cell_h);
    if (c >= view->cols || r >= view->rows) return 0;
    *col = c;
    *row = r;
    return 1;
}

int gui_terminal_point_to_cell(int x, int y, uint32_t *col, uint32_t *row) {
    gui_window_t *w = g_gui.terminal.window;
    gui_rect_t viewport;
    gui_terminal_view_layout_t layout;
    if (!w || !g_gui.terminal.enabled || !col || !row) return 0;
    if (!w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return 0;
    viewport.x = w->rect.x;
    viewport.y = w->rect.y + GUI_TITLE_HEIGHT;
    viewport.w = w->rect.w;
    viewport.h = w->rect.h - GUI_TITLE_HEIGHT;
    gui_terminal_view_make_layout(viewport, 6, 5, &layout);
    return gui_terminal_view_point_to_cell(&g_gui.terminal.view, &layout, x, y, col, row);
}

void gui_terminal_view_begin_selection(gui_terminal_view_t *view, uint32_t col, uint32_t row) {
    if (!view || view->cols == 0 || view->rows == 0) return;
    if (col >= view->cols) col = view->cols - 1;
    if (row >= view->rows) row = view->rows - 1;
    view->selecting = 1;
    view->has_selection = 0;
    view->selection_anchor_x = col;
    view->selection_anchor_y = row;
    view->selection_start_x = col;
    view->selection_start_y = row;
    view->selection_end_x = col;
    view->selection_end_y = row;
}

void gui_terminal_view_update_selection(gui_terminal_view_t *view, uint32_t col, uint32_t row) {
    uint32_t a;
    uint32_t b;
    if (!view || view->cols == 0 || view->rows == 0) return;
    if (col >= view->cols) col = view->cols - 1;
    if (row >= view->rows) row = view->rows - 1;
    a = view->selection_anchor_y * view->cols + view->selection_anchor_x;
    b = row * view->cols + col;
    if (b < a) {
        view->selection_start_x = col;
        view->selection_start_y = row;
        view->selection_end_x = view->selection_anchor_x;
        view->selection_end_y = view->selection_anchor_y;
    } else {
        view->selection_start_x = view->selection_anchor_x;
        view->selection_start_y = view->selection_anchor_y;
        view->selection_end_x = col;
        view->selection_end_y = row;
    }
    view->has_selection = (a != b) ? 1 : 0;
}

void gui_terminal_view_end_selection(gui_terminal_view_t *view) {
    if (!view) return;
    view->selecting = 0;
}

void gui_terminal_update_selection(uint32_t col, uint32_t row) {
    gui_terminal_view_update_selection(&g_gui.terminal.view, col, row);
    gui_terminal_invalidate_body();
}

int gui_terminal_view_cell_selected(const gui_terminal_view_t *view, uint32_t col, uint32_t row) {
    uint32_t p, s, e;
    if (!view || !view->has_selection || view->cols == 0) return 0;
    p = row * view->cols + col;
    s = view->selection_start_y * view->cols + view->selection_start_x;
    e = view->selection_end_y * view->cols + view->selection_end_x;
    return (p >= s && p <= e) ? 1 : 0;
}

void gui_terminal_view_clear_selection(gui_terminal_view_t *view) {
    if (!view || (!view->has_selection && !view->selecting)) return;
    view->selecting = 0;
    view->has_selection = 0;
}

void gui_terminal_clear_selection(void) {
    gui_terminal_view_t *view = &g_gui.terminal.view;
    if (!view->has_selection && !view->selecting) return;
    gui_terminal_view_clear_selection(view);
    gui_terminal_invalidate_body();
}

int gui_terminal_view_copy_selection(gui_terminal_view_t *view) {
    uint32_t r, c;
    uint32_t len = 0;
    uint32_t copied = 0;
    if (!view || !view->has_selection || view->cols == 0) return 0;
    for (r = view->selection_start_y; r <= view->selection_end_y && r < view->rows; r++) {
        uint32_t start_c = 0;
        uint32_t end_c = view->cols - 1;
        int last_non_space = -1;
        if (r == view->selection_start_y) start_c = view->selection_start_x;
        if (r == view->selection_end_y) end_c = view->selection_end_x;
        for (c = start_c; c <= end_c && c < view->cols; c++) {
            if (view->cells[r][c] != ' ') last_non_space = (int)c;
        }
        if (last_non_space < (int)start_c && r != view->selection_end_y) {
            if (len + 1 < GUI_TERM_CLIPBOARD_SIZE) view->clipboard[len++] = '\n';
            continue;
        }
        if (last_non_space >= (int)start_c) {
            for (c = start_c; c <= (uint32_t)last_non_space && c < view->cols; c++) {
                if (len + 1 >= GUI_TERM_CLIPBOARD_SIZE) break;
                view->clipboard[len++] = view->cells[r][c];
                copied = 1;
            }
        }
        if (r != view->selection_end_y && len + 1 < GUI_TERM_CLIPBOARD_SIZE) {
            view->clipboard[len++] = '\n';
        }
        if (len + 1 >= GUI_TERM_CLIPBOARD_SIZE) break;
    }
    view->clipboard[len] = '\0';
    view->clipboard_len = len;
    return copied || len > 0;
}

int gui_terminal_copy_selection(void) {
    if (!g_gui.initialized) return 0;
    return gui_terminal_view_copy_selection(&g_gui.terminal.view);
}

int gui_terminal_view_has_clipboard_text(const gui_terminal_view_t *view) {
    return (view && view->clipboard_len > 0 && view->clipboard[0] != '\0') ? 1 : 0;
}

int gui_terminal_view_set_clipboard_text(gui_terminal_view_t *view, const char *text) {
    uint32_t len = 0;
    if (!view) return 0;
    if (!text) text = "";
    while (text[len] && len + 1 < GUI_TERM_CLIPBOARD_SIZE) {
        view->clipboard[len] = text[len];
        len++;
    }
    view->clipboard[len] = '\0';
    view->clipboard_len = len;
    return len > 0;
}

const char *gui_terminal_view_get_clipboard_text(const gui_terminal_view_t *view) {
    return gui_terminal_view_has_clipboard_text(view) ? view->clipboard : "";
}

int gui_terminal_has_clipboard_text(void) {
    return gui_terminal_view_has_clipboard_text(&g_gui.terminal.view);
}

int gui_terminal_set_clipboard_text(const char *text) {
    return gui_terminal_view_set_clipboard_text(&g_gui.terminal.view, text);
}

const char *gui_terminal_get_clipboard_text(void) {
    return gui_terminal_view_get_clipboard_text(&g_gui.terminal.view);
}

void gui_terminal_view_clear(gui_terminal_view_t *view) {
    uint32_t r, c;
    if (!view) return;
    for (r = 0; r < GUI_TERM_ROWS; r++) {
        for (c = 0; c < GUI_TERM_COLS; c++) {
            view->cells[r][c] = ' ';
        }
    }
    view->cursor_x = 0;
    view->cursor_y = 0;
    view->selecting = 0;
    view->has_selection = 0;
}

void gui_terminal_view_init(gui_terminal_view_t *view, uint32_t cols, uint32_t rows) {
    if (!view) return;
    view->cols = cols;
    view->rows = rows;
    if (view->cols > GUI_TERM_COLS) view->cols = GUI_TERM_COLS;
    if (view->rows > GUI_TERM_ROWS) view->rows = GUI_TERM_ROWS;
    view->clipboard_len = 0;
    view->clipboard[0] = '\0';
    gui_terminal_view_clear(view);
}

void gui_terminal_clear(void) {
    if (!g_gui.initialized || !g_gui.terminal.enabled) return;
    gui_terminal_view_clear(&g_gui.terminal.view);
    g_gui.terminal.dirty = 1;
    gui_terminal_invalidate_body();
}

void gui_terminal_init(void) {
    gui_window_t *term;
    uint32_t cols;
    uint32_t rows;
    if (g_gui.terminal.window) return;
    term = gui_create_window(24, 420, (int)g_gui.width - 48, (int)g_gui.height - 448, i18n_t(I18N_KEY_WIN_TERMINAL));
    if (!term) return;
    term->flags |= GUI_WINDOW_FLAG_TERMINAL;
    term->bg_color = gui_rgb(10, 14, 22);
    g_gui.terminal.window = term;
    cols = (uint32_t)((term->rect.w - 12) / GUI_CHAR_W);
    rows = (uint32_t)((term->rect.h - GUI_TITLE_HEIGHT - 10) / (GUI_CHAR_H + 1));
    gui_terminal_view_init(&g_gui.terminal.view, cols, rows);
    g_gui.terminal.enabled = 1;
    g_gui.terminal.input_focused = 1;
    g_gui.terminal.cursor_visible = 1;
    g_gui.terminal.cursor_blink_ticks = 0;
    g_gui.terminal.cwd[0] = '/';
    g_gui.terminal.cwd[1] = 0;
    gui_terminal_clear();
    gui_terminal_write("TERMINAL ready. Keyboard input is routed here.\n> ");
}

void gui_terminal_view_scroll(gui_terminal_view_t *view) {
    uint32_t r;
    if (!view || view->rows == 0 || view->cols == 0) return;
    for (r = 1; r < view->rows; r++) {
        memcpy(view->cells[r - 1], view->cells[r], view->cols);
    }
    memset(view->cells[view->rows - 1], ' ', view->cols);
    if (view->cursor_y > 0) view->cursor_y--;
}

static void gui_terminal_scroll(void) {
    gui_terminal_view_scroll(&g_gui.terminal.view);
    gui_terminal_invalidate_body();
}

static void gui_terminal_view_handle_carriage_return(gui_terminal_view_t *view) {
    if (!view) return;
    /* CR returns to the beginning of the current line. It must not scroll or move down. */
    view->cursor_x = 0;
}

static void gui_terminal_view_handle_line_feed(gui_terminal_view_t *view, int *body_dirty) {
    if (!view || !body_dirty) return;
    /* LF advances to the next display line. Keep the existing console convention of column 0. */
    view->cursor_x = 0;
    view->cursor_y++;
    if (view->cursor_y >= view->rows) {
        gui_terminal_view_scroll(view);
        *body_dirty = 1;
    }
}

static void gui_terminal_view_handle_backspace(gui_terminal_view_t *view) {
    if (!view || view->cols == 0) return;
    if (view->cursor_x > 0) {
        view->cursor_x--;
    } else if (view->cursor_y > 0) {
        view->cursor_y--;
        view->cursor_x = view->cols - 1;
    }
    /* 清除光标当前所在 cell，使退格在视觉上真正删除字符 */
    view->cells[view->cursor_y][view->cursor_x] = ' ';
}

int gui_terminal_view_putc(gui_terminal_view_t *view, char ch, int *body_dirty) {
    int dirty = 0;
    if (!view || view->cols == 0 || view->rows == 0) return 0;
    if (ch == '\n') {
        gui_terminal_view_handle_line_feed(view, &dirty);
    } else if (ch == '\r') {
        gui_terminal_view_handle_carriage_return(view);
    } else if (ch == '\b') {
        gui_terminal_view_handle_backspace(view);
        dirty = 1;
    } else {
        if (view->cursor_x >= view->cols) {
            gui_terminal_view_handle_line_feed(view, &dirty);
        }
        if (view->cursor_y >= view->rows) {
            gui_terminal_view_scroll(view);
            dirty = 1;
        }
        view->cells[view->cursor_y][view->cursor_x] = ch;
        view->cursor_x++;
    }
    if (view->cursor_y >= view->rows) {
        gui_terminal_view_scroll(view);
        dirty = 1;
    }
    if (body_dirty) *body_dirty = dirty;
    return 1;
}

void gui_terminal_view_write(gui_terminal_view_t *view, const char *text) {
    if (!text) return;
    while (*text) {
        gui_terminal_view_putc(view, *text++, 0);
    }
}

void gui_terminal_putc(char ch) {
    gui_terminal_view_t *view = &g_gui.terminal.view;
    uint32_t old_x, old_y;
    int body_dirty = 0;
    if (!g_gui.initialized || !g_gui.terminal.enabled) { serial_write("[TPUTC] reject init/enabled\n"); return; }
    if (view->cols == 0 || view->rows == 0) { serial_write("[TPUTC] reject cols/rows==0\n"); return; }
    if (view->has_selection && ch != 0x03 && ch != 0x16) gui_terminal_clear_selection();

    old_x = view->cursor_x;
    old_y = view->cursor_y;
    gui_terminal_invalidate_cursor_at(old_x, old_y);
    gui_terminal_view_putc(view, ch, &body_dirty);
    if (body_dirty) {
        gui_terminal_invalidate_body();
    } else {
        gui_terminal_invalidate_cell(old_x, old_y);
        gui_terminal_invalidate_cursor_at(view->cursor_x, view->cursor_y);
    }
    g_gui.terminal.cursor_visible = 1;
    g_gui.terminal.cursor_blink_ticks = 0;
    g_gui.terminal.dirty = 1;
}

void gui_terminal_set_input_focus(int focused) {
    g_gui.terminal.input_focused = focused ? 1 : 0;
    g_gui.terminal.cursor_visible = g_gui.terminal.input_focused ? 1 : 0;
    g_gui.terminal.cursor_blink_ticks = 0;
    gui_terminal_invalidate_cursor();
}

void gui_terminal_open(void) {
    extern void kernel_start_shell_thread(void);
    int already_active;

    if (!g_gui.initialized) return;

    if (!g_gui.terminal.window) {
#if GUI_DEBUG_LOG
        serial_write("[GUI] terminal rebuild\n");
#endif
        gui_terminal_init();
    }
    if (!g_gui.terminal.window) {
#if GUI_DEBUG_LOG
        serial_write("[GUI] terminal open failed\n");
#endif
        return;
    }

    already_active = g_gui.terminal.enabled &&
                     g_gui.terminal.input_focused &&
                     g_gui.terminal.window->visible &&
                     !(g_gui.terminal.window->flags & GUI_WINDOW_FLAG_MINIMIZED) &&
                     g_gui.active_window == g_gui.terminal.window;

    gui_set_focused_widget(0);
    gui_restore_window(g_gui.terminal.window);
    gui_set_active_window(g_gui.terminal.window);
    g_gui.terminal.enabled = 1;
    g_gui.terminal.input_focused = 1;
    g_gui.terminal.cursor_visible = 1;
    g_gui.terminal.cursor_blink_ticks = 0;

    gui_invalidate_rect(g_gui.terminal.window->rect.x, g_gui.terminal.window->rect.y,
                        g_gui.terminal.window->rect.w, g_gui.terminal.window->rect.h);
    gui_invalidate_all();

    if (already_active) return;

    input_flush();
#if GUI_TERMINAL_START_SHELL
    kernel_start_shell_thread();
#else
#endif
}

int gui_terminal_is_active(void) {
    return g_gui.initialized &&
           g_gui.terminal.enabled &&
           g_gui.terminal.window &&
           g_gui.terminal.window->visible &&
           !(g_gui.terminal.window->flags & GUI_WINDOW_FLAG_MINIMIZED);
}

void gui_terminal_minimize(void) {
    if (!g_gui.initialized || !g_gui.terminal.window) return;
    gui_minimize_window(g_gui.terminal.window);
    g_gui.terminal.input_focused = 0;
    g_gui.terminal.cursor_visible = 0;
    g_gui.terminal.cursor_blink_ticks = 0;
}

void gui_terminal_show_prompt(void) {
    gui_terminal_write("openos> ");
    g_gui.terminal.prompt_shown = 1;
}

static uint32_t gui_term_slen(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void gui_term_scpy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (src) { while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; } }
    dst[i] = 0;
}

static void gui_term_resolve(const char *arg_in, char *out, uint32_t cap) {
    char tmp[128];
    uint32_t ti = 0;
    const char *cwd = g_gui.terminal.cwd;
    /* trim leading/trailing whitespace from arg (Tab completion may append a space) */
    char argbuf[128];
    const char *arg = argbuf;
    if (arg_in) {
        const char *p = arg_in;
        while (*p == ' ' || *p == '\t') p++;
        uint32_t k = 0;
        while (*p && k < sizeof(argbuf) - 1) argbuf[k++] = *p++;
        while (k > 0 && (argbuf[k - 1] == ' ' || argbuf[k - 1] == '\t')) k--;
        argbuf[k] = 0;
    } else {
        argbuf[0] = 0;
    }
    if (arg[0] == 0) {
        gui_term_scpy(out, cwd, cap);
        return;
    }
    if (arg[0] == '/') {
        /* absolute */
        while (arg[ti] && ti < sizeof(tmp) - 1) { tmp[ti] = arg[ti]; ti++; }
        tmp[ti] = 0;
    } else {
        /* cwd + '/' + arg */
        uint32_t ci = 0;
        while (cwd[ci] && ti < sizeof(tmp) - 1) { tmp[ti++] = cwd[ci++]; }
        if (ti == 0 || tmp[ti - 1] != '/') { if (ti < sizeof(tmp) - 1) tmp[ti++] = '/'; }
        uint32_t ai = 0;
        while (arg[ai] && ti < sizeof(tmp) - 1) { tmp[ti++] = arg[ai++]; }
        tmp[ti] = 0;
    }
    /* normalize: collapse '//', handle trailing '/', '.', '..' segment-wise */
    char norm[128];
    uint32_t ni = 0;
    norm[ni++] = '/';
    uint32_t i = 0;
    while (tmp[i]) {
        while (tmp[i] == '/') i++;
        if (!tmp[i]) break;
        /* read segment */
        char seg[64];
        uint32_t si = 0;
        while (tmp[i] && tmp[i] != '/' && si < sizeof(seg) - 1) seg[si++] = tmp[i++];
        seg[si] = 0;
        if (seg[0] == '.' && seg[1] == 0) {
            continue;
        } else if (seg[0] == '.' && seg[1] == '.' && seg[2] == 0) {
            /* pop last segment in norm */
            if (ni > 1) {
                ni--; /* drop trailing char if any */
                while (ni > 1 && norm[ni - 1] != '/') ni--;
                if (ni > 1 && norm[ni - 1] == '/') ni--; /* remove the slash too, re-add below */
                if (ni == 0) ni = 1;
            }
        } else {
            if (ni > 1 && norm[ni - 1] != '/') { if (ni < sizeof(norm) - 1) norm[ni++] = '/'; }
            else if (ni == 1) { /* root slash already there */ }
            uint32_t k = 0;
            while (seg[k] && ni < sizeof(norm) - 1) norm[ni++] = seg[k++];
        }
    }
    norm[ni] = 0;
    if (ni == 0) { norm[0] = '/'; norm[1] = 0; }
    gui_term_scpy(out, norm, cap);
}

#define GUI_TERM_CMD_COUNT 13

static const char *g_gui_term_commands[GUI_TERM_CMD_COUNT] = {
    "help", "clear", "echo", "ver", "ls", "cat",
    "cd", "mkdir", "touch", "rm", "rmdir", "pwd", "sync"
};

static int gui_term_starts(const char *s, const char *pre) {
    uint32_t i = 0;
    if (!s || !pre) return 0;
    while (pre[i]) { if (s[i] != pre[i]) return 0; i++; }
    return 1;
}

static void gui_term_append_input(const char *tail) {
    if (!tail) return;
    while (*tail) {
        if (g_gui.terminal.cmd_len < (int)sizeof(g_gui.terminal.cmd_buf) - 1) {
            g_gui.terminal.cmd_buf[g_gui.terminal.cmd_len++] = *tail;
            gui_terminal_putc(*tail);
        }
        tail++;
    }
}

static void gui_terminal_complete(void) {
    int len = g_gui.terminal.cmd_len;
    char *buf = g_gui.terminal.cmd_buf;
    buf[len] = 0;
    /* 找最后一个 token 的起点（最后一个空格之后） */
    int tok_start = len;
    while (tok_start > 0 && buf[tok_start - 1] != ' ') tok_start--;
    /* 判断当前 token 是否是命令名（前面没有非空格内容） */
    int has_prev = 0;
    { int k = 0; while (k < tok_start) { if (buf[k] != ' ') { has_prev = 1; break; } k++; } }
    const char *token = buf + tok_start;

    if (!has_prev) {
        /* ---- 命令名补全 ---- */
        const char *matches[GUI_TERM_CMD_COUNT];
        int nmatch = 0;
        for (int i = 0; i < GUI_TERM_CMD_COUNT; i++) {
            if (gui_term_starts(g_gui_term_commands[i], token)) {
                matches[nmatch++] = g_gui_term_commands[i];
            }
        }
        if (nmatch == 0) return;
        if (nmatch == 1) {
            gui_term_append_input(matches[0] + gui_term_slen(token));
            gui_term_append_input(" ");
            return;
        }
        /* 多匹配：求公共前缀 */
        char common[32];
        gui_term_scpy(common, matches[0], sizeof(common));
        for (int i = 1; i < nmatch; i++) {
            uint32_t j = 0;
            while (common[j] && matches[i][j] && common[j] == matches[i][j]) j++;
            common[j] = 0;
        }
        uint32_t tlen = gui_term_slen(token);
        if (gui_term_slen(common) > tlen) {
            gui_term_append_input(common + tlen);
        } else {
            /* 打印候选列表 */
            gui_terminal_putc('\n');
            for (int i = 0; i < nmatch; i++) {
                gui_terminal_write(matches[i]);
                gui_terminal_putc(' ');
            }
            gui_terminal_putc('\n');
            gui_terminal_show_prompt();
            gui_terminal_write(buf);
        }
        return;
    }

    /* ---- 路径补全 ---- */
    /* 把 token 拆成 dir 部分和前缀部分（最后一个 '/' 之后） */
    int slash = -1;
    for (int i = 0; token[i]; i++) if (token[i] == '/') slash = i;
    char dirarg[128];
    char prefix[64];
    if (slash < 0) {
        /* 没有斜杠：目录为 cwd，前缀为整个 token */
        dirarg[0] = 0;
        gui_term_scpy(prefix, token, sizeof(prefix));
    } else {
        int di = 0;
        for (int i = 0; i <= slash && di < (int)sizeof(dirarg) - 1; i++) dirarg[di++] = token[i];
        dirarg[di] = 0;
        gui_term_scpy(prefix, token + slash + 1, sizeof(prefix));
    }
    /* 解析目录绝对路径 */
    char absdir[128];
    gui_term_resolve(dirarg[0] ? dirarg : ".", absdir, sizeof(absdir));
    /* 遍历目录匹配前缀 */
    char match_name[64];
    int nmatch = 0;
    int match_is_dir = 0;
    char common[64];
    int have_common = 0;
    dentry_t *e;
    /* 第一遍：统计、记录唯一匹配、求公共前缀 */
    for (int idx = 0; (e = vfs_readdir(absdir, idx)) != 0; idx++) {
        if (e->name[0] == '.' && e->name[1] == 0) continue;
        if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == 0) continue;
        if (!gui_term_starts(e->name, prefix)) continue;
        nmatch++;
        if (nmatch == 1) {
            gui_term_scpy(match_name, e->name, sizeof(match_name));
            gui_term_scpy(common, e->name, sizeof(common));
            have_common = 1;
        } else {
            uint32_t j = 0;
            while (common[j] && e->name[j] && common[j] == e->name[j]) j++;
            common[j] = 0;
        }
    }
    if (nmatch == 0) return;
    uint32_t plen = gui_term_slen(prefix);
    if (nmatch == 1) {
        /* 判断唯一匹配是否目录 */
        char full[128];
        gui_term_scpy(full, absdir, sizeof(full));
        uint32_t fl = gui_term_slen(full);
        if (fl == 0 || full[fl - 1] != '/') { if (fl < sizeof(full) - 1) { full[fl++] = '/'; full[fl] = 0; } }
        gui_term_scpy(full + fl, match_name, sizeof(full) - fl);
        inode_t st;
        if (vfs_stat(full, &st) == 0 && (st.mode & FS_DIR)) match_is_dir = 1;
        gui_term_append_input(match_name + plen);
        gui_term_append_input(match_is_dir ? "/" : " ");
        return;
    }
    /* 多匹配 */
    if (have_common && gui_term_slen(common) > plen) {
        gui_term_append_input(common + plen);
        return;
    }
    /* 打印候选 */
    gui_terminal_putc('\n');
    for (int idx = 0; (e = vfs_readdir(absdir, idx)) != 0; idx++) {
        if (e->name[0] == '.' && e->name[1] == 0) continue;
        if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == 0) continue;
        if (!gui_term_starts(e->name, prefix)) continue;
        gui_terminal_write(e->name);
        if (e->inode && (e->inode->mode & FS_DIR)) gui_terminal_putc('/');
        gui_terminal_putc(' ');
    }
    gui_terminal_putc('\n');
    gui_terminal_show_prompt();
    gui_terminal_write(buf);
}

static void gui_terminal_run_command(const char *cmd) {
    /* skip leading spaces */
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') {
        return;
    }
    if (gui_str_eq(cmd, "help")) {
        gui_terminal_write("commands: help clear echo ver time\n");
        gui_terminal_write("          ls cat pwd cd mkdir rmdir rm touch\n");
        gui_terminal_write("          echo TEXT > FILE  (write to file)\n");
        gui_terminal_write("          sync  (save filesystem to disk)\n");
        gui_terminal_write("  net:    ifconfig  ping HOST  nslookup HOST\n");
        gui_terminal_write("          wget [-1] HOST [PATH]  (fetch webpage)\n");
        gui_terminal_write("          run /bin/PROG [args]  (launch user ELF)\n");
    } else if (gui_str_eq(cmd, "clear") || gui_str_eq(cmd, "cls")) {
        gui_terminal_clear();
    } else if (gui_str_eq(cmd, "ver") || gui_str_eq(cmd, "version")) {
        gui_terminal_write("openos x86_64 (UEFI GUI)\n");
    } else if (gui_str_eq(cmd, "sync")) {
        int sr = ramfs_snapshot_save();
        if (sr == 0)      gui_terminal_write("sync: filesystem saved to disk\n");
        else if (sr == -1) gui_terminal_write("sync: no data disk / fs not ready\n");
        else               gui_terminal_write("sync: save failed\n");
    } else if (gui_str_eq(cmd, "pwd")) {
        gui_terminal_write(g_gui.terminal.cwd);
        gui_terminal_write("\n");
    } else if (cmd[0] == 'l' && cmd[1] == 's' && (cmd[2] == ' ' || cmd[2] == '\0')) {
        const char *arg = cmd + 2;
        while (*arg == ' ') arg++;
        char path[128];
        gui_term_resolve(arg, path, sizeof(path));
        int count = 0;
        for (int i = 0; i < 128; i++) {
            dentry_t *e = vfs_readdir(path, i);
            if (!e) break;
            gui_terminal_write(e->name);
            gui_terminal_write("  ");
            count++;
        }
        if (count == 0) {
            gui_terminal_write("(empty or not found)");
        }
        gui_terminal_write("\n");
    } else if (cmd[0] == 'c' && cmd[1] == 'd' && (cmd[2] == ' ' || cmd[2] == '\0')) {
        const char *arg = cmd + 2;
        while (*arg == ' ') arg++;
        char path[128];
        gui_term_resolve(arg, path, sizeof(path));
        /* verify it is a directory: readdir must yield at least one entry, or path=="/" */
        int ok = 0;
        if (path[0] == '/' && path[1] == 0) {
            ok = 1;
        } else {
            dentry_t *e = vfs_readdir(path, 0);
            if (e) ok = 1;
        }
        if (ok) {
            gui_term_scpy(g_gui.terminal.cwd, path, sizeof(g_gui.terminal.cwd));
        } else {
            gui_terminal_write("cd: no such directory: ");
            gui_terminal_write(path);
            gui_terminal_write("\n");
        }
    } else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && (cmd[3] == ' ' || cmd[3] == '\0')) {
        const char *arg = cmd + 3;
        while (*arg == ' ') arg++;
        if (*arg == 0) {
            gui_terminal_write("cat: missing file operand\n");
        } else {
            char path[128];
            gui_term_resolve(arg, path, sizeof(path));
            int fd = vfs_open(path, 0, 0);
            if (fd < 0) {
                gui_terminal_write("cat: cannot open: ");
                gui_terminal_write(path);
                gui_terminal_write("\n");
            } else {
                char buf[129];
                int n;
                while ((n = vfs_read(fd, buf, 128)) > 0) {
                    buf[n] = 0;
                    gui_terminal_write(buf);
                }
                vfs_close(fd);
                gui_terminal_write("\n");
            }
        }
    } else if (cmd[0] == 'm' && cmd[1] == 'k' && cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r' &&
               (cmd[5] == ' ' || cmd[5] == '\0')) {
        const char *arg = cmd + 5;
        while (*arg == ' ') arg++;
        if (*arg == 0) {
            gui_terminal_write("mkdir: missing operand\n");
        } else {
            char path[128];
            gui_term_resolve(arg, path, sizeof(path));
            if (vfs_mkdir(path, 0755) == 0) {
                /* ok, silent */
            } else {
                gui_terminal_write("mkdir: cannot create directory: ");
                gui_terminal_write(path);
                gui_terminal_write("\n");
            }
        }
    } else if (cmd[0] == 't' && cmd[1] == 'o' && cmd[2] == 'u' && cmd[3] == 'c' && cmd[4] == 'h' &&
               (cmd[5] == ' ' || cmd[5] == '\0')) {
        const char *arg = cmd + 5;
        while (*arg == ' ') arg++;
        if (*arg == 0) {
            gui_terminal_write("touch: missing file operand\n");
        } else {
            char path[128];
            gui_term_resolve(arg, path, sizeof(path));
            int fd = vfs_open(path, O_CREAT, 0644);
            if (fd < 0) {
                gui_terminal_write("touch: cannot create: ");
                gui_terminal_write(path);
                gui_terminal_write("\n");
            } else {
                vfs_close(fd);
            }
        }
    } else if (cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r' &&
               (cmd[5] == ' ' || cmd[5] == '\0')) {
        const char *arg = cmd + 5;
        while (*arg == ' ') arg++;
        if (*arg == 0) {
            gui_terminal_write("rmdir: missing operand\n");
        } else {
            char path[128];
            gui_term_resolve(arg, path, sizeof(path));
            if (vfs_rmdir(path) != 0) {
                gui_terminal_write("rmdir: failed (not empty or not a dir): ");
                gui_terminal_write(path);
                gui_terminal_write("\n");
            }
        }
    } else if (cmd[0] == 'r' && cmd[1] == 'm' && (cmd[2] == ' ' || cmd[2] == '\0')) {
        const char *arg = cmd + 2;
        while (*arg == ' ') arg++;
        if (*arg == 0) {
            gui_terminal_write("rm: missing operand\n");
        } else {
            char path[128];
            gui_term_resolve(arg, path, sizeof(path));
            if (vfs_unlink(path) != 0) {
                /* 可能是目录，试着当空目录删 */
                if (vfs_rmdir(path) != 0) {
                    gui_terminal_write("rm: cannot remove: ");
                    gui_terminal_write(path);
                    gui_terminal_write("\n");
                }
            }
        }
    } else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' &&
               (cmd[4] == ' ' || cmd[4] == '\0')) {
        const char *arg = cmd + 4;
        while (*arg == ' ') arg++;
        /* 检查是否有重定向 '>'：echo TEXT > FILE */
        const char *redir = arg;
        const char *gt = 0;
        while (*redir) { if (*redir == '>') { gt = redir; break; } redir++; }
        if (gt) {
            /* 提取文本（去掉尾部空格）与文件名 */
            char text[128];
            int tl = 0;
            const char *tp = arg;
            while (tp < gt && tl < (int)sizeof(text) - 1) text[tl++] = *tp++;
            while (tl > 0 && text[tl - 1] == ' ') tl--;
            text[tl] = 0;
            const char *fp = gt + 1;
            while (*fp == ' ') fp++;
            if (*fp == 0) {
                gui_terminal_write("echo: missing file after >\n");
            } else {
                char path[128];
                gui_term_resolve(fp, path, sizeof(path));
                int fd = vfs_open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
                if (fd < 0) {
                    gui_terminal_write("echo: cannot write: ");
                    gui_terminal_write(path);
                    gui_terminal_write("\n");
                } else {
                    if (tl > 0) vfs_write(fd, text, (uint32_t)tl);
                    vfs_write(fd, "\n", 1);
                    vfs_close(fd);
                }
            }
        } else {
            gui_terminal_write(arg);
            gui_terminal_write("\n");
        }
    } else if (gui_net_alias_match(cmd)) {
        /* M2.3 异步化：网络工具（ping/nslookup/wget）不再走 launch_path 同步阻塞，
         * 改为启动 gui_nettool 状态机，由 GUI 主循环每帧 tick 驱动，不卡界面。 */
        static char ntbuf[256];
        int bl = 0;
        while (cmd[bl] && bl < (int)sizeof(ntbuf) - 1) { ntbuf[bl] = cmd[bl]; bl++; }
        ntbuf[bl] = 0;
        /* 分词：tok0=工具名，其余为参数 */
        const char *toks[8];
        int ntok = 0;
        char *cur = ntbuf;
        while (*cur && ntok < 8) {
            toks[ntok++] = cur;
            while (*cur && *cur != ' ') cur++;
            if (*cur == ' ') { *cur = 0; cur++; while (*cur == ' ') cur++; }
        }
        const char *tool = toks[0];
        nt_tool_t nt = NT_TOOL_NONE;
        if (tool[0]=='p'&&tool[1]=='i'&&tool[2]=='n'&&tool[3]=='g') nt = NT_TOOL_PING;
        else if (tool[0]=='n'&&tool[1]=='s') nt = NT_TOOL_NSLOOKUP;
        else if (tool[0]=='w'&&tool[1]=='g') nt = NT_TOOL_WGET;
        else if (tool[0]=='i'&&tool[1]=='f') {
            /* ifconfig 仍走旧 ring3（不阻塞，瞬间返回），这里直接提示用 run */
            gui_terminal_write("ifconfig: use 'run /bin/ifconfig'\n");
            nt = NT_TOOL_NONE;
        }
        if (nt == NT_TOOL_NONE) {
            /* 无法映射到异步工具，不处理 */
        } else {
            /* 参数解析：跳过以 '-' 开头的选项，取第一个非选项作为 host，下一个作为 path */
            const char *host = 0, *p2 = 0;
            for (int ai = 1; ai < ntok; ai++) {
                if (toks[ai][0] == '-') continue;
                if (!host) host = toks[ai];
                else if (!p2) { p2 = toks[ai]; break; }
            }
            gui_nettool_start(nt, host, p2);
            /* 注意：不在此处显示提示符；由 nt_finish 在状态机完成时回调 */
        }
        return;
    } else if ((cmd[0] == 'r' && cmd[1] == 'u' && cmd[2] == 'n' && (cmd[3] == ' ' || cmd[3] == '\0')) ||
               (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'e' && cmd[3] == 'c' && (cmd[4] == ' ' || cmd[4] == '\0'))) {
        /* run/exec <path>: launch a user-mode ELF program.
         * Resolves relative paths against cwd, then hands off to the
         * kernel launcher (VFS-first ELF load + ring3 execve/fork rounds).
         * The call blocks until the program tree exits; program stdout is
         * mirrored to the terminal via gui_terminal_write (see SYS_WRITE). */
        /* run/exec <path>：剔除前缀得到参数串。（网络工具别名已在前面分支异步处理） */
        const char *arg = cmd + ((cmd[0] == 'r') ? 3 : 4);
        while (*arg == ' ') arg++;
        if (*arg == 0) {
            gui_terminal_write("run: missing program path\n");
        } else {
            /* M1.8: 按空格分词，token0=程序路径，其余作为 argv 传给 ring3 程序，
             * 让 `run /bin/wget example.com /` 这类带参命令能正确工作。 */
            #define GUI_MAX_ARGV 8
            static char argbuf[256];
            int bl = 0;
            while (arg[bl] && bl < (int)sizeof(argbuf) - 1) { argbuf[bl] = arg[bl]; bl++; }
            argbuf[bl] = 0;
            const char *toks[GUI_MAX_ARGV];
            int ntok = 0;
            char *cur = argbuf;
            while (*cur && ntok < GUI_MAX_ARGV) {
                toks[ntok++] = cur;
                while (*cur && *cur != ' ') cur++;
                if (*cur == ' ') { *cur = 0; cur++; while (*cur == ' ') cur++; }
            }
            char path[128];
            gui_term_resolve(toks[0], path, sizeof(path));
            const char *argv[GUI_MAX_ARGV];
            argv[0] = path;
            for (int ai = 1; ai < ntok; ai++) argv[ai] = toks[ai];
            gui_terminal_write("[run] launching ");
            gui_terminal_write(path);
            gui_terminal_write("\n");
            gui_terminal_set_capture(1);
            int code = arch_x86_64_usermode_launch_path(path, ntok, argv, 0, 0);
            gui_terminal_set_capture(0);
            gui_terminal_write("[run] exit code=");
            {
                char nb[12];
                int ni = 0;
                int v = code;
                if (v < 0) { gui_terminal_write("-"); v = -v; }
                char tmp[12]; int ti = 0;
                if (v == 0) tmp[ti++] = '0';
                while (v > 0 && ti < 11) { tmp[ti++] = (char)('0' + (v % 10)); v /= 10; }
                while (ti > 0) nb[ni++] = tmp[--ti];
                nb[ni] = 0;
                gui_terminal_write(nb);
            }
            gui_terminal_write("\n");
            #undef GUI_MAX_ARGV
        }
    } else {
        gui_terminal_write("unknown command: ");
        gui_terminal_write(cmd);
        gui_terminal_write("\n");
    }
}

void gui_terminal_on_input(char ch) {
    serial_write("[TERMIN] on_input called\n");
    if (!g_gui.initialized || !g_gui.terminal.enabled || !g_gui.terminal.input_focused) {
        serial_write("[TERMIN] guard REJECT (init/enabled/focus)\n");
        return;
    }
    serial_write("[TERMIN] guard PASS\n");

    if (!g_gui.terminal.prompt_shown) {
        gui_terminal_show_prompt();
    }

    if (ch == '\r' || ch == '\n') {
        /* commit command line */
        gui_terminal_putc('\n');
        g_gui.terminal.cmd_buf[g_gui.terminal.cmd_len] = '\0';
        /* trim 首尾空白（空格/制表符/回车），防止参数带尾随空格导致路径匹配失败 */
        {
            char *cb = g_gui.terminal.cmd_buf;
            int cl = g_gui.terminal.cmd_len;
            while (cl > 0 && (cb[cl-1]==' '||cb[cl-1]=='\t'||cb[cl-1]=='\r'||cb[cl-1]=='\n')) cb[--cl]=0;
            int st = 0;
            while (cb[st]==' '||cb[st]=='\t') st++;
            if (st > 0) { int d=0; while (cb[st]) cb[d++]=cb[st++]; cb[d]=0; cl=d; }
            g_gui.terminal.cmd_len = cl;
        }
        gui_terminal_run_command(g_gui.terminal.cmd_buf);
        g_gui.terminal.cmd_len = 0;
        gui_terminal_show_prompt();
        return;
    }

    if (ch == '\b' || ch == 127) {
        /* backspace */
        if (g_gui.terminal.cmd_len > 0) {
            g_gui.terminal.cmd_len--;
            gui_terminal_putc('\b');
        }
        return;
    }

    if (ch == '\t') {
        /* Tab 补全：命令名 / 路径 */
        gui_terminal_complete();
        return;
    }

    /* printable ASCII only */
    if ((unsigned char)ch >= 32 && (unsigned char)ch < 127) {
        if (g_gui.terminal.cmd_len < (int)sizeof(g_gui.terminal.cmd_buf) - 1) {
            g_gui.terminal.cmd_buf[g_gui.terminal.cmd_len++] = ch;
            gui_terminal_putc(ch);
        }
    }
}

void gui_terminal_write(const char *text) {
    if (!text) return;
    while (*text) gui_terminal_putc(*text++);
}

static void gui_terminal_mirror_sink(char c) {
    char s[2];
    s[0] = c;
    s[1] = 0;
    gui_terminal_enqueue_output(s);
}

void gui_terminal_set_capture(int on) {
    arch_x86_64_fd_set_stdout_mirror(on ? gui_terminal_mirror_sink : (void (*)(char))0);
}

void gui_terminal_enqueue_output(const char *text) {
    if (!text) return;
    while (*text) {
        uint32_t head = g_terminal_out_head;
        uint32_t next = (head + 1u) % GUI_TERMINAL_OUTPUT_QUEUE_SIZE;
        if (next == g_terminal_out_tail) {
            return;
        }
        g_terminal_out_queue[head] = *text++;
        g_terminal_out_head = next;
    }
}

void gui_terminal_drain_output_queue(void) {
    uint32_t drained = 0;
    while (g_terminal_out_tail != g_terminal_out_head && drained < GUI_TERMINAL_OUTPUT_DRAIN_LIMIT) {
        char ch = g_terminal_out_queue[g_terminal_out_tail];
        g_terminal_out_tail = (g_terminal_out_tail + 1u) % GUI_TERMINAL_OUTPUT_QUEUE_SIZE;
        gui_terminal_putc(ch);
        drained++;
    }
}

void gui_terminal_view_draw(const gui_terminal_view_t *view, const gui_terminal_view_layout_t *layout, int draw_cursor, uint32_t fg, uint32_t selection_bg, uint32_t selection_fg, uint32_t cursor_color) {
    uint32_t r, c;
    uint32_t r_start = 0, r_end;
    uint32_t c_start = 0, c_end;
    if (!view || !layout || view->cols == 0 || view->rows == 0) return;
    r_end = view->rows;
    c_end = view->cols;
    {
        gui_rect_t clip = layout->clip_rect;
        gui_set_clip_rect(&clip);
    }
    if (g_gui.clip_enabled && g_gui.clip_rect.w <= 0) {
        gui_clear_clip_rect();
        return;
    }
    if (g_gui.clip_enabled) {
        int left = g_gui.clip_rect.x - layout->x;
        int top = g_gui.clip_rect.y - layout->y;
        int right = g_gui.clip_rect.x + g_gui.clip_rect.w - layout->x + layout->cell_w - 1;
        int bottom = g_gui.clip_rect.y + g_gui.clip_rect.h - layout->y + layout->char_h;
        if (left > 0) c_start = (uint32_t)(left / layout->cell_w);
        if (top > 0) r_start = (uint32_t)(top / layout->cell_h);
        if (right > 0) c_end = (uint32_t)(right / layout->cell_w);
        if (bottom > 0) r_end = (uint32_t)(bottom / layout->cell_h);
        if (c_end > view->cols) c_end = view->cols;
        if (r_end > view->rows) r_end = view->rows;
        if (c_start > c_end) c_start = c_end;
        if (r_start > r_end) r_start = r_end;
    }
    for (r = r_start; r < r_end; r++) {
        for (c = c_start; c < c_end; c++) {
            char ch = view->cells[r][c];
            int px = layout->x + (int)c * layout->cell_w;
            int py = layout->y + (int)r * layout->cell_h;
            int selected = gui_terminal_view_cell_selected(view, c, r);
            if (selected) {
                gui_raw_fill_rect(px, py, layout->cell_w, layout->cell_h, selection_bg);
            }
            if (ch != ' ') {
                gui_draw_char(px, py, ch, selected ? selection_fg : fg);
            }
        }
    }
    if (draw_cursor && view->cursor_x < view->cols && view->cursor_y < view->rows) {
        gui_raw_fill_rect(layout->x + (int)view->cursor_x * layout->cell_w,
                          layout->y + (int)view->cursor_y * layout->cell_h + layout->char_h,
                          layout->cell_w - 1, 1, cursor_color);
    }
    gui_clear_clip_rect();
}

void gui_terminal_redraw(void) {
    gui_window_t *w = g_gui.terminal.window;
    gui_rect_t client;
    gui_terminal_view_layout_t layout;
    if (!w || !w->visible || (w->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;
    client.x = w->rect.x + GUI_BORDER_SIZE;
    client.y = w->rect.y + GUI_TITLE_HEIGHT;
    client.w = w->rect.w - GUI_BORDER_SIZE * 2;
    client.h = w->rect.h - GUI_TITLE_HEIGHT - GUI_BORDER_SIZE;
    gui_terminal_view_make_layout(client, 6 - GUI_BORDER_SIZE, 5, &layout);
    gui_terminal_view_draw(&g_gui.terminal.view, &layout,
                           g_gui.terminal.input_focused && g_gui.terminal.cursor_visible,
                           gui_rgb(185, 255, 185),
                           gui_rgb(70, 105, 180),
                           gui_rgb(255, 255, 255),
                           gui_rgb(185, 255, 185));
}

void gui_terminal_invalidate_cursor(void) {
    gui_terminal_invalidate_cursor_at(g_gui.terminal.view.cursor_x, g_gui.terminal.view.cursor_y);
}

void gui_terminal_tick_cursor(void) {
    if (!g_gui.initialized || !g_gui.terminal.enabled || !g_gui.terminal.input_focused) return;
    if (!g_gui.terminal.window || !g_gui.terminal.window->visible ||
        (g_gui.terminal.window->flags & GUI_WINDOW_FLAG_MINIMIZED)) return;

    if (!g_gui.terminal.cursor_visible) {
        g_gui.terminal.cursor_visible = 1;
        g_gui.terminal.cursor_blink_ticks = 0;
        gui_terminal_invalidate_cursor();
    }
}

