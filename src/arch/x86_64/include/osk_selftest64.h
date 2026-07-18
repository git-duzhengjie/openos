#ifndef OPENOS_ARCH_X86_64_OSK_SELFTEST64_H
#define OPENOS_ARCH_X86_64_OSK_SELFTEST64_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M8-D.1 On-Screen Keyboard selftest. Drives synthetic taps against the
 * OSK layout and asserts key dispatch / layer switching. Pure logic;
 * no HID hardware required.
 *
 * Returns true on PASS, false on FAIL.
 */
bool arch_x86_64_osk_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
