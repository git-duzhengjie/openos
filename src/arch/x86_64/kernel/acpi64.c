#include "../include/acpi64.h"
#include "../include/uefi64.h"
#include "../include/handoff64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.3a — ACPI RSDP/XSDT/MADT parser
 *
 * Walks EFI configuration table -> RSDP -> XSDT (or RSDT) -> MADT,
 * enumerating enabled LAPICs, IO-APICs and ISA->GSI overrides. Output is
 * a single static `arch_x86_64_acpi_info_t` exposed via getters declared
 * in acpi64.h.
 *
 * No libc / no dynamic alloc. UEFI identity mapping is assumed to be in
 * effect (true while we have not switched to our own paging structures).
 */

/* ------------------------------------------------------------------ */
/* EFI configuration table GUIDs                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} acpi_efi_guid_t;

typedef struct {
    acpi_efi_guid_t vendor_guid;
    void           *vendor_table;
} acpi_efi_config_entry_t;

static const acpi_efi_guid_t k_acpi20_guid = {
    0x8868e871, 0xe4f1, 0x11d3,
    { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
};

static const acpi_efi_guid_t k_acpi10_guid = {
    0xeb9d2d30, 0x2d88, 0x11d3,
    { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
};

/* ------------------------------------------------------------------ */
/* On-disk ACPI structures (packed)                                    */
/* ------------------------------------------------------------------ */

typedef struct __attribute__((packed)) {
    char     signature[8];      /* "RSD PTR " */
    uint8_t  checksum;          /* sum of first 20 bytes == 0 */
    char     oem_id[6];
    uint8_t  revision;          /* 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    uint32_t rsdt_address;      /* phys */
    /* --- ACPI 2.0+ extension --- */
    uint32_t length;
    uint64_t xsdt_address;      /* phys */
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} acpi_rsdp_t;

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t          lapic_address;   /* phys */
    uint32_t          flags;           /* bit0 = PCAT_COMPAT */
} acpi_madt_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
} acpi_madt_entry_hdr_t;

/* type 0: Processor Local APIC */
typedef struct __attribute__((packed)) {
    acpi_madt_entry_hdr_t hdr;
    uint8_t  processor_id;
    uint8_t  apic_id;
    uint32_t flags;             /* bit0 = enabled, bit1 = online-capable */
} acpi_madt_lapic_t;

/* type 1: IO-APIC */
typedef struct __attribute__((packed)) {
    acpi_madt_entry_hdr_t hdr;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} acpi_madt_ioapic_t;

/* type 2: Interrupt Source Override */
typedef struct __attribute__((packed)) {
    acpi_madt_entry_hdr_t hdr;
    uint8_t  bus;               /* 0 = ISA */
    uint8_t  source;            /* ISA IRQ */
    uint32_t gsi;
    uint16_t flags;
} acpi_madt_irqoverride_t;

/* ------------------------------------------------------------------ */
/* Singleton state                                                     */
/* ------------------------------------------------------------------ */

static arch_x86_64_acpi_info_t g_acpi_info;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static bool acpi_guid_equal(const acpi_efi_guid_t *a, const acpi_efi_guid_t *b)
{
    if (a->data1 != b->data1) return false;
    if (a->data2 != b->data2) return false;
    if (a->data3 != b->data3) return false;
    for (int i = 0; i < 8; ++i) {
        if (a->data4[i] != b->data4[i]) return false;
    }
    return true;
}

