/* ============================================================
 * openos - M9 GUI Metrics implementation
 * ============================================================ */

#include "gui_metrics.h"

#define GUI_METRICS_MAX_LISTENERS 8

static gui_density_t g_density = GUI_DENSITY_DESKTOP;

/* Legacy DESKTOP values match gui.h defaults, so pre-M9 code paths
 * see no change when density stays DESKTOP. */
static const gui_metrics_t g_metrics_desktop = {
    /* taskbar_h            */ 40,
    /* title_h              */ 22,
    /* icon_w               */ 72,
    /* icon_h               */ 64,
    /* launcher_item_h      */ 24,
    /* menu_item_h          */ 22,
    /* scrollbar_w          */ 10,
    /* scrollbar_knob_w     */ 11,
    /* scrollbar_knob_h     */ 14,
    /* scrollbar_thumb_min  */ 12,
    /* min_hit_size         */ 20,
    /* touch_slop_px        */ 4,
};

/* Touch mode: bigger everything, mobile-friendly. */
static const gui_metrics_t g_metrics_touch = {
    /* taskbar_h            */ 56,
    /* title_h              */ 34,
    /* icon_w               */ 96,
    /* icon_h               */ 96,
    /* launcher_item_h      */ 44,
    /* menu_item_h          */ 40,
    /* scrollbar_w          */ 20,
    /* scrollbar_knob_w     */ 22,
    /* scrollbar_knob_h     */ 28,
    /* scrollbar_thumb_min  */ 24,
    /* min_hit_size         */ 44,
    /* touch_slop_px        */ 8,
};

typedef struct {
    gui_metrics_listener_t cb;
    void *user;
} listener_slot_t;

static listener_slot_t g_listeners[GUI_METRICS_MAX_LISTENERS];
static int g_listener_count = 0;

void gui_metrics_init(void) {
    g_density = GUI_DENSITY_DESKTOP;
    g_listener_count = 0;
}

void gui_metrics_set_density(gui_density_t d) {
    int i;
    if (d != GUI_DENSITY_DESKTOP && d != GUI_DENSITY_TOUCH) return;
    if (d == g_density) return;
    g_density = d;
    for (i = 0; i < g_listener_count; i++) {
        if (g_listeners[i].cb) {
            g_listeners[i].cb(d, g_listeners[i].user);
        }
    }
}

gui_density_t gui_metrics_get_density(void) {
    return g_density;
}

const gui_metrics_t *gui_metrics_get(void) {
    return (g_density == GUI_DENSITY_TOUCH) ? &g_metrics_touch : &g_metrics_desktop;
}

int gui_metrics_add_listener(gui_metrics_listener_t cb, void *user) {
    if (!cb) return -1;
    if (g_listener_count >= GUI_METRICS_MAX_LISTENERS) return -1;
    g_listeners[g_listener_count].cb = cb;
    g_listeners[g_listener_count].user = user;
    g_listener_count++;
    return 0;
}

int gui_metrics_taskbar_h(void)          { return gui_metrics_get()->taskbar_h; }
int gui_metrics_title_h(void)            { return gui_metrics_get()->title_h; }
int gui_metrics_icon_w(void)             { return gui_metrics_get()->icon_w; }
int gui_metrics_icon_h(void)             { return gui_metrics_get()->icon_h; }
int gui_metrics_launcher_item_h(void)    { return gui_metrics_get()->launcher_item_h; }
int gui_metrics_menu_item_h(void)        { return gui_metrics_get()->menu_item_h; }
int gui_metrics_scrollbar_w(void)        { return gui_metrics_get()->scrollbar_w; }
int gui_metrics_scrollbar_knob_w(void)   { return gui_metrics_get()->scrollbar_knob_w; }
int gui_metrics_scrollbar_knob_h(void)   { return gui_metrics_get()->scrollbar_knob_h; }
int gui_metrics_scrollbar_thumb_min(void){ return gui_metrics_get()->scrollbar_thumb_min; }
int gui_metrics_min_hit_size(void)       { return gui_metrics_get()->min_hit_size; }
int gui_metrics_touch_slop(void)         { return gui_metrics_get()->touch_slop_px; }
