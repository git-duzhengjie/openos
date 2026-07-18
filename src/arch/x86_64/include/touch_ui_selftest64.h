#ifndef OPENOS_ARCH_X86_64_TOUCH_UI_SELFTEST64_H
#define OPENOS_ARCH_X86_64_TOUCH_UI_SELFTEST64_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M8-D.2/.5 touch UI event router selftest. Injects synthetic gesture
 * events into touch_ui_on_gesture() and asserts the resulting stats /
 * OSK visibility.
 *
 * Returns true on PASS, false on FAIL.
 */
bool arch_x86_64_touch_ui_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
