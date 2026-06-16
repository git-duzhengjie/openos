#ifndef OPENOS_ARCH_X86_64_PMM64_H
#define OPENOS_ARCH_X86_64_PMM64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_PMM_PAGE_SIZE 4096ULL
#define OPENOS_X86_64_PMM_MAX_PAGES 262144ULL
#define OPENOS_X86_64_PMM_BITMAP_WORD_BITS 64ULL
#define OPENOS_X86_64_PMM_MAX_MMAP_ENTRIES 64u

#define OPENOS_X86_64_MMAP_USABLE 1u
#define OPENOS_X86_64_MMAP_RESERVED 2u
#define OPENOS_X86_64_MMAP_ACPI_RECLAIMABLE 3u
#define OPENOS_X86_64_MMAP_ACPI_NVS 4u
#define OPENOS_X86_64_MMAP_BAD_MEMORY 5u

typedef struct x86_64_mmap_entry {
    x86_64_phys_addr_t base;
    x86_64_size_t length;
    uint32_t type;
    uint32_t attributes;
} x86_64_mmap_entry_t;

typedef struct x86_64_pmm_info {
    uint64_t total_pages;
    uint64_t used_pages;
    uint64_t free_pages;
    x86_64_phys_addr_t highest_addr;
    uint64_t bitmap_words;
    x86_64_size_t bitmap_bytes;
} x86_64_pmm_info_t;

void arch_x86_64_pmm_init(x86_64_phys_addr_t kernel_phys_start, x86_64_phys_addr_t kernel_phys_end);
void arch_x86_64_pmm_init_from_mmap(const x86_64_mmap_entry_t *mmap,
                                    uint32_t count,
                                    x86_64_phys_addr_t kernel_phys_start,
                                    x86_64_phys_addr_t kernel_phys_end);
x86_64_phys_addr_t arch_x86_64_pmm_alloc_page(void);
x86_64_phys_addr_t arch_x86_64_pmm_alloc_pages(uint64_t count);
void arch_x86_64_pmm_free_page(x86_64_phys_addr_t phys_addr);
void arch_x86_64_pmm_reserve_range(x86_64_phys_addr_t base, x86_64_size_t length);
void arch_x86_64_pmm_free_range(x86_64_phys_addr_t base, x86_64_size_t length);
uint64_t arch_x86_64_pmm_get_free_pages(void);
const x86_64_pmm_info_t *arch_x86_64_pmm_get_info(void);
void arch_x86_64_pmm_print_status(void);

#endif /* OPENOS_ARCH_X86_64_PMM64_H */
