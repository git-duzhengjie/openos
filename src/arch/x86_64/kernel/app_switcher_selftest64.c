/*
 * app_switcher_selftest64.c — M10.6 app-switcher UI + gesture3 selftest（8 阶段）
 *
 * 阶段：
 *   1) init/close 初始态
 *   2) open 后 layout 拉 LRU 卡片，count = min(depth, cap)
 *   3) 卡片矩形不重叠、都在面板内
 *   4) tap 命中卡片 → 返回 stack_index、switcher 关闭
 *   5) tap 面板内空白 → 返回 -1、关闭
 *   6) tap 面板外 → 返回 -2、不消费（本模块不关闭；由 touch_ui 层策略）
 *   7) gesture3 三指按下持续 → BEGIN，抬起且位移>阈值 → SWIPE_UP
 *   8) gesture3 位移不足 → CANCEL
 */
#include "../include/app_switcher_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/app_stack.h"
#include "../../../kernel/include/app_manifest.h"
#include "../../../kernel/include/app_switcher_ui.h"
#include "../../../kernel/include/gesture3.h"
#include "../../../kernel/include/gesture_multi.h"

#include <stdint.h>

static void sw_log(const char *s) { early_console64_write(s); }

/* 全局收集 gesture3 事件（selftest 用） */
static gesture3_type_t g_last_type;
static int             g_last_dy;
static uint32_t        g_swipes;
static void g3_cb(const gesture3_event_t *ev, void *u) {
    (void)u;
    g_last_type = ev->type;
    g_last_dy   = ev->dy;
    if (ev->type == GESTURE3_SWIPE_UP) g_swipes++;
}

/* 一帧 slots：三指 or 全离开 */
static void mk3(gesture_multi_slot_t *sl, int x0, int y0, int step) {
    for (int i = 0; i < GESTURE_MULTI_MAX_SLOTS; i++) {
        sl[i].present = 0; sl[i].tip = 0; sl[i].x = 0; sl[i].y = 0;
    }
    for (int i = 0; i < 3; i++) {
        sl[i].present = 1;
        sl[i].tip     = 1;
        sl[i].x       = x0 + i * step;
        sl[i].y       = y0;
    }
}
static void mk0(gesture_multi_slot_t *sl) {
    for (int i = 0; i < GESTURE_MULTI_MAX_SLOTS; i++) {
        sl[i].present = 0; sl[i].tip = 0; sl[i].x = 0; sl[i].y = 0;
    }
}

