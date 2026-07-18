/*
 * app_lifecycle_selftest64.c — M10.9 app-lifecycle selftest（8 阶段）
 *
 * 阶段：
 *   1) launcher_init 空栈干净
 *   2) app_launch("terminal") 触发 on_launch + on_resume
 *   3) app_launch("dmesg") 触发 on_pause(旧顶) + on_launch + on_resume
 *   4) app_exit 触发 on_pause+on_destroy(dmesg) + on_resume(terminal)
 *   5) 未知 name / 栈满 / backend fail
 *   6) app_switcher_show/hide/select 语义
 *   7) gui_mode 切换与 listener 回调
 *   8) 综合统计校验
 */
#include "../include/app_lifecycle_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/app_launcher.h"
#include "../../../kernel/include/app_stack.h"
#include "../../../kernel/include/app_manifest.h"
#include "../../../kernel/include/gui_mode.h"

#include <stdint.h>

static void al_log(const char *s) { early_console64_write(s); }
static int  al_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    uint32_t i=0; while(a[i]&&b[i]){if(a[i]!=b[i])return 0;i++;}
    return a[i]==0 && b[i]==0;
}

/* --------- 生命周期钩子探针：记录调用序列 ---------- */
#define TRACE_MAX 64
static char     g_trace[TRACE_MAX][8];  /* 事件缩写: "L","R","P","D" + name 首字母 */
static int      g_trace_n;
static uint32_t g_cnt_launch, g_cnt_resume, g_cnt_pause, g_cnt_destroy;

static void trace_reset(void) {
    for (int i=0;i<TRACE_MAX;i++) for (int j=0;j<8;j++) g_trace[i][j]=0;
    g_trace_n=0;
    g_cnt_launch=g_cnt_resume=g_cnt_pause=g_cnt_destroy=0;
}
static void trace_push(char ev, const char *name) {
    if (g_trace_n >= TRACE_MAX) return;
    char *dst = g_trace[g_trace_n++];
    dst[0]=ev;
    dst[1]=':';
    int k=2;
    for (int i=0; name && name[i] && k<7; i++) dst[k++]=name[i];
    dst[k]=0;
}

static void hook_launch (const app_manifest_t *m, const app_slot_t *s, void *ctx){ (void)s;(void)ctx; trace_push('L', m?m->name:"?"); g_cnt_launch++; }
static void hook_resume (const app_manifest_t *m, const app_slot_t *s, void *ctx){ (void)s;(void)ctx; trace_push('R', m?m->name:"?"); g_cnt_resume++; }
static void hook_pause  (const app_manifest_t *m, const app_slot_t *s, void *ctx){ (void)s;(void)ctx; trace_push('P', m?m->name:"?"); g_cnt_pause++; }
static void hook_destroy(const app_manifest_t *m, const app_slot_t *s, void *ctx){ (void)s;(void)ctx; trace_push('D', m?m->name:"?"); g_cnt_destroy++; }

/* backend 桩：模拟 ELF 加载成功/失败 */
static int32_t g_backend_next_pid = 42;
static int32_t backend_ok(const char *path, void *ctx){ (void)path;(void)ctx; return g_backend_next_pid; }
static int32_t backend_fail(const char *path, void *ctx){ (void)path;(void)ctx; return -1; }

/* gui_mode listener */
static int      g_mode_calls;
static gui_mode_t g_mode_last_old, g_mode_last_new;
static void mode_cb(gui_mode_t o, gui_mode_t n, void *ctx){ (void)ctx; g_mode_calls++; g_mode_last_old=o; g_mode_last_new=n; }

