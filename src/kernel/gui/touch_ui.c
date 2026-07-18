/* ============================================================================
 * touch_ui.c — 触屏 UI 消费桥（M8-D.2/.3/.4/.5）
 *
 * 将 gesture 模块产出的事件路由到具体 GUI 动作：
 *   - SWIPE_UP    → 底边上滑：OSK toggle
 *   - SWIPE_DOWN  → 顶边下滑：通知中心（打桩）
 *   - SWIPE_LEFT  → 右边滑入：快速面板（打桩）
 *   - SWIPE_RIGHT → 左边滑入：返回/关闭前台（打桩）
 *   - TAP         → 先给 OSK 消费；未消费则交给 GUI（保持鼠标点击行为）
 *
 * 边缘判定：由 gesture 模块基于 edge_flags 提供 SWIPE_* 时才触发系统级动作，
 * 避免误触屏幕中央的自然滑动。
 * ============================================================================ */
#include "touch_ui.h"
#include "osk.h"
#include "notif_center.h"
#include "gesture.h"
#include "gui.h"

/* -------- 内部状态 -------- */
static touch_ui_stats_t g_stats;
static int g_touch_ui_ready = 0;

/* -------- 单点公共接口 -------- */

static void touch_ui_listener_(const gesture_event_t *ev, void *ctx) {
    (void)ctx;
    touch_ui_on_gesture(ev);
}

void touch_ui_init(int screen_w, int screen_h) {
    /* 清零统计 */
    for (uint32_t i = 0; i < sizeof(g_stats); i++) ((uint8_t *)&g_stats)[i] = 0;
    osk_init(screen_w, screen_h);
    nc_init(screen_w, screen_h);
    gesture_init(screen_w, screen_h);
    gesture_set_listener(touch_ui_listener_, 0);
    g_touch_ui_ready = 1;
}

const touch_ui_stats_t *touch_ui_get_stats(void) { return &g_stats; }

void touch_ui_reset_stats(void) {
    for (uint32_t i = 0; i < sizeof(g_stats); i++) ((uint8_t *)&g_stats)[i] = 0;
}

/* -------- 事件路由 -------- */

void touch_ui_on_gesture(const gesture_event_t *ev) {
    if (!ev) return;
    g_stats.total_events++;

    switch (ev->type) {
    case GESTURE_TYPE_TAP: {
        /* Tap 优先级：OSK > 通知中心/快速面板 > 透传 */
        g_stats.taps_forwarded++;
        int consumed = osk_handle_tap(ev->x, ev->y);
        if (consumed) {
            g_stats.taps_consumed_by_osk++;
            break;
        }
        /* 通知中心 / 快速面板命中处理 */
        consumed = nc_handle_tap(ev->x, ev->y);
        if (consumed) {
            g_stats.taps_consumed_by_nc++;
        }
        break;
    }
    case GESTURE_TYPE_SWIPE_UP:
        /* 底边上滑 → 切换 OSK */
        osk_toggle();
        g_stats.osk_toggle++;
        g_stats.last_action = TOUCH_UI_ACT_TOGGLE_OSK;
        break;
    case GESTURE_TYPE_SWIPE_DOWN:
        /* 顶边下滑 → 通知中心 */
        nc_notif_toggle();
        g_stats.notif_center++;
        g_stats.last_action = TOUCH_UI_ACT_NOTIF_CENTER;
        break;
    case GESTURE_TYPE_SWIPE_LEFT:
        /* 右边滑入向左 → 快速面板 */
        nc_quick_toggle();
        g_stats.quick_panel++;
        g_stats.last_action = TOUCH_UI_ACT_QUICK_PANEL;
        break;
    case GESTURE_TYPE_SWIPE_RIGHT:
        /* 左边滑入向右 → 返回/关闭前台窗口（打桩） */
        g_stats.back++;
        g_stats.last_action = TOUCH_UI_ACT_BACK;
        break;
    default:
        /* LONG_PRESS / DRAG_* 目前不消费，由 mouse 层承担 */
        break;
    }
}

/* selftest 实现位于 src/arch/x86_64/kernel/touch_ui_selftest64.c */
