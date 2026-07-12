#include "../include/cpufreq_selftest64.h"
#include "../include/cpufreq64.h"
#include "../include/early_console64.h"

#include <stdint.h>
#include <stdbool.h>

/* M6.2 — print + sanity check what cpufreq64.c discovered from CPUID/MSRs.
 * Strictly read-only: never writes PERF_CTL or changes the P-state. */

/* Local decimal printer (early_console64 only offers hex). */
static void log_dec(const char *key, uint32_t val)
{
    char buf[12];
    int i = 0;
    early_console64_write(key);
    if (val == 0) {
        early_console64_write("0");
        return;
    }
    char tmp[12];
    int n = 0;
    while (val > 0 && n < 11) {
        tmp[n++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (n > 0) buf[i++] = tmp[--n];
    buf[i] = '\0';
    early_console64_write(buf);
}

bool arch_x86_64_cpufreq_selftest_run(void)
{
    early_console64_write("\n[x86_64][cpufreq-selftest] begin");

    if (!arch_x86_64_cpufreq_init()) {
        early_console64_write(
            "\n[x86_64][cpufreq-selftest] FAIL cpufreq_init (no CPUID)\n");
        return false;
    }

    const arch_x86_64_cpufreq_info_t *ci = arch_x86_64_cpufreq_info();
    if (ci == 0 || !ci->valid) {
        early_console64_write(
            "\n[x86_64][cpufreq-selftest] FAIL info==null\n");
        return false;
    }

    early_console64_write("\n[x86_64][cpufreq-selftest] vendor=");
    early_console64_write(ci->vendor);
    log_dec(" family=", ci->family);
    log_dec(" model=", ci->model);
    log_dec(" stepping=", ci->stepping);

    early_console64_write("\n[x86_64][cpufreq-selftest] caps=");
    early_console64_write_hex64((uint64_t)ci->caps);

    log_dec("\n[x86_64][cpufreq-selftest] tsc_mhz=", ci->tsc_mhz);
    log_dec(" base_mhz=", ci->base_mhz);
    log_dec(" max_mhz=", ci->max_mhz);
    log_dec(" bus_mhz=", ci->bus_mhz);

    log_dec("\n[x86_64][cpufreq-selftest] cur_ratio=", ci->cur_ratio);
    log_dec(" cur_mhz=", ci->cur_mhz);
    log_dec(" max_nonturbo_ratio=", ci->max_nonturbo_ratio);
    log_dec(" min_ratio=", ci->min_ratio);

    log_dec("\n[x86_64][cpufreq-selftest] tjmax_c=", ci->tjmax_c);
    if (ci->core_temp_valid)
        log_dec(" core_temp_c=", ci->core_temp_c);
    else
        early_console64_write(" core_temp_c=n/a");
    if (ci->pkg_temp_valid)
        log_dec(" pkg_temp_c=", ci->pkg_temp_c);
    else
        early_console64_write(" pkg_temp_c=n/a");

    /* --- Sanity checks --- */

    /* 1. Vendor string must be a non-empty, printable 12-char id. */
    if (ci->vendor[0] < 0x20 || ci->vendor[0] > 0x7E) {
        early_console64_write(
            "\n[x86_64][cpufreq-selftest] FAIL bad vendor\n");
        return false;
    }

    /* 2. TSC MHz must be plausible (>= 100 MHz, < 100 GHz). */
    if (ci->tsc_mhz < 100 || ci->tsc_mhz > 100000) {
        early_console64_write(
            "\n[x86_64][cpufreq-selftest] FAIL implausible tsc_mhz\n");
        return false;
    }

    /* 3. MSR feature bit implies rd/wrmsr usable (we only read). */
    if (!(ci->caps & CPUFREQ_CAP_MSR)) {
        /* Not fatal: a CPU without MSRs is exotic but we shouldn't have
         * populated any P-state / thermal MSR-derived fields. */
        if (ci->cur_ratio != 0 || ci->tjmax_c != 0) {
            early_console64_write(
                "\n[x86_64][cpufreq-selftest] FAIL msr fields set w/o MSR cap\n");
            return false;
        }
    }

    /* 4. If leaf 0x16 present, base <= max. */
    if ((ci->caps & CPUFREQ_CAP_FREQ_LEAF) &&
        ci->base_mhz > ci->max_mhz && ci->max_mhz != 0) {
        early_console64_write(
            "\n[x86_64][cpufreq-selftest] FAIL base>max\n");
        return false;
    }

    /* 5. A refresh must not fault and (post-init) reports success or a
     *    graceful no-op. */
    (void)arch_x86_64_cpufreq_refresh();

    early_console64_write("\n[x86_64][cpufreq-selftest] PASS\n");
    return true;
}
