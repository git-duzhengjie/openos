#include "../include/smp_selftest64.h"
#include "../include/smp64.h"
#include "../include/ap_trampoline64.h"
#include "../include/early_console64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.4.1 — SMP topology dump (no AP wakeup). */

static void log_kv(const char *key, uint64_t val)
{
    early_console64_write(key);
    early_console64_write_hex64(val);
}

void arch_x86_64_smp_selftest_run(void)
{
    early_console64_write("\n[x86_64][smp-selftest] begin");

    if (!arch_x86_64_smp_init()) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL smp_init (lapic not ready?)\n");
        return;
    }

    log_kv("\n[x86_64][smp-selftest] bsp_apic_id=", (uint64_t)arch_x86_64_smp_bsp_apic_id());
    log_kv(" cpu_count=", (uint64_t)arch_x86_64_smp_cpu_count());
    log_kv(" ap_count=",  (uint64_t)arch_x86_64_smp_ap_count());
    log_kv(" trampoline_phys=", arch_x86_64_smp_trampoline_phys());

    uint32_t n = arch_x86_64_smp_ap_count();
    for (uint32_t i = 0; i < n; ++i) {
        early_console64_write("\n[x86_64][smp-selftest] ap[");
        early_console64_write_hex64((uint64_t)i);
        early_console64_write("] apic_id=");
        early_console64_write_hex64((uint64_t)arch_x86_64_smp_ap_apic_id(i));
    }

    /* G.4.2: install AP trampoline blob and verify magic. */
    log_kv("\n[x86_64][smp-selftest] tramp_blob_size=", arch_x86_64_ap_trampoline_size());
    if (!arch_x86_64_smp_install_trampoline()) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL trampoline install/verify\n");
        return;
    }
    early_console64_write("\n[x86_64][smp-selftest] trampoline installed @ ");
    early_console64_write_hex64(arch_x86_64_smp_trampoline_phys());

    early_console64_write("\n[x86_64][smp-selftest] PASS\n");
}
