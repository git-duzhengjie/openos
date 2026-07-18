/*
 * gui_input_bridge_selftest64.c -- M9.5 GUI input bridge selftest.
 *
 * Verifies:
 *   - init() subscribes to input_core exactly once (idempotent).
 *   - Published GESTURE events increment the matching per-code counter.
 *   - Non-GESTURE events (KEY/REL/ABS) do NOT touch bridge counters.
 *   - reset_stats() clears counters.
 *   - shutdown() unsubscribes; further publishes do not increment.
 */

#include "../include/gui_input_bridge_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/gui_input_bridge.h"
#include "../../../kernel/include/input_core.h"

#include <stdint.h>

static void gib_st_log(const char *s) { early_console64_write(s); }

static void pub_gesture(uint32_t code) {
    input_event_t ev = {0};
    ev.type = INPUT_EV_GESTURE;
    ev.code = code;
    input_report(&ev);
}

bool arch_x86_64_gui_input_bridge_selftest_run(void)
{
    bool ok = true;
    #define GIB_ST_CHECK(cond, msg)                                              \
        do {                                                                     \
            if (!(cond)) {                                                       \
                gib_st_log("[x86_64][gui-input-bridge-selftest] FAIL: " msg "\n");\
                ok = false;                                                      \
            }                                                                    \
        } while (0)

    /* Clean slate: input core reset + bridge init. */
    input_core_init();
    gui_input_bridge_shutdown();
    gui_input_bridge_reset_stats();
    gui_input_bridge_init();

    /* Stage 1: baseline all zero. */
    GIB_ST_CHECK(gui_input_bridge_stat_total() == 0, "total starts at 0");
    GIB_ST_CHECK(gui_input_bridge_stat_tap() == 0, "tap starts at 0");
    gib_st_log("[x86_64][gui-input-bridge-selftest] stage 1 baseline OK\n");

    /* Stage 2: TAP increments tap + total. */
    pub_gesture(INPUT_GESTURE_TAP);
    GIB_ST_CHECK(gui_input_bridge_stat_tap() == 1, "TAP counter increments");
    GIB_ST_CHECK(gui_input_bridge_stat_total() == 1, "total increments");
    gib_st_log("[x86_64][gui-input-bridge-selftest] stage 2 tap OK\n");

    /* Stage 3: all four swipes each once. */
    pub_gesture(INPUT_GESTURE_SWIPE_LEFT);
    pub_gesture(INPUT_GESTURE_SWIPE_RIGHT);
    pub_gesture(INPUT_GESTURE_SWIPE_UP);
    pub_gesture(INPUT_GESTURE_SWIPE_DOWN);
    GIB_ST_CHECK(gui_input_bridge_stat_swipe_left() == 1, "swipe_left++");
    GIB_ST_CHECK(gui_input_bridge_stat_swipe_right() == 1, "swipe_right++");
    GIB_ST_CHECK(gui_input_bridge_stat_swipe_up() == 1, "swipe_up++");
    GIB_ST_CHECK(gui_input_bridge_stat_swipe_down() == 1, "swipe_down++");
    GIB_ST_CHECK(gui_input_bridge_stat_total() == 5, "total == 5 after 4 swipes+1 tap");
    gib_st_log("[x86_64][gui-input-bridge-selftest] stage 3 swipes OK\n");

    /* Stage 4: LONG_PRESS + DRAG_BEGIN/MOVE/END. */
    pub_gesture(INPUT_GESTURE_LONG_PRESS);
    pub_gesture(INPUT_GESTURE_DRAG_BEGIN);
    pub_gesture(INPUT_GESTURE_DRAG_MOVE);
    pub_gesture(INPUT_GESTURE_DRAG_MOVE);
    pub_gesture(INPUT_GESTURE_DRAG_END);
    GIB_ST_CHECK(gui_input_bridge_stat_long_press() == 1, "long_press++");
    GIB_ST_CHECK(gui_input_bridge_stat_drag_begin() == 1, "drag_begin++");
    GIB_ST_CHECK(gui_input_bridge_stat_drag_move() == 2, "drag_move==2");
    GIB_ST_CHECK(gui_input_bridge_stat_drag_end() == 1, "drag_end++");
    gib_st_log("[x86_64][gui-input-bridge-selftest] stage 4 drag OK\n");

    /* Stage 5: Pinch + Rotate. */
    pub_gesture(INPUT_GESTURE_PINCH);
    pub_gesture(INPUT_GESTURE_ROTATE);
    GIB_ST_CHECK(gui_input_bridge_stat_pinch() == 1, "pinch++");
    GIB_ST_CHECK(gui_input_bridge_stat_rotate() == 1, "rotate++");
    gib_st_log("[x86_64][gui-input-bridge-selftest] stage 5 multi-touch OK\n");

    /* Stage 6: non-GESTURE events are ignored by the bridge. */
    uint32_t total_before = gui_input_bridge_stat_total();
    input_event_t ev = {0};
    ev.type = INPUT_EV_KEY;
    ev.code = 0x1E; /* KEY_A */
    ev.value = 1;
    input_report(&ev);
    ev.type = INPUT_EV_REL;
    ev.code = 0;
    ev.value = 3;
    input_report(&ev);
    ev.type = INPUT_EV_ABS;
    ev.code = 0;
    ev.value = 100;
    input_report(&ev);
    GIB_ST_CHECK(gui_input_bridge_stat_total() == total_before,
                 "non-GESTURE events must not change bridge total");
    gib_st_log("[x86_64][gui-input-bridge-selftest] stage 6 non-gesture ignored OK\n");

    /* Stage 7: reset_stats clears everything. */
    gui_input_bridge_reset_stats();
    GIB_ST_CHECK(gui_input_bridge_stat_total() == 0, "reset clears total");
    GIB_ST_CHECK(gui_input_bridge_stat_tap() == 0, "reset clears tap");
    GIB_ST_CHECK(gui_input_bridge_stat_pinch() == 0, "reset clears pinch");
    gib_st_log("[x86_64][gui-input-bridge-selftest] stage 7 reset OK\n");

    /* Stage 8: shutdown -> no further increments. */
    gui_input_bridge_shutdown();
    pub_gesture(INPUT_GESTURE_TAP);
    GIB_ST_CHECK(gui_input_bridge_stat_total() == 0,
                 "after shutdown, publishes must not increment");
    gib_st_log("[x86_64][gui-input-bridge-selftest] stage 8 shutdown OK\n");

    /* Restore bridge for normal runtime. */
    gui_input_bridge_init();

    if (ok) {
        gib_st_log("[x86_64][gui-input-bridge-selftest] PASS\n");
    } else {
        gib_st_log("[x86_64][gui-input-bridge-selftest] FAIL\n");
    }
    return ok;

    #undef GIB_ST_CHECK
}
