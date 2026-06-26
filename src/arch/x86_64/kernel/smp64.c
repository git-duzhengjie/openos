#include "../include/smp64.h"
#include "../include/acpi64.h"
#include "../include/lapic64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.4.1 — SMP topology snapshot.
 * No AP wakeup is performed here; this only consumes ACPI MADT data and
 * captures the BSP apic_id at runtime so later stages have authoritative
 * input. See smp64.h for the staged plan. */

typedef struct smp_state {
    bool     ready;
    uint8_t  bsp_apic_id;
    uint32_t cpu_count;          /* enabled CPUs incl. BSP */
    uint32_t ap_count;
    uint8_t  ap_apic_ids[OPENOS_X86_64_SMP_MAX_CPUS];
} smp_state_t;

static smp_state_t g_smp;

bool arch_x86_64_smp_init(void) {
    g_smp.ready = false;
    g_smp.bsp_apic_id = 0;
    g_smp.cpu_count = 0;
    g_smp.ap_count = 0;

    /* Capture BSP apic_id from LAPIC ID register (must be after lapic init). */
    if (!arch_x86_64_lapic_is_ready()) {
        return false;
    }
    g_smp.bsp_apic_id = (uint8_t)arch_x86_64_lapic_id();

    const arch_x86_64_acpi_info_t *acpi = arch_x86_64_acpi_info();
    if (acpi == 0 || acpi->cpu_count == 0) {
        /* No ACPI: still mark ready as a UP system. */
        g_smp.cpu_count = 1;
        g_smp.ap_count = 0;
        g_smp.ready = true;
        return true;
    }

    uint32_t enabled = 0;
    uint32_t ap_idx = 0;
    for (uint32_t i = 0; i < acpi->cpu_count; ++i) {
        const acpi_cpu_entry_t *cpu = &acpi->cpus[i];
        if ((cpu->flags & 0x1u) == 0u) {
            continue;
        }
        enabled++;
        if (cpu->apic_id == g_smp.bsp_apic_id) {
            continue;
        }
        if (ap_idx < OPENOS_X86_64_SMP_MAX_CPUS) {
            g_smp.ap_apic_ids[ap_idx++] = cpu->apic_id;
        }
    }
    g_smp.cpu_count = enabled;
    g_smp.ap_count  = ap_idx;
    g_smp.ready = true;
    return true;
}

bool arch_x86_64_smp_is_ready(void) { return g_smp.ready; }
uint8_t  arch_x86_64_smp_bsp_apic_id(void) { return g_smp.bsp_apic_id; }
uint32_t arch_x86_64_smp_ap_count(void) { return g_smp.ap_count; }
uint32_t arch_x86_64_smp_cpu_count(void) { return g_smp.cpu_count; }

uint8_t arch_x86_64_smp_ap_apic_id(uint32_t index) {
    if (index >= g_smp.ap_count) {
        return 0xFFu;
    }
    return g_smp.ap_apic_ids[index];
}

uint64_t arch_x86_64_smp_trampoline_phys(void) {
    /* G.4.1: fixed low-1MB landing zone. We do not actually write to this
     * page yet; G.4.2 will install the trampoline blob and validate the
     * UEFI memory map marks it as conventional memory. */
    return OPENOS_X86_64_SMP_TRAMPOLINE_PHYS;
}
