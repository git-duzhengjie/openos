/*
 * input_selftest64.c -- M8-E Input Abstraction Layer selftest.
 *
 * Eight-stage synthetic test:
 *   1) register/lookup/idempotent revive
 *   2) key/rel/abs/syn shim round-trip
 *   3) subscriber fan-out
 *   4) ring drop-oldest overflow
 *   5) unsubscribe stops callbacks
 *   6) unregister marks absent, preserves dev_id
 *   7) gesture engine tee arrives on the bus
 *   8) stat counters advance
 */
#include "../include/input_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/input_core.h"
#include "../../../kernel/include/gesture.h"

#include <stdint.h>
#include <stddef.h>

/* ---- capture buffer for subscriber callback ---- */
#define ISC_CAP 32
static input_event_t g_cap[ISC_CAP];
static int           g_cap_n;

static void isc_listener(const input_event_t *ev, void *user) {
    (void)user;
    if (g_cap_n < ISC_CAP) g_cap[g_cap_n++] = *ev;
}

static void isc_reset_cap(void) { g_cap_n = 0; }

static void isc_drain_ring(void) {
    input_event_t tmp;
    while (input_poll_event(&tmp)) { /* drop */ }
}

static void isc_log(const char *s) { early_console64_write(s); }

bool arch_x86_64_input_selftest_run(void) {
    bool ok = true;
    #define ISC_CHECK(cond, msg)                                        \
        do {                                                            \
            if (!(cond)) {                                              \
                isc_log("[x86_64][input-selftest] FAIL: " msg "\n");    \
                ok = false;                                             \
            }                                                           \
        } while (0)

    input_core_init();

    /* -------- Stage 1: register / lookup / idempotent revive ---------- */
    uint16_t d1 = input_device_register(INPUT_DEV_MOUSE_USB, "sel-mouse");
    uint16_t d2 = input_device_register(INPUT_DEV_TOUCH_USB, "sel-touch");
    ISC_CHECK(d1 != 0 && d2 != 0 && d1 != d2, "stage1 unique dev_id");
    const input_device_t *r = input_device_get(d1);
    ISC_CHECK(r && r->klass == INPUT_DEV_MOUSE_USB && r->present == 1,
              "stage1 device lookup");
    uint16_t d1b = input_device_register(INPUT_DEV_MOUSE_USB, "sel-mouse");
    ISC_CHECK(d1b == d1, "stage1 idempotent register");

    /* -------- Stage 2: shim event round-trip ------------------------- */
    isc_drain_ring();
    uint32_t p0 = input_stat_events_produced();
    input_report_key(d1, INPUT_KEY_MOUSE_LEFT, 1, 111);
    input_report_rel(d1, INPUT_REL_X, 7, 112);
    input_report_abs(d2, 320, 240, 0, 113);
    input_report_syn(d1, 114);
    input_event_t ev;
    int n = 0;
    while (input_poll_event(&ev)) {
        if (n == 0) {
            ISC_CHECK(ev.type == INPUT_EV_KEY && ev.code == INPUT_KEY_MOUSE_LEFT
                      && ev.value == 1 && ev.timestamp_ms == 111,
                      "stage2 KEY payload");
        } else if (n == 1) {
            ISC_CHECK(ev.type == INPUT_EV_REL && ev.code == INPUT_REL_X
                      && ev.value == 7, "stage2 REL payload");
        } else if (n == 2) {
            ISC_CHECK(ev.type == INPUT_EV_ABS && ev.x == 320 && ev.y == 240,
                      "stage2 ABS payload");
        } else if (n == 3) {
            ISC_CHECK(ev.type == INPUT_EV_SYN, "stage2 SYN");
        }
        n++;
    }
    ISC_CHECK(n == 4, "stage2 event count = 4");
    ISC_CHECK(input_stat_events_produced() == p0 + 4, "stage2 produced++");

    /* -------- Stage 3: subscriber fan-out ---------------------------- */
    isc_reset_cap();
    isc_drain_ring();
    int h = input_subscribe(isc_listener, 0);
    ISC_CHECK(h >= 1, "stage3 subscribe handle");
    input_report_key(d1, INPUT_KEY_MOUSE_LEFT, 0, 200);
    input_report_syn(d1, 201);
    ISC_CHECK(g_cap_n == 2, "stage3 listener saw 2 events");
    ISC_CHECK(g_cap[0].type == INPUT_EV_KEY && g_cap[0].value == 0,
              "stage3 KEY release");
    ISC_CHECK(g_cap[1].type == INPUT_EV_SYN, "stage3 SYN");

    /* -------- Stage 4: overflow drop-oldest --------------------------- */
    isc_reset_cap();
    isc_drain_ring();
    uint32_t drop0 = input_stat_events_dropped();
    /* Ring capacity = 256; push 260 → 4 drops. */
    for (int i = 0; i < 260; ++i) {
        input_report_rel(d1, INPUT_REL_X, i, 0);
    }
    ISC_CHECK(input_stat_events_dropped() >= drop0 + 4,
              "stage4 dropped >= 4");
    /* Drain what's left; must be exactly the ring capacity. */
    isc_reset_cap();     /* subscriber still active but capped at ISC_CAP */
    int drained = 0;
    while (input_poll_event(&ev)) drained++;
    ISC_CHECK(drained == 256, "stage4 drained == 256");

    /* -------- Stage 5: unsubscribe ----------------------------------- */
    input_unsubscribe(h);
    isc_reset_cap();
    isc_drain_ring();
    input_report_syn(d1, 900);
    ISC_CHECK(g_cap_n == 0, "stage5 no callback after unsubscribe");

    /* -------- Stage 6: unregister keeps dev_id ----------------------- */
    ISC_CHECK(input_device_unregister(d2) == 0, "stage6 unregister ok");
    const input_device_t *r2 = input_device_get(d2);
    ISC_CHECK(r2 && r2->present == 0, "stage6 present=0 after unregister");
    uint16_t d2b = input_device_register(INPUT_DEV_TOUCH_USB, "sel-touch");
    ISC_CHECK(d2b == d2, "stage6 re-register returns same dev_id");

    /* -------- Stage 7: gesture tee lands on the bus ------------------ */
    /* We can't easily drive gesture_feed() here without disturbing
     * global state further; just verify the gesture device gets
     * auto-registered on demand by feeding a TAP frame sequence. */
    isc_reset_cap();
    isc_drain_ring();
    int h2 = input_subscribe(isc_listener, 0);
    ISC_CHECK(h2 >= 1, "stage7 re-subscribe");
    gesture_init(640, 480);
    gesture_reset();
    touch_frame_t f;
    f.x = 100; f.y = 100; f.tip = 1; f.now_ms = 0;   gesture_feed(&f);
    f.x = 100; f.y = 100; f.tip = 1; f.now_ms = 20;  gesture_feed(&f);
    f.x = 100; f.y = 100; f.tip = 0; f.now_ms = 50;  gesture_feed(&f);
    int saw_tap = 0;
    for (int i = 0; i < g_cap_n; ++i) {
        if (g_cap[i].type == INPUT_EV_GESTURE
            && g_cap[i].code == INPUT_GESTURE_TAP) {
            saw_tap = 1;
            ISC_CHECK(g_cap[i].x == 100 && g_cap[i].y == 100,
                      "stage7 TAP coords via IAL");
        }
    }
    ISC_CHECK(saw_tap, "stage7 gesture tee delivered TAP");
    input_unsubscribe(h2);

    /* -------- Stage 8: stat counters non-zero ------------------------ */
    ISC_CHECK(input_stat_events_produced() > 0, "stage8 produced > 0");
    ISC_CHECK(input_stat_device_count() >= 2,   "stage8 device_count >= 2");

    if (ok) isc_log("[x86_64][input-selftest] PASS\n");
    #undef ISC_CHECK
    return ok;
}
