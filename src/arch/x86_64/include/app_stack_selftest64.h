#ifndef OPENOS_ARCH_X86_64_APP_STACK_SELFTEST64_H
#define OPENOS_ARCH_X86_64_APP_STACK_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M10.9 app-stack selftest：8 阶段验证栈骨架 + manifest 查表 + LRU。
 * 纯逻辑，无 IRQ / GUI 依赖。返回 true = PASS。
 */
bool arch_x86_64_app_stack_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
