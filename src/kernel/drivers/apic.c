/* ============================================================
 * openos - APIC / IOAPIC discovery
 * ============================================================ */
#include "../include/apic.h"
#include "../include/acpi.h"
#include "../include/serial.h"
#include "../include/vmm.h"

#define APIC_BASE_MSR 0x1Bu
#define APIC_BASE_ENABLE 0x800u
#define LAPIC_REG_EOI 0xB0u
#define IOAPIC_REGSEL 0x00u
#define IOAPIC_WINDOW 0x10u
#define IOAPIC_REG_VERSION 0x01u
#define IOAPIC_REDIR_BASE 0x10u
#define IOAPIC_DEFAULT_GSI_COUNT 24u
#define MADT_ENTRY_LAPIC 0u
#define MADT_ENTRY_IOAPIC 1u
#define MADT_ENTRY_INTERRUPT_OVERRIDE 2u
#define MADT_ENTRY_X2APIC 9u

typedef struct acpi_madt {
    acpi_table_header_t header;
    uint32_t lapic_address;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_t;

typedef struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) acpi_madt_entry_header_t;

typedef struct acpi_madt_lapic {
    acpi_madt_entry_header_t header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_lapic_t;

typedef struct acpi_madt_x2apic {
    acpi_madt_entry_header_t header;
    uint16_t reserved;
    uint32_t x2apic_id;
    uint32_t flags;
    uint32_t acpi_processor_uid;
} __attribute__((packed)) acpi_madt_x2apic_t;

typedef struct acpi_madt_ioapic {
    acpi_madt_entry_header_t header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed)) acpi_madt_ioapic_t;

typedef struct acpi_madt_interrupt_override {
    acpi_madt_entry_header_t header;
    uint8_t bus;
    uint8_t source_irq;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed)) acpi_madt_interrupt_override_t;

static apic_info_t g_apic_info;

static void apic_add_cpu(uint32_t acpi_processor_id, uint32_t apic_id, uint32_t flags) {
    uint32_t index;

    if (g_apic_info.lapic_cpu_count >= APIC_MAX_CPUS) {
        return;
    }

    index = g_apic_info.lapic_cpu_count++;
    g_apic_info.lapic_cpus[index].acpi_processor_id = acpi_processor_id;
    g_apic_info.lapic_cpus[index].apic_id = apic_id;
    g_apic_info.lapic_cpus[index].flags = flags;
}

static void apic_add_ioapic(uint32_t id, uint32_t addr, uint32_t gsi_base) {
    uint32_t index;

    if (g_apic_info.ioapic_count >= APIC_MAX_IOAPICS) {
        return;
    }

    index = g_apic_info.ioapic_count++;
    g_apic_info.ioapics[index].id = id;
    g_apic_info.ioapics[index].addr = addr;
    g_apic_info.ioapics[index].gsi_base = gsi_base;
    g_apic_info.ioapics[index].gsi_count = IOAPIC_DEFAULT_GSI_COUNT;

    if (index == 0) {
        g_apic_info.first_ioapic_id = id;
        g_apic_info.first_ioapic_addr = addr;
        g_apic_info.first_ioapic_gsi_base = gsi_base;
    }
}

static void apic_add_irq_override(uint32_t bus, uint32_t source_irq,
                                  uint32_t gsi, uint32_t flags) {
    uint32_t index;

    if (g_apic_info.irq_override_count >= APIC_MAX_IRQ_OVERRIDES) {
        return;
    }

    index = g_apic_info.irq_override_count++;
    g_apic_info.irq_overrides[index].bus = bus;
    g_apic_info.irq_overrides[index].source_irq = source_irq;
    g_apic_info.irq_overrides[index].gsi = gsi;
    g_apic_info.irq_overrides[index].flags = flags;
}

static int apic_map_mmio(uint32_t addr) {
    uint32_t page;
    uint32_t pte;
    if (addr == 0) return 0;
    page = addr & PAGE_MASK;
    vmm_map_range(page, page, PAGE_SIZE, VMM_RW);
    pte = vmm_get_mapping(page);
    if ((pte & PTE_PRESENT) == 0) {
        serial_write("[APIC] MMIO map failed, skip addr=");
        serial_write_hex(page);
        serial_write("\n");
        return 0;
    }
    return 1;
}

