/* ============================================================================
 * app_switcher_ui.h — M10.6 应用切换器 overlay UI（纯逻辑）
 *
 * 手机式"最近应用"卡片瀑布：从 app_stack LRU 快照生成横向排列的卡片列表。
 * 本模块只做几何布局 + 命中测试 + 状态管理，真渲染由 gui 层查询坐标后绘制。
 *
 * 密度对接 gui_metrics：desktop 用小卡，touch 用大卡。
 *
 * 优先级：OSK > NC > Switcher > Desktop（switcher 打开时若 NC 也在，则 NC 优先）。
 * ============================================================================ */
#ifndef OPENOS_APP_SWITCHER_UI_H
#define OPENOS_APP_SWITCHER_UI_H

#include <stdint.h>
#include "app_stack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SW_UI_MAX_CARDS APP_STACK_MAX_DEPTH  /* 与 app_stack 对齐 = 8 */

/* 卡片几何 */
typedef struct app_sw_card_s {
    int      valid;           /* 该槽是否有效卡片（若 <count 一般为 1） */
    int      x, y, w, h;      /* 屏幕坐标 */
    int32_t  stack_index;     /* 对应 app_stack 的 index（0..depth-1） */
    char     name[APP_NAME_MAX];
    uint32_t launched_seq;    /* 用于排序；调用方一般不用 */
} app_sw_card_t;

typedef struct app_sw_ui_stats_s {
    uint32_t opens;
    uint32_t closes;
    uint32_t taps_hit;       /* 命中卡片的 tap 次数 */
    uint32_t taps_miss;      /* 面板内空白 tap → 关闭 */
    uint32_t layouts;        /* layout() 次数 */
    uint32_t last_card_count;
} app_sw_ui_stats_t;

/* ============================================================================
 * 初始化
 *   w/h: 屏幕像素；也可再次 init 以适配旋转。幂等。
 * ============================================================================ */
void app_sw_ui_init(int screen_w, int screen_h);

/* 打开 / 关闭 / 查询 */
void app_sw_ui_open(void);
void app_sw_ui_close(void);
int  app_sw_ui_is_open(void);

/* 主动重排：从 app_stack_lru_snapshot 拉数并计算卡片矩形。
 * 打开时 open() 已内部调用一次；应用栈变化后可手动调用。 */
void app_sw_ui_layout(void);

/* 卡片查询 */
int  app_sw_ui_card_count(void);
const app_sw_card_t *app_sw_ui_get_card(int i);

/* 命中测试：
 *   返回值：
 *     >=0  : 命中卡片对应的 app_stack index（调用方随后可调 app_switcher_select）
 *     -1   : 未命中卡片但在面板内（点击空白→关闭 switcher）
 *     -2   : 未在 switcher 面板 bounds 内（未消费，交给下层）
 *
 * 命中卡片或空白后，本模块会自动关闭 switcher。
 * ============================================================================ */
int  app_sw_ui_handle_tap(int x, int y);

/* 面板几何（供 gui 层作背景遮罩用）*/
void app_sw_ui_get_panel_bounds(int *out_x, int *out_y, int *out_w, int *out_h);

/* 统计 */
const app_sw_ui_stats_t *app_sw_ui_get_stats(void);
void app_sw_ui_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_APP_SWITCHER_UI_H */
