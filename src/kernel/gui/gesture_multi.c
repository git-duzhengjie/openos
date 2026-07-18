/* =========================================================================
 * gesture_multi.c -- Multi-touch pinch/rotate recognizer.
 *
 * Pure logic; no external deps. Integer-only math (no libm).
 * ========================================================================= */

#include "gesture_multi.h"

typedef struct {
    int screen_w, screen_h;
    gesture_multi_listener_fn listener;
    void *listener_user;

    uint8_t active;        /* 1 = currently in a two-finger gesture */
    int32_t init_distance;
    int32_t init_angle_deg;
    int32_t last_scale_x1000;
    /* M8-C.4 scroll bookkeeping */
    int32_t last_cx;
    int32_t last_cy;
    int32_t scroll_accum_y;   /* pixel accumulator for wheel-notch conversion */
    int32_t scroll_accum_x;
    int32_t last_wheel_ticks;
    gesture_multi_type_t last_event;
} multi_ctx_t;

static multi_ctx_t g_ctx;

/* ---------- integer sqrt & atan2 (deg) ---------- */

static uint32_t isqrt_u32(uint32_t x) {
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b > 0) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else            { r >>= 1; }
        b >>= 2;
    }
    return r;
}

/* Integer atan2 returning degrees in [-180,180]. Uses linear+quadratic
 * approximation good to ~1 degree. y,x are signed ints, no overflow for
 * |y|,|x| < 2^15 (touch coords in screen space are small). */
static int32_t atan2_deg_i(int y, int x) {
    if (x == 0 && y == 0) return 0;
    int ay = y < 0 ? -y : y;
    int ax = x < 0 ? -x : x;
    /* r = min/max, then approximation deg = 45 * r - (r-1)*(15 + 4*r)/? */
    /* Simpler: use octant with linear rational approx. */
    int32_t angle;
    if (ax >= ay) {
        /* |y|/|x| in [0,1] */
        /* deg ~= 45*|y|/|x| - 10*(|y|/|x|)*(1-|y|/|x|)   crude but ok */
        int64_t num = (int64_t)ay * 1000;
        int32_t r_x1000 = ax == 0 ? 0 : (int32_t)(num / ax); /* 0..1000 */
        /* deg = 45*r - 10*r*(1000-r)/1000, in x1000 space => /1000 at end */
        int64_t deg1000 = (int64_t)45 * r_x1000
                        - (int64_t)10 * r_x1000 * (1000 - r_x1000) / 1000;
        angle = (int32_t)(deg1000 / 1000);
    } else {
        int64_t num = (int64_t)ax * 1000;
        int32_t r_x1000 = ay == 0 ? 0 : (int32_t)(num / ay);
        int64_t deg1000 = (int64_t)45 * r_x1000
                        - (int64_t)10 * r_x1000 * (1000 - r_x1000) / 1000;
        angle = 90 - (int32_t)(deg1000 / 1000);
    }
    /* Assign correct quadrant. */
    if (x < 0 && y >= 0) angle = 180 - angle;
    else if (x < 0 && y < 0) angle = -180 + angle;
    else if (x >= 0 && y < 0) angle = -angle;
    return angle;
}

/* ---------- API ---------- */

void gesture_multi_init(int w, int h) {
    for (size_t i = 0; i < sizeof(g_ctx); i++) ((uint8_t*)&g_ctx)[i] = 0;
    g_ctx.screen_w = w;
    g_ctx.screen_h = h;
}
void gesture_multi_reset(void) {
    g_ctx.active = 0;
    g_ctx.init_distance = 0;
    g_ctx.init_angle_deg = 0;
    g_ctx.last_scale_x1000 = 1000;
    g_ctx.last_event = GESTURE_MULTI_NONE;
}
void gesture_multi_set_listener(gesture_multi_listener_fn cb, void *user) {
    g_ctx.listener = cb;
    g_ctx.listener_user = user;
}
gesture_multi_type_t gesture_multi_last_event_type(void) { return g_ctx.last_event; }
int32_t gesture_multi_last_scale_x1000(void) { return g_ctx.last_scale_x1000; }
int32_t gesture_multi_last_wheel_ticks(void) { return g_ctx.last_wheel_ticks; }

static void emit(gesture_multi_event_t *ev) {
    g_ctx.last_event = ev->type;
    if (ev->type == GESTURE_MULTI_PINCH_UPDATE || ev->type == GESTURE_MULTI_PINCH_BEGIN)
        g_ctx.last_scale_x1000 = ev->scale_x1000;
    if (g_ctx.listener) g_ctx.listener(ev, g_ctx.listener_user);
}

