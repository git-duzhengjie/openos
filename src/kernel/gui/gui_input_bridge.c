/* ============================================================
 * openos - M9.5 GUI input bridge implementation
 * ============================================================ */

#include "gui_input_bridge.h"
#include "input_core.h"

typedef struct {
    uint32_t tap;
    uint32_t long_press;
    uint32_t drag_begin;
    uint32_t drag_move;
    uint32_t drag_end;
    uint32_t swipe_left;
    uint32_t swipe_right;
    uint32_t swipe_up;
    uint32_t swipe_down;
    uint32_t pinch;
    uint32_t rotate;
    uint32_t total;
} bridge_stats_t;

static bridge_stats_t g_stats;
static int g_sub_handle = 0;
static int g_initialised = 0;

static void on_input_event(const input_event_t *ev, void *user) {
    (void)user;
    if (!ev) return;
    if (ev->type != INPUT_EV_GESTURE) return;
    g_stats.total++;
    switch (ev->code) {
        case INPUT_GESTURE_TAP:         g_stats.tap++;         break;
        case INPUT_GESTURE_LONG_PRESS:  g_stats.long_press++;  break;
        case INPUT_GESTURE_DRAG_BEGIN:  g_stats.drag_begin++;  break;
        case INPUT_GESTURE_DRAG_MOVE:   g_stats.drag_move++;   break;
        case INPUT_GESTURE_DRAG_END:    g_stats.drag_end++;    break;
        case INPUT_GESTURE_SWIPE_LEFT:  g_stats.swipe_left++;  break;
        case INPUT_GESTURE_SWIPE_RIGHT: g_stats.swipe_right++; break;
        case INPUT_GESTURE_SWIPE_UP:    g_stats.swipe_up++;    break;
        case INPUT_GESTURE_SWIPE_DOWN:  g_stats.swipe_down++;  break;
        case INPUT_GESTURE_PINCH:       g_stats.pinch++;       break;
        case INPUT_GESTURE_ROTATE:      g_stats.rotate++;      break;
        default: break;
    }
}

void gui_input_bridge_init(void) {
    if (g_initialised) return;
    input_core_init();
    g_sub_handle = input_subscribe(on_input_event, 0);
    if (g_sub_handle > 0) {
        g_initialised = 1;
    }
}

void gui_input_bridge_shutdown(void) {
    if (!g_initialised) return;
    if (g_sub_handle > 0) {
        input_unsubscribe(g_sub_handle);
    }
    g_sub_handle = 0;
    g_initialised = 0;
}

void gui_input_bridge_reset_stats(void) {
    g_stats.tap = 0;
    g_stats.long_press = 0;
    g_stats.drag_begin = 0;
    g_stats.drag_move = 0;
    g_stats.drag_end = 0;
    g_stats.swipe_left = 0;
    g_stats.swipe_right = 0;
    g_stats.swipe_up = 0;
    g_stats.swipe_down = 0;
    g_stats.pinch = 0;
    g_stats.rotate = 0;
    g_stats.total = 0;
}

uint32_t gui_input_bridge_stat_tap(void)         { return g_stats.tap; }
uint32_t gui_input_bridge_stat_long_press(void)  { return g_stats.long_press; }
uint32_t gui_input_bridge_stat_drag_begin(void)  { return g_stats.drag_begin; }
uint32_t gui_input_bridge_stat_drag_move(void)   { return g_stats.drag_move; }
uint32_t gui_input_bridge_stat_drag_end(void)    { return g_stats.drag_end; }
uint32_t gui_input_bridge_stat_swipe_left(void)  { return g_stats.swipe_left; }
uint32_t gui_input_bridge_stat_swipe_right(void) { return g_stats.swipe_right; }
uint32_t gui_input_bridge_stat_swipe_up(void)    { return g_stats.swipe_up; }
uint32_t gui_input_bridge_stat_swipe_down(void)  { return g_stats.swipe_down; }
uint32_t gui_input_bridge_stat_pinch(void)       { return g_stats.pinch; }
uint32_t gui_input_bridge_stat_rotate(void)      { return g_stats.rotate; }
uint32_t gui_input_bridge_stat_total(void)       { return g_stats.total; }
