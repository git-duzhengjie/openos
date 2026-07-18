/* ============================================================================
 * gui_mode.h — M10.5 GUI 模式抽象
 *
 * 让 GUI 系统在"桌面窗口模式"和"全屏应用模式"之间可切换。桌面模式保持向后
 * 兼容（M1~M9 的窗口/任务栏行为），全屏模式是手机式一次一个前台应用。
 *
 * 本层只提供 mode 状态 + 通知回调注册；具体渲染切换由 GUI 层在自身重绘时
 * 通过 gui_mode_get() 读取当前 mode 决定路径。
 * ============================================================================ */
#ifndef OPENOS_GUI_MODE_H
#define OPENOS_GUI_MODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GUI_MODE_DESKTOP    = 0,  /* 传统桌面窗口模式（默认） */
    GUI_MODE_FULLSCREEN = 1,  /* 手机式全屏应用模式 */
} gui_mode_t;

typedef void (*gui_mode_change_cb_t)(gui_mode_t old_mode, gui_mode_t new_mode, void *ctx);

#define GUI_MODE_MAX_LISTENERS 4

/* 初始化：默认 desktop 模式；幂等 */
void gui_mode_init(void);

/* 读取当前 mode */
gui_mode_t gui_mode_get(void);

/* 切换 mode；若与当前相同则 no-op；成功返回 0 */
int gui_mode_set(gui_mode_t mode);

/* 注册切换通知；返回 listener id (>=0)，满则 -1 */
int gui_mode_add_listener(gui_mode_change_cb_t cb, void *ctx);

/* 统计 */
typedef struct gui_mode_stats_s {
    uint32_t transitions;         /* set 触发实际切换次数 */
    uint32_t noop_sets;           /* 相同 mode 的 set */
    uint32_t listeners_registered;
    uint32_t listener_calls;      /* 累计回调次数 */
} gui_mode_stats_t;

const gui_mode_stats_t *gui_mode_get_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_GUI_MODE_H */
