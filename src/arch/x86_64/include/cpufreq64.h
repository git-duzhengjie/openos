/*
 * cpufreq64.h — M6.2: CPU frequency / thermal observation.
 *
 * A small, read-only facility that reports CPU power-management state:
 *   - CPUID-derived capability bits (thermal sensor, turbo, invariant TSC,
 *     the 0x16 frequency leaf, HWP, ...).
 *   - Current / base / max P-state ratios (via IA32_PERF_STATUS,
 *     MSR_PLATFORM_INFO) converted to MHz.
 *   - Package / core digital thermal-sensor readings converted to Celsius
 *     (via IA32_THERM_STATUS + MSR_TEMPERATURE_TARGET / Tjmax).
 *
 * Design notes:
 *   - **CPUID-gated MSR access.** Every MSR read is guarded by the CPUID
 *     feature bit that guarantees the MSR exists. This avoids #GP on
 *     minimal virtual CPUs (e.g. QEMU `-cpu qemu64`) which do not implement
 *     the thermal / P-state MSRs. Missing features degrade gracefully to
 *     "unknown" rather than faulting.
 *   - **Strictly read-only.** We never write IA32_PERF_CTL to change the
 *     P-state. Frequency *scaling* (writing PERF_CTL / HWP_REQUEST) is left
 *     for a later milestone; here we only observe.
 *   - Kept in a separate module from acpi64.c / power64.c, matching the
 *     "one small module per feature" convention.
 *   - Reuses the PIT-calibrated TSC frequency (tsc64) as a fallback base
 *     frequency estimate when CPUID leaf 0x16 is unavailable.
 *
 * Milestone breakdown:
 *   M6.2a  CPUID capability probe + info snapshot skeleton (this file).
 *   M6.2b  P-state ratios -> MHz (IA32_PERF_STATUS, MSR_PLATFORM_INFO).
 *   M6.2c  Thermal readings -> Celsius (IA32_THERM_STATUS, Tjmax).
 *   M6.2d  SYS_CPUINFO=481 + openos64_cpuinfo() user ABI.
 */
#ifndef OPENOS_ARCH_X86_64_CPUFREQ64_H
#define OPENOS_ARCH_X86_64_CPUFREQ64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------- */
/* Capability bits (bitmask in arch_x86_64_cpufreq_info_t.caps).         */
/* -------------------------------------------------------------------- */
#define CPUFREQ_CAP_TSC          (1u << 0)  /* CPUID.1:EDX.TSC[4]           */
#define CPUFREQ_CAP_MSR          (1u << 1)  /* CPUID.1:EDX.MSR[5] (rd/wrmsr)*/
#define CPUFREQ_CAP_INVARIANT_TSC (1u << 2) /* CPUID.80000007:EDX[8]        */
#define CPUFREQ_CAP_THERM_DTS    (1u << 3)  /* CPUID.1:EDX.ACPI? no; use 0x6:EAX[0] digital thermal sensor */
#define CPUFREQ_CAP_TURBO        (1u << 4)  /* CPUID.6:EAX[1] Intel Turbo    */
#define CPUFREQ_CAP_ARAT         (1u << 5)  /* CPUID.6:EAX[2] always-running APIC timer */
#define CPUFREQ_CAP_HWP          (1u << 6)  /* CPUID.6:EAX[7] HWP (Speed Shift) */
#define CPUFREQ_CAP_PKG_THERM    (1u << 7)  /* CPUID.6:EAX[6] package thermal   */
#define CPUFREQ_CAP_FREQ_LEAF    (1u << 8)  /* CPUID leaf 0x16 present (base/max/bus MHz) */
#define CPUFREQ_CAP_PLATFORM_INFO (1u << 9) /* MSR_PLATFORM_INFO readable (Intel) */
#define CPUFREQ_CAP_PERF_STATUS  (1u << 10) /* IA32_PERF_STATUS readable          */

/* Snapshot of CPU frequency / thermal state. All MHz fields are 0 when
 * the underlying CPUID leaf / MSR is unavailable ("unknown"). */
typedef struct {
    uint32_t valid;          /* 1 once arch_x86_64_cpufreq_init() succeeded  */
    uint32_t caps;           /* CPUFREQ_CAP_* bitmask                        */

    /* Vendor / brand. */
    char     vendor[13];     /* CPUID.0 vendor string, NUL-terminated       */
    uint32_t family;         /* display family                              */
    uint32_t model;          /* display model                               */
    uint32_t stepping;

    /* CPUID leaf 0x16 frequency reporting (Skylake+; 0 if absent). */
    uint32_t base_mhz;       /* CPUID.16:EAX  processor base frequency (MHz) */
    uint32_t max_mhz;        /* CPUID.16:EBX  max (turbo) frequency    (MHz) */
    uint32_t bus_mhz;        /* CPUID.16:ECX  bus/reference freq       (MHz) */

    /* P-state ratios (Intel; 0 if MSRs unavailable). Filled in M6.2b. */
    uint32_t cur_ratio;      /* IA32_PERF_STATUS[15:8] current ratio        */
    uint32_t max_nonturbo_ratio; /* MSR_PLATFORM_INFO[15:8]                  */
    uint32_t min_ratio;      /* MSR_PLATFORM_INFO[47:40]                    */
    uint32_t cur_mhz;        /* cur_ratio * 100 (approx, 100 MHz bclk)      */
    uint32_t max_nonturbo_mhz;

    /* TSC-derived estimate (always available after tsc64 calibration). */
    uint32_t tsc_mhz;        /* PIT-calibrated TSC frequency (MHz)          */

    /* Thermal readings (Intel DTS; filled in M6.2c). */
    uint32_t tjmax_c;        /* MSR_TEMPERATURE_TARGET[23:16] junction max  */
    uint32_t core_temp_c;    /* current core temperature (Celsius)          */
    uint32_t pkg_temp_c;     /* current package temperature (Celsius)       */
    uint8_t  core_temp_valid;
    uint8_t  pkg_temp_valid;
    uint8_t  thermal_alert;  /* IA32_THERM_STATUS thermal status bit        */
    uint8_t  reserved0;
} arch_x86_64_cpufreq_info_t;

/*
 * Probe CPUID capability leaves and populate the info snapshot's static
 * (non-time-varying) fields: vendor, family/model, caps, base/max/bus MHz,
 * tjmax, tsc_mhz. Time-varying fields (cur_ratio, temperatures) are
 * refreshed by arch_x86_64_cpufreq_refresh().
 *
 * Idempotent; returns 1 on success (always succeeds on a CPUID-capable
 * CPU), 0 only if CPUID itself is unavailable (never on x86_64).
 */
int arch_x86_64_cpufreq_init(void);

/* Refresh the time-varying fields (current P-state ratio, temperatures).
 * Safe to call repeatedly. No-op before init. Returns 1 if any live field
 * was updated, 0 otherwise. */
int arch_x86_64_cpufreq_refresh(void);

/* Read-only snapshot; NULL until arch_x86_64_cpufreq_init() has succeeded. */
const arch_x86_64_cpufreq_info_t *arch_x86_64_cpufreq_info(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_CPUFREQ64_H */
