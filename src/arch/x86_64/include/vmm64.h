#ifndef OPENOS_ARCH_X86_64_VMM64_H
#define OPENOS_ARCH_X86_64_VMM64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_VMM_PAGE_SIZE 4096ULL
#define OPENOS_X86_64_VMM_TABLE_ENTRIES 512u
#define OPENOS_X86_64_VMM_MAX_TABLES 256u

#define OPENOS_X86_64_PTE_PRESENT 0x001ULL
#define OPENOS_X86_64_PTE_RW      0x002ULL
#define OPENOS_X86_64_PTE_USER    0x004ULL
#define OPENOS_X86_64_PTE_PWT     0x008ULL
#define OPENOS_X86_64_PTE_PCD     0x010ULL
#define OPENOS_X86_64_PTE_ACCESSED 0x020ULL
#define OPENOS_X86_64_PTE_DIRTY   0x040ULL
#define OPENOS_X86_64_PTE_PS      0x080ULL
#define OPENOS_X86_64_PTE_GLOBAL  0x100ULL
#define OPENOS_X86_64_PTE_NX      (1ULL << 63)
#define OPENOS_X86_64_PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define OPENOS_X86_64_VMM_KERNEL_FLAGS \
    (OPENOS_X86_64_PTE_PRESENT | OPENOS_X86_64_PTE_RW | OPENOS_X86_64_PTE_GLOBAL)
#define OPENOS_X86_64_VMM_MMIO_FLAGS \
    (OPENOS_X86_64_PTE_PRESENT | OPENOS_X86_64_PTE_RW | OPENOS_X86_64_PTE_PCD | OPENOS_X86_64_PTE_PWT)
#define OPENOS_X86_64_VMM_USER_FLAGS \
    (OPENOS_X86_64_PTE_PRESENT | OPENOS_X86_64_PTE_RW | OPENOS_X86_64_PTE_USER)

typedef struct x86_64_vmm_info {
    x86_64_phys_addr_t pml4_phys;
    uint64_t mapped_pages;
    uint64_t allocated_tables;
    uint64_t max_tables;
} x86_64_vmm_info_t;

void arch_x86_64_vmm_init(void);
int arch_x86_64_vmm_map_page(x86_64_virt_addr_t virt_addr, x86_64_phys_addr_t phys_addr, uint64_t flags);
int arch_x86_64_vmm_map_range(x86_64_virt_addr_t virt_addr,
                              x86_64_phys_addr_t phys_addr,
                              x86_64_size_t length,
                              uint64_t flags);
void arch_x86_64_vmm_unmap_page(x86_64_virt_addr_t virt_addr);
x86_64_phys_addr_t arch_x86_64_vmm_translate(x86_64_virt_addr_t virt_addr);
x86_64_phys_addr_t arch_x86_64_vmm_get_cr3(void);
void arch_x86_64_vmm_load_cr3(x86_64_phys_addr_t cr3);
const x86_64_vmm_info_t *arch_x86_64_vmm_get_info(void);
void arch_x86_64_vmm_print_status(void);

#endif /* OPENOS_ARCH_X86_64_VMM64_H */
