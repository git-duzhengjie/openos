/* ============================================================
 * openos - ACPI discovery
 * ============================================================ */
#include "../include/acpi.h"
#include "../include/serial.h"

#define ACPI_EBDA_SEG_PTR ((const uint16_t *)0x0000040Eu)
#define ACPI_BIOS_ROM_START 0x000E0000u
#define ACPI_BIOS_ROM_END   0x00100000u
#define ACPI_SCAN_ALIGN     16u

typedef struct acpi_rsdp_v1 {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) acpi_rsdp_v1_t;

typedef struct acpi_rsdp_v2 {
    acpi_rsdp_v1_t first;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_v2_t;

static acpi_rsdp_info_t g_acpi_info;

static int acpi_memcmp(const char *a, const char *b, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)((unsigned char)a[i] - (unsigned char)b[i]);
    }
    return 0;
}

static uint32_t acpi_phys32_from64(uint64_t value) {
    if ((value >> 32) != 0) {
        return 0;
    }
    return (uint32_t)value;
}

static uint8_t acpi_checksum(const uint8_t *ptr, uint32_t len) {
    uint8_t sum = 0;
    uint32_t i;
    for (i = 0; i < len; i++) {
        sum = (uint8_t)(sum + ptr[i]);
    }
    return sum;
}

static const acpi_rsdp_v1_t *acpi_validate_rsdp(uint32_t addr) {
    const acpi_rsdp_v1_t *rsdp = (const acpi_rsdp_v1_t *)addr;
    const acpi_rsdp_v2_t *rsdp2;

    if (acpi_memcmp(rsdp->signature, ACPI_RSDP_SIGNATURE, 8) != 0) {
        return 0;
    }
    if (acpi_checksum((const uint8_t *)rsdp, ACPI_RSDP_V1_SIZE) != 0) {
        return 0;
    }

    if (rsdp->revision >= 2) {
        rsdp2 = (const acpi_rsdp_v2_t *)addr;
        if (rsdp2->length >= ACPI_RSDP_V2_MIN_SIZE &&
            acpi_checksum((const uint8_t *)rsdp2, rsdp2->length) != 0) {
            return 0;
        }
    }

    return rsdp;
}

static const acpi_rsdp_v1_t *acpi_scan_range(uint32_t start, uint32_t end) {
    uint32_t addr;
    for (addr = start; addr + ACPI_RSDP_V1_SIZE <= end; addr += ACPI_SCAN_ALIGN) {
        const acpi_rsdp_v1_t *rsdp = acpi_validate_rsdp(addr);
        if (rsdp) return rsdp;
    }
    return 0;
}

static void acpi_copy_oem_id(char out[7], const char in[6]) {
    uint32_t i;
    for (i = 0; i < 6; i++) {
        out[i] = in[i];
    }
    out[6] = '\0';
}

const acpi_rsdp_info_t *acpi_get_rsdp_info(void) {
    return &g_acpi_info;
}

static const acpi_table_header_t *acpi_validate_table(uint32_t addr) {
    const acpi_table_header_t *hdr;
    if (addr == 0) return 0;
    hdr = (const acpi_table_header_t *)addr;
    if (hdr->length < sizeof(acpi_table_header_t)) return 0;
    if (acpi_checksum((const uint8_t *)hdr, hdr->length) != 0) return 0;
    return hdr;
}

static const acpi_table_header_t *acpi_find_in_rsdt(const acpi_table_header_t *rsdt,
                                                    const char signature[4]) {
    uint32_t count;
    uint32_t i;
    const uint32_t *entries;
    if (!rsdt) return 0;
    count = (rsdt->length - sizeof(acpi_table_header_t)) / sizeof(uint32_t);
    entries = (const uint32_t *)((const uint8_t *)rsdt + sizeof(acpi_table_header_t));
    for (i = 0; i < count; i++) {
        const acpi_table_header_t *hdr = acpi_validate_table(entries[i]);
        if (hdr && acpi_memcmp(hdr->signature, signature, 4) == 0) return hdr;
    }
    return 0;
}

static const acpi_table_header_t *acpi_find_in_xsdt(const acpi_table_header_t *xsdt,
                                                    const char signature[4]) {
    uint32_t count;
    uint32_t i;
    const uint64_t *entries;
    if (!xsdt) return 0;
    count = (xsdt->length - sizeof(acpi_table_header_t)) / sizeof(uint64_t);
    entries = (const uint64_t *)((const uint8_t *)xsdt + sizeof(acpi_table_header_t));
    for (i = 0; i < count; i++) {
        const acpi_table_header_t *hdr = acpi_validate_table(acpi_phys32_from64(entries[i]));
        if (hdr && acpi_memcmp(hdr->signature, signature, 4) == 0) return hdr;
    }
    return 0;
}

const acpi_table_header_t *acpi_find_table(const char signature[4]) {
    const acpi_table_header_t *table = 0;
    if (!g_acpi_info.found) return 0;
    if (g_acpi_info.xsdt_addr != 0) {
        table = acpi_find_in_xsdt(acpi_validate_table(acpi_phys32_from64(g_acpi_info.xsdt_addr)), signature);
        if (table) return table;
    }
    return acpi_find_in_rsdt(acpi_validate_table(g_acpi_info.rsdt_addr), signature);
}

void acpi_init(void) {
    const acpi_rsdp_v1_t *rsdp = 0;
    uint32_t ebda_seg = (uint32_t)(*ACPI_EBDA_SEG_PTR);
    uint32_t ebda_addr = ebda_seg << 4;

    serial_write("=====================================\n");
    serial_write("ACPI Discovery\n");
    serial_write("=====================================\n");

    g_acpi_info.found = 0;
    g_acpi_info.revision = 0;
    g_acpi_info.rsdp_addr = 0;
    g_acpi_info.rsdt_addr = 0;
    g_acpi_info.xsdt_addr = 0;
    acpi_copy_oem_id(g_acpi_info.oem_id, "      ");

    if (ebda_addr >= 0x80000u && ebda_addr < ACPI_BIOS_ROM_START) {
        rsdp = acpi_scan_range(ebda_addr, ebda_addr + 1024u);
    }
    if (!rsdp) {
        rsdp = acpi_scan_range(ACPI_BIOS_ROM_START, ACPI_BIOS_ROM_END);
    }

    if (!rsdp) {
        serial_write("[ACPI] RSDP not found\n");
        return;
    }

    g_acpi_info.found = 1;
    g_acpi_info.revision = rsdp->revision;
    g_acpi_info.rsdp_addr = (uint32_t)rsdp;
    g_acpi_info.rsdt_addr = rsdp->rsdt_address;
    acpi_copy_oem_id(g_acpi_info.oem_id, rsdp->oem_id);

    if (rsdp->revision >= 2) {
        const acpi_rsdp_v2_t *rsdp2 = (const acpi_rsdp_v2_t *)rsdp;
        if (rsdp2->length >= ACPI_RSDP_V2_MIN_SIZE) {
            g_acpi_info.xsdt_addr = rsdp2->xsdt_address;
        }
    }

    serial_write("[ACPI] RSDP @ ");
    serial_write_hex(g_acpi_info.rsdp_addr);
    serial_write(" OEM=");
    serial_write(g_acpi_info.oem_id);
    serial_write(" REV=");
    serial_write_hex((uint32_t)g_acpi_info.revision);
    serial_write(" RSDT=");
    serial_write_hex(g_acpi_info.rsdt_addr);
    serial_write(" XSDT_LO=");
    serial_write_hex((uint32_t)g_acpi_info.xsdt_addr);
    serial_write("\n");
    serial_write("=====================================\n");
}