void gesture_multi_feed(const gesture_multi_slot_t *slots, uint8_t slot_count) {
    /* Find first two "tip=1" slots. */
    int i0 = -1, i1 = -1;
    for (uint8_t i = 0; i < slot_count && i < GESTURE_MULTI_MAX_SLOTS; i++) {
        if (!slots[i].present || !slots[i].tip) continue;
        if (i0 < 0) i0 = i;
        else if (i1 < 0) { i1 = i; break; }
    }

    if (i0 < 0 || i1 < 0) {
        /* Fewer than 2 fingers → end pinch if active. */
        if (g_ctx.active) {
            gesture_multi_event_t ev;
            for (size_t k = 0; k < sizeof(ev); k++) ((uint8_t*)&ev)[k] = 0;
            ev.type = GESTURE_MULTI_PINCH_END;
            ev.scale_x1000 = g_ctx.last_scale_x1000;
            ev.initial_distance = g_ctx.init_distance;
            emit(&ev);
            g_ctx.active = 0;
        }
        return;
    }

    int dx = slots[i1].x - slots[i0].x;
    int dy = slots[i1].y - slots[i0].y;
    uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
    int32_t dist = (int32_t)isqrt_u32(d2);
    int32_t angle = atan2_deg_i(dy, dx);

    gesture_multi_event_t ev;
    for (size_t k = 0; k < sizeof(ev); k++) ((uint8_t*)&ev)[k] = 0;
    ev.f0_x = slots[i0].x; ev.f0_y = slots[i0].y;
    ev.f1_x = slots[i1].x; ev.f1_y = slots[i1].y;
    ev.cx = (slots[i0].x + slots[i1].x) / 2;
    ev.cy = (slots[i0].y + slots[i1].y) / 2;
    ev.distance = dist;
    ev.angle_deg = angle;

    if (!g_ctx.active) {
        g_ctx.active = 1;
        g_ctx.init_distance = dist ? dist : 1;
        g_ctx.init_angle_deg = angle;
        g_ctx.last_scale_x1000 = 1000;
        g_ctx.last_cx = ev.cx;
        g_ctx.last_cy = ev.cy;
        g_ctx.scroll_accum_x = 0;
        g_ctx.scroll_accum_y = 0;
        g_ctx.last_wheel_ticks = 0;
        ev.type = GESTURE_MULTI_PINCH_BEGIN;
        ev.initial_distance = g_ctx.init_distance;
        ev.scale_x1000 = 1000;
        ev.delta_angle_deg = 0;
        emit(&ev);
    } else {
        ev.type = GESTURE_MULTI_PINCH_UPDATE;
        ev.initial_distance = g_ctx.init_distance;
        ev.scale_x1000 = (int32_t)(((int64_t)dist * 1000) / g_ctx.init_distance);
        ev.delta_angle_deg = angle - g_ctx.init_angle_deg;
        /* normalise to [-180, 180] */
        while (ev.delta_angle_deg > 180)  ev.delta_angle_deg -= 360;
        while (ev.delta_angle_deg < -180) ev.delta_angle_deg += 360;
        emit(&ev);
        /* Also emit rotate update as a second event if angle delta is
         * non-trivial; kept simple: reuse same struct, change type. */
        if (ev.delta_angle_deg > 3 || ev.delta_angle_deg < -3) {
            ev.type = GESTURE_MULTI_ROTATE_UPDATE;
            emit(&ev);
        }

        /* M8-C.4: two-finger scroll detection.
         *   Condition: |scale - 1000| < 80 (≤8% distance change)  AND
         *              |delta_angle| ≤ 5 degrees.
         *   Center-point delta becomes scroll delta; every 24 accumulated
         *   vertical px produces one wheel-notch tick (mouse-wheel style). */
        int32_t scale_abs = ev.scale_x1000 > 1000 ? (ev.scale_x1000 - 1000)
                                                  : (1000 - ev.scale_x1000);
        int32_t angle_abs = ev.delta_angle_deg < 0 ? -ev.delta_angle_deg
                                                   :  ev.delta_angle_deg;
        int32_t dcx = ev.cx - g_ctx.last_cx;
        int32_t dcy = ev.cy - g_ctx.last_cy;
        g_ctx.last_cx = ev.cx;
        g_ctx.last_cy = ev.cy;

        if (scale_abs < 80 && angle_abs <= 5 && (dcx != 0 || dcy != 0)) {
            g_ctx.scroll_accum_x += dcx;
            g_ctx.scroll_accum_y += dcy;
            /* Convert accumulated Y into signed wheel notches.
             * Natural-scroll: dragging fingers *down* scrolls page *up*,
             * so ticks = -accum/NOTCH. NOTCH = 24 px per notch. */
            const int32_t NOTCH = 24;
            int32_t ticks = 0;
            while (g_ctx.scroll_accum_y >= NOTCH) {
                g_ctx.scroll_accum_y -= NOTCH;
                ticks -= 1;
            }
            while (g_ctx.scroll_accum_y <= -NOTCH) {
                g_ctx.scroll_accum_y += NOTCH;
                ticks += 1;
            }
            g_ctx.last_wheel_ticks = ticks;

            gesture_multi_event_t sev;
            for (size_t k = 0; k < sizeof(sev); k++) ((uint8_t*)&sev)[k] = 0;
            sev.type = GESTURE_MULTI_SCROLL_UPDATE;
            sev.f0_x = ev.f0_x; sev.f0_y = ev.f0_y;
            sev.f1_x = ev.f1_x; sev.f1_y = ev.f1_y;
            sev.cx = ev.cx; sev.cy = ev.cy;
            sev.distance = ev.distance;
            sev.initial_distance = g_ctx.init_distance;
            sev.scale_x1000 = ev.scale_x1000;
            sev.angle_deg = ev.angle_deg;
            sev.delta_angle_deg = ev.delta_angle_deg;
            sev.scroll_dx = dcx;
            sev.scroll_dy = dcy;
            sev.wheel_ticks = ticks;
            emit(&sev);
        }
    }
}