bool arch_x86_64_app_switcher_selftest_run(void)
{
    bool ok = true;
    #define SW_CHK(cond, msg) do { if (!(cond)) { \
        sw_log("[x86_64][app-switcher-selftest] FAIL: " msg "\n"); ok = false; } } while (0)

    const int SW = 1024, SH = 768;

    /* --- Stage 1: init/close --- */
    app_stack_init();
    app_manifest_init();
    app_sw_ui_init(SW, SH);
    app_sw_ui_reset_stats();
    SW_CHK(app_sw_ui_is_open() == 0,           "S1 not open after init");
    SW_CHK(app_sw_ui_card_count() == 0,        "S1 card_count == 0");

    /* --- Stage 2: 压 3 app，open，count 应 == 3 --- */
    app_stack_push("terminal","builtin:terminal", app_manifest_find_index("terminal"), -1);
    app_stack_push("dmesg",   "builtin:dmesg",    app_manifest_find_index("dmesg"),    -1);
    app_stack_push("hello64", "/bin/hello64.elf", app_manifest_find_index("hello64"),  -1);
    app_sw_ui_open();
    SW_CHK(app_sw_ui_is_open() == 1,           "S2 opened");
    SW_CHK(app_sw_ui_card_count() == 3,        "S2 card_count == 3");
    SW_CHK(app_sw_ui_get_stats()->opens >= 1,  "S2 opens>=1");
    SW_CHK(app_sw_ui_get_stats()->layouts >= 1,"S2 layouts>=1");

    /* --- Stage 3: 卡片 rect 都在 panel 内，且不重叠 --- */
    int px, py, pw, ph;
    app_sw_ui_get_panel_bounds(&px, &py, &pw, &ph);
    SW_CHK(pw > 0 && ph > 0,                   "S3 panel size>0");
    for (int i = 0; i < 3; i++) {
        const app_sw_card_t *c = app_sw_ui_get_card(i);
        SW_CHK(c && c->valid,                  "S3 card valid");
        if (!c) continue;
        SW_CHK(c->x >= px && c->y >= py,       "S3 card >= panel origin");
        SW_CHK(c->x + c->w <= px + pw,         "S3 card within panel right");
        SW_CHK(c->y + c->h <= py + ph,         "S3 card within panel bottom");
    }
    /* 相邻不重叠 */
    for (int i = 0; i + 1 < 3; i++) {
        const app_sw_card_t *a = app_sw_ui_get_card(i);
        const app_sw_card_t *b = app_sw_ui_get_card(i + 1);
        SW_CHK(a && b && (a->x + a->w) <= b->x, "S3 non-overlap");
    }

    /* --- Stage 4: tap 命中卡片[0]（LRU 首 = 最近 hello64） --- */
    {
        const app_sw_card_t *c0 = app_sw_ui_get_card(0);
        SW_CHK(c0 != 0,                        "S4 card0");
        int tap_x = c0->x + c0->w / 2;
        int tap_y = c0->y + c0->h / 2;
        int r = app_sw_ui_handle_tap(tap_x, tap_y);
        SW_CHK(r >= 0,                         "S4 hit >=0");
        SW_CHK(app_sw_ui_is_open() == 0,       "S4 closed after hit");
        SW_CHK(app_sw_ui_get_stats()->taps_hit >= 1, "S4 taps_hit>=1");
    }

    /* --- Stage 5: 面板内空白 tap → -1 --- */
    app_sw_ui_open();
    {
        /* 找一个"面板内 + 所有卡片外"的坐标：面板顶部一像素 */
        int panel_x, panel_y, panel_w, panel_h;
        app_sw_ui_get_panel_bounds(&panel_x, &panel_y, &panel_w, &panel_h);
        int r = app_sw_ui_handle_tap(panel_x + 1, panel_y + 1);
        SW_CHK(r == -1,                        "S5 blank == -1");
        SW_CHK(app_sw_ui_is_open() == 0,       "S5 closed on blank");
        SW_CHK(app_sw_ui_get_stats()->taps_miss >= 1, "S5 taps_miss>=1");
    }

    /* --- Stage 6: 面板外 → -2，且不关闭（因本模块不主动） --- */
    app_sw_ui_open();
    {
        int r = app_sw_ui_handle_tap(0, 0);
        SW_CHK(r == -2,                        "S6 outside == -2");
        SW_CHK(app_sw_ui_is_open() == 1,       "S6 remain open");
    }
    app_sw_ui_close();

    /* --- Stage 7: gesture3 三指上滑 → SWIPE_UP --- */
    gesture3_init(SW, SH);
    gesture3_reset_stats();
    gesture3_set_listener(g3_cb, 0);
    g_last_type = GESTURE3_NONE; g_last_dy = 0; g_swipes = 0;

    gesture_multi_slot_t slots[GESTURE_MULTI_MAX_SLOTS];
    /* 起点在下方，位移超过阈值（SH/6 ≈ 128） */
    mk3(slots, 400, 700, 40);
    gesture3_feed(slots, GESTURE_MULTI_MAX_SLOTS);
    SW_CHK(g_last_type == GESTURE3_BEGIN,      "S7 begin");
    mk3(slots, 400, 500, 40);   /* 中间位置 */
    gesture3_feed(slots, GESTURE_MULTI_MAX_SLOTS);
    mk3(slots, 400, 400, 40);   /* 更高 */
    gesture3_feed(slots, GESTURE_MULTI_MAX_SLOTS);
    /* 全部抬起 */
    mk0(slots);
    gesture3_feed(slots, GESTURE_MULTI_MAX_SLOTS);
    SW_CHK(g_last_type == GESTURE3_SWIPE_UP,   "S7 swipe_up");
    SW_CHK(g_swipes == 1,                      "S7 swipes==1");
    SW_CHK(g_last_dy < 0,                      "S7 dy<0 (upward)");
    SW_CHK(gesture3_get_stats()->swipe_ups >= 1, "S7 stats.swipe_ups>=1");

    /* --- Stage 8: 位移不足 → CANCEL --- */
    gesture3_reset();
    gesture3_reset_stats();
    g_last_type = GESTURE3_NONE; g_swipes = 0;
    mk3(slots, 400, 500, 40);
    gesture3_feed(slots, GESTURE_MULTI_MAX_SLOTS);
    SW_CHK(g_last_type == GESTURE3_BEGIN,      "S8 begin");
    mk3(slots, 400, 495, 40);   /* 只上移 5px，远不够 */
    gesture3_feed(slots, GESTURE_MULTI_MAX_SLOTS);
    mk0(slots);
    gesture3_feed(slots, GESTURE_MULTI_MAX_SLOTS);
    SW_CHK(g_last_type == GESTURE3_CANCEL,     "S8 cancel");
    SW_CHK(g_swipes == 0,                      "S8 no swipe");
    SW_CHK(gesture3_get_stats()->cancels >= 1, "S8 stats.cancels>=1");

    /* 清理 */
    gesture3_set_listener(0, 0);
    app_stack_init();
    app_sw_ui_init(SW, SH);

    if (ok) sw_log("[x86_64][app-switcher-selftest] PASS\n");
    return ok;
    #undef SW_CHK
}
