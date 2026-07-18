#ifndef OPENOS_ARCH_X86_64_NOTIF_CENTER_SELFTEST64_H
#define OPENOS_ARCH_X86_64_NOTIF_CENTER_SELFTEST64_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M8-F notif_center + quick_panel selftest. Drives the pure-logic module
 * via public API and asserts state transitions / hit-testing / toggle
 * accounting.
 *
 * Returns true on PASS, false on FAIL.
 */
bool arch_x86_64_notif_center_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
