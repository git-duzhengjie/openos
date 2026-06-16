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

typedef struct acpi_table_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_table_header_t;

void acpi_init(void);
const acpi_rsdp_info_t *acpi_get_rsdp_info(void);
const acpi_table_header_t *acpi_find_table(const char signature[4]);

#endif /* ACPI_H */