bool arch_x86_64_app_lifecycle_selftest_run(void)
{
    bool ok = true;
    #define AL_CHK(cond, msg) do { if (!(cond)) { \
        al_log("[x86_64][app-lifecycle-selftest] FAIL: " msg "\n"); ok=false; } } while (0)

    /* 装配钩子 */
    trace_reset();
    app_launcher_init();
    app_launcher_hooks_t hooks = {
        .on_launch=hook_launch, .on_resume=hook_resume,
        .on_pause=hook_pause,   .on_destroy=hook_destroy,
        .ctx=(void*)0
    };
    app_launcher_set_hooks(&hooks);
    app_launcher_bind_backend((app_elf_backend_fn)0, (void*)0); /* 先解绑 */

    /* Stage 1: 空栈 */
    AL_CHK(app_stack_depth()==0,               "S1 depth 0");
    AL_CHK(!app_switcher_is_visible(),         "S1 switcher hidden");

    /* Stage 2: launch terminal */
    int r = app_launch("terminal");
    AL_CHK(r == APP_LAUNCH_OK,                 "S2 launch ok");
    AL_CHK(app_stack_depth()==1,               "S2 depth 1");
    const app_slot_t *tp = app_stack_top();
    AL_CHK(tp && al_streq(tp->name,"terminal"),"S2 top=terminal");
    AL_CHK(tp && tp->state==APP_STATE_RESUMED, "S2 state RESUMED");
    AL_CHK(g_cnt_launch==1 && g_cnt_resume==1, "S2 hook launch+resume");
    AL_CHK(g_cnt_pause==0 && g_cnt_destroy==0, "S2 no pause/destroy");

    /* Stage 3: launch dmesg -> pause terminal + launch/resume dmesg */
    r = app_launch("dmesg");
    AL_CHK(r == APP_LAUNCH_OK,                 "S3 launch dmesg ok");
    AL_CHK(app_stack_depth()==2,               "S3 depth 2");
    AL_CHK(g_cnt_pause==1,                     "S3 pause=1 (terminal)");
    AL_CHK(g_cnt_launch==2 && g_cnt_resume==2, "S3 launch+resume increm");
    const app_slot_t *dm = app_stack_top();
    AL_CHK(dm && al_streq(dm->name,"dmesg"),   "S3 top=dmesg");
    const app_slot_t *t2 = app_stack_at(0);
    AL_CHK(t2 && t2->state==APP_STATE_PAUSED,  "S3 bottom PAUSED");

    /* Stage 4: exit dmesg -> pause+destroy dmesg, resume terminal */
    uint32_t p0=g_cnt_pause, d0=g_cnt_destroy, r0=g_cnt_resume;
    r = app_exit();
    AL_CHK(r==0,                               "S4 exit ok");
    AL_CHK(g_cnt_pause==p0+1,                  "S4 pause++ (dmesg)");
    AL_CHK(g_cnt_destroy==d0+1,                "S4 destroy++ (dmesg)");
    AL_CHK(g_cnt_resume==r0+1,                 "S4 resume++ (terminal)");
    AL_CHK(app_stack_depth()==1,               "S4 depth 1");
    const app_slot_t *tp2 = app_stack_top();
    AL_CHK(tp2 && al_streq(tp2->name,"terminal") && tp2->state==APP_STATE_RESUMED, "S4 terminal RESUMED");

    if (ok) al_log("[x86_64][app-lifecycle-selftest] stages 1-4 OK\n");

    /* Stage 5: 错误路径 */
    r = app_launch("no_such_app");
    AL_CHK(r == APP_LAUNCH_ERR_NOT_FOUND,      "S5 not found");
    r = app_launch((const char*)0);
    AL_CHK(r == APP_LAUNCH_ERR_INVAL,          "S5 null inval");

    /* backend 失败：绑定 backend_fail，尝试启动 ELF hello64 */
    app_launcher_bind_backend(backend_fail, (void*)0);
    r = app_launch("hello64");
    AL_CHK(r == APP_LAUNCH_ERR_BACKEND_FAIL,   "S5 backend fail");
    AL_CHK(app_stack_depth()==1,               "S5 depth unchanged");

    /* backend 成功：绑定 backend_ok，pid=42 */
    app_launcher_bind_backend(backend_ok, (void*)0);
    r = app_launch("hello64");
    AL_CHK(r == APP_LAUNCH_OK,                 "S5 backend ok");
    AL_CHK(app_stack_depth()==2,               "S5 depth 2");
    const app_slot_t *hp = app_stack_top();
    AL_CHK(hp && hp->pid == 42,                "S5 pid=42 from backend");

    /* 填满栈测溢出 */
    while (app_stack_depth() < APP_STACK_MAX_DEPTH) {
        r = app_launch("terminal");
        AL_CHK(r == APP_LAUNCH_OK,             "S5 fill push");
    }
    r = app_launch("dmesg");
    AL_CHK(r == APP_LAUNCH_ERR_STACK_FULL,     "S5 stack full");

    /* Stage 6: switcher show/hide/select */
    AL_CHK(!app_switcher_is_visible(),         "S6 hidden default");
    app_switcher_show();
    AL_CHK(app_switcher_is_visible(),          "S6 shown");
    app_switcher_show();  /* 幂等 */
    AL_CHK(app_switcher_is_visible(),          "S6 double show ok");
    app_switcher_hide();
    AL_CHK(!app_switcher_is_visible(),         "S6 hidden after hide");

    /* select 触发 pause+resume；select 后 switcher 自动隐藏 */
    app_switcher_show();
    uint32_t pp=g_cnt_pause, rp=g_cnt_resume;
    int idx0 = 0;  /* 栈底 */
    r = app_switcher_select(idx0);
    AL_CHK(r==0,                               "S6 select ok");
    AL_CHK(!app_switcher_is_visible(),         "S6 hidden after select");
    AL_CHK(g_cnt_pause >= pp+1,                "S6 pause fired");
    AL_CHK(g_cnt_resume >= rp+1,               "S6 resume fired");

    /* select 自身 top: no-op */
    app_switcher_show();
    uint32_t pp2=g_cnt_pause, rp2=g_cnt_resume;
    r = app_switcher_select(app_stack_depth()-1);
    AL_CHK(r==0,                               "S6 select self ok");
    AL_CHK(g_cnt_pause == pp2 && g_cnt_resume == rp2, "S6 select self no hook");
    AL_CHK(!app_switcher_is_visible(),         "S6 hidden after select self");

    /* select 越界 */
    r = app_switcher_select(9999);
    AL_CHK(r == -1,                            "S6 select oob");

    /* Stage 7: gui_mode + listener */
    gui_mode_init();
    g_mode_calls=0;
    int lid = gui_mode_add_listener(mode_cb, (void*)0);
    AL_CHK(lid >= 0,                           "S7 listener registered");
    AL_CHK(gui_mode_get() == GUI_MODE_DESKTOP, "S7 default DESKTOP");

    r = gui_mode_set(GUI_MODE_DESKTOP);  /* 相同 */
    AL_CHK(r==0 && g_mode_calls==0,            "S7 same mode noop");

    r = gui_mode_set(GUI_MODE_FULLSCREEN);
    AL_CHK(r==0,                               "S7 set FULLSCREEN ok");
    AL_CHK(gui_mode_get() == GUI_MODE_FULLSCREEN, "S7 read FULLSCREEN");
    AL_CHK(g_mode_calls == 1,                  "S7 listener called 1");
    AL_CHK(g_mode_last_old==GUI_MODE_DESKTOP && g_mode_last_new==GUI_MODE_FULLSCREEN, "S7 old/new");

    r = gui_mode_set(GUI_MODE_DESKTOP);
    AL_CHK(r==0 && g_mode_calls == 2,          "S7 back to DESKTOP");

    const gui_mode_stats_t *ms = gui_mode_get_stats();
    AL_CHK(ms->transitions >= 2,               "S7 transitions>=2");
    AL_CHK(ms->noop_sets >= 1,                 "S7 noop_sets>=1");

    /* Stage 8: 综合统计 */
    const app_launcher_stats_t *ls = app_launcher_get_stats();
    AL_CHK(ls->launches_ok >= 3,               "S8 launches_ok>=3");
    AL_CHK(ls->launches_not_found >= 1,        "S8 not_found>=1");
    AL_CHK(ls->launches_backend_fail >= 1,     "S8 backend_fail>=1");
    AL_CHK(ls->launches_stack_full >= 1,       "S8 stack_full>=1");
    AL_CHK(ls->switcher_opens >= 1,            "S8 switcher_opens>=1");
    AL_CHK(ls->switcher_selects >= 1,          "S8 switcher_selects>=1");
    AL_CHK(ls->hook_on_launch_calls >= 1,      "S8 hook_launch>=1");
    AL_CHK(ls->hook_on_resume_calls >= 1,      "S8 hook_resume>=1");
    AL_CHK(ls->hook_on_pause_calls >= 1,       "S8 hook_pause>=1");
    AL_CHK(ls->hook_on_destroy_calls >= 1,     "S8 hook_destroy>=1");

    /* 收尾：清栈 & 复位 mode */
    while (app_stack_depth() > 0) app_stack_pop();
    gui_mode_init();

    if (ok) al_log("[x86_64][app-lifecycle-selftest] PASS\n");
    return ok;
    #undef AL_CHK
}
