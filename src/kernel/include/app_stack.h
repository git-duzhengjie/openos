/* ============================================================================
 * app_stack.h — M10.1 全屏应用栈接口（mobile-touch-roadmap M10-A/C）
 *
 * 手机式"一次一个前台应用"模型：应用以栈的形式压入，最顶为前台。
 * 纯逻辑 + 静态存储，无动态分配。生命周期状态与 M10-C 的钩子对齐。
 *
 * 依赖：无（不依赖 GUI / proc64，本模块只做栈骨架，具体启动 ELF 由 M10.4
 * 的 app_launch 桥接到 proc64/elf64_loader）。
 * ============================================================================ */
#ifndef OPENOS_APP_STACK_H
#define OPENOS_APP_STACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_STACK_MAX_DEPTH  8
#define APP_NAME_MAX         32
#define APP_PATH_MAX         64

/* 生命周期状态（M10-C） */
typedef enum {
    APP_STATE_INIT     = 0,  /* 已 push 尚未 resume */
    APP_STATE_RESUMED  = 1,  /* 前台运行 */
    APP_STATE_PAUSED   = 2,  /* 被更高层覆盖，仍在栈中 */
    APP_STATE_DESTROYED = 3, /* 已 pop 待回收 */
} app_state_t;

typedef struct app_slot_s {
    int      used;                /* 该槽是否有效 */
    char     name[APP_NAME_MAX];  /* 应用名（对齐 manifest.name） */
    char     path[APP_PATH_MAX];  /* /bin/xxx.elf 或内建 name */
    int32_t  manifest_id;         /* 关联 manifest 索引；-1 表示无 */
    int32_t  pid;                 /* 关联的 proc64 pid；-1 表示纯占位 */
    app_state_t state;
    uint32_t launched_seq;        /* 单调 seq，用于 LRU 排序 */
} app_slot_t;

typedef struct app_stack_stats_s {
    uint32_t launches;      /* 累计 push 次数 */
    uint32_t exits;         /* 累计 pop 次数 */
    uint32_t resumes;       /* on_resume 累计次数 */
    uint32_t pauses;        /* on_pause 累计次数 */
    uint32_t destroys;      /* on_destroy 累计次数 */
    uint32_t overflow_drops;/* 栈满时丢弃的 push 次数 */
    uint32_t depth;         /* 当前深度 */
    uint32_t max_depth_seen;/* 历史峰值 */
} app_stack_stats_t;

/* ============================================================================
 * 初始化 / 复位（幂等；可多次调用）
 * ============================================================================ */
void app_stack_init(void);

/* ============================================================================
 * 基础栈操作
 * ============================================================================
 * push:  向栈顶压入一个应用槽。返回 slot 索引 (0..MAX-1)，失败返回 -1。
 *        若栈已满，返回 -1 且 overflow_drops++。
 *        push 会自动把之前的 top 从 RESUMED 置为 PAUSED（on_pause），
 *        新槽初始为 INIT。需要显式调用 app_stack_resume_top() 才会 RESUMED。
 * pop:   弹出栈顶，将其置为 DESTROYED；若栈中还有元素，把新的 top 恢复到
 *        RESUMED（on_resume）。返回 0 成功，-1 空栈。
 * ============================================================================ */
int  app_stack_push(const char *name, const char *path, int32_t manifest_id, int32_t pid);
int  app_stack_pop(void);

/* 显式切换 top 到 RESUMED（第一次 push 后由 launcher 主动 resume） */
void app_stack_resume_top(void);

/* ============================================================================
 * 查询
 * ============================================================================ */
int              app_stack_depth(void);
int              app_stack_is_empty(void);
const app_slot_t *app_stack_top(void);        /* 无则返回 NULL */
const app_slot_t *app_stack_at(int index);    /* 0 = 栈底，depth-1 = 栈顶 */
const app_stack_stats_t *app_stack_get_stats(void);

/* ============================================================================
 * LRU：按 launched_seq 递减排序，返回最近使用的 N 个（含前台）。
 * out 由调用方提供，容量 >= n。返回实际填充个数。
 * ============================================================================ */
int app_stack_lru_snapshot(const app_slot_t **out, int n);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_APP_STACK_H */
