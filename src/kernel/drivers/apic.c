/* ============================================================
 * openos - APIC / IOAPIC discovery
 * ============================================================ */
#include "../include/apic.h"
#include "../include/acpi.h"
#include "../include/serial.h"

#define APIC_BASE_MSR 0x1Bu
#define APIC_BASE_ENABLE 0x800u
#define MADT_ENTRY_LAPIC 0u
#define MADT_ENTRY_IOAPIC 1u

typedef struct acpi_madt {
    acpi_table_header_t header;
    uint32_t lapic_address;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_t;

typedef struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) acpi_madt_entry_header_t;

typedef struct acpi_madt_ioapic {
    acpi_madt_entry_header_t header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed)) acpi_madt_ioapic_t;

static apic_info_t g_apic_info;

static void apic_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                       uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

static uint64_t apic_read_msr(uint32_t msr) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void apic_parse_madt(const acpi_madt_t *madt) {
    const uint8_t *ptr;
    const uint8_t *end;

    if (!madt) return;

    g_apic_info.madt_found = 1;
    g_apic_info.madt_lapic_addr = madt->lapic_address;

    ptr = (const uint8_t *)madt + sizeof(acpi_madt_t);
    end = (const uint8_t *)madt + madt->header.length;
    while (ptr + sizeof(acpi_madt_entry_header_t) <= end) {
        const acpi_madt_entry_header_t *entry = (const acpi_madt_entry_header_t *)ptr;
        if (entry->length < sizeof(acpi_madt_entry_header_t) || ptr + entry->length > end) {
            break;
        }

        if (entry->type == MADT_ENTRY_IOAPIC && entry->length >= sizeof(acpi_madt_ioapic_t)) {
            const acpi_madt_ioapic_t *ioapic = (const acpi_madt_ioapic_t *)ptr;
            if (g_apic_info.ioapic_count == 0) {
                g_apic_info.first_ioapic_id = ioapic->ioapic_id;
                g_apic_info.first_ioapic_addr = ioapic->ioapic_address;
                g_apic_info.first_ioapic_gsi_base = ioapic->global_system_interrupt_base;
            }
            g_apic_info.ioapic_count++;
        }

        ptr += entry->length;
    }
}

const apic_info_t *apic_get_info(void) {
    return &g_apic_info;
}

void apic_init(void) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint64_t apic_base_msr;
    const acpi_madt_t *madt;

    g_apic_info.cpuid_apic = 0;
    g_apic_info.lapic_base = 0;
    g_apic_info.lapic_enabled = 0;
    g_apic_info.madt_found = 0;
    g_apic_info.madt_lapic_addr = 0;
    g_apic_info.ioapic_count = 0;
    g_apic_info.first_ioapic_id = 0;
    g_apic_info.first_ioapic_addr = 0;
    g_apic_info.first_ioapic_gsi_base = 0;

    serial_write("=====================================\n");
    serial_write("APIC / IOAPIC Discovery\n");
    serial_write("=====================================\n");

    apic_cpuid(1, &eax, &ebx, &ecx, &edx);
    g_apic_info.cpuid_apic = (edx & (1u << 9)) ? 1u : 0u;
    if (g_apic_info.cpuid_apic) {
        apic_base_msr = apic_read_msr(APIC_BASE_MSR);
        g_apic_info.lapic_base = (uint32_t)(apic_base_msr & 0xFFFFF000u);
        g_apic_info.lapic_enabled = (apic_base_msr & APIC_BASE_ENABLE) ? 1u : 0u;
    }

    madt = (const acpi_madt_t *)acpi_find_table("APIC");
    apic_parse_madt(madt);

    serial_write("[APIC] CPUID=");
    serial_write_hex(g_apic_info.cpuid_apic);
    serial_write(" LAPIC_BASE=");
    serial_write_hex(g_apic_info.lapic_base);
    serial_write(" ENABLED=");
    serial_write_hex(g_apic_info.lapic_enabled);
    serial_write("\n");

    if (g_apic_info.madt_found) {
        serial_write("[APIC] MADT LAPIC=");
        serial_write_hex(g_apic_info.madt_lapic_addr);
        serial_write(" IOAPIC_COUNT=");
        serial_write_hex(g_apic_info.ioapic_count);
        if (g_apic_info.ioapic_count != 0) {
            serial_write(" FIRST_IOAPIC_ID=");
            serial_write_hex(g_apic_info.first_ioapic_id);
            serial_write(" ADDR=");
            serial_write_hex(g_apic_info.first_ioapic_addr);
            serial_write(" GSI_BASE=");
            serial_write_hex(g_apic_info.first_ioapic_gsi_base);
        }
        serial_write("\n");
    } else {
        serial_write("[APIC] MADT not found\n");
    }

    /* Keep legacy PIC active until APIC interrupt routing is fully installed. */
    serial_write("[APIC] legacy PIC remains active\n");

    serial_write("=====================================\n");
}
