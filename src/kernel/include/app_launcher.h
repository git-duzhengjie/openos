/* ============================================================================
 * app_launcher.h — M10.4 应用启动器（对外 API）
 *
 * 提供 app_launch(name) / app_exit() / app_switcher_show() 三大手机操作。
 * 内部：manifest 查表 → 生命周期 on_launch → app_stack push → on_resume。
 *
 * 与 proc64/elf64_loader 的对接采用弱引用：本模块只在 pid 上打桩。真正的
 * ELF 加载在 M10.4 后半段（app_launcher_bind_backend）注入，目前 pid = -1
 * 表示内建 GUI 模块或占位。
 * ============================================================================ */
#ifndef OPENOS_APP_LAUNCHER_H
#define OPENOS_APP_LAUNCHER_H

#include <stdint.h>
#include "app_stack.h"
#include "app_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 启动结果码 */
typedef enum {
    APP_LAUNCH_OK               = 0,
    APP_LAUNCH_ERR_NOT_FOUND    = -1,  /* manifest 查不到 name */
    APP_LAUNCH_ERR_STACK_FULL   = -2,  /* app_stack 已满 */
    APP_LAUNCH_ERR_BACKEND_FAIL = -3,  /* ELF 加载失败（后端返回 pid<0） */
    APP_LAUNCH_ERR_INVAL        = -4,
} app_launch_result_t;

/* ELF 后端函数指针；返回 pid>0 表示成功，<=0 表示失败。可为 NULL（内建 app 走占位）。 */
typedef int32_t (*app_elf_backend_fn)(const char *path, void *ctx);

/* 通用生命周期钩子（可选）；ctx 由注册时提供 */
typedef void (*app_lifecycle_cb_t)(const app_manifest_t *m, const app_slot_t *s, void *ctx);

typedef struct app_launcher_hooks_s {
    app_lifecycle_cb_t on_launch;
    app_lifecycle_cb_t on_resume;
    app_lifecycle_cb_t on_pause;
    app_lifecycle_cb_t on_destroy;
    void              *ctx;
} app_launcher_hooks_t;

/* 初始化（会调用 app_stack_init + app_manifest_init）；幂等 */
void app_launcher_init(void);

/* 绑定 ELF 后端（可选） */
void app_launcher_bind_backend(app_elf_backend_fn fn, void *ctx);

/* 注册生命周期钩子（覆盖式，只保留一组；nullable） */
void app_launcher_set_hooks(const app_launcher_hooks_t *hooks);

/* ============================================================================
 * 主 API
 * ============================================================================ */
int  app_launch(const char *name);        /* 返回 app_launch_result_t */
int  app_exit(void);                       /* 弹出栈顶 */
int  app_switcher_show(void);              /* 打开 switcher overlay（本模块只置状态） */
int  app_switcher_hide(void);
int  app_switcher_is_visible(void);

/* 从 switcher 切换到指定深度（0=底部）；把该 slot 上浮到栈顶 */
int  app_switcher_select(int stack_index);

/* 统计 */
typedef struct app_launcher_stats_s {
    uint32_t launches_ok;
    uint32_t launches_not_found;
    uint32_t launches_stack_full;
    uint32_t launches_backend_fail;
    uint32_t exits;
    uint32_t switcher_opens;
    uint32_t switcher_closes;
    uint32_t switcher_selects;
    uint32_t hook_on_launch_calls;
    uint32_t hook_on_resume_calls;
    uint32_t hook_on_pause_calls;
    uint32_t hook_on_destroy_calls;
} app_launcher_stats_t;

const app_launcher_stats_t *app_launcher_get_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_APP_LAUNCHER_H */
