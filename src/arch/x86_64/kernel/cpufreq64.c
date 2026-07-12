/*
 * cpufreq64.c — M6.2: CPU frequency / thermal observation.
 *
 * See cpufreq64.h for the design rationale. In short: a read-only facility
 * that reports CPU capability bits, P-state ratios (-> MHz) and digital
 * thermal-sensor temperatures (-> Celsius), using CPUID-gated MSR access so
 * it never faults on a minimal virtual CPU (QEMU `-cpu qemu64`).
 */
#include "../include/cpufreq64.h"
#include "../include/tsc64.h"

/* Serial debug print (shared kernel helper). */
extern void arch_x86_64_serial_write(const char *s);

/* -------------------------------------------------------------------- */
/* Low-level x86 helpers (local copies; percpu64.c keeps its own static  */
/* rdmsr/wrmsr, so we provide our own to stay module-independent).       */
/* -------------------------------------------------------------------- */

static inline void cpuid_raw(uint32_t leaf, uint32_t subleaf,
                             uint32_t *a, uint32_t *b,
                             uint32_t *c, uint32_t *d) {
    uint32_t ra, rb, rc, rd;
    __asm__ __volatile__("cpuid"
                         : "=a"(ra), "=b"(rb), "=c"(rc), "=d"(rd)
                         : "a"(leaf), "c"(subleaf));
    if (a) *a = ra;
    if (b) *b = rb;
    if (c) *c = rc;
    if (d) *d = rd;
}

