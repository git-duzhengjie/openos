#ifndef OPENOS_ARCH_X86_64_GUI_METRICS_SELFTEST64_H
#define OPENOS_ARCH_X86_64_GUI_METRICS_SELFTEST64_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M9.1..M9.4 GUI metrics selftest. Validates density switch, listener
 * dispatch, and per-density touch-friendliness invariants (min hit size,
 * scrollbar >= 20px in touch mode, etc.).
 *
 * Returns true on PASS, false on FAIL.
 */
bool arch_x86_64_gui_metrics_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
