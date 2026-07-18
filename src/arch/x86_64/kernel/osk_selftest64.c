/*
 * osk_selftest64.c -- M8-D.1 On-Screen Keyboard selftest.
 *
 * Drives synthetic taps against the OSK layout and asserts key dispatch,
 * shift/symbol layer transitions, backspace/enter stats, and hide gating.
 * No HID hardware required; state is read via osk_get_state().
 */
#include "../include/osk_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/osk.h"

#include <stdint.h>
#include <stddef.h>

/* gui_post_key / gui_post_key_code 由 gui.c 提供；selftest 时它会尝试写入
 * 事件队列。为避免 selftest 依赖完整 GUI 主循环，允许弱链接兜底。 */

static void osk_st_log(const char *s) { early_console64_write(s); }

bool arch_x86_64_osk_selftest_run(void)
{
    bool ok = true;
    #define OSK_ST_CHECK(cond, msg)                                       \
        do {                                                              \
            if (!(cond)) {                                                \
                osk_st_log("[x86_64][osk-selftest] FAIL: " msg "\n");     \
                ok = false;                                               \
            }                                                             \
        } while (0)

    osk_init(640, 480);
    osk_show();
    const osk_state_t *s = osk_get_state();
    OSK_ST_CHECK(s->visible == 1,      "init not visible");
    OSK_ST_CHECK(s->layer == OSK_LAYER_LOWER, "init layer != LOWER");
    OSK_ST_CHECK(s->panel_h > 0,       "panel height == 0");
    OSK_ST_CHECK(s->key_w  > 0,        "key width == 0");

    /* 1) 面板外点击不消费 */
    int consumed = osk_handle_tap(10, 10);
    OSK_ST_CHECK(consumed == 0, "outside tap consumed");

    /* 2) LOWER 层点击 'q'（row 1, key 0） */
    int cx = 0, cy = 0;
    osk_center_of_(OSK_LAYER_LOWER, 1, 0, &cx, &cy);
    consumed = osk_handle_tap(cx, cy);
    OSK_ST_CHECK(consumed == 1,             "q tap not consumed");
    OSK_ST_CHECK(s->last_key_code == 'q',   "q not dispatched");
    OSK_ST_CHECK(s->stat_chars_emitted == 1,"chars_emitted != 1");

    /* 3) Shift → UPPER (row 3, key 0) */
    osk_center_of_(OSK_LAYER_LOWER, 3, 0, &cx, &cy);
    (void)osk_handle_tap(cx, cy);
    OSK_ST_CHECK(s->layer == OSK_LAYER_UPPER, "shift did not enter UPPER");

    /* 4) UPPER 层点击 'Q' 后自动回落 LOWER */
    osk_center_of_(OSK_LAYER_UPPER, 1, 0, &cx, &cy);
    (void)osk_handle_tap(cx, cy);
    OSK_ST_CHECK(s->last_key_code == 'Q',     "Q not dispatched");
    OSK_ST_CHECK(s->layer == OSK_LAYER_LOWER, "did not auto-return to LOWER");

    /* 5) ?123 → SYMBOL (row 4, key 0) */
    osk_center_of_(OSK_LAYER_LOWER, 4, 0, &cx, &cy);
    (void)osk_handle_tap(cx, cy);
    OSK_ST_CHECK(s->layer == OSK_LAYER_SYMBOL, "?123 did not enter SYMBOL");

    /* 6) SYMBOL 层点击 space（row 4, key 2） */
    uint32_t before_chars = s->stat_chars_emitted;
    osk_center_of_(OSK_LAYER_SYMBOL, 4, 2, &cx, &cy);
    (void)osk_handle_tap(cx, cy);
    OSK_ST_CHECK(s->stat_chars_emitted == before_chars + 1, "space did not emit char");
    OSK_ST_CHECK(s->last_text[0] == ' ',                    "space char != ' '");

    /* 7) SYMBOL 层点击 Enter（row 4, key 4） */
    osk_center_of_(OSK_LAYER_SYMBOL, 4, 4, &cx, &cy);
    (void)osk_handle_tap(cx, cy);
    OSK_ST_CHECK(s->stat_enters == 1, "enter count != 1");

    /* 8) SYMBOL 层点击 Backspace（row 3, key 7） */
    osk_center_of_(OSK_LAYER_SYMBOL, 3, 7, &cx, &cy);
    (void)osk_handle_tap(cx, cy);
    OSK_ST_CHECK(s->stat_backspaces == 1, "backspace count != 1");

    /* 9) hide 后所有 tap 不消费 */
    osk_hide();
    OSK_ST_CHECK(s->visible == 0, "hide did not clear visible");
    consumed = osk_handle_tap(cx, cy);
    OSK_ST_CHECK(consumed == 0, "tap consumed while hidden");

    if (ok) {
        osk_st_log("[x86_64][osk-selftest] PASS\n");
    }
    return ok;

    #undef OSK_ST_CHECK
}
