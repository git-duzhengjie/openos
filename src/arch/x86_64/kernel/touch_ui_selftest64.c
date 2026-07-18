/*
 * touch_ui_selftest64.c -- M8-D.2/.5 touch UI event router selftest.
 *
 * Verifies:
 *   - SWIPE_UP toggles OSK.
 *   - SWIPE_DOWN/LEFT/RIGHT increment the correct stat counters.
 *   - TAP is forwarded to OSK; consumed if inside panel.
 *   - hidden OSK returns unconsumed taps to the caller.
 */
#include "../include/touch_ui_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/touch_ui.h"
#include "../../../kernel/include/osk.h"
#include "../../../kernel/include/gesture.h"

#include <stdint.h>

static void tui_st_log(const char *s) { early_console64_write(s); }

static void emit_(gesture_type_t t, int x, int y) {
    gesture_event_t ev = {0};
    ev.type = t;
    ev.x = x; ev.y = y;
    ev.start_x = x; ev.start_y = y;
    touch_ui_on_gesture(&ev);
}

bool arch_x86_64_touch_ui_selftest_run(void)
{
    bool ok = true;
    #define TUI_ST_CHECK(cond, msg)                                        \
        do {                                                               \
            if (!(cond)) {                                                 \
                tui_st_log("[x86_64][touch-ui-selftest] FAIL: " msg "\n"); \
                ok = false;                                                \
            }                                                              \
        } while (0)

    touch_ui_init(640, 480);
    touch_ui_reset_stats();
    const touch_ui_stats_t *st = touch_ui_get_stats();
    const osk_state_t *osk = osk_get_state();

    /* 初始 OSK 应关闭 */
    osk_hide();
    TUI_ST_CHECK(osk->visible == 0, "osk should start hidden");

    /* 1) SWIPE_UP -> osk toggle -> visible */
    emit_(GESTURE_TYPE_SWIPE_UP, 320, 470);
    TUI_ST_CHECK(st->osk_toggle == 1, "osk_toggle != 1 after SWIPE_UP");
    TUI_ST_CHECK(osk->visible == 1,   "osk not visible after SWIPE_UP");
    TUI_ST_CHECK(st->last_action == TOUCH_UI_ACT_TOGGLE_OSK, "last_action != OSK");

    /* 2) SWIPE_UP again -> osk hidden */
    emit_(GESTURE_TYPE_SWIPE_UP, 320, 470);
    TUI_ST_CHECK(osk->visible == 0, "osk not toggled hidden");

    /* 重新弹出以便测试后续 TAP 命中面板 */
    emit_(GESTURE_TYPE_SWIPE_UP, 320, 470);
    TUI_ST_CHECK(osk->visible == 1, "osk failed to re-show");

    /* 3) SWIPE_DOWN -> notif_center */
    emit_(GESTURE_TYPE_SWIPE_DOWN, 320, 5);
    TUI_ST_CHECK(st->notif_center == 1, "notif_center != 1");
    TUI_ST_CHECK(st->last_action == TOUCH_UI_ACT_NOTIF_CENTER, "last_action != NOTIF");

    /* 4) SWIPE_LEFT -> quick_panel */
    emit_(GESTURE_TYPE_SWIPE_LEFT, 630, 240);
    TUI_ST_CHECK(st->quick_panel == 1, "quick_panel != 1");

    /* 5) SWIPE_RIGHT -> back */
    emit_(GESTURE_TYPE_SWIPE_RIGHT, 10, 240);
    TUI_ST_CHECK(st->back == 1, "back != 1");

    /* 6) TAP 命中 OSK 面板（底部区域） */
    /* OSK 面板从 y = 480 - 40% = 288 起 */
    emit_(GESTURE_TYPE_TAP, 320, 470);
    TUI_ST_CHECK(st->taps_forwarded == 1,      "taps_forwarded != 1");
    TUI_ST_CHECK(st->taps_consumed_by_osk == 1,"osk did not consume tap");

    /* 7) TAP 在面板外（顶部）→ 不被 OSK 消费 */
    emit_(GESTURE_TYPE_TAP, 100, 100);
    TUI_ST_CHECK(st->taps_forwarded == 2,       "taps_forwarded != 2");
    TUI_ST_CHECK(st->taps_consumed_by_osk == 1, "top tap should NOT be consumed");

    /* 8) 隐藏 OSK 后所有 TAP 都不消费 */
    osk_hide();
    emit_(GESTURE_TYPE_TAP, 320, 470);
    TUI_ST_CHECK(st->taps_consumed_by_osk == 1, "hidden OSK still consuming");

    TUI_ST_CHECK(st->total_events == 9, "total_events wrong");

    if (ok) tui_st_log("[x86_64][touch-ui-selftest] PASS\n");
    return ok;

    #undef TUI_ST_CHECK
}
