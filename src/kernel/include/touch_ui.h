/* ============================================================================
 * touch_ui.h — 触屏 UI 消费桥
 *
 * 定位：
 *   把 gesture 模块产出的抽象手势事件，路由到具体 GUI 动作（OSK 弹出、
 *   桌面切换、任务栏、通知中心…）。
 *
 * 设计要点：
 *  1. 无绘制职责：仅做"事件→动作"翻译，实际绘制在 OSK / GUI 主渲染回调中。
 *  2. 单入口 touch_ui_on_gesture()：selftest 直接调用即可断言路由结果。
 *  3. 通过 stat_* 计数器暴露动作次数，供 selftest 无副作用地校验。
 *  4. 通知中心/应用切换器仅打桩 (stat 累加 + klog 打印)，M8-F 再补 UI。
 * ============================================================================ */
#ifndef OPENOS_TOUCH_UI_H
#define OPENOS_TOUCH_UI_H

#include <stdint.h>

/* 触屏边缘动作枚举（与 gesture 事件解耦，方便扩展键位映射） */
typedef enum touch_ui_action {
    TOUCH_UI_ACT_NONE = 0,
    TOUCH_UI_ACT_TOGGLE_OSK,      /* 底边上滑：呼出/收起 OSK */
    TOUCH_UI_ACT_APP_SWITCHER,    /* 底边中滑（保留，M8-F 实现 UI） */
    TOUCH_UI_ACT_NOTIF_CENTER,    /* 顶边下滑：通知中心（M8-F 实现 UI） */
    TOUCH_UI_ACT_QUICK_PANEL,     /* 右边滑入：快速面板（M8-F 实现 UI） */
    TOUCH_UI_ACT_BACK,            /* 左边滑入：返回/关闭前台窗口 */
} touch_ui_action_t;

/* 统计计数器：selftest 用于断言事件路由是否正确 */
typedef struct touch_ui_stats {
    uint32_t osk_toggle;
    uint32_t app_switcher;
    uint32_t notif_center;
    uint32_t quick_panel;
    uint32_t back;
    uint32_t taps_forwarded;      /* TAP 事件透传给 OSK/桌面 */
    uint32_t taps_consumed_by_osk;
    uint32_t taps_consumed_by_nc; /* M8-F: 通知中心/快速面板消费 */
    uint32_t total_events;
    touch_ui_action_t last_action;
} touch_ui_stats_t;

/* 初始化：绑定 gesture listener，注册虚拟输入设备。screen_w/h 用于 OSK 布局。 */
void touch_ui_init(int screen_w, int screen_h);

/* 主动接收 gesture 事件（gesture 模块 listener 回调路径）。
 * 也可被 selftest 直接调用。 */
struct gesture_event_s; /* forward decl unused; use gesture_event_t via include */
#include "gesture.h"
void touch_ui_on_gesture(const gesture_event_t *ev);

/* 只读统计（selftest / dmesg 观测） */
const touch_ui_stats_t *touch_ui_get_stats(void);

/* 强制复位统计（selftest 各阶段隔离用） */
void touch_ui_reset_stats(void);

/* selftest 入口：返回 0 = PASS */
int  touch_ui_selftest(void);

#endif /* OPENOS_TOUCH_UI_H */
