/* ============================================================================
 * app_launcher.c — M10.4 应用启动器实现
 *
 * 职责：
 *   1. 解析 manifest 得到 path & 类型
 *   2. 内建应用 pid = -1（GUI 直接渲染），ELF 应用调用后端 backend(path,ctx)
 *   3. 调 app_stack_push → 触发 on_launch → app_stack_resume_top → on_resume
 *   4. app_exit 弹栈 → on_destroy → 新 top on_resume
 * ============================================================================ */
#include "app_launcher.h"
#include "app_stack.h"
#include "app_manifest.h"

#include <stdint.h>

static app_elf_backend_fn    g_backend;
static void                 *g_backend_ctx;
static app_launcher_hooks_t  g_hooks;
static int                   g_switcher_visible;
static app_launcher_stats_t  g_stats;
static int                   g_inited;

static void al_zero_(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static int al_streq_(const char *a, const char *b) __attribute__((unused));
static int al_streq_(const char *a, const char *b) {
    if (!a || !b) return 0;
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == 0 && b[i] == 0;
}

/* 内建 path 前缀识别 */
static int al_is_builtin_path_(const char *p) {
    if (!p) return 0;
    return (p[0]=='b' && p[1]=='u' && p[2]=='i' && p[3]=='l' && p[4]=='t' &&
            p[5]=='i' && p[6]=='n' && p[7]==':');
}

static void al_fire_(app_lifecycle_cb_t cb, const app_manifest_t *m, const app_slot_t *s) {
    if (cb) cb(m, s, g_hooks.ctx);
}

void app_launcher_init(void) {
    if (!g_inited) {
        al_zero_(&g_hooks, sizeof(g_hooks));
        al_zero_(&g_stats, sizeof(g_stats));
        g_backend = (app_elf_backend_fn)0;
        g_backend_ctx = (void*)0;
        g_switcher_visible = 0;
        g_inited = 1;
    }
    app_stack_init();
    app_manifest_init();
}

void app_launcher_bind_backend(app_elf_backend_fn fn, void *ctx) {
    g_backend = fn;
    g_backend_ctx = ctx;
}

void app_launcher_set_hooks(const app_launcher_hooks_t *hooks) {
    if (!hooks) { al_zero_(&g_hooks, sizeof(g_hooks)); return; }
    g_hooks = *hooks;
}

/* ------------------------- app_launch ------------------------- */

int app_launch(const char *name) {
    if (!name || !name[0]) return APP_LAUNCH_ERR_INVAL;

    int mi = app_manifest_find_index(name);
    if (mi < 0) { g_stats.launches_not_found++; return APP_LAUNCH_ERR_NOT_FOUND; }
    const app_manifest_t *m = app_manifest_at(mi);
    if (!m) { g_stats.launches_not_found++; return APP_LAUNCH_ERR_NOT_FOUND; }

    int32_t pid = -1;
    if (!m->is_builtin && !al_is_builtin_path_(m->path)) {
        if (g_backend) {
            pid = g_backend(m->path, g_backend_ctx);
            if (pid <= 0) {
                g_stats.launches_backend_fail++;
                return APP_LAUNCH_ERR_BACKEND_FAIL;
            }
        }
        /* backend 未绑定时也允许 pid=-1 占位启动（用于 selftest 无 proc64 环境） */
    }

    /* 若栈非空：先触发旧顶的 on_pause 钩子（app_stack_push 内部只做状态迁移，钩子由 launcher 层负责） */
    {
        const app_slot_t *old_top = app_stack_top();
        if (old_top && old_top->manifest_id >= 0) {
            const app_manifest_t *om = app_manifest_at(old_top->manifest_id);
            if (om && (om->hooks & APP_HOOK_ON_PAUSE)) {
                al_fire_(g_hooks.on_pause, om, old_top);
                g_stats.hook_on_pause_calls++;
            }
        }
    }

    int idx = app_stack_push(m->name, m->path, mi, pid);
    if (idx < 0) {
        g_stats.launches_stack_full++;
        return APP_LAUNCH_ERR_STACK_FULL;
    }

    const app_slot_t *s = app_stack_at(idx);
    /* on_launch */
    if (m->hooks & APP_HOOK_ON_LAUNCH) {
        al_fire_(g_hooks.on_launch, m, s);
        g_stats.hook_on_launch_calls++;
    }
    /* resume top */
    app_stack_resume_top();
    if (m->hooks & APP_HOOK_ON_RESUME) {
        al_fire_(g_hooks.on_resume, m, s);
        g_stats.hook_on_resume_calls++;
    }

    g_stats.launches_ok++;
    return APP_LAUNCH_OK;
}

/* ------------------------- app_exit ------------------------- */

int app_exit(void) {
    const app_slot_t *top = app_stack_top();
    if (!top) return -1;
    const app_manifest_t *m = (top->manifest_id >= 0) ? app_manifest_at(top->manifest_id) : (const app_manifest_t*)0;

    /* on_pause 前置（栈顶即将 destroyed，可视为 pause->destroy 序列） */
    if (m && (m->hooks & APP_HOOK_ON_PAUSE)) {
        al_fire_(g_hooks.on_pause, m, top);
        g_stats.hook_on_pause_calls++;
    }
    if (m && (m->hooks & APP_HOOK_ON_DESTROY)) {
        al_fire_(g_hooks.on_destroy, m, top);
        g_stats.hook_on_destroy_calls++;
    }

    int rc = app_stack_pop();
    if (rc != 0) return rc;
    g_stats.exits++;

    /* pop 后自动 resume 新 top（app_stack_pop 内部已做状态切换，此处补钩子） */
    const app_slot_t *new_top = app_stack_top();
    if (new_top && new_top->manifest_id >= 0) {
        const app_manifest_t *mn = app_manifest_at(new_top->manifest_id);
        if (mn && (mn->hooks & APP_HOOK_ON_RESUME)) {
            al_fire_(g_hooks.on_resume, mn, new_top);
            g_stats.hook_on_resume_calls++;
        }
    }
    return 0;
}

/* ------------------------- app_switcher ------------------------- */

int app_switcher_show(void) {
    if (g_switcher_visible) return 0;
    g_switcher_visible = 1;
    g_stats.switcher_opens++;
    return 0;
}

int app_switcher_hide(void) {
    if (!g_switcher_visible) return 0;
    g_switcher_visible = 0;
    g_stats.switcher_closes++;
    return 0;
}

int app_switcher_is_visible(void) { return g_switcher_visible; }

/* 把 stack_index 处的 slot 上浮到栈顶：
 * 简化实现——若已在栈顶，no-op；否则 pause 当前 top，然后把目标 slot 的内容
 * 与栈顶交换（保持槽内容不丢失），并触发 hook。
 *
 * 注意：这里对 app_stack 直接读，写通过 pop+push 的组合太重（会触发 destroy），
 * 因此我们用一次"轻量提升"——通过再次 push 一份该 manifest 达到"多实例"效果
 * 会破坏语义。真正的 reorder 需要 app_stack 提供接口，故本期简单做法是：
 *   - 找到该 slot 的 manifest_id；如果它就是 top，no-op；
 *   - 否则先 pause 现 top（触发钩子），再 resume 目标（用 launched_seq 抬高）。
 *
 * 由于 app_stack 目前不暴露 reorder，我们把目标标记为 "触发一次 resume 钩子"，
 * 视觉上 switcher 展示会依据 launched_seq 排序把它显示到最新。这层局限记录在
 * TODO：M11 增强 app_stack_reorder。
 */
int app_switcher_select(int stack_index) {
    const app_slot_t *tgt = app_stack_at(stack_index);
    if (!tgt) return -1;
    const app_slot_t *top = app_stack_top();
    if (!top) return -1;
    g_stats.switcher_selects++;
    g_switcher_visible = 0;
    g_stats.switcher_closes++;

    if (tgt == top) return 0;

    /* pause 老 top */
    if (top->manifest_id >= 0) {
        const app_manifest_t *m = app_manifest_at(top->manifest_id);
        if (m && (m->hooks & APP_HOOK_ON_PAUSE)) {
            al_fire_(g_hooks.on_pause, m, top);
            g_stats.hook_on_pause_calls++;
        }
    }
    /* resume 目标（提升 seq，供 LRU 排序） */
    if (tgt->manifest_id >= 0) {
        const app_manifest_t *m = app_manifest_at(tgt->manifest_id);
        if (m && (m->hooks & APP_HOOK_ON_RESUME)) {
            al_fire_(g_hooks.on_resume, m, tgt);
            g_stats.hook_on_resume_calls++;
        }
    }
    return 0;
}

const app_launcher_stats_t *app_launcher_get_stats(void) { return &g_stats; }
