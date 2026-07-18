/*
 * gesture_selftest64.c -- M8-B.4 gesture engine selftest.
 *
 * Six synthetic scenarios drive the state machine deterministically:
 *   1) TAP, 2) LONG_PRESS, 3) DRAG, 4) SWIPE_RIGHT (from left edge),
 *   5) SWIPE_UP (from bottom edge), 6) release outside tap window → nothing.
 *
 * No real HID input required; time is supplied via touch_frame_t::now_ms.
 */
#include "../include/gesture_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/gesture.h"

#include <stdint.h>
#include <stddef.h>

/* --------- captured event log ---------- */
#define GST_LOG_MAX 32
static gesture_event_t g_evs[GST_LOG_MAX];
static int             g_ev_n;

static void gst_listener(const gesture_event_t *ev, void *user)
{
    (void)user;
    if (g_ev_n < GST_LOG_MAX) {
        g_evs[g_ev_n++] = *ev;
    }
}

static void gst_reset_capture(void)
{
    g_ev_n = 0;
    for (int i = 0; i < GST_LOG_MAX; ++i) {
        g_evs[i].type = GESTURE_TYPE_NONE;
    }
}

static void gst_feed(int x, int y, int tip, uint32_t ms)
{
    touch_frame_t f;
    f.x      = x;
    f.y      = y;
    f.tip    = (uint8_t)tip;
    f.now_ms = ms;
    gesture_feed(&f);
}

static void gst_log(const char *s) { early_console64_write(s); }

