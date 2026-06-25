#ifndef OPENOS_KERNEL_BOOTINFO_H
#define OPENOS_KERNEL_BOOTINFO_H

#include <stdint.h>

#define OPENOS_BOOTINFO_MAGIC 0x4F42494Fu /* "OBIO" */
#define OPENOS_BOOTINFO_VERSION 1u
#define OPENOS_BOOTINFO_MAX_MEMORY_REGIONS 64u
#define OPENOS_BOOTINFO_MAX_CMDLINE 256u

#define OPENOS_BOOTINFO_MEMORY_USABLE 1u
#define OPENOS_BOOTINFO_MEMORY_RESERVED 2u
#define OPENOS_BOOTINFO_MEMORY_ACPI_RECLAIMABLE 3u
#define OPENOS_BOOTINFO_MEMORY_ACPI_NVS 4u
#define OPENOS_BOOTINFO_MEMORY_BAD 5u

#define OPENOS_BOOTINFO_FLAG_FRAMEBUFFER_VALID (1u << 0)
#define OPENOS_BOOTINFO_FLAG_INITRD_VALID      (1u << 1)
#define OPENOS_BOOTINFO_FLAG_MEMORY_MAP_VALID  (1u << 2)
#define OPENOS_BOOTINFO_FLAG_ACPI_RSDP_VALID   (1u << 3)
#define OPENOS_BOOTINFO_FLAG_DEVICE_TREE_VALID (1u << 4)
#define OPENOS_BOOTINFO_FLAG_CMDLINE_VALID     (1u << 5)

typedef struct openos_bootinfo_memory_region {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
} openos_bootinfo_memory_region_t;

typedef struct openos_bootinfo_framebuffer {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} openos_bootinfo_framebuffer_t;

typedef struct openos_bootinfo_module {
    uint64_t base;
    uint64_t size;
} openos_bootinfo_module_t;

typedef struct openos_bootinfo {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t checksum;
    uint32_t flags;
    uint32_t memory_region_count;
    uint32_t reserved0;
    uint32_t reserved1;
    openos_bootinfo_memory_region_t memory_regions[OPENOS_BOOTINFO_MAX_MEMORY_REGIONS];
    openos_bootinfo_framebuffer_t framebuffer;
    openos_bootinfo_module_t initrd;
    openos_bootinfo_module_t device_tree;
    uint64_t acpi_rsdp;
    uint64_t cmdline;
    uint32_t cmdline_size;
    uint32_t reserved2;
    uint64_t kernel_phys_start;
    uint64_t kernel_phys_end;
} openos_bootinfo_t;

static inline uint32_t openos_bootinfo_checksum32(const openos_bootinfo_t *bootinfo)
{
    const uint32_t *words = (const uint32_t *)bootinfo;
    uint32_t count = bootinfo ? (bootinfo->size / sizeof(uint32_t)) : 0u;
    uint32_t sum = 0u;
    for (uint32_t i = 0; i < count; ++i) {
        sum += words[i];
    }
    return sum;
}

static inline void openos_bootinfo_finalize(openos_bootinfo_t *bootinfo)
{
    if (!bootinfo) {
        return;
    }
    bootinfo->magic = OPENOS_BOOTINFO_MAGIC;
    bootinfo->version = OPENOS_BOOTINFO_VERSION;
    bootinfo->size = (uint32_t)sizeof(*bootinfo);
    bootinfo->checksum = 0u;
    bootinfo->checksum = 0u - openos_bootinfo_checksum32(bootinfo);
}

static inline int openos_bootinfo_is_valid(const openos_bootinfo_t *bootinfo)
{
    return bootinfo &&
           bootinfo->magic == OPENOS_BOOTINFO_MAGIC &&
           bootinfo->version == OPENOS_BOOTINFO_VERSION &&
           bootinfo->size == (uint32_t)sizeof(*bootinfo) &&
           openos_bootinfo_checksum32(bootinfo) == 0u;
}

#endif /* OPENOS_KERNEL_BOOTINFO_H */
