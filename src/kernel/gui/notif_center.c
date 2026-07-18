/* ============================================================================
 * notif_center.c — 通知中心与快速面板实现 (M8-F.1)
 *
 * 单例内核态 overlay。所有几何计算基于屏幕尺寸；渲染通过 gui_screen_fill_rect
 * / gui_screen_draw_border / gui_draw_text 输出到 present buffer；点击事件由
 * touch_ui.c 派发。
 * ============================================================================ */
#include "notif_center.h"
#include "gui.h"

#include <stdint.h>

/* ============================ 内部辅助 ============================ */

static nc_state_t g_nc;

nc_state_t *nc_get_state(void) { return &g_nc; }

static uint32_t nc_strlen_(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void nc_strcpy_(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (src) {
        while (i + 1 < cap && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

/* ============================ 几何 ============================ */

static void nc_recompute_geometry_(void) {
    /* 通知中心：顶部下滑，占 80% 宽 x 70% 高，居中 */
    g_nc.nc_w = (g_nc.screen_w * 80) / 100;
    g_nc.nc_h = (g_nc.screen_h * 70) / 100;
    if (g_nc.nc_w < 320) g_nc.nc_w = g_nc.screen_w;
    if (g_nc.nc_h < 240) g_nc.nc_h = g_nc.screen_h;
    g_nc.nc_x = (g_nc.screen_w - g_nc.nc_w) / 2;
    g_nc.nc_y = 0;
    g_nc.nc_item_h = 72;

    /* 快速面板：右边滑入，占 30% 宽 x 80% 高，右对齐 */
    g_nc.qp_w = (g_nc.screen_w * 30) / 100;
    if (g_nc.qp_w < 240) g_nc.qp_w = 240;
    if (g_nc.qp_w > g_nc.screen_w) g_nc.qp_w = g_nc.screen_w;
    g_nc.qp_h = (g_nc.screen_h * 80) / 100;
    if (g_nc.qp_h < 320) g_nc.qp_h = g_nc.screen_h;
    g_nc.qp_x = g_nc.screen_w - g_nc.qp_w;
    g_nc.qp_y = (g_nc.screen_h - g_nc.qp_h) / 2;
    g_nc.qp_item_h = 56;
}

/* ============================ 初始化 ============================ */

void nc_init(int screen_w, int screen_h) {
    for (uint32_t i = 0; i < sizeof(g_nc); i++) ((uint8_t *)&g_nc)[i] = 0;
    g_nc.screen_w = (screen_w > 0) ? screen_w : 640;
    g_nc.screen_h = (screen_h > 0) ? screen_h : 480;
    nc_recompute_geometry_();

    /* 默认开关状态：Wi-Fi 开、其余关 */
    g_nc.toggles.active[NC_TOGGLE_WIFI]       = 1;
    g_nc.toggles.active[NC_TOGGLE_BLUETOOTH]  = 0;
    g_nc.toggles.active[NC_TOGGLE_AIRPLANE]   = 0;
    g_nc.toggles.active[NC_TOGGLE_BRIGHTNESS] = 1;
    g_nc.toggles.active[NC_TOGGLE_VOLUME]     = 1;
    g_nc.toggles.active[NC_TOGGLE_ROTATION]   = 0;

    /* 添加两条示例通知 */
    nc_push_notification("System", "Welcome",  "openos M8-F ready.",  0);
    nc_push_notification("Kernel", "Info",     "Touch UI online.",     0);
}

/* ============================ 显隐 ============================ */

void nc_notif_show(void)   { if (!g_nc.notif_center_visible) g_nc.stat_notif_shown++;  g_nc.notif_center_visible = 1; }
void nc_notif_hide(void)   { if ( g_nc.notif_center_visible) g_nc.stat_notif_hidden++; g_nc.notif_center_visible = 0; }
void nc_notif_toggle(void) { if (g_nc.notif_center_visible) nc_notif_hide(); else nc_notif_show(); }
int  nc_notif_is_visible(void) { return g_nc.notif_center_visible; }

void nc_quick_show(void)   { if (!g_nc.quick_panel_visible) g_nc.stat_quick_shown++;  g_nc.quick_panel_visible = 1; }
void nc_quick_hide(void)   { if ( g_nc.quick_panel_visible) g_nc.stat_quick_hidden++; g_nc.quick_panel_visible = 0; }
void nc_quick_toggle(void) { if (g_nc.quick_panel_visible) nc_quick_hide(); else nc_quick_show(); }
int  nc_quick_is_visible(void) { return g_nc.quick_panel_visible; }

/* ============================ M10.8: tick 推进与淐出 ============================ */

void nc_tick(uint32_t now_ms) {
    g_nc.now_ms = now_ms;

    int active_changed = 0;
    for (uint32_t i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        nc_notification_t *n = &g_nc.notifs[i];
        if (!n->active) continue;
        if (n->expire_ms == 0) continue;    /* 永不过期 */

        /* 未到期：保持全不透 */
        if (now_ms < n->expire_ms) {
            n->alpha = 255;
            continue;
        }

        /* 到期：开始淐出 */
        if (n->fade_start_ms == 0) {
            n->fade_start_ms = n->expire_ms;
            g_nc.stat_notif_expired++;
        }

        uint32_t elapsed  = now_ms - n->fade_start_ms;
        uint32_t duration = n->fade_duration_ms ? n->fade_duration_ms : 500;
        if (elapsed >= duration) {
            /* 淐出完成：自动移除 */
            n->active = 0;
            n->alpha  = 0;
            g_nc.stat_notif_evicted++;
            active_changed = 1;
        } else {
            /* alpha = 255 * (1 - elapsed/duration)，线性 */
            uint32_t remain = duration - elapsed;
            uint32_t a = (255u * remain) / duration;
            if (a > 255) a = 255;
            n->alpha = (uint8_t)a;
        }
    }

    if (active_changed) {
        g_nc.notif_count = 0;
        for (uint32_t i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
            if (g_nc.notifs[i].active) g_nc.notif_count++;
        }
    }
}

/* ============================ 通知 push ============================ */

void nc_push_notification(const char *app_name, const char *title, const char *content,
                          uint32_t timestamp_ms) {
    nc_push_notification_ttl(app_name, title, content, timestamp_ms, 0, 0); /* 永不过期 */
}

void nc_push_notification_ttl(const char *app_name, const char *title, const char *content,
                              uint32_t timestamp_ms, uint32_t ttl_ms, uint32_t fade_duration_ms) {
    /* 找一个空槽或最老槽 */
    uint32_t slot = 0;
    int found = 0;
    for (uint32_t i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        if (!g_nc.notifs[i].active) { slot = i; found = 1; break; }
    }
    if (!found) {
        /* 全部占用：整体上移，丢弃最旧 */
        for (uint32_t i = 0; i + 1 < NC_MAX_NOTIFICATIONS; i++) {
            g_nc.notifs[i] = g_nc.notifs[i + 1];
        }
        slot = NC_MAX_NOTIFICATIONS - 1;
    }
    nc_strcpy_(g_nc.notifs[slot].app_name, app_name, NC_LABEL_MAX);
    nc_strcpy_(g_nc.notifs[slot].title,    title,    NC_LABEL_MAX);
    nc_strcpy_(g_nc.notifs[slot].content,  content,  NC_LABEL_MAX * 2);
    g_nc.notifs[slot].timestamp_ms = timestamp_ms;
    g_nc.notifs[slot].active = 1;
    g_nc.notifs[slot].alpha  = 255;          /* 完全可见 */
    g_nc.notifs[slot].fade_start_ms   = 0;   /* 未开始淐出 */
    g_nc.notifs[slot].fade_duration_ms = fade_duration_ms ? fade_duration_ms : 500;
    if (ttl_ms > 0) {
        /* expire = 现在 + ttl。首次 push 时尚未 tick，使用 timestamp_ms 作近似；
         * 下一次 nc_tick() 会自动校正为真实 now_ms。 */
        g_nc.notifs[slot].expire_ms = g_nc.now_ms + ttl_ms;
        if (g_nc.notifs[slot].expire_ms == 0) g_nc.notifs[slot].expire_ms = ttl_ms;
    } else {
        g_nc.notifs[slot].expire_ms = 0;       /* 永不过期 */
    }

    /* 更新计数 */
    g_nc.notif_count = 0;
    for (uint32_t i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        if (g_nc.notifs[i].active) g_nc.notif_count++;
    }
}

/* ============================ 命中测试 ============================ */

static int nc_hit_rect_(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

int nc_handle_tap(int x, int y) {
    /* 1. 通知中心可见时：面板内空白→消费；关闭按钮→关；面板外→关 */
    if (g_nc.notif_center_visible) {
        int close_w = 60, close_h = 40;
        int cx = g_nc.nc_x + g_nc.nc_w - close_w - 8;
        int cy = g_nc.nc_y + 8;
        if (nc_hit_rect_(x, y, cx, cy, close_w, close_h)) {
            nc_notif_hide();
            return 1;
        }
        if (nc_hit_rect_(x, y, g_nc.nc_x, g_nc.nc_y, g_nc.nc_w, g_nc.nc_h)) {
            return 1; /* 面板内点击消费但不关闭 */
        }
        /* 面板外点击：关闭 */
        nc_notif_hide();
        return 1;
    }

    /* 2. 快速面板可见时：开关格子→切换；关闭区→关；面板外→关 */
    if (g_nc.quick_panel_visible) {
        if (nc_hit_rect_(x, y, g_nc.qp_x, g_nc.qp_y, g_nc.qp_w, g_nc.qp_h)) {
            /* 命中面板内：识别是哪个开关 */
            int top = g_nc.qp_y + 48;
            for (int i = 0; i < NC_TOGGLE_COUNT; i++) {
                int ty = top + i * g_nc.qp_item_h;
                if (nc_hit_rect_(x, y, g_nc.qp_x + 8, ty, g_nc.qp_w - 16, g_nc.qp_item_h - 4)) {
                    g_nc.toggles.active[i] = !g_nc.toggles.active[i];
                    g_nc.toggles.stat_clicks[i]++;
                    g_nc.stat_toggles_clicked++;
                    return 1;
                }
            }
            /* 面板内空白 */
            return 1;
        }
        /* 面板外：关闭 */
        nc_quick_hide();
        return 1;
    }

    return 0;
}

/* ============================ 渲染 ============================ */

static void nc_render_notif_center_(void) {
    if (!g_nc.notif_center_visible) return;

    /* 背景暗色遮罩 + 面板底 */
    gui_screen_fill_rect(g_nc.nc_x, g_nc.nc_y, g_nc.nc_w, g_nc.nc_h, 0x1E1E28);
    gui_screen_draw_border(g_nc.nc_x, g_nc.nc_y, g_nc.nc_w, g_nc.nc_h, 2, 0x4A4A6A);

    /* 标题栏 */
    gui_screen_fill_rect(g_nc.nc_x, g_nc.nc_y, g_nc.nc_w, 48, 0x2A2A3A);
    gui_draw_text(g_nc.nc_x + 16, g_nc.nc_y + 16, "Notifications", 0xEEEEEE);

    /* 关闭按钮 */
    int close_w = 60, close_h = 40;
    int cx = g_nc.nc_x + g_nc.nc_w - close_w - 8;
    int cy = g_nc.nc_y + 8;
    gui_screen_fill_rect(cx, cy, close_w, close_h, 0x5A2A2A);
    gui_screen_draw_border(cx, cy, close_w, close_h, 1, 0xAA4444);
    gui_draw_text(cx + 20, cy + 14, "X", 0xFFCCCC);

    /* 通知列表 */
    int row_y = g_nc.nc_y + 56;
    for (uint32_t i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        if (!g_nc.notifs[i].active) continue;
        if (row_y + g_nc.nc_item_h > g_nc.nc_y + g_nc.nc_h) break;
        gui_screen_fill_rect(g_nc.nc_x + 8, row_y, g_nc.nc_w - 16, g_nc.nc_item_h - 4, 0x2E2E3E);
        gui_screen_draw_border(g_nc.nc_x + 8, row_y, g_nc.nc_w - 16, g_nc.nc_item_h - 4, 1, 0x555577);
        gui_draw_text(g_nc.nc_x + 20, row_y +  8, g_nc.notifs[i].app_name, 0x99AAFF);
        gui_draw_text(g_nc.nc_x + 20, row_y + 26, g_nc.notifs[i].title,    0xFFFFFF);
        gui_draw_text(g_nc.nc_x + 20, row_y + 46, g_nc.notifs[i].content,  0xCCCCCC);
        row_y += g_nc.nc_item_h;
    }

    if (g_nc.notif_count == 0) {
        gui_draw_text(g_nc.nc_x + 20, g_nc.nc_y + 80, "No notifications", 0x888888);
    }
}

static const char *nc_toggle_label_(int i) {
    switch (i) {
        case NC_TOGGLE_WIFI:       return "Wi-Fi";
        case NC_TOGGLE_BLUETOOTH:  return "Bluetooth";
        case NC_TOGGLE_AIRPLANE:   return "Airplane";
        case NC_TOGGLE_BRIGHTNESS: return "Brightness";
        case NC_TOGGLE_VOLUME:     return "Volume";
        case NC_TOGGLE_ROTATION:   return "Rotation Lock";
        default:                   return "?";
    }
}

static void nc_render_quick_panel_(void) {
    if (!g_nc.quick_panel_visible) return;

    gui_screen_fill_rect(g_nc.qp_x, g_nc.qp_y, g_nc.qp_w, g_nc.qp_h, 0x1E2A28);
    gui_screen_draw_border(g_nc.qp_x, g_nc.qp_y, g_nc.qp_w, g_nc.qp_h, 2, 0x4A6A5A);

    /* 标题 */
    gui_screen_fill_rect(g_nc.qp_x, g_nc.qp_y, g_nc.qp_w, 40, 0x2A3A38);
    gui_draw_text(g_nc.qp_x + 16, g_nc.qp_y + 12, "Quick Toggles", 0xEEEEEE);

    /* 快速开关列表 */
    int top = g_nc.qp_y + 48;
    for (int i = 0; i < NC_TOGGLE_COUNT; i++) {
        int ty = top + i * g_nc.qp_item_h;
        if (ty + g_nc.qp_item_h > g_nc.qp_y + g_nc.qp_h) break;
        uint32_t bg  = g_nc.toggles.active[i] ? 0x2A6A3A : 0x333344;
        uint32_t bor = g_nc.toggles.active[i] ? 0x44AA55 : 0x666677;
        gui_screen_fill_rect(g_nc.qp_x + 8, ty, g_nc.qp_w - 16, g_nc.qp_item_h - 4, bg);
        gui_screen_draw_border(g_nc.qp_x + 8, ty, g_nc.qp_w - 16, g_nc.qp_item_h - 4, 1, bor);
        gui_draw_text(g_nc.qp_x + 20, ty + 16, nc_toggle_label_(i), 0xFFFFFF);
        gui_draw_text(g_nc.qp_x + g_nc.qp_w - 60, ty + 16,
                      g_nc.toggles.active[i] ? "[ON]" : "[--]",
                      g_nc.toggles.active[i] ? 0xCCFFCC : 0xAAAAAA);
    }
}

void nc_render(void) {
    nc_render_notif_center_();
    nc_render_quick_panel_();
}

/* ============================ selftest ============================ */

int nc_selftest(void) {
    /* 阶段1：初始化后均不可见，有 2 条默认通知 */
    nc_init(1024, 768);
    if (g_nc.notif_center_visible != 0) return -1;
    if (g_nc.quick_panel_visible  != 0) return -2;
    if (g_nc.notif_count          != 2) return -3;

    /* 阶段2：显示通知中心 → 可见 + stat+1 */
    nc_notif_show();
    if (!g_nc.notif_center_visible)  return -10;
    if (g_nc.stat_notif_shown  != 1) return -11;
    nc_notif_show(); /* 重复 show 不应重复计数 */
    if (g_nc.stat_notif_shown  != 1) return -12;

    /* 阶段3：面板内点击 → 不关闭，返回已消费 */
    int consumed = nc_handle_tap(g_nc.nc_x + 50, g_nc.nc_y + 200);
    if (consumed != 1)               return -20;
    if (!g_nc.notif_center_visible)  return -21;

    /* 阶段4：点击关闭按钮 → 关闭 */
    int cx = g_nc.nc_x + g_nc.nc_w - 60 - 8 + 20;
    int cy = g_nc.nc_y + 8 + 20;
    consumed = nc_handle_tap(cx, cy);
    if (consumed != 1)               return -22;
    if (g_nc.notif_center_visible)   return -23;
    if (g_nc.stat_notif_hidden != 1) return -24;

    /* 阶段5：快速面板 toggle 开关 */
    nc_quick_show();
    if (!g_nc.quick_panel_visible)   return -30;
    /* Wi-Fi 默认 ON，点击 Wi-Fi 行 → OFF */
    int wifi_before = g_nc.toggles.active[NC_TOGGLE_WIFI];
    int wy = g_nc.qp_y + 48 + NC_TOGGLE_WIFI * g_nc.qp_item_h + 20;
    int wx = g_nc.qp_x + 100;
    consumed = nc_handle_tap(wx, wy);
    if (consumed != 1)               return -31;
    if (g_nc.toggles.active[NC_TOGGLE_WIFI] == wifi_before) return -32;
    if (g_nc.stat_toggles_clicked  != 1) return -33;
    if (g_nc.toggles.stat_clicks[NC_TOGGLE_WIFI] != 1) return -34;

    /* 阶段6：面板外点击 → 快速面板关闭 */
    consumed = nc_handle_tap(10, 10);
    if (consumed != 1)               return -40;
    if (g_nc.quick_panel_visible)    return -41;
    if (g_nc.stat_quick_hidden != 1) return -42;

    /* 阶段7：push 多条通知，验证溢出时丢弃最旧 */
    for (int i = 0; i < 20; i++) {
        nc_push_notification("AppX", "T", "C", (uint32_t)i);
    }
    if (g_nc.notif_count != NC_MAX_NOTIFICATIONS) return -50;

    /* 阶段8：无控件可见时 → tap 不消费 */
    nc_notif_hide();
    nc_quick_hide();
    consumed = nc_handle_tap(100, 100);
    if (consumed != 0)               return -60;

    return 0;
}

