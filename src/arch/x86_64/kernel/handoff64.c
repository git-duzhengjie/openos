#include "handoff64.h"

#include "arch64.h"
#include "early_console64.h"
#include "pmm64.h"

extern char __kernel64_start[];
extern char __kernel64_end[];

static openos_bootinfo_t g_openos_bootinfo;
static uint64_t g_last_memory_region_count;
static uint64_t g_last_usable_bytes;

static uint32_t bootinfo_memory_type_from_uefi(uint32_t uefi_type)
{
    switch (uefi_type) {
    case 7:  /* EfiConventionalMemory */
        return OPENOS_BOOTINFO_MEMORY_USABLE;
    case 9:  /* EfiACPIReclaimMemory */
        return OPENOS_BOOTINFO_MEMORY_ACPI_RECLAIMABLE;
    case 10: /* EfiACPIMemoryNVS */
        return OPENOS_BOOTINFO_MEMORY_ACPI_NVS;
    case 12: /* EfiPersistentMemory */
        return OPENOS_BOOTINFO_MEMORY_USABLE;
    default:
        return OPENOS_BOOTINFO_MEMORY_RESERVED;
    }
}

static uint32_t x86_64_memory_type_from_bootinfo(uint32_t bootinfo_type)
{
    switch (bootinfo_type) {
    case OPENOS_BOOTINFO_MEMORY_USABLE:
        return OPENOS_X86_64_MMAP_USABLE;
    case OPENOS_BOOTINFO_MEMORY_ACPI_RECLAIMABLE:
        return OPENOS_X86_64_MMAP_ACPI_RECLAIMABLE;
    case OPENOS_BOOTINFO_MEMORY_ACPI_NVS:
        return OPENOS_X86_64_MMAP_ACPI_NVS;
    case OPENOS_BOOTINFO_MEMORY_BAD:
        return OPENOS_X86_64_MMAP_BAD_MEMORY;
    case OPENOS_BOOTINFO_MEMORY_RESERVED:
    default:
        return OPENOS_X86_64_MMAP_RESERVED;
    }
}

static x86_64_phys_addr_t kernel_phys_from_virt(uintptr_t virt)
{
    if (virt >= OPENOS_X86_64_KERNEL_BASE) {
        return (x86_64_phys_addr_t)(virt - OPENOS_X86_64_KERNEL_BASE + 0x200000ULL);
    }
    return (x86_64_phys_addr_t)virt;
}

const openos_bootinfo_t *arch_x86_64_bootinfo_from_uefi_handoff(const uefi64_handoff_info_t *handoff)
{
    openos_bootinfo_t *bootinfo = &g_openos_bootinfo;

    for (uint64_t i = 0; i < sizeof(*bootinfo); ++i) {
        ((uint8_t *)bootinfo)[i] = 0;
    }

    bootinfo->kernel_phys_start = kernel_phys_from_virt((uintptr_t)__kernel64_start);
    bootinfo->kernel_phys_end = kernel_phys_from_virt((uintptr_t)__kernel64_end);

    if (!handoff || handoff->magic != OPENOS_UEFI64_HANDOFF_MAGIC ||
        handoff->version != OPENOS_UEFI64_HANDOFF_VERSION) {
        g_last_memory_region_count = 0;
        g_last_usable_bytes = 0;
        openos_bootinfo_finalize(bootinfo);
        return bootinfo;
    }

    if (handoff->framebuffer.base && handoff->framebuffer.width && handoff->framebuffer.height) {
        bootinfo->flags |= OPENOS_BOOTINFO_FLAG_FRAMEBUFFER_VALID;
        bootinfo->framebuffer.base = handoff->framebuffer.base;
        bootinfo->framebuffer.width = handoff->framebuffer.width;
        bootinfo->framebuffer.height = handoff->framebuffer.height;
        bootinfo->framebuffer.pitch = handoff->framebuffer.pitch;
        bootinfo->framebuffer.bpp = handoff->framebuffer.bpp;
    }

    uint64_t source_count = handoff->memory_descriptor_count;
    if (source_count > OPENOS_BOOTINFO_MAX_MEMORY_REGIONS) {
        source_count = OPENOS_BOOTINFO_MAX_MEMORY_REGIONS;
    }

    uint64_t usable_bytes = 0;
    for (uint64_t i = 0; i < source_count; ++i) {
        const uefi64_memory_descriptor_info_t *src = &handoff->descriptors[i];
        openos_bootinfo_memory_region_t *dst = &bootinfo->memory_regions[i];
        uint64_t length = src->number_of_pages << 12;

        dst->base = src->physical_start;
        dst->length = length;
        dst->type = bootinfo_memory_type_from_uefi(src->type);
        dst->attributes = (uint32_t)src->attribute;

        if (dst->type == OPENOS_BOOTINFO_MEMORY_USABLE) {
            usable_bytes += length;
        }
    }

    bootinfo->memory_region_count = (uint32_t)source_count;
    if (source_count > 0) {
        bootinfo->flags |= OPENOS_BOOTINFO_FLAG_MEMORY_MAP_VALID;
    }

    g_last_memory_region_count = source_count;
    g_last_usable_bytes = usable_bytes;
    openos_bootinfo_finalize(bootinfo);
    return bootinfo;
}

void arch_x86_64_memory_init_from_bootinfo(const openos_bootinfo_t *bootinfo)
{
    if (!openos_bootinfo_is_valid(bootinfo) ||
        !(bootinfo->flags & OPENOS_BOOTINFO_FLAG_MEMORY_MAP_VALID)) {
        arch_x86_64_pmm_init(0, 0);
        return;
    }

    x86_64_mmap_entry_t mmap[OPENOS_X86_64_PMM_MAX_MMAP_ENTRIES];
    uint32_t count = bootinfo->memory_region_count;
    if (count > OPENOS_X86_64_PMM_MAX_MMAP_ENTRIES) {
        count = OPENOS_X86_64_PMM_MAX_MMAP_ENTRIES;
    }

    for (uint32_t i = 0; i < count; ++i) {
        mmap[i].base = bootinfo->memory_regions[i].base;
        mmap[i].length = bootinfo->memory_regions[i].length;
        mmap[i].type = x86_64_memory_type_from_bootinfo(bootinfo->memory_regions[i].type);
        mmap[i].attributes = bootinfo->memory_regions[i].attributes;
    }

    arch_x86_64_pmm_init_from_mmap(mmap,
                                   count,
                                   (x86_64_phys_addr_t)bootinfo->kernel_phys_start,
                                   (x86_64_phys_addr_t)bootinfo->kernel_phys_end);
}

void arch_x86_64_handoff_print_status(void)
{
    early_console64_write("[x86_64] OpenOSBootInfo memory regions: ");
    early_console64_write_hex64(g_last_memory_region_count);
    early_console64_write(" usable bytes: ");
    early_console64_write_hex64(g_last_usable_bytes);
    early_console64_write("\n");
}