static bool acpi_sig_eq8(const char *a, const char *b)
{
    for (int i = 0; i < 8; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static bool acpi_sig_eq4(const char *a, const char *b)
{
    for (int i = 0; i < 4; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static uint8_t acpi_checksum(const void *ptr, uint64_t len)
{
    const uint8_t *p = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (uint64_t i = 0; i < len; ++i) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum;
}

static uint8_t acpi_read_bsp_apic_id(void)
{
    /* CPUID(0x01).EBX[31:24] holds the initial local APIC ID. */
    uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile (
        "cpuid"
        : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx)
    );
    return (uint8_t)((ebx >> 24) & 0xFFu);
}

static void acpi_zero_info(arch_x86_64_acpi_info_t *out)
{
    uint8_t *bp = (uint8_t *)out;
    for (uint64_t i = 0; i < sizeof(*out); ++i) bp[i] = 0;
}

/* ------------------------------------------------------------------ */
/* RSDP discovery via EFI configuration table                          */
/* ------------------------------------------------------------------ */

static const acpi_rsdp_t *acpi_find_rsdp_from_efi(void)
{
    const uefi64_handoff_info_t *handoff = arch_x86_64_uefi_handoff();
    if (handoff == 0 || handoff->system_table == 0) {
        return 0;
    }

    const efi_system_table64_t *st = handoff->system_table;
    const acpi_efi_config_entry_t *cfg =
        (const acpi_efi_config_entry_t *)st->configuration_table;
    if (cfg == 0) {
        return 0;
    }
    uint64_t n = st->number_of_table_entries;

    const acpi_rsdp_t *acpi20 = 0;
    const acpi_rsdp_t *acpi10 = 0;

    for (uint64_t i = 0; i < n; ++i) {
        const acpi_efi_config_entry_t *e = &cfg[i];
        if (acpi_guid_equal(&e->vendor_guid, &k_acpi20_guid)) {
            acpi20 = (const acpi_rsdp_t *)e->vendor_table;
        } else if (acpi_guid_equal(&e->vendor_guid, &k_acpi10_guid)) {
            acpi10 = (const acpi_rsdp_t *)e->vendor_table;
        }
    }

    const acpi_rsdp_t *r = (acpi20 != 0) ? acpi20 : acpi10;
    if (r == 0) return 0;

    if (!acpi_sig_eq8(r->signature, "RSD PTR ")) return 0;
    if (acpi_checksum(r, 20) != 0) return 0;
    if (r->revision >= 2) {
        if (r->length < 20 || r->length > 4096) return 0;
        if (acpi_checksum(r, r->length) != 0) return 0;
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* MADT parsing                                                        */
/* ------------------------------------------------------------------ */

static void acpi_parse_madt(const acpi_madt_t *madt,
                            arch_x86_64_acpi_info_t *out)
{
    out->lapic_address = (uint64_t)madt->lapic_address;

    const uint8_t *p   = (const uint8_t *)madt + sizeof(*madt);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;

    while (p + sizeof(acpi_madt_entry_hdr_t) <= end) {
        const acpi_madt_entry_hdr_t *hdr = (const acpi_madt_entry_hdr_t *)p;
        if (hdr->length == 0) break;
        if (p + hdr->length > end) break;

        switch (hdr->type) {
        case 0: { /* Processor Local APIC */
            const acpi_madt_lapic_t *la = (const acpi_madt_lapic_t *)p;
            if ((la->flags & 0x1u) != 0) {
                if (out->cpu_count < OPENOS_X86_64_ACPI_MAX_CPUS) {
                    out->cpus[out->cpu_count].acpi_processor_id = la->processor_id;
                    out->cpus[out->cpu_count].apic_id           = la->apic_id;
                    out->cpus[out->cpu_count].flags             = la->flags;
                    out->cpu_count++;
                }
            }
            break;
        }
        case 1: { /* IO-APIC */
            const acpi_madt_ioapic_t *io = (const acpi_madt_ioapic_t *)p;
            if (out->ioapic_count < OPENOS_X86_64_ACPI_MAX_IOAPICS) {
                out->ioapics[out->ioapic_count].id       = io->ioapic_id;
                out->ioapics[out->ioapic_count].address  = io->ioapic_address;
                out->ioapics[out->ioapic_count].gsi_base = io->global_system_interrupt_base;
                out->ioapic_count++;
            }
            break;
        }
        case 2: { /* Interrupt Source Override */
            const acpi_madt_irqoverride_t *ov = (const acpi_madt_irqoverride_t *)p;
            if (out->irq_override_count < OPENOS_X86_64_ACPI_MAX_IRQ_OVERRIDES) {
                out->irq_overrides[out->irq_override_count].bus        = ov->bus;
                out->irq_overrides[out->irq_override_count].source_irq = ov->source;
                out->irq_overrides[out->irq_override_count].gsi        = ov->gsi;
                out->irq_overrides[out->irq_override_count].flags      = ov->flags;
                out->irq_override_count++;
            }
            break;
        }
        default:
            /* type 4 NMI / type 5 LAPIC addr override are deferred to G.3b */
            break;
        }

        p += hdr->length;
    }
}

/* ------------------------------------------------------------------ */
/* X/RSDT walk to locate MADT                                          */
/* ------------------------------------------------------------------ */

static const acpi_madt_t *acpi_find_madt(const acpi_rsdp_t *rsdp,
                                         arch_x86_64_acpi_info_t *out)
{
    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        const acpi_sdt_header_t *xsdt =
            (const acpi_sdt_header_t *)(uintptr_t)rsdp->xsdt_address;
        out->xsdt_phys = rsdp->xsdt_address;
        out->rsdt_phys = rsdp->rsdt_address;

        if (!acpi_sig_eq4(xsdt->signature, "XSDT")) return 0;
        if (acpi_checksum(xsdt, xsdt->length) != 0)  return 0;

        uint64_t entries =
            (xsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint64_t);
        const uint64_t *e =
            (const uint64_t *)((const uint8_t *)xsdt + sizeof(acpi_sdt_header_t));
        for (uint64_t i = 0; i < entries; ++i) {
            const acpi_sdt_header_t *h =
                (const acpi_sdt_header_t *)(uintptr_t)e[i];
            if (h == 0) continue;
            if (acpi_sig_eq4(h->signature, "APIC")) {
                if (acpi_checksum(h, h->length) == 0) {
                    return (const acpi_madt_t *)h;
                }
            }
        }
    } else {
        const acpi_sdt_header_t *rsdt =
            (const acpi_sdt_header_t *)(uintptr_t)rsdp->rsdt_address;
        out->rsdt_phys = rsdp->rsdt_address;
        out->xsdt_phys = 0;

        if (!acpi_sig_eq4(rsdt->signature, "RSDT")) return 0;
        if (acpi_checksum(rsdt, rsdt->length) != 0) return 0;

        uint64_t entries =
            (rsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint32_t);
        const uint32_t *e =
            (const uint32_t *)((const uint8_t *)rsdt + sizeof(acpi_sdt_header_t));
        for (uint64_t i = 0; i < entries; ++i) {
            const acpi_sdt_header_t *h =
                (const acpi_sdt_header_t *)(uintptr_t)e[i];
            if (h == 0) continue;
            if (acpi_sig_eq4(h->signature, "APIC")) {
                if (acpi_checksum(h, h->length) == 0) {
                    return (const acpi_madt_t *)h;
                }
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API (declared in acpi64.h)                                   */
/* ------------------------------------------------------------------ */

int arch_x86_64_acpi_init(void)
{
    acpi_zero_info(&g_acpi_info);
    g_acpi_info.bsp_apic_id = acpi_read_bsp_apic_id();

    const acpi_rsdp_t *rsdp = acpi_find_rsdp_from_efi();
    if (rsdp == 0) {
        return 0;
    }
    g_acpi_info.rsdp_phys = (uint64_t)(uintptr_t)rsdp;

    const acpi_madt_t *madt = acpi_find_madt(rsdp, &g_acpi_info);
    if (madt == 0) {
        /* RSDP found but MADT missing: still mark valid for diagnostics. */
        g_acpi_info.valid = 1;
        return 1;
    }
    g_acpi_info.madt_phys = (uint64_t)(uintptr_t)madt;
    acpi_parse_madt(madt, &g_acpi_info);

    g_acpi_info.valid = 1;
    return 1;
}

const arch_x86_64_acpi_info_t *arch_x86_64_acpi_info(void)
{
    if (!g_acpi_info.valid) return 0;
    return &g_acpi_info;
}

uint32_t arch_x86_64_acpi_cpu_count(void)
{
    if (!g_acpi_info.valid) return 0;
    return g_acpi_info.cpu_count;
}

uint8_t arch_x86_64_acpi_bsp_apic_id(void)
{
    return g_acpi_info.bsp_apic_id;
}

uint64_t arch_x86_64_acpi_first_ioapic_base(void)
{
    if (!g_acpi_info.valid) return 0;
    if (g_acpi_info.ioapic_count == 0) return 0;
    return (uint64_t)g_acpi_info.ioapics[0].address;
}

uint32_t arch_x86_64_acpi_first_ioapic_gsi_base(void)
{
    if (!g_acpi_info.valid) return 0;
    if (g_acpi_info.ioapic_count == 0) return 0;
    return g_acpi_info.ioapics[0].gsi_base;
}

int arch_x86_64_acpi_resolve_isa_gsi(uint8_t irq,
                                     uint32_t *out_gsi,
                                     uint16_t *out_flags)
{
    if (!g_acpi_info.valid) return -1;

    /* Linear search override table (typically <= 5 entries on PC). */
    for (uint32_t i = 0; i < g_acpi_info.irq_override_count; ++i) {
        const acpi_irq_override_entry_t *ov = &g_acpi_info.irq_overrides[i];
        /* bus == 0 means ISA per ACPI 6.x */
        if (ov->bus == 0 && ov->source_irq == irq) {
            if (out_gsi)   *out_gsi   = ov->gsi;
            if (out_flags) *out_flags = ov->flags;
            return 1;
        }
    }

    /* Identity fallback (ISA IRQn == GSIn, edge / active-high). */
    if (out_gsi)   *out_gsi   = irq;
    if (out_flags) *out_flags = 0;
    return 0;
}
