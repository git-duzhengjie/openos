/* ============================================================================
 * app_stack.c — M10.1 全屏应用栈实现
 *
 * 纯逻辑 + 静态存储，IRQ 安全（memory barrier 屏障）。
 * 生命周期钩子在 on_resume/on_pause/on_destroy 位置暴露 klog 日志锚点，
 * 后续可挂 manifest 声明的 hook。
 * ============================================================================ */
#include "app_stack.h"

#include <stdint.h>

static app_slot_t        g_slots[APP_STACK_MAX_DEPTH];
static int               g_depth;
static uint32_t          g_seq;
static app_stack_stats_t g_stats;

/* ------------------------- 内部工具 ------------------------- */

static void as_memzero_(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void as_strcpy_(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (src) {
        while (i + 1 < cap && src[i]) { dst[i] = src[i]; i++; }
    }
    dst[i] = 0;
}

static void as_barrier_(void) { __asm__ volatile("" ::: "memory"); }

/* on_resume/on_pause/on_destroy 目前只是纯状态迁移 + 统计。
 * 真实钩子（manifest 定义 entry）会在 M10.4 launcher 层触发。 */
static void as_on_resume_(app_slot_t *s) {
    if (!s) return;
    if (s->state != APP_STATE_RESUMED) g_stats.resumes++;
    s->state = APP_STATE_RESUMED;
    s->launched_seq = ++g_seq;
}
static void as_on_pause_(app_slot_t *s) {
    if (!s) return;
    if (s->state == APP_STATE_RESUMED) {
        s->state = APP_STATE_PAUSED;
        g_stats.pauses++;
    }
}
static void as_on_destroy_(app_slot_t *s) {
    if (!s) return;
    if (s->state != APP_STATE_DESTROYED) g_stats.destroys++;
    s->state = APP_STATE_DESTROYED;
}

/* ------------------------- 初始化 ------------------------- */

void app_stack_init(void) {
    as_memzero_(g_slots, sizeof(g_slots));
    as_memzero_(&g_stats, sizeof(g_stats));
    g_depth = 0;
    g_seq   = 0;
    as_barrier_();
}

/* ------------------------- push / pop ------------------------- */

int app_stack_push(const char *name, const char *path, int32_t manifest_id, int32_t pid) {
    if (g_depth >= APP_STACK_MAX_DEPTH) {
        g_stats.overflow_drops++;
        return -1;
    }

    /* 之前的 top 从 RESUMED 转 PAUSED */
    if (g_depth > 0) {
        as_on_pause_(&g_slots[g_depth - 1]);
    }

    int idx = g_depth;
    app_slot_t *s = &g_slots[idx];
    as_memzero_(s, sizeof(*s));
    s->used         = 1;
    s->manifest_id  = manifest_id;
    s->pid          = pid;
    s->state        = APP_STATE_INIT;
    s->launched_seq = ++g_seq;
    as_strcpy_(s->name, name, APP_NAME_MAX);
    as_strcpy_(s->path, path, APP_PATH_MAX);
    as_barrier_();
    g_depth++;

    g_stats.launches++;
    g_stats.depth = (uint32_t)g_depth;
    if (g_stats.depth > g_stats.max_depth_seen) g_stats.max_depth_seen = g_stats.depth;
    return idx;
}

int app_stack_pop(void) {
    if (g_depth <= 0) return -1;
    int idx = g_depth - 1;
    as_on_destroy_(&g_slots[idx]);
    g_slots[idx].used = 0;
    as_barrier_();
    g_depth--;
    g_stats.exits++;
    g_stats.depth = (uint32_t)g_depth;

    /* 恢复新的 top */
    if (g_depth > 0) {
        as_on_resume_(&g_slots[g_depth - 1]);
    }
    return 0;
}

void app_stack_resume_top(void) {
    if (g_depth <= 0) return;
    as_on_resume_(&g_slots[g_depth - 1]);
}

/* ------------------------- 查询 ------------------------- */

int app_stack_depth(void)   { return g_depth; }
int app_stack_is_empty(void){ return g_depth == 0 ? 1 : 0; }

const app_slot_t *app_stack_top(void) {
    if (g_depth <= 0) return (const app_slot_t *)0;
    return &g_slots[g_depth - 1];
}

const app_slot_t *app_stack_at(int index) {
    if (index < 0 || index >= g_depth) return (const app_slot_t *)0;
    return &g_slots[index];
}

const app_stack_stats_t *app_stack_get_stats(void) { return &g_stats; }

/* ------------------------- LRU 快照 ------------------------- */

/* 简单选择排序：从 g_slots[0..depth-1] 按 launched_seq 递减取前 n 个。 */
int app_stack_lru_snapshot(const app_slot_t **out, int n) {
    if (!out || n <= 0 || g_depth <= 0) return 0;
    int fill = 0;
    /* mark 位图 */
    int taken[APP_STACK_MAX_DEPTH];
    for (int i = 0; i < APP_STACK_MAX_DEPTH; i++) taken[i] = 0;

    while (fill < n && fill < g_depth) {
        int best = -1;
        uint32_t best_seq = 0;
        for (int i = 0; i < g_depth; i++) {
            if (taken[i]) continue;
            if (best < 0 || g_slots[i].launched_seq > best_seq) {
                best = i;
                best_seq = g_slots[i].launched_seq;
            }
        }
        if (best < 0) break;
        taken[best] = 1;
        out[fill++] = &g_slots[best];
    }
    return fill;
}
