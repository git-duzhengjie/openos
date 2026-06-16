#ifndef APIC_H
#define APIC_H

#include <types.h>

#define APIC_MAX_CPUS 32u
#define APIC_MAX_IOAPICS 8u
#define APIC_MAX_IRQ_OVERRIDES 16u

#define IOAPIC_REDIR_MASKED        (1u << 16)
#define IOAPIC_REDIR_POLARITY_LOW  (1u << 13)
#define IOAPIC_REDIR_TRIGGER_LEVEL (1u << 15)

typedef struct apic_cpu_info {
    uint32_t acpi_processor_id;
    uint32_t apic_id;
    uint32_t flags;
} apic_cpu_info_t;

typedef struct apic_ioapic_info {
    uint32_t id;
    uint32_t addr;
    uint32_t gsi_base;
    uint32_t gsi_count;
} apic_ioapic_info_t;

typedef struct apic_irq_override_info {
    uint32_t bus;
    uint32_t source_irq;
    uint32_t gsi;
    uint32_t flags;
} apic_irq_override_info_t;

typedef struct apic_info {
    uint32_t cpuid_apic;
    uint32_t lapic_base;
    uint32_t lapic_enabled;
    uint32_t madt_found;
    uint32_t madt_lapic_addr;
    uint32_t bsp_apic_id;
    uint32_t lapic_cpu_count;
    apic_cpu_info_t lapic_cpus[APIC_MAX_CPUS];
    uint32_t ioapic_count;
    apic_ioapic_info_t ioapics[APIC_MAX_IOAPICS];
    uint32_t irq_override_count;
    apic_irq_override_info_t irq_overrides[APIC_MAX_IRQ_OVERRIDES];
    uint32_t first_ioapic_id;
    uint32_t first_ioapic_addr;
    uint32_t first_ioapic_gsi_base;
} apic_info_t;

void apic_init(void);
const apic_info_t *apic_get_info(void);

uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t value);
void lapic_eoi(void);

const apic_ioapic_info_t *ioapic_find_for_gsi(uint32_t gsi);
uint32_t ioapic_irq_to_gsi(uint8_t irq);
uint32_t ioapic_irq_flags(uint8_t irq);
uint32_t ioapic_read(uint32_t reg);
void ioapic_write(uint32_t reg, uint32_t value);
int ioapic_set_gsi_redirect(uint32_t gsi, uint8_t vector, uint8_t dest_apic_id, uint32_t flags);
int ioapic_set_irq_redirect(uint8_t irq, uint8_t vector, uint8_t dest_apic_id, uint32_t flags);
int ioapic_mask_gsi(uint32_t gsi);
int ioapic_unmask_gsi(uint32_t gsi);
int ioapic_mask_irq(uint8_t irq);
int ioapic_unmask_irq(uint8_t irq);

#endif /* APIC_H */
