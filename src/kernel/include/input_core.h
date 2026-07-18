/*
 * input_core.h -- M8-E Input Abstraction Layer (IAL).
 *
 * Unified evdev-style input event bus. Producers (PS/2 mouse, USB HID
 * tablet/touchscreen, gesture engine, future keyboards, I²C HID) call
 * input_report() to tee events onto the bus in parallel with the legacy
 * g_mouse injection path. Consumers subscribe with input_subscribe() to
 * receive synchronous callbacks, or drain the ring via input_poll_event().
 *
 * Design goals:
 *   - Zero heap: all storage is static (ring buffer, device table,
 *     subscriber slots).
 *   - Zero libm, no floats.
 *   - Legacy consumers (gui.c / lockscreen.c) keep working through g_mouse;
 *     IAL is a parallel side-channel that upper layers (M8-F notification,
 *     M8-D system gestures, future input-method) can start using immediately.
 *   - IRQ-safe production: caller may hold interrupts off; input_report()
 *     does not sleep, does not allocate, does not call listeners with
 *     interrupts on (listeners are expected to be quick).
 */
#ifndef OPENOS_INPUT_CORE_H
#define OPENOS_INPUT_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- event types (evdev-inspired) ---------------- */

typedef enum {
    INPUT_EV_SYN     = 0,   /* end-of-frame separator */
    INPUT_EV_KEY     = 1,   /* keyboard key or mouse button (code + value) */
    INPUT_EV_REL     = 2,   /* relative axis (dx/dy/wheel) */
    INPUT_EV_ABS     = 3,   /* absolute axis (x/y/pressure) */
    INPUT_EV_TOUCH   = 4,   /* multi-touch contact frame */
    INPUT_EV_GESTURE = 5,   /* high-level gesture event */
} input_ev_type_t;

/* KEY codes (subset; matches Linux evdev where practical) */
#define INPUT_KEY_MOUSE_LEFT      0x110u  /* BTN_LEFT   */
#define INPUT_KEY_MOUSE_RIGHT     0x111u  /* BTN_RIGHT  */
#define INPUT_KEY_MOUSE_MIDDLE    0x112u  /* BTN_MIDDLE */
#define INPUT_KEY_TOUCH           0x14au  /* BTN_TOUCH  */

/* REL axis codes */
#define INPUT_REL_X               0u
#define INPUT_REL_Y               1u
#define INPUT_REL_WHEEL           8u

/* ABS axis codes */
#define INPUT_ABS_X               0u
#define INPUT_ABS_Y               1u
#define INPUT_ABS_PRESSURE        24u

/* TOUCH sub-code stored in code field */
#define INPUT_TOUCH_CONTACT       0u   /* single-contact update: value=(id<<24)|(tip<<16)|reserved; x/y in extra fields */
#define INPUT_TOUCH_FRAME_END     1u   /* value = active contact count */

/* GESTURE sub-code (mirrors gesture_type_t values 1..9) */
#define INPUT_GESTURE_TAP         1u
#define INPUT_GESTURE_LONG_PRESS  2u
#define INPUT_GESTURE_DRAG_BEGIN  3u
#define INPUT_GESTURE_DRAG_MOVE   4u
#define INPUT_GESTURE_DRAG_END    5u
#define INPUT_GESTURE_SWIPE_LEFT  6u
#define INPUT_GESTURE_SWIPE_RIGHT 7u
#define INPUT_GESTURE_SWIPE_UP    8u
#define INPUT_GESTURE_SWIPE_DOWN  9u
#define INPUT_GESTURE_PINCH       10u
#define INPUT_GESTURE_ROTATE      11u

/* ---------------- device classes ---------------- */

typedef enum {
    INPUT_DEV_UNKNOWN     = 0,
    INPUT_DEV_MOUSE_PS2   = 1,
    INPUT_DEV_MOUSE_USB   = 2,
    INPUT_DEV_TABLET_USB  = 3,
    INPUT_DEV_TOUCH_USB   = 4,
    INPUT_DEV_TOUCH_I2C   = 5,
    INPUT_DEV_KEYBOARD    = 6,
    INPUT_DEV_GESTURE     = 7,   /* synthetic device: emits INPUT_EV_GESTURE */
    INPUT_DEV_MAX,
} input_dev_class_t;

/* ---------------- event struct ---------------- */

typedef struct {
    uint32_t timestamp_ms;   /* provided by caller or 0 if unknown */
    uint16_t dev_id;         /* device id from input_device_register(); 0 = unknown/synthetic */
    uint16_t type;           /* input_ev_type_t */
    uint32_t code;           /* axis / key / gesture sub-code */
    int32_t  value;          /* key state, delta, gesture id, contact packed */
    int32_t  x;              /* auxiliary payload (touch x, abs x, rotate deg) */
    int32_t  y;              /* auxiliary payload (touch y, abs y, pinch scale_x1000) */
} input_event_t;

/* ---------------- subscriber callback ---------------- */

typedef void (*input_listener_fn)(const input_event_t *ev, void *user);

/* ---------------- device descriptor ---------------- */

typedef struct {
    uint16_t          dev_id;
    input_dev_class_t klass;
    const char       *name;      /* short static string (no ownership) */
    uint8_t           present;   /* 1 = plugged in, 0 = removed */
    uint32_t          event_count;
} input_device_t;

/* ---------------- API ---------------- */

/* Initialise the input core. Safe to call multiple times (idempotent). */
void input_core_init(void);

/* Register a device. Returns dev_id (1..INPUT_MAX_DEVICES), or 0 on failure.
 * `name` must point to a string with static lifetime.
 * Multiple calls for the same class+name reuse the same slot (idempotent). */
uint16_t input_device_register(input_dev_class_t klass, const char *name);

/* Mark a device as unplugged. Slot remains reserved so its dev_id stays
 * stable across re-plug. Returns 0 on success. */
int input_device_unregister(uint16_t dev_id);

/* Publish one event onto the bus. Copies into the ring, then fans out to
 * every subscriber synchronously. IRQ-safe (does not sleep or allocate). */
void input_report(const input_event_t *ev);

/* Convenience shims for common producers. */
void input_report_key(uint16_t dev_id, uint32_t key, int32_t value, uint32_t ts_ms);
void input_report_rel(uint16_t dev_id, uint32_t axis, int32_t delta, uint32_t ts_ms);
void input_report_abs(uint16_t dev_id, int32_t x, int32_t y, int32_t pressure, uint32_t ts_ms);
void input_report_syn(uint16_t dev_id, uint32_t ts_ms);

/* Subscribe / unsubscribe. Returns subscription handle (>=1) or 0. */
int  input_subscribe(input_listener_fn fn, void *user);
void input_unsubscribe(int handle);

/* Pull one event out of the ring (non-blocking). Returns 1 on success,
 * 0 if the queue is empty. Consumers may use either subscribe() OR
 * poll(); the ring is shared so poll() will observe events even without
 * any subscriber. */
int  input_poll_event(input_event_t *out);

/* Diagnostics for selftest / dmesg. */
uint32_t input_stat_events_produced(void);
uint32_t input_stat_events_dropped(void);
uint32_t input_stat_ring_depth(void);
uint16_t input_stat_device_count(void);
const input_device_t *input_device_get(uint16_t dev_id);

/* Compile-time capacities (exposed for tests). */
#define INPUT_MAX_DEVICES     16
#define INPUT_MAX_SUBSCRIBERS 8
#define INPUT_RING_CAPACITY   256   /* MUST be a power of two */

#ifdef __cplusplus
}
#endif
#endif /* OPENOS_INPUT_CORE_H */
