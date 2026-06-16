#ifndef ACPI_H
#define ACPI_H

#include <types.h>

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_RSDP_V1_SIZE 20u
#define ACPI_RSDP_V2_MIN_SIZE 36u

typedef struct acpi_rsdp_info {
    uint32_t found;
    uint8_t revision;
    uint32_t rsdp_addr;
    uint32_t rsdt_addr;
    uint64_t xsdt_addr;
    char oem_id[7];
} acpi_rsdp_info_t;

void acpi_init(void);
const acpi_rsdp_info_t *acpi_get_rsdp_info(void);

#endif /* ACPI_H */
