/* ============================================================================
 * gui_mode.c — M10.5 GUI 模式抽象实现
 * ============================================================================ */
#include "gui_mode.h"

#include <stdint.h>

typedef struct gm_listener_s {
    int                  used;
    gui_mode_change_cb_t cb;
    void                *ctx;
} gm_listener_t;

static gui_mode_t        g_mode;
static gm_listener_t     g_listeners[GUI_MODE_MAX_LISTENERS];
static gui_mode_stats_t  g_stats;
static int               g_inited;

static void gm_zero_(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

void gui_mode_init(void) {
    /* 保留 listeners 与 stats 累计；每次 init 只把 mode 复位到 desktop */
    if (!g_inited) {
        gm_zero_(g_listeners, sizeof(g_listeners));
        gm_zero_(&g_stats,    sizeof(g_stats));
        g_inited = 1;
    }
    g_mode = GUI_MODE_DESKTOP;
}

gui_mode_t gui_mode_get(void) { return g_mode; }

int gui_mode_set(gui_mode_t mode) {
    if (mode != GUI_MODE_DESKTOP && mode != GUI_MODE_FULLSCREEN) return -1;
    if (mode == g_mode) { g_stats.noop_sets++; return 0; }
    gui_mode_t old = g_mode;
    g_mode = mode;
    g_stats.transitions++;
    /* 通知 listener */
    for (int i = 0; i < GUI_MODE_MAX_LISTENERS; i++) {
        if (g_listeners[i].used && g_listeners[i].cb) {
            g_stats.listener_calls++;
            g_listeners[i].cb(old, mode, g_listeners[i].ctx);
        }
    }
    return 0;
}

int gui_mode_add_listener(gui_mode_change_cb_t cb, void *ctx) {
    if (!cb) return -1;
    for (int i = 0; i < GUI_MODE_MAX_LISTENERS; i++) {
        if (!g_listeners[i].used) {
            g_listeners[i].used = 1;
            g_listeners[i].cb   = cb;
            g_listeners[i].ctx  = ctx;
            g_stats.listeners_registered++;
            return i;
        }
    }
    return -1;
}

const gui_mode_stats_t *gui_mode_get_stats(void) { return &g_stats; }
