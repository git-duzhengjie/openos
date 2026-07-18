/* ============================================================================
 * notif_center.h — 通知中心与快速面板 (M8-F)
 *
 * 设计要点：
 *  1. 纯计算模块：布局/命中测试/状态机零依赖，可 selftest
 *  2. 单例内核态实例：顶部通知中心 + 右侧快速面板，可独立显隐
 *  3. 上层接口极简：show/hide/toggle/is_visible/on_touch/render
 *  4. 点击事件：先命中测试，消费返回 1，未消费返回 0 透传给 GUI
 *  5. selftest：模拟触点 → 断言显隐切换 → PASS 打印
 * ============================================================================ */
#ifndef OPENOS_NOTIF_CENTER_H
#define OPENOS_NOTIF_CENTER_H

#include <stdint.h>

#define NC_MAX_NOTIFICATIONS  8
#define NC_MAX_QUICK_TOGGLES  6
#define NC_LABEL_MAX         24

/* 快速开关类型 */
typedef enum nc_toggle_type {
    NC_TOGGLE_WIFI = 0,
    NC_TOGGLE_BLUETOOTH,
    NC_TOGGLE_AIRPLANE,
    NC_TOGGLE_BRIGHTNESS,
    NC_TOGGLE_VOLUME,
    NC_TOGGLE_ROTATION,
    NC_TOGGLE_COUNT
} nc_toggle_type_t;

/* 单个通知 */
typedef struct nc_notification {
    char     app_name[NC_LABEL_MAX];
    char     title[NC_LABEL_MAX];
    char     content[NC_LABEL_MAX * 2];
    uint32_t timestamp_ms;
    int      active;
    /* M10.8: 超时淐出字段 */
    uint32_t expire_ms;      /* 0 = 永不过期；>0 = 到期后开始淐出的绝对时间 (ms) */
    uint32_t fade_start_ms;  /* 0 = 未开始淐出；>0 = 淐出起始时间 */
    uint32_t fade_duration_ms; /* 淐出总时长（默认 500ms） */
    uint8_t  alpha;          /* 0-255；非淐出期为 255；0 时自动移除 */
} nc_notification_t;

/* 快速开关状态 */
typedef struct nc_toggle_state {
    int      active[NC_TOGGLE_COUNT];
    uint32_t stat_clicks[NC_TOGGLE_COUNT];
} nc_toggle_state_t;

/* ===== 内部状态（selftest 需要读取） ===== */
typedef struct nc_state {
    int       notif_center_visible;
    int       quick_panel_visible;
    int       screen_w;
    int       screen_h;

    /* 通知中心：顶部下滑，占屏 70% 高度，80% 宽度居中 */
    int       nc_x;
    int       nc_y;
    int       nc_w;
    int       nc_h;
    int       nc_item_h;

    /* 快速面板：右边滑入，占屏 30% 宽度，80% 高度居中 */
    int       qp_x;
    int       qp_y;
    int       qp_w;
    int       qp_h;
    int       qp_item_h;

    /* 通知列表 */
    nc_notification_t notifs[NC_MAX_NOTIFICATIONS];
    uint32_t          notif_count;

    /* 快速开关 */
    nc_toggle_state_t toggles;

    /* 统计：selftest 校验 */
    uint32_t  stat_notif_shown;
    uint32_t  stat_notif_hidden;
    uint32_t  stat_quick_shown;
    uint32_t  stat_quick_hidden;
    uint32_t  stat_toggles_clicked;
    /* M10.8: 淐出相关统计 */
    uint32_t  stat_notif_expired;   /* 因超时开始淐出的总次数 */
    uint32_t  stat_notif_evicted;   /* 因 alpha 归 0 自动移除的总次数 */
    uint32_t  now_ms;               /* 最后一次 tick 推进的时间 */
} nc_state_t;

/* ============================================================================
 * 公共 API
 * ============================================================================ */

/* 初始化：确定屏幕尺寸、加载默认状态 */
void nc_init(int screen_w, int screen_h);

/* 通知中心 API */
void nc_notif_show(void);
void nc_notif_hide(void);
void nc_notif_toggle(void);
int  nc_notif_is_visible(void);

/* 快速面板 API */
void nc_quick_show(void);
void nc_quick_hide(void);
void nc_quick_toggle(void);
int  nc_quick_is_visible(void);

/* 发送新通知（自动滚入列表） */
void nc_push_notification(const char *app_name, const char *title, const char *content,
                          uint32_t timestamp_ms);

/* M10.8: 发送带自动淐出的通知。
 *   ttl_ms == 0  -> 永不过期
 *   ttl_ms  > 0  -> ttl_ms 后开始淐出，fade_duration_ms 后自动移除
 *                 fade_duration_ms == 0 时黙认为 500ms */
void nc_push_notification_ttl(const char *app_name, const char *title, const char *content,
                              uint32_t timestamp_ms, uint32_t ttl_ms, uint32_t fade_duration_ms);

/* M10.8: tick 推进。由 pit64 IRQ 或 selftest 人工驱动。
 *   now_ms = 当前单调递增的毫秒时间（不需要与 wall clock 对齐）
 * 高频安全：内部仅扫描至多 NC_MAX_NOTIFICATIONS 个条目，无堆/无锁 */
void nc_tick(uint32_t now_ms);

/* 点击命中：处理 TAP 事件，返回 1=已消费，0=未命中 */
int  nc_handle_tap(int x, int y);

/* 渲染：调用 gui_screen_fill_rect / gui_draw_text 绘制到 present buffer */
void nc_render(void);

/* 内部状态导出（供 selftest 访问；非 const，允许 selftest 白盒操作） */
nc_state_t *nc_get_state(void);

/* selftest 入口：返回 0 = PASS，非 0 = FAIL */
int  nc_selftest(void);

#endif /* OPENOS_NOTIF_CENTER_H */
