#ifndef OPENOS_ARCH_X86_64_SYS_INPUT_READ_SELFTEST64_H
#define OPENOS_ARCH_X86_64_SYS_INPUT_READ_SELFTEST64_H

#include "../../../kernel/include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M10.7 SYS_INPUT_READ selftest.
 *
 * Exercises the SYS_INPUT_READ syscall path (bypasses ring3/dispatch,
 * calls the same helpers used by dispatch to keep host-testable):
 *   1) empty queue -> returns 0 (non-blocking)
 *   2) single event round-trip
 *   3) multi-event batch (fill+drain)
 *   4) max_events cap (kernel returns min(available, max_events))
 *   5) FIFO order preserved
 *   6) queue empties after full drain (idempotent second call)
 *   7) argument validation (max_events=0, oversized, bad flags)
 *   8) drop-oldest interaction (overflow before read -> oldest lost)
 *
 * Pure logic; no hardware / no ring3 required. Returns true on PASS.
 */
bool arch_x86_64_sys_input_read_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
