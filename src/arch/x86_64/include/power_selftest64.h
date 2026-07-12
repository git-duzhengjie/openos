#ifndef OPENOS_ARCH_X86_64_POWER_SELFTEST64_H
#define OPENOS_ARCH_X86_64_POWER_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M6.1 power-management selftest. Verifies the FADT / \_S5 parser populated
 * a sane snapshot (PM1a_CNT present, \_S5 decoded or reset register present)
 * WITHOUT actually powering the machine off or rebooting. Returns true on
 * PASS. Safe to call after arch_x86_64_power_init().
 */
bool arch_x86_64_power_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_POWER_SELFTEST64_H */
