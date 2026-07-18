/* ============================================================================
 * app_switcher_ui.c — M10.6 应用切换器 overlay UI
 * ============================================================================
 * 布局策略：
 *   - 面板：屏幕正中，横向居中；宽 = min(screen_w - 2*margin, kMaxPanelW)
 *   - 卡片：横向排列，等宽等高；根据 gui_metrics 密度分档
 *   - 若卡片总宽 > 面板宽，取最右 N 个（截断栈底，保留最近使用）
 *
 * 命中链：
 *   app_sw_ui_handle_tap(x, y):
 *     · 卡片命中 → 返回 stack_index，关闭 switcher
 *     · 面板内空白 → 返回 -1，关闭 switcher
 *     · 面板外 → 返回 -2，不消费
 * ============================================================================ */
#include "app_switcher_ui.h"
#include "app_stack.h"
#include "gui_metrics.h"

#include <string.h>

/* -------- 内部状态 -------- */
static struct {
    int      inited;
    int      is_open;
    int      screen_w;
    int      screen_h;

    /* 面板矩形 */
    int      panel_x, panel_y, panel_w, panel_h;

    /* 卡片数组 */
    app_sw_card_t cards[APP_SW_UI_MAX_CARDS];
    int           card_count;

    app_sw_ui_stats_t stats;
} S;

/* -------- 布局参数（会依据密度调整） -------- */
static void get_layout_params(int *card_w, int *card_h, int *gap,
                              int *panel_max_w, int *panel_h_out) {
    /* 密度：touch 用大卡；desktop 保留较紧凑 */
    gui_density_t d = gui_metrics_get_density();
    if (d == GUI_DENSITY_TOUCH) {
        *card_w        = 180;
        *card_h        = 260;
        *gap           = 24;
        *panel_max_w   = 960;
        *panel_h_out   = 320;
    } else {
        *card_w        = 120;
        *card_h        = 170;
        *gap           = 16;
        *panel_max_w   = 720;
        *panel_h_out   = 210;
    }
}

/* -------- 公共 API -------- */

void app_sw_ui_init(int screen_w, int screen_h) {
    if (screen_w <= 0) screen_w = 800;
    if (screen_h <= 0) screen_h = 600;
    S.screen_w = screen_w;
    S.screen_h = screen_h;
    S.is_open  = 0;
    S.card_count = 0;
    for (int i = 0; i < APP_SW_UI_MAX_CARDS; ++i) {
        S.cards[i].valid = 0;
    }
    /* stats 保留累积（不清零） */
    S.inited = 1;
}

void app_sw_ui_reset_stats(void) {
    memset(&S.stats, 0, sizeof(S.stats));
}

