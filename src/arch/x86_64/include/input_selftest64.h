#ifndef OPENOS_ARCH_X86_64_INPUT_SELFTEST64_H
#define OPENOS_ARCH_X86_64_INPUT_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M8-E input core selftest. Exercises the Input Abstraction Layer:
 *   1) device registration (register/lookup/idempotent revive)
 *   2) key/rel/abs/syn shim event round-trip through poll()
 *   3) subscriber fan-out
 *   4) ring drop-oldest overflow behaviour
 *   5) unsubscribe stops callbacks
 *   6) unregister marks device absent but preserves dev_id
 *   7) gesture engine tee (parallel to listener)
 *   8) stat counters
 *
 * Pure logic; no hardware required. Returns true on PASS.
 */
bool arch_x86_64_input_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
