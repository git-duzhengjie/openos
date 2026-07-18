#ifndef OPENOS_ARCH_X86_64_GUI_INPUT_BRIDGE_SELFTEST64_H
#define OPENOS_ARCH_X86_64_GUI_INPUT_BRIDGE_SELFTEST64_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M9.5 GUI input bridge selftest. Publishes synthetic gesture events via
 * input_publish() and verifies the bridge counters increment accordingly,
 * without disturbing the legacy g_mouse path.
 *
 * Returns true on PASS, false on FAIL.
 */
bool arch_x86_64_gui_input_bridge_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
