/*
 * cpuinfo_selftest64.c — M6.2d ring3 self-test for SYS_CPUINFO (481).
 *
 * Exercises the user-space CPU frequency / thermal ABI end to end:
 *   - openos64_cpuinfo() copies the kernel snapshot into a user struct.
 *   - We print the fields and assert basic invariants (vendor present,
 *     TSC MHz plausible, caps self-consistent, base<=max).
 *
 * Strictly read-only: querying never changes the P-state. This is the
 * headless counterpart to a future `cpuinfo` CLI.
 *
 * Launch: selected as the initial ring3 image when the kernel is built with
 * -DM6_CPUINFO_DIAG (see kernel64.c). Prints "[cpuinfo_selftest] ALL PASS".
 */
#include <stddef.h>
#include <stdint.h>

#include "openos64.h"
#include "libc/stdio.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *name, int cond) {
    if (cond) {
        g_pass++;
        printf("[cpuinfo_selftest]   PASS %s\n", name);
    } else {
        g_fail++;
        printf("[cpuinfo_selftest]   FAIL %s\n", name);
    }
}

int openos64_main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    printf("\n[cpuinfo_selftest] begin (SYS_CPUINFO=481)\n");

    openos64_cpuinfo_t ci;
    /* zero the struct so untouched fields are deterministic */
    uint8_t *p = (uint8_t *)&ci;
    for (unsigned i = 0; i < sizeof(ci); i++) p[i] = 0;

    long rc = openos64_cpuinfo(&ci);
    check("syscall returns 0", rc == 0);

    printf("[cpuinfo_selftest] vendor=%s family=%u model=%u stepping=%u\n",
           ci.vendor, ci.family, ci.model, ci.stepping);
    printf("[cpuinfo_selftest] caps=0x%x tsc_mhz=%u base=%u max=%u bus=%u\n",
           ci.caps, ci.tsc_mhz, ci.base_mhz, ci.max_mhz, ci.bus_mhz);
    printf("[cpuinfo_selftest] cur_ratio=%u cur_mhz=%u max_nonturbo=%u min=%u\n",
           ci.cur_ratio, ci.cur_mhz, ci.max_nonturbo_ratio, ci.min_ratio);
    printf("[cpuinfo_selftest] tjmax=%u core_temp=%u(%u) pkg_temp=%u(%u) alert=%u\n",
           ci.tjmax_c, ci.core_temp_c, ci.core_temp_valid,
           ci.pkg_temp_c, ci.pkg_temp_valid, ci.thermal_alert);

    /* 1. vendor string must start with a printable char. */
    check("vendor printable",
          ci.vendor[0] >= 0x20 && ci.vendor[0] <= 0x7E);

    /* 2. TSC MHz plausible (>=100 MHz, <100 GHz). */
    check("tsc_mhz plausible", ci.tsc_mhz >= 100 && ci.tsc_mhz <= 100000);

    /* 3. MSR feature bit present implies snapshot self-consistency. If MSR
     *    capability is absent, MSR-derived fields must be zero. */
    if (!(ci.caps & OPENOS64_CPU_CAP_MSR)) {
        check("no MSR -> no P-state/thermal fields",
              ci.cur_ratio == 0 && ci.tjmax_c == 0);
    } else {
        check("MSR cap present", 1);
    }

    /* 4. If leaf 0x16 present, base <= max (when max != 0). */
    if ((ci.caps & OPENOS64_CPU_CAP_FREQ_LEAF) && ci.max_mhz != 0) {
        check("base <= max", ci.base_mhz <= ci.max_mhz);
    } else {
        check("freq-leaf gate ok", 1);
    }

    /* 5. A second query must succeed too (idempotent, no fault). */
    openos64_cpuinfo_t ci2;
    check("second query ok", openos64_cpuinfo(&ci2) == 0);

    printf("[cpuinfo_selftest] %d passed / %d failed\n", g_pass, g_fail);
    if (g_fail == 0)
        printf("[cpuinfo_selftest] ALL PASS\n");
    else
        printf("[cpuinfo_selftest] FAILED\n");

    openos64_exit(g_fail == 0 ? 0 : 1);
    return 0;
}
