/* =========================================================================
 * gesture_multi.h -- M8-C.3 Multi-touch gesture recognition.
 *
 * Independent of gesture.c (single-finger state machine). Consumes a list
 * of per-slot touch frames and emits pinch / rotate events.
 *
 * Recognised events:
 *   PINCH_BEGIN     — two fingers stably down, first frame with both tips=1
 *   PINCH_UPDATE    — subsequent two-finger frames; scale=cur_dist/init_dist
 *   PINCH_END       — one of the two fingers released
 *   ROTATE_UPDATE   — same lifetime as pinch; carries delta_angle_deg
 * ========================================================================= */
#ifndef OPENOS_GESTURE_MULTI_H
#define OPENOS_GESTURE_MULTI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GESTURE_MULTI_MAX_SLOTS 10

typedef enum {
    GESTURE_MULTI_NONE = 0,
    GESTURE_MULTI_PINCH_BEGIN,
    GESTURE_MULTI_PINCH_UPDATE,
    GESTURE_MULTI_PINCH_END,
    GESTURE_MULTI_ROTATE_UPDATE,
    /* M8-C.4 Two-finger scroll: two fingers move together without
     * significantly changing distance or angle. Center-point delta is
     * reported as scroll_dx/scroll_dy in screen pixels. Wheel-notch
     * accumulator (wheel_ticks) is a signed vertical count consumers
     * can consume like a mouse wheel event. */
    GESTURE_MULTI_SCROLL_UPDATE,
} gesture_multi_type_t;

typedef struct {
    gesture_multi_type_t type;
    int      cx, cy;             /* geometric center of the two fingers */
    int      f0_x, f0_y;         /* finger 0 screen coords */
    int      f1_x, f1_y;         /* finger 1 screen coords */
    int      distance;           /* current euclidean distance (integer px) */
    int      initial_distance;   /* distance at pinch-begin */
    int32_t  scale_x1000;        /* fixed-point scale = 1000 * dist/init_dist */
    int32_t  angle_deg;          /* current absolute angle in degrees */
    int32_t  delta_angle_deg;    /* delta from initial angle (rotate) */
    int32_t  scroll_dx;          /* two-finger scroll: center-point delta X (px) */
    int32_t  scroll_dy;          /* two-finger scroll: center-point delta Y (px) */
    int32_t  wheel_ticks;        /* signed wheel notches produced this event */
} gesture_multi_event_t;

typedef struct {
    uint8_t present;     /* 1 = this slot is being tracked this frame */
    uint8_t tip;         /* 1 = finger down, 0 = finger up */
    int     x, y;        /* screen coords */
} gesture_multi_slot_t;

typedef void (*gesture_multi_listener_fn)(const gesture_multi_event_t *ev, void *user);

/* Init / reset. */
void gesture_multi_init(int screen_w, int screen_h);
void gesture_multi_reset(void);
void gesture_multi_set_listener(gesture_multi_listener_fn cb, void *user);

/* Feed one *frame* containing an array of slot states (index=slot_id). */
void gesture_multi_feed(const gesture_multi_slot_t *slots, uint8_t slot_count);

/* Diagnostics. */
gesture_multi_type_t gesture_multi_last_event_type(void);
int32_t gesture_multi_last_scale_x1000(void);
int32_t gesture_multi_last_wheel_ticks(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_GESTURE_MULTI_H */
