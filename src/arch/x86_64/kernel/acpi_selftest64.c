#include "../include/acpi_selftest64.h"
#include "../include/acpi64.h"
#include "../include/early_console64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.3a — print + sanity check what acpi64.c discovered. */

static void log_kv(const char *key, uint64_t val)
{
    early_console64_write(key);
    early_console64_write_hex64(val);
}

bool arch_x86_64_acpi_selftest_run(void)
{
    early_console64_write("\n[x86_64][acpi-selftest] begin");

    if (!arch_x86_64_acpi_init()) {
        early_console64_write("\n[x86_64][acpi-selftest] FAIL acpi_init (no RSDP)\n");
        return false;
    }

    const arch_x86_64_acpi_info_t *info = arch_x86_64_acpi_info();
    if (info == 0) {
        early_console64_write("\n[x86_64][acpi-selftest] FAIL info==null\n");
        return false;
    }

    log_kv("\n[x86_64][acpi-selftest] rsdp_phys=", info->rsdp_phys);
    log_kv(" xsdt=", info->xsdt_phys);
    log_kv(" rsdt=", info->rsdt_phys);
    log_kv(" madt=", info->madt_phys);
    log_kv(" lapic_base=", info->lapic_address);
    log_kv(" lapic_addr_override=",
           (uint64_t)info->lapic_addr_override_present);
    log_kv(" bsp_apic_id=", (uint64_t)info->bsp_apic_id);

    log_kv("\n[x86_64][acpi-selftest] cpu_count=", info->cpu_count);
    for (uint32_t i = 0; i < info->cpu_count; ++i) {
        early_console64_write(" cpu");
        early_console64_write_hex64(i);
        early_console64_write("=apic");
        early_console64_write_hex64(info->cpus[i].apic_id);
    }

    log_kv("\n[x86_64][acpi-selftest] ioapic_count=", info->ioapic_count);
    for (uint32_t i = 0; i < info->ioapic_count; ++i) {
        early_console64_write(" io[");
        early_console64_write_hex64(i);
        early_console64_write("]=");
        early_console64_write_hex64(info->ioapics[i].address);
        early_console64_write(",gsi=");
        early_console64_write_hex64(info->ioapics[i].gsi_base);
    }

    log_kv("\n[x86_64][acpi-selftest] irq_override_count=",
           info->irq_override_count);
    for (uint32_t i = 0; i < info->irq_override_count; ++i) {
        early_console64_write(" ovr irq");
        early_console64_write_hex64(info->irq_overrides[i].source_irq);
        early_console64_write("->gsi");
        early_console64_write_hex64(info->irq_overrides[i].gsi);
    }

    if (info->cpu_count == 0) {
        early_console64_write("\n[x86_64][acpi-selftest] FAIL cpu_count==0\n");
        return false;
    }
    if (info->ioapic_count == 0) {
        early_console64_write("\n[x86_64][acpi-selftest] WARN ioapic_count==0");
        /* not fatal */
    }
    if (info->lapic_address == 0) {
        early_console64_write("\n[x86_64][acpi-selftest] FAIL lapic_address==0\n");
        return false;
    }

    early_console64_write("\n[x86_64][acpi-selftest] PASS\n");
    return true;
}