static uint32_t ioapic_read_at(uint32_t base, uint32_t reg) {
    volatile uint32_t *select;
    volatile uint32_t *window;

    if (base == 0) {
        return 0;
    }

    select = (volatile uint32_t *)(base + IOAPIC_REGSEL);
    window = (volatile uint32_t *)(base + IOAPIC_WINDOW);
    *select = reg;
    return *window;
}

static void ioapic_write_at(uint32_t base, uint32_t reg, uint32_t value) {
    volatile uint32_t *select;
    volatile uint32_t *window;

    if (base == 0) {
        return;
    }

    select = (volatile uint32_t *)(base + IOAPIC_REGSEL);
    window = (volatile uint32_t *)(base + IOAPIC_WINDOW);
    *select = reg;
    *window = value;
}

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

        if (entry->type == MADT_ENTRY_LAPIC && entry->length >= sizeof(acpi_madt_lapic_t)) {
            const acpi_madt_lapic_t *lapic = (const acpi_madt_lapic_t *)ptr;
            apic_add_cpu(lapic->acpi_processor_id, lapic->apic_id, lapic->flags);
        } else if (entry->type == MADT_ENTRY_X2APIC && entry->length >= sizeof(acpi_madt_x2apic_t)) {
            const acpi_madt_x2apic_t *x2apic = (const acpi_madt_x2apic_t *)ptr;
            apic_add_cpu(x2apic->acpi_processor_uid, x2apic->x2apic_id, x2apic->flags);
        } else if (entry->type == MADT_ENTRY_IOAPIC && entry->length >= sizeof(acpi_madt_ioapic_t)) {
            const acpi_madt_ioapic_t *ioapic = (const acpi_madt_ioapic_t *)ptr;
            apic_add_ioapic(ioapic->ioapic_id, ioapic->ioapic_address,
                            ioapic->global_system_interrupt_base);
        } else if (entry->type == MADT_ENTRY_INTERRUPT_OVERRIDE &&
                   entry->length >= sizeof(acpi_madt_interrupt_override_t)) {
            const acpi_madt_interrupt_override_t *override;
            override = (const acpi_madt_interrupt_override_t *)ptr;
            apic_add_irq_override(override->bus, override->source_irq,
                                  override->global_system_interrupt, override->flags);
        }

        ptr += entry->length;
    }
}

static uint32_t apic_convert_madt_flags(uint32_t madt_flags) {
    uint32_t redir_flags = 0;
    uint32_t polarity = madt_flags & 0x3u;
    uint32_t trigger = (madt_flags >> 2) & 0x3u;

    if (polarity == 3u) {
        redir_flags |= IOAPIC_REDIR_POLARITY_LOW;
    }
    if (trigger == 3u) {
        redir_flags |= IOAPIC_REDIR_TRIGGER_LEVEL;
    }
    return redir_flags;
}

static void apic_probe_ioapic_versions(void) {
    uint32_t i;

    for (i = 0; i < g_apic_info.ioapic_count; ++i) {
        uint32_t version;
        if (!apic_map_mmio(g_apic_info.ioapics[i].addr)) {
            g_apic_info.ioapics[i].gsi_count = IOAPIC_DEFAULT_GSI_COUNT;
            continue;
        }
        version = ioapic_read_at(g_apic_info.ioapics[i].addr, IOAPIC_REG_VERSION);
        g_apic_info.ioapics[i].gsi_count = ((version >> 16) & 0xFFu) + 1u;
        if (g_apic_info.ioapics[i].gsi_count == 0) {
            g_apic_info.ioapics[i].gsi_count = IOAPIC_DEFAULT_GSI_COUNT;
        }
    }
}

const apic_info_t *apic_get_info(void) {
    return &g_apic_info;
}

uint32_t lapic_read(uint32_t reg) {
    volatile uint32_t *lapic;

    if (!g_apic_info.lapic_base) {
        return 0;
    }

    lapic = (volatile uint32_t *)(g_apic_info.lapic_base + reg);
    return *lapic;
}

void lapic_write(uint32_t reg, uint32_t value) {
    volatile uint32_t *lapic;

    if (!g_apic_info.lapic_base) {
        return;
    }

    lapic = (volatile uint32_t *)(g_apic_info.lapic_base + reg);
    *lapic = value;
}

