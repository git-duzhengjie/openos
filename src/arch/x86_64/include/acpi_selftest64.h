#ifndef OPENOS_ARCH_X86_64_ACPI_SELFTEST64_H
#define OPENOS_ARCH_X86_64_ACPI_SELFTEST64_H

#include <stdbool.h>

/* Step G.3a — run an ACPI parser self-check.
 *
 * Initializes the ACPI subsystem and prints the resulting summary
 * (RSDP/XSDT/RSDT/MADT phys addrs, BSP APIC id, enabled CPU count,
 * IOAPIC table, ISA->GSI overrides).
 *
 * Returns true when:
 *   - RSDP was located via the EFI configuration table,
 *   - X/RSDT and MADT checksums all matched,
 *   - at least one enabled LAPIC entry was found.
 */
bool arch_x86_64_acpi_selftest_run(void);

#endif /* OPENOS_ARCH_X86_64_ACPI_SELFTEST64_H */
