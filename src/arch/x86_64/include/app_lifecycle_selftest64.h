#ifndef OPENOS_ARCH_X86_64_APP_LIFECYCLE_SELFTEST64_H
#define OPENOS_ARCH_X86_64_APP_LIFECYCLE_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M10.9 app-lifecycle selftest：8 阶段验证 app_launcher 生命周期钩子链、
 * gui_mode 切换通知、switcher 显隐/select 语义。返回 true = PASS。
 */
bool arch_x86_64_app_lifecycle_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
