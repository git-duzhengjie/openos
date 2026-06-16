#include "../include/arch64.h"
#include "../include/early_console64.h"
#include "../include/pmm64.h"
#include "../include/vmm64.h"

#define VMM64_EARLY_IDENTITY_SIZE (64ULL * 1024ULL * 1024ULL)
#define VMM64_EARLY_KERNEL_PHYS_BASE 0x200000ULL
#define VMM64_EARLY_KERNEL_MAP_SIZE (64ULL * 1024ULL * 1024ULL)

extern char __kernel64_start[];
extern char __kernel64_end[];

typedef uint64_t vmm64_table_t[OPENOS_X86_64_VMM_TABLE_ENTRIES];

static vmm64_table_t vmm64_pml4 __attribute__((aligned(4096)));
static vmm64_table_t vmm64_tables[OPENOS_X86_64_VMM_MAX_TABLES] __attribute__((aligned(4096)));
static uint64_t vmm64_table_phys[OPENOS_X86_64_VMM_MAX_TABLES];
static x86_64_vmm_info_t vmm64_info;

static x86_64_virt_addr_t align_down_virt(x86_64_virt_addr_t value) {
    return value & ~(OPENOS_X86_64_VMM_PAGE_SIZE - 1ULL);
}

static x86_64_virt_addr_t align_up_virt(x86_64_virt_addr_t value) {
    return (value + OPENOS_X86_64_VMM_PAGE_SIZE - 1ULL) & ~(OPENOS_X86_64_VMM_PAGE_SIZE - 1ULL);
}

static uint16_t pml4_index(x86_64_virt_addr_t virt) {
    return (uint16_t)((virt >> 39) & 0x1FFULL);
}

static uint16_t pdpt_index(x86_64_virt_addr_t virt) {
    return (uint16_t)((virt >> 30) & 0x1FFULL);
}

static uint16_t pd_index(x86_64_virt_addr_t virt) {
    return (uint16_t)((virt >> 21) & 0x1FFULL);
}

static uint16_t pt_index(x86_64_virt_addr_t virt) {
    return (uint16_t)((virt >> 12) & 0x1FFULL);
}

static x86_64_phys_addr_t virt_to_phys_addr(x86_64_virt_addr_t virt) {
    if (virt >= OPENOS_X86_64_KERNEL_BASE) {
        return virt - OPENOS_X86_64_KERNEL_BASE + VMM64_EARLY_KERNEL_PHYS_BASE;
    }
    return virt;
}

static void zero_table(vmm64_table_t table) {
    uint32_t i;
    for (i = 0; i < OPENOS_X86_64_VMM_TABLE_ENTRIES; ++i) {
        table[i] = 0;
    }
}

static uint64_t table_index_from_phys(x86_64_phys_addr_t phys) {
    uint64_t i;
    for (i = 0; i < vmm64_info.allocated_tables; ++i) {
        if (vmm64_table_phys[i] == phys) {
            return i;
        }
    }
    return OPENOS_X86_64_VMM_MAX_TABLES;
}

static vmm64_table_t *table_from_phys(x86_64_phys_addr_t phys) {
    uint64_t idx = table_index_from_phys(phys & OPENOS_X86_64_PTE_ADDR_MASK);
    if (idx >= OPENOS_X86_64_VMM_MAX_TABLES) {
        return 0;
    }
    return &vmm64_tables[idx];
}

static vmm64_table_t *alloc_table(x86_64_phys_addr_t *phys_out) {
    vmm64_table_t *table;
    x86_64_phys_addr_t phys;
    uint64_t index = vmm64_info.allocated_tables;

    if (index >= OPENOS_X86_64_VMM_MAX_TABLES) {
        return 0;
    }

    table = &vmm64_tables[index];
    zero_table(*table);
    phys = virt_to_phys_addr((x86_64_virt_addr_t)(uintptr_t)table);
    vmm64_table_phys[index] = phys;
    ++vmm64_info.allocated_tables;

    if (phys_out) {
        *phys_out = phys;
    }
    return table;
}

