/* ============================================================================
 * osk.h — On-Screen Keyboard (虚拟键盘) 触屏输入模块
 *
 * 设计要点：
 *  1. 纯计算模块：布局/命中测试/状态机零依赖，可 selftest。
 *  2. 单例内核态实例：内部管理显隐、按下键、Shift/Symbol 层切换。
 *  3. 上层接口极简：show/hide/toggle/is_visible/on_touch/render。
 *  4. 输入路径：通过 input_push_key + input_push_text 注入到系统事件队列，
 *     对上层键盘消费者完全透明，与硬件键盘同源。
 *  5. selftest：模拟触点 → 断言按键映射与层切换 → PASS 打印。
 * ============================================================================ */
#ifndef OPENOS_OSK_H
#define OPENOS_OSK_H

#include <stdint.h>

#define OSK_MAX_ROWS      5
#define OSK_MAX_COLS      12
#define OSK_KEY_LABEL_MAX 8

/* 逻辑层：字母层 / 大写层 / 符号层 */
typedef enum osk_layer {
    OSK_LAYER_LOWER = 0,
    OSK_LAYER_UPPER = 1,
    OSK_LAYER_SYMBOL = 2,
    OSK_LAYER_COUNT
} osk_layer_t;

/* 特殊按键 code：负值避开正常 ASCII */
typedef enum osk_special {
    OSK_KEY_NONE      = 0,
    OSK_KEY_SHIFT     = -1,
    OSK_KEY_SYMBOL    = -2,
    OSK_KEY_BACKSPACE = -3,
    OSK_KEY_ENTER     = -4,
    OSK_KEY_SPACE     = -5,
    OSK_KEY_HIDE      = -6,
} osk_special_t;

/* 单个按键定义（布局表项） */
typedef struct osk_key {
    int  code;                       /* >0: ASCII; <=0: osk_special */
    char label[OSK_KEY_LABEL_MAX];   /* 显示文本（UTF-8） */
    uint8_t col_span;                /* 横向占多少列 (>=1) */
} osk_key_t;

typedef struct osk_row {
    uint8_t     key_count;
    osk_key_t   keys[OSK_MAX_COLS];
} osk_row_t;

typedef struct osk_layout {
    uint8_t   row_count;
    uint8_t   col_count;             /* 逻辑列数（每行 span 总和一致） */
    osk_row_t rows[OSK_MAX_ROWS];
} osk_layout_t;

/* ===== 内部状态（selftest 需要读取，故导出结构） ===== */
typedef struct osk_state {
    int         visible;
    osk_layer_t layer;
    int         screen_w;
    int         screen_h;
    int         panel_x;
    int         panel_y;
    int         panel_w;
    int         panel_h;
    int         key_w;               /* 每个逻辑列宽 */
    int         key_h;               /* 每行高度 */
    /* 统计：selftest 校验 */
    uint32_t    stat_keys_pressed;
    uint32_t    stat_chars_emitted;
    uint32_t    stat_backspaces;
    uint32_t    stat_enters;
    /* 最近一次注入到 input 层的字符（selftest 观测） */
    char        last_text[8];
    uint32_t    last_text_len;
    int         last_key_code;       /* 上次 press 的原始 code */
} osk_state_t;

/* ============================================================================
 * 公共 API
 * ============================================================================ */

/* 初始化 OSK：确定屏幕尺寸、加载默认布局、注册虚拟输入设备。 */
void osk_init(int screen_w, int screen_h);

/* 显示/隐藏/切换/查询。 */
void osk_show(void);
void osk_hide(void);
void osk_toggle(void);
int  osk_is_visible(void);

/* 触点命中：仅处理"抬起"事件（TAP-like 语义），x,y 为屏幕坐标。
 * 返回：
 *   1  已消费（点中键盘）
 *   0  未消费（点在键盘之外，事件应回落到 GUI）
 */
int  osk_handle_tap(int x, int y);

/* 渲染：调用 gui_screen_fill_rect / gui_draw_text 绘制到 screen present buffer。
 * 若 !visible 则空操作。渲染完调用方需自行 gui_screen_present。
 */
void osk_render(void);

/* 内部状态导出（供 selftest / touch_ui 只读访问） */
const osk_state_t *osk_get_state(void);
const osk_layout_t *osk_get_layout(osk_layer_t layer);

/* selftest 内部使用：取指定键中心坐标 */
void osk_center_of_(osk_layer_t layer, int row, int key_index, int *px, int *py);

/* selftest 入口：返回 0 = PASS，非 0 = FAIL(错误码) */
int  osk_selftest(void);

#endif /* OPENOS_OSK_H */