void lapic_eoi(void) {
    if (g_apic_info.lapic_enabled) {
        lapic_write(LAPIC_REG_EOI, 0);
    }
}

const apic_ioapic_info_t *ioapic_find_for_gsi(uint32_t gsi) {
    uint32_t i;

    for (i = 0; i < g_apic_info.ioapic_count; ++i) {
        uint32_t base = g_apic_info.ioapics[i].gsi_base;
        uint32_t end = base + g_apic_info.ioapics[i].gsi_count;
        if (gsi >= base && gsi < end) {
            return &g_apic_info.ioapics[i];
        }
    }
    return 0;
}

uint32_t ioapic_irq_to_gsi(uint8_t irq) {
    uint32_t i;

    for (i = 0; i < g_apic_info.irq_override_count; ++i) {
        if (g_apic_info.irq_overrides[i].source_irq == irq) {
            return g_apic_info.irq_overrides[i].gsi;
        }
    }
    return (uint32_t)irq;
}

uint32_t ioapic_irq_flags(uint8_t irq) {
    uint32_t i;

    for (i = 0; i < g_apic_info.irq_override_count; ++i) {
        if (g_apic_info.irq_overrides[i].source_irq == irq) {
            return apic_convert_madt_flags(g_apic_info.irq_overrides[i].flags);
        }
    }
    return 0;
}

uint32_t ioapic_read(uint32_t reg) {
    return ioapic_read_at(g_apic_info.first_ioapic_addr, reg);
}

void ioapic_write(uint32_t reg, uint32_t value) {
    ioapic_write_at(g_apic_info.first_ioapic_addr, reg, value);
}

int ioapic_set_gsi_redirect(uint32_t gsi, uint8_t vector, uint8_t dest_apic_id, uint32_t flags) {
    const apic_ioapic_info_t *ioapic;
    uint32_t local_irq;
    uint32_t index;
    uint32_t low;
    uint32_t high;

    ioapic = ioapic_find_for_gsi(gsi);
    if (!ioapic) {
        return -1;
    }

    local_irq = gsi - ioapic->gsi_base;
    index = IOAPIC_REDIR_BASE + (local_irq * 2u);
    low = ((uint32_t)vector & 0xFFu) | (flags & 0x0001FF00u);
    high = ((uint32_t)dest_apic_id) << 24;
    ioapic_write_at(ioapic->addr, index + 1u, high);
    ioapic_write_at(ioapic->addr, index, low);
    return 0;
}

int ioapic_set_irq_redirect(uint8_t irq, uint8_t vector, uint8_t dest_apic_id, uint32_t flags) {
    uint32_t gsi = ioapic_irq_to_gsi(irq);
    return ioapic_set_gsi_redirect(gsi, vector, dest_apic_id, flags | ioapic_irq_flags(irq));
}

int ioapic_mask_gsi(uint32_t gsi) {
    const apic_ioapic_info_t *ioapic;
    uint32_t local_irq;
    uint32_t index;
    uint32_t low;

    ioapic = ioapic_find_for_gsi(gsi);
    if (!ioapic) {
        return -1;
    }

    local_irq = gsi - ioapic->gsi_base;
    index = IOAPIC_REDIR_BASE + (local_irq * 2u);
    low = ioapic_read_at(ioapic->addr, index);
    ioapic_write_at(ioapic->addr, index, low | IOAPIC_REDIR_MASKED);
    return 0;
}

int ioapic_unmask_gsi(uint32_t gsi) {
    const apic_ioapic_info_t *ioapic;
    uint32_t local_irq;
    uint32_t index;
    uint32_t low;

    ioapic = ioapic_find_for_gsi(gsi);
    if (!ioapic) {
        return -1;
    }

    local_irq = gsi - ioapic->gsi_base;
    index = IOAPIC_REDIR_BASE + (local_irq * 2u);
    low = ioapic_read_at(ioapic->addr, index);
    ioapic_write_at(ioapic->addr, index, low & ~IOAPIC_REDIR_MASKED);
    return 0;
}

int ioapic_mask_irq(uint8_t irq) {
    return ioapic_mask_gsi(ioapic_irq_to_gsi(irq));
}

