/*
 * gesture.c -- M8-B touch gesture recognition engine (single-finger).
 *
 * Design goals:
 *   - Pure logic, no dependencies on kernel timer / allocator / GUI.
 *   - Time source is caller-supplied via touch_frame_t::now_ms.
 *   - Events are dispatched synchronously via a registered listener.
 *   - Zero heap: single static state block.
 *
 * State machine:
 *   IDLE          — no finger down; frame with tip=1 → PRESSED
 *   PRESSED       — finger down, movement <8px, duration <500ms
 *                     tip=0  and dur<200ms  → emit TAP        → IDLE
 *                     tip=0  and dur>=200ms → (drop, cancel)  → IDLE
 *                     tip=1  and dur>=500ms → emit LONG_PRESS → PRESSED_LOCKED
 *                     tip=1  and move>=8px  → emit DRAG_BEGIN → DRAGGING
 *   PRESSED_LOCKED— after LONG_PRESS; consume until release, no further events
 *   DRAGGING      — finger down and moved; emit DRAG_MOVE per frame
 *                     tip=0 → emit SWIPE_* if edge criterion met, else DRAG_END
 */
#include "../include/gesture.h"

/* --------------------------- state ---------------------------- */

typedef enum {
    GS_IDLE = 0,
    GS_PRESSED,
    GS_PRESSED_LOCKED,
    GS_DRAGGING,
} gesture_state_t;

typedef struct {
    gesture_state_t     state;

    int                 screen_w;
    int                 screen_h;

    /* current frame */
    int                 cur_x, cur_y;
    uint8_t             prev_tip;

    /* press-anchor */
    int                 start_x, start_y;
    uint32_t            start_ms;
    uint8_t             from_edge;          /* which edge (L/R/T/B), 0 if none */
    uint8_t             long_press_emitted;

    /* diagnostics */
    gesture_type_t      last_event;

    gesture_listener_fn listener;
    void               *listener_user;
} gesture_ctx_t;

/* edge flags (bitmask; only one is set at press time) */
#define EDGE_L   0x01u
#define EDGE_R   0x02u
#define EDGE_T   0x04u
#define EDGE_B   0x08u

static gesture_ctx_t g_g;

/* --------------------------- helpers --------------------------- */

static int gs_abs(int v) { return v < 0 ? -v : v; }

static void gs_emit(gesture_type_t t,
                    int x, int y,
                    int dx, int dy,
                    uint32_t dur_ms)
{
    g_g.last_event = t;
    if (!g_g.listener) return;

    gesture_event_t ev;
    ev.type        = t;
    ev.x           = x;
    ev.y           = y;
    ev.start_x     = g_g.start_x;
    ev.start_y     = g_g.start_y;
    ev.dx          = dx;
    ev.dy          = dy;
    ev.duration_ms = dur_ms;
    g_g.listener(&ev, g_g.listener_user);
}

static uint8_t gs_classify_edge(int x, int y)
{
    /* When the initial press lands within GESTURE_EDGE_THRESHOLD px of a
     * screen edge, the gesture is *armed* for edge-swipe detection. Only
     * one edge is picked (the closest); ties → prefer horizontal. */
    int dl = x;
    int dr = (g_g.screen_w > 0) ? (g_g.screen_w - 1 - x) : 999999;
    int dt = y;
    int db = (g_g.screen_h > 0) ? (g_g.screen_h - 1 - y) : 999999;
    if (dl < 0) dl = 0;
    if (dr < 0) dr = 0;
    if (dt < 0) dt = 0;
    if (db < 0) db = 0;

    int min_h = dl < dr ? dl : dr;
    int min_v = dt < db ? dt : db;
    if (min_h > GESTURE_EDGE_THRESHOLD && min_v > GESTURE_EDGE_THRESHOLD) return 0;

    if (min_h <= min_v) {
        return dl <= dr ? EDGE_L : EDGE_R;
    }
    return dt <= db ? EDGE_T : EDGE_B;
}

static gesture_type_t gs_check_swipe(int dx, int dy)
{
    if (!g_g.from_edge) return GESTURE_TYPE_NONE;

    switch (g_g.from_edge) {
    case EDGE_L: if (dx >=  GESTURE_SWIPE_MIN_DIST) return GESTURE_TYPE_SWIPE_RIGHT; break;
    case EDGE_R: if (dx <= -GESTURE_SWIPE_MIN_DIST) return GESTURE_TYPE_SWIPE_LEFT;  break;
    case EDGE_T: if (dy >=  GESTURE_SWIPE_MIN_DIST) return GESTURE_TYPE_SWIPE_DOWN;  break;
    case EDGE_B: if (dy <= -GESTURE_SWIPE_MIN_DIST) return GESTURE_TYPE_SWIPE_UP;    break;
    default: break;
    }
    return GESTURE_TYPE_NONE;
}

