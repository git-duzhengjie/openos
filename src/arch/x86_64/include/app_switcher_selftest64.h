#ifndef OPENOS_ARCH_X86_64_APP_SWITCHER_SELFTEST64_H
#define OPENOS_ARCH_X86_64_APP_SWITCHER_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M10.6 app-switcher selftest：8 阶段验证 switcher UI + 三指手势 + tap 优先级。
 * 纯逻辑，无 IRQ / GUI 依赖。返回 true = PASS。
 */
bool arch_x86_64_app_switcher_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