int ioapic_unmask_irq(uint8_t irq) {
    return ioapic_unmask_gsi(ioapic_irq_to_gsi(irq));
}

void apic_init(void) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint64_t apic_base_msr;
    const acpi_madt_t *madt;
    uint32_t i;

    g_apic_info.cpuid_apic = 0;
    g_apic_info.lapic_base = 0;
    g_apic_info.lapic_enabled = 0;
    g_apic_info.madt_found = 0;
    g_apic_info.madt_lapic_addr = 0;
    g_apic_info.bsp_apic_id = 0;
    g_apic_info.lapic_cpu_count = 0;
    g_apic_info.ioapic_count = 0;
    g_apic_info.irq_override_count = 0;
    g_apic_info.first_ioapic_id = 0;
    g_apic_info.first_ioapic_addr = 0;
    g_apic_info.first_ioapic_gsi_base = 0;

    serial_write("=====================================\n");
    serial_write("APIC / IOAPIC Discovery\n");
    serial_write("=====================================\n");

    apic_cpuid(1, &eax, &ebx, &ecx, &edx);
    g_apic_info.bsp_apic_id = (ebx >> 24) & 0xFFu;
    g_apic_info.cpuid_apic = (edx & (1u << 9)) ? 1u : 0u;
    if (g_apic_info.cpuid_apic) {
        apic_base_msr = apic_read_msr(APIC_BASE_MSR);
        g_apic_info.lapic_base = (uint32_t)(apic_base_msr & 0xFFFFF000u);
        g_apic_info.lapic_enabled = (apic_base_msr & APIC_BASE_ENABLE) ? 1u : 0u;
        if (!apic_map_mmio(g_apic_info.lapic_base)) {
            g_apic_info.lapic_enabled = 0;
        }
    }

    madt = (const acpi_madt_t *)acpi_find_table("APIC");
    apic_parse_madt(madt);
    apic_probe_ioapic_versions();

    serial_write("[APIC] CPUID=");
    serial_write_hex(g_apic_info.cpuid_apic);
    serial_write(" BSP_APIC_ID=");
    serial_write_hex(g_apic_info.bsp_apic_id);
    serial_write(" LAPIC_BASE=");
    serial_write_hex(g_apic_info.lapic_base);
    serial_write(" ENABLED=");
    serial_write_hex(g_apic_info.lapic_enabled);
    serial_write("\n");

    if (g_apic_info.madt_found) {
        serial_write("[APIC] MADT LAPIC=");
        serial_write_hex(g_apic_info.madt_lapic_addr);
        serial_write(" CPU_COUNT=");
        serial_write_hex(g_apic_info.lapic_cpu_count);
        serial_write(" IOAPIC_COUNT=");
        serial_write_hex(g_apic_info.ioapic_count);
        serial_write(" IRQ_OVERRIDES=");
        serial_write_hex(g_apic_info.irq_override_count);
        serial_write("\n");
        for (i = 0; i < g_apic_info.ioapic_count; ++i) {
            serial_write("[IOAPIC] id=");
            serial_write_hex(g_apic_info.ioapics[i].id);
            serial_write(" addr=");
            serial_write_hex(g_apic_info.ioapics[i].addr);
            serial_write(" gsi_base=");
            serial_write_hex(g_apic_info.ioapics[i].gsi_base);
            serial_write(" gsi_count=");
            serial_write_hex(g_apic_info.ioapics[i].gsi_count);
            serial_write("\n");
        }
        for (i = 0; i < g_apic_info.irq_override_count; ++i) {
            serial_write("[IOAPIC] irq_override irq=");
            serial_write_hex(g_apic_info.irq_overrides[i].source_irq);
            serial_write(" gsi=");
            serial_write_hex(g_apic_info.irq_overrides[i].gsi);
            serial_write(" flags=");
            serial_write_hex(g_apic_info.irq_overrides[i].flags);
            serial_write("\n");
        }
    } else {
        serial_write("[APIC] MADT not found\n");
    }

    /* Keep legacy PIC active until APIC interrupt routing is fully installed. */
    serial_write("[APIC] legacy PIC remains active\n");

    serial_write("=====================================\n");
}
