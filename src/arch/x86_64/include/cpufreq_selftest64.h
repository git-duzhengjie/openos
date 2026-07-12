#ifndef OPENOS_ARCH_X86_64_CPUFREQ_SELFTEST64_H
#define OPENOS_ARCH_X86_64_CPUFREQ_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M6.2 CPU frequency / thermal selftest. Verifies arch_x86_64_cpufreq_init()
 * populated a sane snapshot (vendor string present, TSC MHz calibrated, caps
 * bitmask self-consistent). Read-only: never writes PERF_CTL / changes the
 * P-state. Returns true on PASS. Safe after arch_x86_64_cpufreq_init().
 */
bool arch_x86_64_cpufreq_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_CPUFREQ_SELFTEST64_H */
