/* ============================================================================
 * gesture3.c — 三指上滑手势状态机
 *
 * 状态：
 *   IDLE  → 见到 3 指 tip=1 → ARMED (记录 start_cy)
 *   ARMED → 3 指持续 tip=1 → 更新 last_cy
 *   ARMED → 见到 <3 指 tip=1 → 抬起，若 (start_cy - last_cy) >= threshold
 *                                   触发 SWIPE_UP，否则 CANCEL
 *
 * 阈值默认 = screen_h / 6（min 40 px）。
 * ============================================================================ */
#include "gesture3.h"

#include <string.h>

typedef struct {
    int screen_w, screen_h;
    int threshold_px;         /* >0 表示有效；<=0 用 screen_h/6 兜底 */

    /* 状态 */
    int armed;                /* 0/1 */
    int start_cx, start_cy;
    int last_cx,  last_cy;
    uint32_t frames_in_state;

    gesture3_listener_fn listener;
    void *listener_user;

    gesture3_stats_t stats;
    gesture3_type_t  last;
} ctx_t;

static ctx_t G;

static int effective_threshold(void) {
    if (G.threshold_px > 0) return G.threshold_px;
    int th = G.screen_h / 6;
    if (th < 40) th = 40;
    return th;
}

static void emit(gesture3_type_t t, int end_cx, int end_cy) {
    G.last = t;
    if (t == GESTURE3_BEGIN)     G.stats.begins++;
    else if (t == GESTURE3_SWIPE_UP) G.stats.swipe_ups++;
    else if (t == GESTURE3_CANCEL)   G.stats.cancels++;
    if (!G.listener) return;
    gesture3_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = t;
    ev.start_cx = G.start_cx;
    ev.start_cy = G.start_cy;
    ev.end_cx   = end_cx;
    ev.end_cy   = end_cy;
    ev.dy       = end_cy - G.start_cy;
    ev.frames   = G.frames_in_state;
    G.listener(&ev, G.listener_user);
}

void gesture3_init(int w, int h) {
    memset(&G, 0, sizeof(G));
    G.screen_w = w > 0 ? w : 800;
    G.screen_h = h > 0 ? h : 600;
    G.threshold_px = 0;   /* 用默认 */
    G.last = GESTURE3_NONE;
}
void gesture3_reset(void) {
    G.armed = 0;
    G.start_cx = G.start_cy = 0;
    G.last_cx  = G.last_cy  = 0;
    G.frames_in_state = 0;
    G.last = GESTURE3_NONE;
}
void gesture3_reset_stats(void) { memset(&G.stats, 0, sizeof(G.stats)); }
void gesture3_set_up_threshold(int px) { G.threshold_px = px; }
void gesture3_set_listener(gesture3_listener_fn cb, void *u) {
    G.listener = cb; G.listener_user = u;
}
const gesture3_stats_t *gesture3_get_stats(void) { return &G.stats; }
gesture3_type_t gesture3_last_type(void) { return G.last; }

void gesture3_feed(const gesture_multi_slot_t *slots, uint8_t slot_count) {
    G.stats.frames_fed++;

    /* 统计当前 tip=1 的手指数 + 中心 */
    int tips = 0, sx = 0, sy = 0;
    for (uint8_t i = 0; i < slot_count && i < GESTURE_MULTI_MAX_SLOTS; ++i) {
        if (slots[i].present && slots[i].tip) {
            tips++;
            sx += slots[i].x;
            sy += slots[i].y;
        }
    }

    if (!G.armed) {
        if (tips >= 3) {
            /* 进入 ARMED */
            G.armed = 1;
            G.start_cx = sx / tips;
            G.start_cy = sy / tips;
            G.last_cx  = G.start_cx;
            G.last_cy  = G.start_cy;
            G.frames_in_state = 1;
            emit(GESTURE3_BEGIN, G.start_cx, G.start_cy);
        }
        return;
    }

    /* ARMED */
    if (tips >= 3) {
        /* 继续按压：更新最新位置 */
        G.last_cx = sx / tips;
        G.last_cy = sy / tips;
        G.frames_in_state++;
        return;
    }

    /* 抬起（不足 3 指）→ 判定 */
    int th = effective_threshold();
    int dy = G.start_cy - G.last_cy;   /* 向上位移为正 */
    int end_cx = G.last_cx;
    int end_cy = G.last_cy;

    if (dy >= th) {
        emit(GESTURE3_SWIPE_UP, end_cx, end_cy);
    } else {
        emit(GESTURE3_CANCEL, end_cx, end_cy);
    }
    G.armed = 0;
    G.frames_in_state = 0;
}
