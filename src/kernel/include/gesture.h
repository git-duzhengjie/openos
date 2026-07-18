/*
 * gesture.h -- M8-B touch gesture recognition engine.
 *
 * A pure-logic single-finger gesture state machine driven by touch frames
 * (x, y, tip, now_ms). Produces high-level gesture events via a listener
 * callback. Has no external dependencies (no timer, no allocator, no GUI):
 *   - time is supplied by the caller (now_ms)
 *   - events are delivered synchronously to a registered listener
 *
 * Recognised events (M8-B.2 / M8-B.3):
 *   TAP           — tip down→up in <200ms and <8px movement
 *   LONG_PRESS    — tip held ≥500ms with <8px movement
 *   DRAG_BEGIN    — first frame after crossing 8px hysteresis while held
 *   DRAG_MOVE     — subsequent frames while dragging
 *   DRAG_END      — tip release from dragging state
 *   SWIPE_LEFT/RIGHT/UP/DOWN — release from an edge-swipe gesture
 *                              (started within 32px of an edge and moved
 *                               ≥80px in that inward direction)
 *
 * NOTE: gesture output does NOT replace mouse injection; the touchscreen
 * driver keeps calling mouse_set_absolute_position_with_wheel() for
 * cursor movement / left-click emulation, so applications unaware of
 * gestures still work.
 */
#ifndef OPENOS_GESTURE_H
#define OPENOS_GESTURE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- tunables (compile-time defaults) -------------------- */
#define GESTURE_TAP_MAX_MS        200u   /* TAP: press+release within */
#define GESTURE_LONG_PRESS_MS     500u   /* LONG_PRESS threshold */
#define GESTURE_MOVE_HYSTERESIS   8      /* px: below→still, above→drag */
#define GESTURE_EDGE_THRESHOLD    32     /* px from edge to arm swipe */
#define GESTURE_SWIPE_MIN_DIST    80     /* px inward travel for swipe */

typedef enum {
    GESTURE_TYPE_NONE = 0,
    GESTURE_TYPE_TAP,
    GESTURE_TYPE_LONG_PRESS,
    GESTURE_TYPE_DRAG_BEGIN,
    GESTURE_TYPE_DRAG_MOVE,
    GESTURE_TYPE_DRAG_END,
    GESTURE_TYPE_SWIPE_LEFT,
    GESTURE_TYPE_SWIPE_RIGHT,
    GESTURE_TYPE_SWIPE_UP,
    GESTURE_TYPE_SWIPE_DOWN,
} gesture_type_t;

typedef struct {
    gesture_type_t type;
    int      x, y;           /* current touch point (screen coords) */
    int      start_x, start_y;
    int      dx, dy;         /* cumulative dx/dy since press */
    uint32_t duration_ms;    /* since press */
} gesture_event_t;

typedef struct {
    int      x, y;      /* screen-space coordinates */
    uint8_t  tip;       /* 1 = finger down, 0 = up */
    uint32_t now_ms;    /* caller-supplied monotonic ms; needn't be wall clock */
} touch_frame_t;

typedef void (*gesture_listener_fn)(const gesture_event_t *ev, void *user);

/* Initialise or reset the state machine. Must be called before feeding
 * frames. `screen_w`/`screen_h` are used for edge-swipe detection. */
void gesture_init(int screen_w, int screen_h);

/* Full state reset (keeps screen dims and listener). Used by selftests. */
void gesture_reset(void);

/* Register / clear the listener. Passing cb=NULL disables events. */
void gesture_set_listener(gesture_listener_fn cb, void *user);

/* Feed one touch frame. Emits zero or one gesture event synchronously. */
void gesture_feed(const touch_frame_t *frame);

/* Query the last emitted event (for selftests / diagnostics). Returns
 * GESTURE_TYPE_NONE if nothing has been emitted since gesture_reset(). */
gesture_type_t gesture_last_event_type(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_GESTURE_H */