static vmm64_table_t *walk_create(x86_64_virt_addr_t virt_addr) {
    x86_64_phys_addr_t phys;
    uint64_t entry_flags = OPENOS_X86_64_PTE_PRESENT | OPENOS_X86_64_PTE_RW;
    vmm64_table_t *pdpt;
    vmm64_table_t *pd;
    vmm64_table_t *pt;

    if ((vmm64_pml4[pml4_index(virt_addr)] & OPENOS_X86_64_PTE_PRESENT) == 0) {
        pdpt = alloc_table(&phys);
        if (!pdpt) {
            return 0;
        }
        vmm64_pml4[pml4_index(virt_addr)] = phys | entry_flags;
    } else {
        pdpt = table_from_phys(vmm64_pml4[pml4_index(virt_addr)]);
    }

    if (!pdpt) {
        return 0;
    }
    if (((*pdpt)[pdpt_index(virt_addr)] & OPENOS_X86_64_PTE_PRESENT) == 0) {
        pd = alloc_table(&phys);
        if (!pd) {
            return 0;
        }
        (*pdpt)[pdpt_index(virt_addr)] = phys | entry_flags;
    } else {
        pd = table_from_phys((*pdpt)[pdpt_index(virt_addr)]);
    }

    if (!pd) {
        return 0;
    }
    if (((*pd)[pd_index(virt_addr)] & OPENOS_X86_64_PTE_PRESENT) == 0) {
        pt = alloc_table(&phys);
        if (!pt) {
            return 0;
        }
        (*pd)[pd_index(virt_addr)] = phys | entry_flags;
    } else {
        pt = table_from_phys((*pd)[pd_index(virt_addr)]);
    }

    return pt;
}

static vmm64_table_t *walk_existing(x86_64_virt_addr_t virt_addr) {
    vmm64_table_t *pdpt;
    vmm64_table_t *pd;

    if ((vmm64_pml4[pml4_index(virt_addr)] & OPENOS_X86_64_PTE_PRESENT) == 0) {
        return 0;
    }
    pdpt = table_from_phys(vmm64_pml4[pml4_index(virt_addr)]);
    if (!pdpt || (((*pdpt)[pdpt_index(virt_addr)] & OPENOS_X86_64_PTE_PRESENT) == 0)) {
        return 0;
    }
    pd = table_from_phys((*pdpt)[pdpt_index(virt_addr)]);
    if (!pd || (((*pd)[pd_index(virt_addr)] & OPENOS_X86_64_PTE_PRESENT) == 0)) {
        return 0;
    }
    return table_from_phys((*pd)[pd_index(virt_addr)]);
}

int arch_x86_64_vmm_map_page(x86_64_virt_addr_t virt_addr, x86_64_phys_addr_t phys_addr, uint64_t flags) {
    vmm64_table_t *pt;
    uint64_t old_entry;

    virt_addr = align_down_virt(virt_addr);
    phys_addr = align_down_virt(phys_addr);
    pt = walk_create(virt_addr);
    if (!pt) {
        return -1;
    }

    old_entry = (*pt)[pt_index(virt_addr)];
    (*pt)[pt_index(virt_addr)] = (phys_addr & OPENOS_X86_64_PTE_ADDR_MASK) | (flags & ~OPENOS_X86_64_PTE_ADDR_MASK);
    if ((old_entry & OPENOS_X86_64_PTE_PRESENT) == 0) {
        ++vmm64_info.mapped_pages;
    }
    __asm__ __volatile__("invlpg (%0)" : : "r"(virt_addr) : "memory");
    return 0;
}

int arch_x86_64_vmm_map_range(x86_64_virt_addr_t virt_addr, x86_64_phys_addr_t phys_addr, x86_64_size_t length, uint64_t flags) {
    x86_64_virt_addr_t va;
    x86_64_phys_addr_t pa;
    x86_64_virt_addr_t end;

    if (length == 0) {
        return 0;
    }

    va = align_down_virt(virt_addr);
    pa = align_down_virt(phys_addr);
    end = align_up_virt(virt_addr + length);
    while (va < end) {
        if (arch_x86_64_vmm_map_page(va, pa, flags) != 0) {
            return -1;
        }
        va += OPENOS_X86_64_VMM_PAGE_SIZE;
        pa += OPENOS_X86_64_VMM_PAGE_SIZE;
    }
    return 0;
}

