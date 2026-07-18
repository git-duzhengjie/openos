#ifndef OPENOS_ARCH_AARCH64_SELFTEST_H
#define OPENOS_ARCH_AARCH64_SELFTEST_H

/*
 * M11-Z aarch64 platform selftest.
 *
 *   Stage 1 : DTB parse (magic / totalsize)
 *   Stage 2 : GICv3 init state
 *   Stage 3 : Timer PPI wiring + software IRQ dispatch
 *   Stage 4 : I2C stub bus xfer
 *   Stage 5 : GT911 probe + poll + input_core hand-off
 *
 * All stages emit "A11.Z stage <n> PASS/FAIL: <label>".
 * Returns 0 on all-green, non-zero otherwise.
 */

int aarch64_platform_selftest_run(void);

#endif /* OPENOS_ARCH_AARCH64_SELFTEST_H */
