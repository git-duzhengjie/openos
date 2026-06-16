/* ============================================================
 * openos - ACPI power management
 * ============================================================ */
#include "../include/power.h"
#include "../include/acpi.h"
#include "../include/io.h"
#include "../include/serial.h"
#include "../include/string.h"

#define FADT_SIGNATURE "FACP"
#define ACPI_PM1_SCI_EN 0x0001u
#define ACPI_PM1_SLP_EN 0x2000u
#define KBC_STATUS_PORT 0x64u
#define KBC_RESET_CMD   0xFEu

typedef struct acpi_fadt {
    acpi_table_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved0;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t mon_alarm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved1;
    uint32_t flags;
    uint8_t reset_reg[12];
    uint8_t reset_value;
    uint8_t reserved2[3];
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
} __attribute__((packed)) acpi_fadt_t;

static power_info_t g_power_info;

static uint32_t power_phys32_from64(uint64_t value) {
    if ((value >> 32) != 0) return 0;
    return (uint32_t)value;
}

static int power_is_port16(uint32_t port) {
    return port != 0 && port <= 0xFFFFu;
}

static int power_find_s5(const uint8_t *aml, uint32_t len, uint8_t *typ_a, uint8_t *typ_b) {
    uint32_t i;
    for (i = 0; i + 8 < len; i++) {
        if (aml[i] == '_' && aml[i + 1] == 'S' && aml[i + 2] == '5' && aml[i + 3] == '_') {
            uint32_t p = i + 4;
            uint8_t values[2];
            uint32_t found = 0;

            if (p < len && aml[p] == 0x12) { /* PackageOp */
                p++;
                if (p >= len) return 0;
                if ((aml[p] & 0xC0u) == 0) p++;
                else {
                    uint8_t count = (uint8_t)((aml[p] >> 6) & 0x3u);
                    p += count + 1u;
                }
                if (p < len) p++; /* element count */
            }

            while (p < len && found < 2) {
                uint8_t op = aml[p++];
                if (op == 0x0A && p < len) { /* BytePrefix */
                    values[found++] = aml[p++];
                } else if (op == 0x0B && p + 1 < len) { /* WordPrefix */
                    values[found++] = aml[p];
                    p += 2;
                } else if (op <= 0x0F) {
                    values[found++] = op;
                } else if (op == 0x00 || op == 0x01) {
                    values[found++] = op;
                } else if (op == 0x08 || op == 0x5B) {
                    break;
                }
            }

            if (found >= 2) {
                *typ_a = values[0];
                *typ_b = values[1];
                return 1;
            }
        }
    }
    return 0;
}

const power_info_t *power_get_info(void) {
    return &g_power_info;
}

void power_init(void) {
    const acpi_table_header_t *hdr;
    const acpi_fadt_t *fadt;
    uint32_t dsdt_addr;

    memset(&g_power_info, 0, sizeof(g_power_info));
    hdr = acpi_find_table(FADT_SIGNATURE);
    if (!hdr || hdr->length < 76u) {
        serial_write("[POWER] FADT not found\n");
        return;
    }

    fadt = (const acpi_fadt_t *)hdr;
    g_power_info.acpi_available = 1;
    g_power_info.fadt_addr = (uint32_t)hdr;
    g_power_info.sci_int = fadt->sci_int;
    g_power_info.smi_cmd = (uint16_t)fadt->smi_cmd;
    g_power_info.acpi_enable = fadt->acpi_enable;
    g_power_info.pm1a_cnt_blk = (uint16_t)fadt->pm1a_cnt_blk;
    g_power_info.pm1b_cnt_blk = (uint16_t)fadt->pm1b_cnt_blk;
    g_power_info.pm1_cnt_len = fadt->pm1_cnt_len;

    dsdt_addr = fadt->dsdt;
    if (hdr->length >= 148u && fadt->x_dsdt != 0) {
        uint32_t x_dsdt = power_phys32_from64(fadt->x_dsdt);
        if (x_dsdt != 0) dsdt_addr = x_dsdt;
    }
    g_power_info.dsdt_addr = dsdt_addr;

    if (dsdt_addr != 0) {
        const acpi_table_header_t *dsdt = acpi_map_table(dsdt_addr);
        if (dsdt && memcmp(dsdt->signature, "DSDT", 4) == 0 &&
            dsdt->length > sizeof(acpi_table_header_t)) {
            const uint8_t *aml = (const uint8_t *)dsdt + sizeof(acpi_table_header_t);
            uint32_t aml_len = dsdt->length - (uint32_t)sizeof(acpi_table_header_t);
            g_power_info.s5_available = (uint8_t)power_find_s5(aml, aml_len,
                                                               &g_power_info.slp_typa,
                                                               &g_power_info.slp_typb);
        }
    }

    serial_write("[POWER] ACPI power ready\n");
}

int power_shutdown(void) {
    uint16_t pm1a;
    uint16_t pm1b;
    uint16_t value_a;
    uint16_t value_b;
    uint32_t i;

    if (!g_power_info.acpi_available || !g_power_info.s5_available ||
        !power_is_port16(g_power_info.pm1a_cnt_blk)) {
        return -1;
    }

    if (g_power_info.smi_cmd != 0 && g_power_info.acpi_enable != 0) {
        outb(g_power_info.smi_cmd, g_power_info.acpi_enable);
        for (i = 0; i < 100000u; i++) io_wait();
    }

    pm1a = g_power_info.pm1a_cnt_blk;
    pm1b = g_power_info.pm1b_cnt_blk;
    value_a = (uint16_t)(ACPI_PM1_SCI_EN | ((uint16_t)g_power_info.slp_typa << 10) | ACPI_PM1_SLP_EN);
    value_b = (uint16_t)(ACPI_PM1_SCI_EN | ((uint16_t)g_power_info.slp_typb << 10) | ACPI_PM1_SLP_EN);

    outw(pm1a, value_a);
    if (power_is_port16(pm1b)) outw(pm1b, value_b);
    for (;;) __asm__ volatile ("hlt");
}

int power_reboot(void) {
    uint32_t i;
    for (i = 0; i < 10000u; i++) {
        if ((inb(KBC_STATUS_PORT) & 0x02u) == 0) break;
        io_wait();
    }
    outb(KBC_STATUS_PORT, KBC_RESET_CMD);
    for (;;) __asm__ volatile ("hlt");
}
