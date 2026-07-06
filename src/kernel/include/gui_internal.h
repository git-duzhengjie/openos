/* ============================================================
 * openos - GUI 内部共享接口（gui.c 与子模块 gui_browser.c 等之间）
 *
 * 说明：以下符号原为 gui.c 内部 static，因拆分子模块需跨文件共享，
 *       改为非 static 并在此声明。仅供 GUI 各 .c 文件内部使用，
 *       不属于对外公开 API（对外 API 见 gui.h / gui_user.h）。
 * ============================================================ */
#ifndef OPENOS_GUI_INTERNAL_H
#define OPENOS_GUI_INTERNAL_H

#include "gui.h"
#include "types.h"

/* ---- gui.c 导出给子模块的共享全局状态 ---- */
extern gui_system_t g_gui;

/* ---- gui.c 导出给子模块的共享辅助函数 ---- */
uint32_t gui_rgb(uint8_t r, uint8_t g, uint8_t b);
void     gui_notify(const char *text);
int      gui_is_enter_key(int key);
int      gui_append_uint(char *dst, int pos, int cap, uint32_t v);
void     gui_terminal_show_prompt(void);
int      fp_str_append(char *dst, int pos, int cap, const char *src);

/* ---- gui_browser.c 导出给 gui.c 的浏览器/网络工具接口 ---- */
/* 网络工具异步状态机类型（gui.c 终端命令分支与 gui_browser.c 共享）*/
typedef enum {
    NT_TOOL_NONE = 0,
    NT_TOOL_PING,
    NT_TOOL_NSLOOKUP,
    NT_TOOL_WGET,
} nt_tool_t;

void gui_browser_open(void);
gui_window_t *gui_browser_window(void);   /* 返回浏览器窗口指针，供窗口关闭比较 */
void browser_load_tick(void);
int  browser_handle_address_enter(int key);
int  gui_nettool_start(nt_tool_t tool, const char *host, const char *path2);
void gui_nettool_tick(void);
int  gui_nettool_active(void);

#endif /* OPENOS_GUI_INTERNAL_H */
