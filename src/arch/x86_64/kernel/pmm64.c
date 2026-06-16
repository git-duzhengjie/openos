#include "../include/arch64.h"
#include "../include/early_console64.h"
#include "../include/pmm64.h"

#define PMM64_FALLBACK_MEMORY_SIZE (128ULL * 1024ULL * 1024ULL)

extern char __kernel64_start[];
extern char __kernel64_end[];

static uint64_t pmm64_bitmap[OPENOS_X86_64_PMM_MAX_PAGES / OPENOS_X86_64_PMM_BITMAP_WORD_BITS] __attribute__((aligned(4096)));
static x86_64_pmm_info_t pmm64_info;

static x86_64_phys_addr_t align_down_addr(x86_64_phys_addr_t value, x86_64_size_t align) {
    return value & ~(align - 1ULL);
}

static x86_64_phys_addr_t align_up_addr(x86_64_phys_addr_t value, x86_64_size_t align) {
    return (value + align - 1ULL) & ~(align - 1ULL);
}

static x86_64_phys_addr_t virt_to_phys_addr(x86_64_virt_addr_t virt) {
    if (virt >= OPENOS_X86_64_KERNEL_BASE) {
        return virt - OPENOS_X86_64_KERNEL_BASE + 0x200000ULL;
    }
    return virt;
}

static uint64_t page_index(x86_64_phys_addr_t phys_addr) {
    return phys_addr / OPENOS_X86_64_PMM_PAGE_SIZE;
}

static x86_64_phys_addr_t page_addr(uint64_t page) {
    return page * OPENOS_X86_64_PMM_PAGE_SIZE;
}

static int page_in_range(uint64_t page) {
    return page < pmm64_info.total_pages && page < OPENOS_X86_64_PMM_MAX_PAGES;
}

static int page_is_free(uint64_t page) {
    uint64_t word = page / OPENOS_X86_64_PMM_BITMAP_WORD_BITS;
    uint64_t bit = page % OPENOS_X86_64_PMM_BITMAP_WORD_BITS;

    if (!page_in_range(page)) {
        return 0;
    }
    return (pmm64_bitmap[word] & (1ULL << bit)) != 0;
}

static void set_page_state(uint64_t page, int free) {
    uint64_t word = page / OPENOS_X86_64_PMM_BITMAP_WORD_BITS;
    uint64_t bit = page % OPENOS_X86_64_PMM_BITMAP_WORD_BITS;
    uint64_t mask = 1ULL << bit;
    int was_free;

    if (!page_in_range(page)) {
        return;
    }

    was_free = (pmm64_bitmap[word] & mask) != 0;
    if (free) {
        if (!was_free) {
            pmm64_bitmap[word] |= mask;
            ++pmm64_info.free_pages;
            if (pmm64_info.used_pages > 0) {
                --pmm64_info.used_pages;
            }
        }
    } else {
        if (was_free) {
            pmm64_bitmap[word] &= ~mask;
            if (pmm64_info.free_pages > 0) {
                --pmm64_info.free_pages;
            }
            ++pmm64_info.used_pages;
        }
    }
}

static void pmm64_reset(x86_64_phys_addr_t highest_addr) {
    uint64_t i;
    uint64_t pages = align_up_addr(highest_addr, OPENOS_X86_64_PMM_PAGE_SIZE) / OPENOS_X86_64_PMM_PAGE_SIZE;

    if (pages > OPENOS_X86_64_PMM_MAX_PAGES) {
        pages = OPENOS_X86_64_PMM_MAX_PAGES;
    }

    pmm64_info.total_pages = pages;
    pmm64_info.used_pages = pages;
    pmm64_info.free_pages = 0;
    pmm64_info.highest_addr = pages * OPENOS_X86_64_PMM_PAGE_SIZE;
    pmm64_info.bitmap_words = (pages + OPENOS_X86_64_PMM_BITMAP_WORD_BITS - 1ULL) / OPENOS_X86_64_PMM_BITMAP_WORD_BITS;
    pmm64_info.bitmap_bytes = pmm64_info.bitmap_words * sizeof(uint64_t);

    for (i = 0; i < (OPENOS_X86_64_PMM_MAX_PAGES / OPENOS_X86_64_PMM_BITMAP_WORD_BITS); ++i) {
        pmm64_bitmap[i] = 0;
    }
}

void arch_x86_64_pmm_free_range(x86_64_phys_addr_t base, x86_64_size_t length) {
    uint64_t start;
    uint64_t end;
    uint64_t page;

    if (length == 0) {
        return;
    }

    start = align_up_addr(base, OPENOS_X86_64_PMM_PAGE_SIZE);
    end = align_down_addr(base + length, OPENOS_X86_64_PMM_PAGE_SIZE);
    if (end <= start) {
        return;
    }

    for (page = page_index(start); page < page_index(end); ++page) {
        set_page_state(page, 1);
    }
}