static void gs_reset_press(void)
{
    g_g.state              = GS_IDLE;
    g_g.from_edge          = 0;
    g_g.long_press_emitted = 0;
}

/* --------------------------- public API --------------------------- */

void gesture_init(int screen_w, int screen_h)
{
    g_g.screen_w      = screen_w;
    g_g.screen_h      = screen_h;
    g_g.state         = GS_IDLE;
    g_g.cur_x         = 0;
    g_g.cur_y         = 0;
    g_g.prev_tip      = 0;
    g_g.start_x       = 0;
    g_g.start_y       = 0;
    g_g.start_ms      = 0;
    g_g.from_edge     = 0;
    g_g.long_press_emitted = 0;
    g_g.last_event    = GESTURE_TYPE_NONE;
    g_g.listener      = 0;
    g_g.listener_user = 0;
}

void gesture_reset(void)
{
    gesture_state_t s    = g_g.state; (void)s;
    int             w    = g_g.screen_w;
    int             h    = g_g.screen_h;
    gesture_listener_fn cb   = g_g.listener;
    void               *user = g_g.listener_user;

    gesture_init(w, h);
    g_g.listener      = cb;
    g_g.listener_user = user;
}

void gesture_set_listener(gesture_listener_fn cb, void *user)
{
    g_g.listener      = cb;
    g_g.listener_user = user;
}

gesture_type_t gesture_last_event_type(void)
{
    return g_g.last_event;
}

void gesture_feed(const touch_frame_t *frame)
{
    if (!frame) return;

    int      x       = frame->x;
    int      y       = frame->y;
    uint8_t  tip     = frame->tip ? 1 : 0;
    uint32_t now_ms  = frame->now_ms;

    g_g.cur_x = x;
    g_g.cur_y = y;

    switch (g_g.state) {

    /* --- IDLE: only a tip-down opens a new press cycle --- */
    case GS_IDLE:
        if (tip && !g_g.prev_tip) {
            g_g.start_x            = x;
            g_g.start_y            = y;
            g_g.start_ms           = now_ms;
            g_g.from_edge          = gs_classify_edge(x, y);
            g_g.long_press_emitted = 0;
            g_g.state              = GS_PRESSED;
        }
        break;

    /* --- PRESSED: still finger, watch for tap / long-press / drag --- */
    case GS_PRESSED: {
        int      dx  = x - g_g.start_x;
        int      dy  = y - g_g.start_y;
        uint32_t dur = now_ms - g_g.start_ms;

        if (!tip) {
            /* Release while still: TAP if within tap window, else discard. */
            if (dur <= GESTURE_TAP_MAX_MS
                && gs_abs(dx) < GESTURE_MOVE_HYSTERESIS
                && gs_abs(dy) < GESTURE_MOVE_HYSTERESIS) {
                gs_emit(GESTURE_TYPE_TAP, x, y, dx, dy, dur);
            }
            gs_reset_press();
            break;
        }

        /* Movement crossed hysteresis → begin drag. */
        if (gs_abs(dx) >= GESTURE_MOVE_HYSTERESIS
            || gs_abs(dy) >= GESTURE_MOVE_HYSTERESIS) {
            gs_emit(GESTURE_TYPE_DRAG_BEGIN, x, y, dx, dy, dur);
            g_g.state = GS_DRAGGING;
            break;
        }

        /* Still finger held long enough → LONG_PRESS. */
        if (dur >= GESTURE_LONG_PRESS_MS && !g_g.long_press_emitted) {
            gs_emit(GESTURE_TYPE_LONG_PRESS, x, y, dx, dy, dur);
            g_g.long_press_emitted = 1;
            g_g.state              = GS_PRESSED_LOCKED;
        }
        break;
    }

    /* --- PRESSED_LOCKED: LONG_PRESS already fired, wait for release. --- */
    case GS_PRESSED_LOCKED:
        if (!tip) {
            gs_reset_press();
        }
        break;

    /* --- DRAGGING: keep emitting DRAG_MOVE until release. --- */
    case GS_DRAGGING: {
        int      dx  = x - g_g.start_x;
        int      dy  = y - g_g.start_y;
        uint32_t dur = now_ms - g_g.start_ms;

        if (tip) {
            gs_emit(GESTURE_TYPE_DRAG_MOVE, x, y, dx, dy, dur);
        } else {
            gesture_type_t sw = gs_check_swipe(dx, dy);
            if (sw != GESTURE_TYPE_NONE) {
                gs_emit(sw, x, y, dx, dy, dur);
            } else {
                gs_emit(GESTURE_TYPE_DRAG_END, x, y, dx, dy, dur);
            }
            gs_reset_press();
        }
        break;
    }

    default:
        gs_reset_press();
        break;
    }

    g_g.prev_tip = tip;
}
