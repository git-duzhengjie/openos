/*
 * acpi64.h — Step G.3a: ACPI RSDP / XSDT / RSDT / MADT parser.
 *
 * Goals (G.3a, prerequisite for SMP AP bring-up):
 *   - Walk the EFI configuration table (still readable post-ExitBootServices)
 *     and find the ACPI 2.0 RSDP (fall back to ACPI 1.0).
 *   - Validate RSDP checksum, locate XSDT (preferred) or RSDT.
 *   - Iterate XSDT/RSDT entries, find MADT (a.k.a. APIC, sig "APIC").
 *   - Parse MADT entries:
 *       * Type 0  Processor Local APIC  -> collect (acpi_id, apic_id, flags)
 *       * Type 1  I/O APIC              -> collect (id, address, gsi_base)
 *       * Type 2  Interrupt Override    -> collect ISA->GSI overrides
 *   - Expose findings via `arch_x86_64_acpi_info_t` and a few getters.
 *
 * G.3b will consume cpu_apic_ids[] to fire INIT-SIPI-SIPI at every
 * APIC ID != bsp_apic_id.
 */
#ifndef OPENOS_ARCH_X86_64_ACPI64_H
#define OPENOS_ARCH_X86_64_ACPI64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENOS_X86_64_ACPI_MAX_CPUS         32u
#define OPENOS_X86_64_ACPI_MAX_IOAPICS       4u
#define OPENOS_X86_64_ACPI_MAX_IRQ_OVERRIDES 16u

typedef struct {
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;       /* bit0 = enabled, bit1 = online-capable */
} acpi_cpu_entry_t;

typedef struct {
    uint8_t  id;
    uint32_t address;
    uint32_t gsi_base;
} acpi_ioapic_entry_t;

typedef struct {
    uint8_t  bus;          /* always 0 = ISA */
    uint8_t  source_irq;
    uint32_t gsi;
    uint16_t flags;
} acpi_irq_override_entry_t;

typedef struct {
    uint32_t valid;        /* 1 once init succeeded */
    uint32_t cpu_count;
    uint32_t ioapic_count;
    uint32_t irq_override_count;
    uint64_t lapic_address; /* MADT-declared LAPIC MMIO base */
    uint8_t  bsp_apic_id;   /* read from CPUID(0x01).EBX[31:24] at init time */

    acpi_cpu_entry_t         cpus[OPENOS_X86_64_ACPI_MAX_CPUS];
    acpi_ioapic_entry_t      ioapics[OPENOS_X86_64_ACPI_MAX_IOAPICS];
    acpi_irq_override_entry_t irq_overrides[OPENOS_X86_64_ACPI_MAX_IRQ_OVERRIDES];

    /* raw RSDP / table pointers retained for diagnostics */
    uint64_t rsdp_phys;
    uint64_t xsdt_phys;
    uint64_t rsdt_phys;
    uint64_t madt_phys;
} arch_x86_64_acpi_info_t;

int arch_x86_64_acpi_init(void);
const arch_x86_64_acpi_info_t *arch_x86_64_acpi_info(void);
uint32_t arch_x86_64_acpi_cpu_count(void);
uint8_t  arch_x86_64_acpi_bsp_apic_id(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_ACPI64_H */
