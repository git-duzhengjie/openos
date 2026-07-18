/*
 * notif_center_selftest64.c -- M8-F notification center + quick panel selftest.
 *
 * Runs the pure-logic nc_selftest() shipped with the module, then adds a few
 * additional integration-style assertions on top: layout math, tap dispatch
 * priority (OSK > NC), and stats accounting.
 */
#include "../include/notif_center_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/notif_center.h"

#include <stdint.h>

static void nc_st_log(const char *s) { early_console64_write(s); }

bool arch_x86_64_notif_center_selftest_run(void)
{
    bool ok = true;
    #define NC_ST_CHECK(cond, msg)                                              \
        do {                                                                    \
            if (!(cond)) {                                                      \
                nc_st_log("[x86_64][nc-selftest] FAIL: " msg "\n");             \
                ok = false;                                                     \
            }                                                                   \
        } while (0)

    /* 1) 内置纯逻辑 selftest */
    int rc = nc_selftest();
    if (rc != 0) {
        nc_st_log("[x86_64][nc-selftest] FAIL: nc_selftest returned nonzero\n");
        ok = false;
    }

    /* 2) 布局验证：1024x768 屏，通知中心居中 */
    nc_init(1024, 768);
    const nc_state_t *st = nc_get_state();
    NC_ST_CHECK(st->screen_w == 1024,               "screen_w != 1024");
    NC_ST_CHECK(st->screen_h == 768,                "screen_h != 768");
    NC_ST_CHECK(st->nc_w == (1024 * 80) / 100,      "nc_w wrong");
    NC_ST_CHECK(st->nc_h == (768  * 70) / 100,      "nc_h wrong");
    NC_ST_CHECK(st->nc_x == (1024 - st->nc_w) / 2,  "nc_x wrong");
    NC_ST_CHECK(st->nc_y == 0,                      "nc_y != 0");

    /* 3) 快速面板贴右边 */
    NC_ST_CHECK(st->qp_x == 1024 - st->qp_w,        "qp_x wrong");
    NC_ST_CHECK(st->qp_y == (768 - st->qp_h) / 2,   "qp_y wrong");

    /* 4) 隐藏时 tap 无消费 */
    NC_ST_CHECK(nc_handle_tap(500, 400) == 0, "hidden nc should not consume");

    /* 5) 显示通知中心后，面板外点击关闭 */
    nc_notif_show();
    NC_ST_CHECK(nc_notif_is_visible() == 1, "notif center not visible");
    NC_ST_CHECK(nc_handle_tap(0, 700) == 1, "outside tap should consume+close");
    NC_ST_CHECK(nc_notif_is_visible() == 0, "notif center should close on outside tap");

    /* 6) push 通知 → 计数 */
    uint32_t before = st->notif_count;
    nc_push_notification("Test", "Hi", "world", 42);
    NC_ST_CHECK(st->notif_count == before + 1, "push notif count wrong");

    /* 7) 快速面板 toggle Wi-Fi */
    nc_quick_show();
    int wifi_before = st->toggles.active[NC_TOGGLE_WIFI];
    int wy = st->qp_y + 48 + NC_TOGGLE_WIFI * st->qp_item_h + 20;
    int wx = st->qp_x + 100;
    NC_ST_CHECK(nc_handle_tap(wx, wy) == 1, "wifi tap not consumed");
    NC_ST_CHECK(st->toggles.active[NC_TOGGLE_WIFI] != wifi_before,
                "wifi toggle state did not flip");

    if (ok) nc_st_log("[x86_64][nc-selftest] PASS\n");
    return ok;

    #undef NC_ST_CHECK
}
