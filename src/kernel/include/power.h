#ifndef POWER_H
#define POWER_H

#include <types.h>

typedef struct power_info {
    uint32_t acpi_available;
    uint32_t fadt_addr;
    uint32_t dsdt_addr;
    uint16_t sci_int;
    uint16_t pm1a_cnt_blk;
    uint16_t pm1b_cnt_blk;
    uint16_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t pm1_cnt_len;
    uint8_t slp_typa;
    uint8_t slp_typb;
    uint8_t s5_available;
} power_info_t;

void power_init(void);
const power_info_t *power_get_info(void);
int power_shutdown(void);
int power_reboot(void);

#endif /* POWER_H */
