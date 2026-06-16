#ifndef OPENOS_ARCH_X86_64_ELF64_LOADER_H
#define OPENOS_ARCH_X86_64_ELF64_LOADER_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_ELF64_MAGIC0 0x7FU
#define OPENOS_ELF64_MAGIC1 'E'
#define OPENOS_ELF64_MAGIC2 'L'
#define OPENOS_ELF64_MAGIC3 'F'
#define OPENOS_ELF64_CLASS64 2U
#define OPENOS_ELF64_DATA_LSB 1U
#define OPENOS_ELF64_VERSION_CURRENT 1U
#define OPENOS_ELF64_OSABI_SYSV 0U

#define OPENOS_ELF64_ET_EXEC 2U
#define OPENOS_ELF64_ET_DYN 3U
#define OPENOS_ELF64_EM_X86_64 62U
#define OPENOS_ELF64_PT_LOAD 1U

#define OPENOS_ELF64_PF_X 0x1U
#define OPENOS_ELF64_PF_W 0x2U
#define OPENOS_ELF64_PF_R 0x4U

#define OPENOS_X86_64_USER_BASE 0x0000000000400000ULL
#define OPENOS_X86_64_USER_TOP  0x0000800000000000ULL

typedef enum elf64_loader_status {
    ELF64_LOADER_OK = 0,
    ELF64_LOADER_ERR_BAD_ARGUMENT = -1,
    ELF64_LOADER_ERR_BAD_MAGIC = -2,
    ELF64_LOADER_ERR_UNSUPPORTED = -3,
    ELF64_LOADER_ERR_TRUNCATED = -4,
    ELF64_LOADER_ERR_BAD_SEGMENT = -5,
    ELF64_LOADER_ERR_NO_MEMORY = -6,
    ELF64_LOADER_ERR_MAP_FAILED = -7
} elf64_loader_status_t;

typedef struct elf64_load_result {
    x86_64_entry_t entry;
    x86_64_virt_addr_t low_addr;
    x86_64_virt_addr_t high_addr;
    x86_64_virt_addr_t brk_start;
    uint16_t load_segments;
    elf64_loader_status_t status;
} elf64_load_result_t;

typedef struct elf64_loader_info {
    uint64_t attempted_loads;
    uint64_t successful_loads;
    uint64_t failed_loads;
    uint64_t mapped_pages;
    elf64_loader_status_t last_status;
} elf64_loader_info_t;

void arch_x86_64_elf64_loader_init(void);
elf64_load_result_t arch_x86_64_elf64_load_image(const void *image, x86_64_size_t image_size);
const elf64_loader_info_t *arch_x86_64_elf64_loader_get_info(void);
void arch_x86_64_elf64_loader_print_status(void);

#endif /* OPENOS_ARCH_X86_64_ELF64_LOADER_H */