bool arch_x86_64_gesture_selftest_run(void)
{
    bool ok = true;
    #define GT_CHECK(cond, msg)                                           \
        do {                                                              \
            if (!(cond)) {                                                \
                gst_log("[x86_64][gesture-selftest] FAIL: " msg "\n");    \
                ok = false;                                               \
            }                                                             \
        } while (0)

    gesture_init(640, 480);
    gesture_set_listener(gst_listener, 0);

    /* ------------ Stage 1: TAP (press+release @ 100,100, dur=50ms) ------- */
    gst_reset_capture();
    gesture_reset();
    gst_feed(100, 100, 1,   0);   /* down */
    gst_feed(100, 100, 1,  20);   /* still */
    gst_feed(100, 100, 0,  50);   /* up (within 200ms) */
    GT_CHECK(g_ev_n == 1,                                 "stage1 event count");
    GT_CHECK(g_evs[0].type == GESTURE_TYPE_TAP,           "stage1 TAP");
    GT_CHECK(g_evs[0].x == 100 && g_evs[0].y == 100,      "stage1 TAP coords");
    GT_CHECK(gesture_last_event_type() == GESTURE_TYPE_TAP, "stage1 last=TAP");

    /* ------------ Stage 2: LONG_PRESS (hold 600ms, still) ---------------- */
    gst_reset_capture();
    gesture_reset();
    gst_feed(200, 200, 1,   0);
    gst_feed(200, 200, 1, 100);
    gst_feed(201, 201, 1, 300);   /* jitter <8px, still counts as still */
    gst_feed(200, 200, 1, 550);   /* triggers LONG_PRESS (>=500ms) */
    gst_feed(200, 200, 1, 700);   /* locked, no repeat */
    gst_feed(200, 200, 0, 800);   /* release, no further event */
    GT_CHECK(g_ev_n == 1,                                     "stage2 count");
    GT_CHECK(g_evs[0].type == GESTURE_TYPE_LONG_PRESS,        "stage2 LONG_PRESS");
    GT_CHECK(g_evs[0].duration_ms >= GESTURE_LONG_PRESS_MS,   "stage2 duration");

    /* ------------ Stage 3: DRAG (begin+moves+end, no edge) --------------- */
    gst_reset_capture();
    gesture_reset();
    gst_feed(300, 300, 1,   0);
    gst_feed(302, 301, 1,  20);   /* <8px → still PRESSED, no event */
    gst_feed(320, 305, 1,  40);   /* dx=20 → crosses hysteresis: DRAG_BEGIN */
    gst_feed(340, 310, 1,  60);   /* DRAG_MOVE */
    gst_feed(360, 315, 1,  80);   /* DRAG_MOVE */
    gst_feed(360, 315, 0, 100);   /* release → DRAG_END (no edge → no swipe) */
    GT_CHECK(g_ev_n == 4,                                   "stage3 count=4");
    GT_CHECK(g_evs[0].type == GESTURE_TYPE_DRAG_BEGIN,      "stage3[0] DRAG_BEGIN");
    GT_CHECK(g_evs[1].type == GESTURE_TYPE_DRAG_MOVE,       "stage3[1] DRAG_MOVE");
    GT_CHECK(g_evs[2].type == GESTURE_TYPE_DRAG_MOVE,       "stage3[2] DRAG_MOVE");
    GT_CHECK(g_evs[3].type == GESTURE_TYPE_DRAG_END,        "stage3[3] DRAG_END");
    GT_CHECK(g_evs[3].dx == 60 && g_evs[3].dy == 15,        "stage3 final dx/dy");

    /* ------------ Stage 4: SWIPE_RIGHT (from left edge, dx=100) ---------- */
    gst_reset_capture();
    gesture_reset();
    gst_feed(10, 240, 1,   0);    /* within 32px of left edge → armed EDGE_L */
    gst_feed(50, 240, 1,  20);    /* dx=40 → DRAG_BEGIN */
    gst_feed(90, 240, 1,  40);    /* DRAG_MOVE */
    gst_feed(110, 240, 0, 60);    /* release, dx=100 ≥ 80 → SWIPE_RIGHT */
    GT_CHECK(g_ev_n == 3,                                   "stage4 count=3");
    GT_CHECK(g_evs[0].type == GESTURE_TYPE_DRAG_BEGIN,      "stage4 DRAG_BEGIN");
    GT_CHECK(g_evs[1].type == GESTURE_TYPE_DRAG_MOVE,       "stage4 DRAG_MOVE");
    GT_CHECK(g_evs[2].type == GESTURE_TYPE_SWIPE_RIGHT,     "stage4 SWIPE_RIGHT");
    GT_CHECK(g_evs[2].dx == 100,                            "stage4 dx=100");

    /* ------------ Stage 5: SWIPE_UP (from bottom edge, dy=-90) ----------- */
    gst_reset_capture();
    gesture_reset();
    gst_feed(320, 470, 1,   0);   /* y=470, h=480 → 9px from bottom → EDGE_B */
    gst_feed(320, 430, 1,  20);   /* dy=-40 → DRAG_BEGIN */
    gst_feed(320, 400, 1,  40);   /* DRAG_MOVE */
    gst_feed(320, 380, 0, 60);    /* dy=-90 ≥ 80 → SWIPE_UP */
    GT_CHECK(g_ev_n == 3,                                   "stage5 count=3");
    GT_CHECK(g_evs[0].type == GESTURE_TYPE_DRAG_BEGIN,      "stage5 DRAG_BEGIN");
    GT_CHECK(g_evs[2].type == GESTURE_TYPE_SWIPE_UP,        "stage5 SWIPE_UP");
    GT_CHECK(g_evs[2].dy == -90,                            "stage5 dy=-90");

    /* ------------ Stage 6: release after 300ms still → nothing ---------- */
    /* Held past the 200ms tap window but under the 500ms long-press window,
     * with <8px movement. Should emit *nothing* on release. */
    gst_reset_capture();
    gesture_reset();
    gst_feed(400, 400, 1,   0);
    gst_feed(400, 400, 1, 150);
    gst_feed(400, 400, 0, 300);
    GT_CHECK(g_ev_n == 0,                                   "stage6 no events");
    GT_CHECK(gesture_last_event_type() == GESTURE_TYPE_NONE,
             "stage6 last=NONE");

    /* Clean up: drop listener so we don't disturb runtime. */
    gesture_set_listener(0, 0);

    if (ok) gst_log("[x86_64][gesture-selftest] PASS\n");
    #undef GT_CHECK
    return ok;
}
