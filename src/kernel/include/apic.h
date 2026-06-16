#ifndef APIC_H
#define APIC_H

#include <types.h>

typedef struct apic_info {
    uint32_t cpuid_apic;
    uint32_t lapic_base;
    uint32_t lapic_enabled;
    uint32_t madt_found;
    uint32_t madt_lapic_addr;
    uint32_t ioapic_count;
    uint32_t first_ioapic_id;
    uint32_t first_ioapic_addr;
    uint32_t first_ioapic_gsi_base;
} apic_info_t;

void apic_init(void);
const apic_info_t *apic_get_info(void);

uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t value);
void lapic_eoi(void);

uint32_t ioapic_read(uint32_t reg);
void ioapic_write(uint32_t reg, uint32_t value);
int ioapic_set_irq_redirect(uint8_t irq, uint8_t vector, uint8_t dest_apic_id, uint32_t flags);
int ioapic_mask_irq(uint8_t irq);
int ioapic_unmask_irq(uint8_t irq);

#endif /* APIC_H */