void arch_x86_64_pmm_reserve_range(x86_64_phys_addr_t base, x86_64_size_t length) {
    uint64_t start;
    uint64_t end;
    uint64_t page;

    if (length == 0) {
        return;
    }

    start = align_down_addr(base, OPENOS_X86_64_PMM_PAGE_SIZE);
    end = align_up_addr(base + length, OPENOS_X86_64_PMM_PAGE_SIZE);
    for (page = page_index(start); page < page_index(end); ++page) {
        set_page_state(page, 0);
    }
}

void arch_x86_64_pmm_init_from_mmap(const x86_64_mmap_entry_t *mmap,
                                    uint32_t count,
                                    x86_64_phys_addr_t kernel_phys_start,
                                    x86_64_phys_addr_t kernel_phys_end) {
    uint32_t i;
    uint64_t highest = 0;
    x86_64_phys_addr_t bitmap_phys;

    if (!mmap || count == 0) {
        arch_x86_64_pmm_init(kernel_phys_start, kernel_phys_end);
        return;
    }
    if (count > OPENOS_X86_64_PMM_MAX_MMAP_ENTRIES) {
        count = OPENOS_X86_64_PMM_MAX_MMAP_ENTRIES;
    }

    for (i = 0; i < count; ++i) {
        x86_64_phys_addr_t end = mmap[i].base + mmap[i].length;
        if (end > highest) {
            highest = end;
        }
    }

    pmm64_reset(highest);

    for (i = 0; i < count; ++i) {
        if (mmap[i].type == OPENOS_X86_64_MMAP_USABLE) {
            arch_x86_64_pmm_free_range(mmap[i].base, mmap[i].length);
        }
    }

    arch_x86_64_pmm_reserve_range(0, 0x100000ULL);
    arch_x86_64_pmm_reserve_range(kernel_phys_start, kernel_phys_end - kernel_phys_start);
    bitmap_phys = virt_to_phys_addr((x86_64_virt_addr_t)(uintptr_t)&pmm64_bitmap[0]);
    arch_x86_64_pmm_reserve_range(bitmap_phys, sizeof(pmm64_bitmap));
}

void arch_x86_64_pmm_init(x86_64_phys_addr_t kernel_phys_start, x86_64_phys_addr_t kernel_phys_end) {
    x86_64_mmap_entry_t fallback;

    if (kernel_phys_end <= kernel_phys_start) {
        kernel_phys_start = virt_to_phys_addr((x86_64_virt_addr_t)(uintptr_t)__kernel64_start);
        kernel_phys_end = virt_to_phys_addr((x86_64_virt_addr_t)(uintptr_t)__kernel64_end);
    }

    fallback.base = 0;
    fallback.length = PMM64_FALLBACK_MEMORY_SIZE;
    fallback.type = OPENOS_X86_64_MMAP_USABLE;
    fallback.attributes = 0;
    arch_x86_64_pmm_init_from_mmap(&fallback, 1, kernel_phys_start, kernel_phys_end);
}

x86_64_phys_addr_t arch_x86_64_pmm_alloc_page(void) {
    return arch_x86_64_pmm_alloc_pages(1);
}

x86_64_phys_addr_t arch_x86_64_pmm_alloc_pages(uint64_t count) {
    uint64_t start;
    uint64_t run = 0;
    uint64_t run_start = 0;
    uint64_t page;

    if (count == 0 || count > pmm64_info.free_pages) {
        return 0;
    }

    for (start = pmm64_info.total_pages; start > 0; --start) {
        page = start - 1ULL;
        if (page_is_free(page)) {
            ++run;
            run_start = page;
            if (run == count) {
                uint64_t i;
                for (i = 0; i < count; ++i) {
                    set_page_state(run_start + i, 0);
                }
                return page_addr(run_start);
            }
        } else {
            run = 0;
        }
    }

    return 0;
}

void arch_x86_64_pmm_free_page(x86_64_phys_addr_t phys_addr) {
    if ((phys_addr & (OPENOS_X86_64_PMM_PAGE_SIZE - 1ULL)) != 0) {
        return;
    }
    set_page_state(page_index(phys_addr), 1);
}

uint64_t arch_x86_64_pmm_get_free_pages(void) {
    return pmm64_info.free_pages;
}

const x86_64_pmm_info_t *arch_x86_64_pmm_get_info(void) {
    return &pmm64_info;
}

void arch_x86_64_pmm_print_status(void) {
    early_console64_write("[x86_64][PMM] total=");
    early_console64_write_hex64(pmm64_info.total_pages);
    early_console64_write(" free=");
    early_console64_write_hex64(pmm64_info.free_pages);
    early_console64_write(" used=");
    early_console64_write_hex64(pmm64_info.used_pages);
    early_console64_write("\n");
}
