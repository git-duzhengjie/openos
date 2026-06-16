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

#endif /* APIC_H */
