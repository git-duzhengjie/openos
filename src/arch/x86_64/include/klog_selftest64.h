#ifndef OPENOS_ARCH_X86_64_KLOG_SELFTEST64_H
#define OPENOS_ARCH_X86_64_KLOG_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M6.12 klog selftest. Exercises the kernel ring log buffer:
 *   - emit / seq monotonic / read-back byte-perfect
 *   - stats reflect emissions and drops
 *   - ring wrap evicts oldest entries in FIFO order (drop counter increments)
 *   - read_tail returns most-recent N entries
 *   - read_from(seq) resumes at an arbitrary sequence number
 * Runs against the *live* ring, so it snapshots pre-test seq/dropped and
 * restores nothing (klog is monotonic; the test's own entries are the
 * expected forensic trace). Returns true on PASS.
 */
bool arch_x86_64_klog_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
