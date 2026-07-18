/* ============================================================================
 * gesture3.h — M10.6 三指手势状态机（独立模块，不动 gesture_multi）
 *
 * 只识别一种：三指同时按下 → 抬起时向上位移 ≥ 阈值 → 触发 SWIPE_UP。
 * 手机式"三指上滑"打开应用切换器的标准手势。
 *
 * 输入：一帧 slots 数组（复用 gesture_multi_slot_t）。
 * 输出：GESTURE3_SWIPE_UP 事件（含起点、终点、位移）。
 * ============================================================================ */
#ifndef OPENOS_GESTURE3_H
#define OPENOS_GESTURE3_H

#include <stdint.h>
#include "gesture_multi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GESTURE3_NONE = 0,
    GESTURE3_BEGIN,       /* 三指同时按下瞬间 */
    GESTURE3_SWIPE_UP,    /* 抬起时 dy 累积 ≥ 阈值 */
    GESTURE3_CANCEL,      /* 抬起但未达阈值 */
} gesture3_type_t;

typedef struct {
    gesture3_type_t type;
    int start_cx, start_cy;    /* 三指按下时中心 */
    int end_cx,   end_cy;      /* 抬起时中心 */
    int dy;                    /* end - start（<0 表示上滑） */
    uint32_t frames;
} gesture3_event_t;

typedef void (*gesture3_listener_fn)(const gesture3_event_t *ev, void *user);

typedef struct {
    uint32_t begins;
    uint32_t swipe_ups;
    uint32_t cancels;
    uint32_t frames_fed;
} gesture3_stats_t;

/* 初始化 / 复位（screen_h 用于位移阈值：默认 h/6） */
void gesture3_init(int screen_w, int screen_h);
void gesture3_reset(void);

/* 手动设置上滑阈值（像素，正数）；<=0 恢复默认 */
void gesture3_set_up_threshold(int px);

/* 注册 listener（可为空） */
void gesture3_set_listener(gesture3_listener_fn cb, void *user);

/* 主接口：喂一帧 slots；内部维持状态机 */
void gesture3_feed(const gesture_multi_slot_t *slots, uint8_t slot_count);

/* 观测 */
const gesture3_stats_t *gesture3_get_stats(void);
void gesture3_reset_stats(void);
gesture3_type_t gesture3_last_type(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_GESTURE3_H */
