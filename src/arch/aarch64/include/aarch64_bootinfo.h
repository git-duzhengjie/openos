#ifndef OPENOS_ARCH_AARCH64_BOOTINFO_H
#define OPENOS_ARCH_AARCH64_BOOTINFO_H

#include "bootinfo.h"
#include <stdint.h>

#define OPENOS_AARCH64_BOOTINFO_DEFAULT_KERNEL_BASE 0x40200000ULL

typedef struct aarch64_boot_stub_args {
    uint64_t device_tree_base;
    uint64_t device_tree_size;
    uint64_t initrd_base;
    uint64_t initrd_size;
    uint64_t cmdline;
    uint32_t cmdline_size;
    uint64_t kernel_phys_start;
    uint64_t kernel_phys_end;
} aarch64_boot_stub_args_t;

const openos_bootinfo_t *arch_aarch64_bootinfo_from_stub(const aarch64_boot_stub_args_t *args);

#endif /* OPENOS_ARCH_AARCH64_BOOTINFO_H */