/* 从 app_stack LRU 生成卡片并计算矩形 */
void app_sw_ui_layout(void) {
    if (!S.inited) app_sw_ui_init(0, 0);
    S.stats.layouts++;

    int card_w, card_h, gap, panel_max_w, panel_h;
    get_layout_params(&card_w, &card_h, &gap, &panel_max_w, &panel_h);

    /* 面板宽度：先按屏幕留 32 边距，最大不超过 panel_max_w */
    int margin = 32;
    int panel_w = S.screen_w - 2 * margin;
    if (panel_w > panel_max_w) panel_w = panel_max_w;
    if (panel_w < 200) panel_w = 200;
    S.panel_w = panel_w;
    S.panel_h = panel_h;
    S.panel_x = (S.screen_w - panel_w) / 2;
    S.panel_y = (S.screen_h - panel_h) / 2;

    /* 拉 LRU snapshot */
    const app_slot_t *lru[APP_SW_UI_MAX_CARDS];
    int n = app_stack_lru_snapshot(lru, APP_SW_UI_MAX_CARDS);
    if (n < 0) n = 0;
    if (n > APP_SW_UI_MAX_CARDS) n = APP_SW_UI_MAX_CARDS;

    /* 计算可放下的卡片数 */
    int usable = panel_w - 2 * gap;
    int per    = card_w + gap;
    int cap    = (usable + gap) / per;   /* 允许最后一张不带尾 gap */
    if (cap < 1) cap = 1;
    if (cap > APP_SW_UI_MAX_CARDS) cap = APP_SW_UI_MAX_CARDS;
    int shown = n < cap ? n : cap;

    /* 卡片总宽（含卡片间 gap，不含两侧内边距） */
    int total_w = shown * card_w + (shown > 0 ? (shown - 1) * gap : 0);
    int start_x = S.panel_x + (panel_w - total_w) / 2;
    int card_y  = S.panel_y + (panel_h - card_h) / 2;

    /* 填充卡片 */
    for (int i = 0; i < APP_SW_UI_MAX_CARDS; ++i) {
        S.cards[i].valid = 0;
    }
    for (int i = 0; i < shown; ++i) {
        const app_slot_t *sl = lru[i];
        if (!sl) continue;
        app_sw_card_t *c = &S.cards[i];
        c->valid       = 1;
        c->x           = start_x + i * (card_w + gap);
        c->y           = card_y;
        c->w           = card_w;
        c->h           = card_h;
        c->launched_seq = sl->launched_seq;
        /* 反查该 slot 在 app_stack 里的 index */
        c->stack_index = -1;
        int depth = app_stack_depth();
        for (int k = 0; k < depth; ++k) {
            const app_slot_t *k_slot = app_stack_at(k);
            if (k_slot && k_slot == sl) { c->stack_index = k; break; }
        }
        /* 拷 name（安全截断） */
        int j = 0;
        for (; j < APP_NAME_MAX - 1 && sl->name[j]; ++j) c->name[j] = sl->name[j];
        c->name[j] = '\0';
    }
    S.card_count = shown;
    S.stats.last_card_count = (uint32_t)shown;
}

void app_sw_ui_open(void) {
    if (!S.inited) app_sw_ui_init(0, 0);
    if (S.is_open) return;
    S.is_open = 1;
    S.stats.opens++;
    app_sw_ui_layout();
}

void app_sw_ui_close(void) {
    if (!S.is_open) return;
    S.is_open = 0;
    S.stats.closes++;
}

int app_sw_ui_is_open(void) { return S.is_open; }

int app_sw_ui_card_count(void) { return S.card_count; }

const app_sw_card_t *app_sw_ui_get_card(int i) {
    if (i < 0 || i >= APP_SW_UI_MAX_CARDS) return 0;
    if (!S.cards[i].valid) return 0;
    return &S.cards[i];
}

void app_sw_ui_get_panel_bounds(int *out_x, int *out_y, int *out_w, int *out_h) {
    if (out_x) *out_x = S.panel_x;
    if (out_y) *out_y = S.panel_y;
    if (out_w) *out_w = S.panel_w;
    if (out_h) *out_h = S.panel_h;
}

int app_sw_ui_handle_tap(int x, int y) {
    if (!S.is_open) return -2;
    /* panel 边界 */
    int px = S.panel_x, py = S.panel_y, pw = S.panel_w, ph = S.panel_h;
    if (x < px || x >= px + pw || y < py || y >= py + ph) {
        /* 面板外，未消费 —— 上层调用方通常应先关闭 switcher（同 NC 行为），
         * 但为保持模块纯粹，本函数不主动关闭；由 touch_ui 层决定策略。 */
        return -2;
    }
    /* 命中卡片？ */
    for (int i = 0; i < APP_SW_UI_MAX_CARDS; ++i) {
        const app_sw_card_t *c = &S.cards[i];
        if (!c->valid) continue;
        if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) {
            S.stats.taps_hit++;
            int idx = c->stack_index;
            app_sw_ui_close();
            return idx;
        }
    }
    /* 面板内空白 → 关闭 */
    S.stats.taps_miss++;
    app_sw_ui_close();
    return -1;
}

const app_sw_ui_stats_t *app_sw_ui_get_stats(void) { return &S.stats; }