void arch_x86_64_vmm_unmap_page(x86_64_virt_addr_t virt_addr) {
    vmm64_table_t *pt;
    uint16_t index;

    virt_addr = align_down_virt(virt_addr);
    pt = walk_existing(virt_addr);
    if (!pt) {
        return;
    }

    index = pt_index(virt_addr);
    if (((*pt)[index] & OPENOS_X86_64_PTE_PRESENT) != 0 && vmm64_info.mapped_pages > 0) {
        --vmm64_info.mapped_pages;
    }
    (*pt)[index] = 0;
    __asm__ __volatile__("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

x86_64_phys_addr_t arch_x86_64_vmm_translate(x86_64_virt_addr_t virt_addr) {
    vmm64_table_t *pt;
    uint64_t entry;

    pt = walk_existing(virt_addr);
    if (!pt) {
        return 0;
    }
    entry = (*pt)[pt_index(virt_addr)];
    if ((entry & OPENOS_X86_64_PTE_PRESENT) == 0) {
        return 0;
    }
    return (entry & OPENOS_X86_64_PTE_ADDR_MASK) | (virt_addr & (OPENOS_X86_64_VMM_PAGE_SIZE - 1ULL));
}

x86_64_phys_addr_t arch_x86_64_vmm_get_cr3(void) {
    x86_64_phys_addr_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void arch_x86_64_vmm_load_cr3(x86_64_phys_addr_t cr3) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

void arch_x86_64_vmm_init(void) {
    x86_64_phys_addr_t pml4_phys;
    x86_64_phys_addr_t kernel_phys_start;
    x86_64_phys_addr_t kernel_phys_end;

    zero_table(vmm64_pml4);
    vmm64_info.pml4_phys = virt_to_phys_addr((x86_64_virt_addr_t)(uintptr_t)&vmm64_pml4[0]);
    vmm64_info.mapped_pages = 0;
    vmm64_info.allocated_tables = 0;
    vmm64_info.max_tables = OPENOS_X86_64_VMM_MAX_TABLES;

    pml4_phys = vmm64_info.pml4_phys;
    (void)pml4_phys;

    kernel_phys_start = virt_to_phys_addr((x86_64_virt_addr_t)(uintptr_t)__kernel64_start);
    kernel_phys_end = virt_to_phys_addr((x86_64_virt_addr_t)(uintptr_t)__kernel64_end);

    if (arch_x86_64_vmm_map_range(0, 0, VMM64_EARLY_IDENTITY_SIZE, OPENOS_X86_64_VMM_KERNEL_FLAGS) != 0) {
        early_console64_write("[x86_64][VMM] identity map failed\n");
        return;
    }
    if (arch_x86_64_vmm_map_range(OPENOS_X86_64_KERNEL_BASE,
                                  VMM64_EARLY_KERNEL_PHYS_BASE,
                                  VMM64_EARLY_KERNEL_MAP_SIZE,
                                  OPENOS_X86_64_VMM_KERNEL_FLAGS) != 0) {
        early_console64_write("[x86_64][VMM] higher-half map failed\n");
        return;
    }
    arch_x86_64_pmm_reserve_range(vmm64_info.pml4_phys, OPENOS_X86_64_VMM_PAGE_SIZE);
    arch_x86_64_pmm_reserve_range(virt_to_phys_addr((x86_64_virt_addr_t)(uintptr_t)&vmm64_tables[0]), sizeof(vmm64_tables));
    arch_x86_64_pmm_reserve_range(kernel_phys_start, kernel_phys_end - kernel_phys_start);
}

const x86_64_vmm_info_t *arch_x86_64_vmm_get_info(void) {
    return &vmm64_info;
}

void arch_x86_64_vmm_print_status(void) {
    early_console64_write("[x86_64][VMM] pml4=");
    early_console64_write_hex64(vmm64_info.pml4_phys);
    early_console64_write(" mapped=");
    early_console64_write_hex64(vmm64_info.mapped_pages);
    early_console64_write(" tables=");
    early_console64_write_hex64(vmm64_info.allocated_tables);
    early_console64_write("\n");
}