static inline uint64_t rdmsr_local(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/* MSR numbers used here (Intel SDM). */
#define MSR_PLATFORM_INFO        0x000000CEu  /* [15:8] max non-turbo, [47:40] min */
#define MSR_IA32_PERF_STATUS     0x00000198u  /* [15:8] current ratio */
#define MSR_IA32_THERM_STATUS    0x0000019Cu  /* [22:16] readout, bit0 thermal */
#define MSR_IA32_TEMPERATURE_TGT 0x000001A2u  /* [23:16] Tjmax */
#define MSR_IA32_PACKAGE_THERM   0x000001B1u  /* package thermal status */

/* -------------------------------------------------------------------- */

static arch_x86_64_cpufreq_info_t g_info;

const arch_x86_64_cpufreq_info_t *arch_x86_64_cpufreq_info(void) {
    return g_info.valid ? &g_info : (const arch_x86_64_cpufreq_info_t *)0;
}

static int is_intel(void) {
    /* "GenuineIntel" */
    return g_info.vendor[0] == 'G' && g_info.vendor[7] == 'I';
}

/* Refresh time-varying fields: current P-state ratio + temperatures.
 * All guarded by capability bits captured at init. */
int arch_x86_64_cpufreq_refresh(void) {
    if (!g_info.valid) return 0;
    int updated = 0;

    /* Current P-state ratio from IA32_PERF_STATUS. */
    if (g_info.caps & CPUFREQ_CAP_PERF_STATUS) {
        uint64_t ps = rdmsr_local(MSR_IA32_PERF_STATUS);
        g_info.cur_ratio = (uint32_t)((ps >> 8) & 0xFFu);
        g_info.cur_mhz   = g_info.cur_ratio * 100u;
        updated = 1;
    }

    /* Core digital thermal sensor: temp = Tjmax - readout. */
    if ((g_info.caps & CPUFREQ_CAP_THERM_DTS) && g_info.tjmax_c) {
        uint64_t ts = rdmsr_local(MSR_IA32_THERM_STATUS);
        if (ts & 0x80000000u /* reading valid bit31 */) {
            uint32_t readout = (uint32_t)((ts >> 16) & 0x7Fu);
            if (readout <= g_info.tjmax_c) {
                g_info.core_temp_c     = g_info.tjmax_c - readout;
                g_info.core_temp_valid = 1;
            }
        }
        g_info.thermal_alert = (uint8_t)(ts & 0x1u);
        updated = 1;
    }

    /* Package thermal status (if supported). */
    if ((g_info.caps & CPUFREQ_CAP_PKG_THERM) && g_info.tjmax_c) {
        uint64_t pt = rdmsr_local(MSR_IA32_PACKAGE_THERM);
        uint32_t readout = (uint32_t)((pt >> 16) & 0x7Fu);
        if (readout <= g_info.tjmax_c) {
            g_info.pkg_temp_c     = g_info.tjmax_c - readout;
            g_info.pkg_temp_valid = 1;
            updated = 1;
        }
    }

    return updated;
}

int arch_x86_64_cpufreq_init(void) {
    if (g_info.valid) return 1;

    uint32_t a, b, c, d;
    uint32_t caps = 0;

    /* Leaf 0: vendor string + max standard leaf. */
    uint32_t max_std;
    cpuid_raw(0, 0, &max_std, &b, &c, &d);
    *(uint32_t *)&g_info.vendor[0] = b;
    *(uint32_t *)&g_info.vendor[4] = d;
    *(uint32_t *)&g_info.vendor[8] = c;
    g_info.vendor[12] = '\0';

    /* Leaf 1: family/model/stepping + feature flags (TSC, MSR). */
    cpuid_raw(1, 0, &a, &b, &c, &d);
    {
        uint32_t base_family = (a >> 8) & 0xF;
        uint32_t base_model  = (a >> 4) & 0xF;
        uint32_t ext_family  = (a >> 20) & 0xFF;
        uint32_t ext_model   = (a >> 16) & 0xF;
        g_info.stepping = a & 0xF;
        g_info.family   = (base_family == 0xF) ? (base_family + ext_family) : base_family;
        g_info.model    = (base_family == 0x6 || base_family == 0xF)
                              ? ((ext_model << 4) | base_model)
                              : base_model;
    }
    if (d & (1u << 4)) caps |= CPUFREQ_CAP_TSC;
    if (d & (1u << 5)) caps |= CPUFREQ_CAP_MSR;

    /* Leaf 6: thermal / power management. Only present if max_std >= 6. */
    if (max_std >= 6) {
        cpuid_raw(6, 0, &a, &b, &c, &d);
        if (a & (1u << 0)) caps |= CPUFREQ_CAP_THERM_DTS;
        if (a & (1u << 1)) caps |= CPUFREQ_CAP_TURBO;
        if (a & (1u << 2)) caps |= CPUFREQ_CAP_ARAT;
        if (a & (1u << 6)) caps |= CPUFREQ_CAP_PKG_THERM;
        if (a & (1u << 7)) caps |= CPUFREQ_CAP_HWP;
    }

    /* Leaf 0x16: frequency reporting (Skylake+). */
    if (max_std >= 0x16) {
        cpuid_raw(0x16, 0, &a, &b, &c, &d);
        g_info.base_mhz = a & 0xFFFFu;
        g_info.max_mhz  = b & 0xFFFFu;
        g_info.bus_mhz  = c & 0xFFFFu;
        if (g_info.base_mhz || g_info.max_mhz) caps |= CPUFREQ_CAP_FREQ_LEAF;
    }

    /* Extended leaf 0x80000007: invariant TSC. */
    cpuid_raw(0x80000000u, 0, &a, &b, &c, &d);
    uint32_t max_ext = a;
    if (max_ext >= 0x80000007u) {
        cpuid_raw(0x80000007u, 0, &a, &b, &c, &d);
        if (d & (1u << 8)) caps |= CPUFREQ_CAP_INVARIANT_TSC;
    }

    /* Intel-only MSRs are gated behind MSR support + vendor check. QEMU
     * qemu64 reports GenuineAMD-ish behaviour without these MSRs, so we are
     * conservative: only probe on GenuineIntel with the MSR feature bit. */
    if ((caps & CPUFREQ_CAP_MSR) && is_intel()) {
        /* MSR_PLATFORM_INFO: max non-turbo + min ratio. */
        uint64_t pi = rdmsr_local(MSR_PLATFORM_INFO);
        if (pi != 0) {
            g_info.max_nonturbo_ratio = (uint32_t)((pi >> 8) & 0xFFu);
            g_info.min_ratio          = (uint32_t)((pi >> 40) & 0xFFu);
            g_info.max_nonturbo_mhz   = g_info.max_nonturbo_ratio * 100u;
            caps |= CPUFREQ_CAP_PLATFORM_INFO;
        }
        /* IA32_PERF_STATUS assumed readable on Intel with MSR support. */
        caps |= CPUFREQ_CAP_PERF_STATUS;

        /* Tjmax from MSR_TEMPERATURE_TARGET (needed to interpret DTS). */
        if (caps & CPUFREQ_CAP_THERM_DTS) {
            uint64_t tt = rdmsr_local(MSR_IA32_TEMPERATURE_TGT);
            uint32_t tj = (uint32_t)((tt >> 16) & 0xFFu);
            /* Sanity: typical Tjmax is 90..110C. Default to 100 if bogus. */
            g_info.tjmax_c = (tj >= 70 && tj <= 130) ? tj : 100u;
        }
    }

    /* TSC MHz from PIT calibration (always available post tsc_init). */
    {
        uint64_t per_ms = arch_x86_64_tsc_per_ms();  /* ticks / ms */
        g_info.tsc_mhz = (uint32_t)(per_ms / 1000ull); /* ticks/us == MHz */
    }

    g_info.caps  = caps;
    g_info.valid = 1;

    /* One live refresh so the snapshot is immediately useful. */
    arch_x86_64_cpufreq_refresh();

    return 1;
}
