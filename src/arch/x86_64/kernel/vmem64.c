/*
 * vmem64.c — M4.1b anonymous virtual-memory bookkeeping.
 *
 * Kernel-backed mode: memory comes from the PMM and the identity-mapped
 * physical address is used directly as the returned virtual address. Every
 * live region (both the brk heap and each mmap area) is tracked in a static
 * table so munmap can hand pages back and the self-test can audit state. This
 * keeps the mmap/munmap/mprotect/brk/sbrk ABI real and testable without a
 * ring3 address space; per-process user mappings layer on top later without an
 * ABI change.
 */

#include "vmem64.h"

#include "../include/pmm64.h"

#define PAGE_SIZE       OPENOS_X86_64_PMM_PAGE_SIZE
#define MAX_REGIONS     64u

/* A single anonymous region: a run of contiguous PMM pages. */
typedef struct vmem_region {
    uint64_t base;      /* virtual == physical (identity) base address */
    uint64_t length;    /* rounded-up length in bytes                  */
    uint64_t pages;     /* number of 4KiB pages backing it             */
    int      prot;      /* current PROT_* bits                         */
    int      is_heap;   /* 1 if this region is the brk heap            */
    int      used;      /* slot occupancy flag                         */
} vmem_region_t;

static vmem_region_t g_regions[MAX_REGIONS];

/* Program break: single contiguous growable heap region. */
static uint64_t g_brk_base;   /* virtual base of the heap (0 = not yet created) */
static uint64_t g_brk_cur;    /* current break (top of used heap)               */
static uint64_t g_brk_pages;  /* pages currently committed for the heap         */
static int      g_heap_slot = -1;  /* index into g_regions for the heap, or -1  */

static uint64_t round_up_page(uint64_t v)
{
    return (v + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

static int region_alloc_slot(void)
{
    for (uint32_t i = 0; i < MAX_REGIONS; ++i) {
        if (!g_regions[i].used) {
            return (int)i;
        }
    }
    return -1;
}

void arch_x86_64_vmem_reset(void)
{
    for (uint32_t i = 0; i < MAX_REGIONS; ++i) {
        if (g_regions[i].used && g_regions[i].pages) {
            arch_x86_64_pmm_free_range(g_regions[i].base,
                                       g_regions[i].pages * PAGE_SIZE);
        }
        g_regions[i].base = 0;
        g_regions[i].length = 0;
        g_regions[i].pages = 0;
        g_regions[i].prot = OPENOS_X86_64_PROT_NONE;
        g_regions[i].is_heap = 0;
        g_regions[i].used = 0;
    }
    g_brk_base = 0;
    g_brk_cur = 0;
    g_brk_pages = 0;
    g_heap_slot = -1;
}

uint64_t arch_x86_64_vmem_mmap(uint64_t length, int prot)
{
    if (length == 0) {
        return OPENOS_X86_64_MAP_FAILED;
    }

    uint64_t need = round_up_page(length);
    uint64_t pages = need / PAGE_SIZE;

    int slot = region_alloc_slot();
    if (slot < 0) {
        return OPENOS_X86_64_MAP_FAILED;
    }

    uint64_t phys = (uint64_t)arch_x86_64_pmm_alloc_pages(pages);
    if (phys == 0) {
        return OPENOS_X86_64_MAP_FAILED;
    }

    g_regions[slot].base = phys;       /* identity mapping: virt == phys */
    g_regions[slot].length = need;
    g_regions[slot].pages = pages;
    g_regions[slot].prot = prot;
    g_regions[slot].is_heap = 0;
    g_regions[slot].used = 1;

    return phys;
}

/* Find the region whose base exactly matches addr. Returns slot or -1. */
static int region_find(uint64_t addr)
{
    for (uint32_t i = 0; i < MAX_REGIONS; ++i) {
        if (g_regions[i].used && g_regions[i].base == addr) {
            return (int)i;
        }
    }
    return -1;
}

int arch_x86_64_vmem_munmap(uint64_t addr, uint64_t length)
{
    if (addr == 0 || (addr & (PAGE_SIZE - 1u)) != 0) {
        return -1;
    }

    int slot = region_find(addr);
    if (slot < 0) {
        return -1;
    }
    /* Only whole-region unmaps are supported in M4.1b. */
    if (round_up_page(length) != g_regions[slot].length) {
        return -1;
    }
    if (g_regions[slot].is_heap) {
        return -1; /* the heap is managed via brk/sbrk, not munmap */
    }

    arch_x86_64_pmm_free_range(g_regions[slot].base,
                               g_regions[slot].pages * PAGE_SIZE);
    g_regions[slot].base = 0;
    g_regions[slot].length = 0;
    g_regions[slot].pages = 0;
    g_regions[slot].prot = OPENOS_X86_64_PROT_NONE;
    g_regions[slot].used = 0;
    return 0;
}

int arch_x86_64_vmem_mprotect(uint64_t addr, uint64_t length, int prot)
{
    if (addr == 0 || (addr & (PAGE_SIZE - 1u)) != 0 || length == 0) {
        return -1;
    }
    int slot = region_find(addr);
    if (slot < 0) {
        return -1;
    }
    if (round_up_page(length) > g_regions[slot].length) {
        return -1;
    }
    /* Kernel-backed identity pages stay present; record the intent so a later
     * per-process AS can apply real PTE permission bits. */
    g_regions[slot].prot = prot;
    return 0;
}

/* Lazily create the heap region on first brk/sbrk touch. */
static int heap_ensure(void)
{
    if (g_heap_slot >= 0) {
        return 0;
    }
    int slot = region_alloc_slot();
    if (slot < 0) {
        return -1;
    }
    /* Start with a single committed page as the heap base. */
    uint64_t phys = (uint64_t)arch_x86_64_pmm_alloc_page();
    if (phys == 0) {
        return -1;
    }
    g_regions[slot].base = phys;
    g_regions[slot].length = PAGE_SIZE;
    g_regions[slot].pages = 1;
    g_regions[slot].prot = OPENOS_X86_64_PROT_READ | OPENOS_X86_64_PROT_WRITE;
    g_regions[slot].is_heap = 1;
    g_regions[slot].used = 1;

    g_brk_base = phys;
    g_brk_cur = phys;             /* break starts empty at the heap base */
    g_brk_pages = 1;
    g_heap_slot = slot;
    return 0;
}

uint64_t arch_x86_64_vmem_brk(uint64_t addr)
{
    if (heap_ensure() != 0) {
        return 0;
    }
    if (addr == 0) {
        return g_brk_cur;   /* query current break */
    }
    if (addr < g_brk_base) {
        return g_brk_cur;   /* cannot move below heap base */
    }

    uint64_t committed_top = g_brk_base + g_brk_pages * PAGE_SIZE;
    if (addr > committed_top) {
        /* Need more pages. Grow by allocating a fresh contiguous run and, for
         * simplicity in kernel-backed mode, only accept growth that stays
         * within a freshly reserved block anchored at the heap base. */
        uint64_t want_pages = (round_up_page(addr - g_brk_base)) / PAGE_SIZE;
        uint64_t extra = want_pages - g_brk_pages;
        uint64_t phys = (uint64_t)arch_x86_64_pmm_alloc_pages(extra);
        if (phys == 0) {
            return g_brk_cur; /* ENOMEM: leave break unchanged */
        }
        /* Record extra pages against the heap accounting. Physical pages need
         * not be contiguous with the base in kernel-backed mode because the
         * self-test only writes within the committed window it just grew; we
         * conservatively free the disjoint block and reject the grow if it is
         * not adjacent to keep semantics honest. */
        if (phys != committed_top) {
            arch_x86_64_pmm_free_range(phys, extra * PAGE_SIZE);
            return g_brk_cur; /* cannot guarantee contiguity: refuse */
        }
        g_brk_pages = want_pages;
        g_regions[g_heap_slot].pages = want_pages;
        g_regions[g_heap_slot].length = want_pages * PAGE_SIZE;
    }
    g_brk_cur = addr;
    return g_brk_cur;
}

uint64_t arch_x86_64_vmem_sbrk(int64_t delta)
{
    if (heap_ensure() != 0) {
        return OPENOS_X86_64_MAP_FAILED;
    }
    uint64_t prev = g_brk_cur;
    if (delta == 0) {
        return prev;
    }
    uint64_t target;
    if (delta < 0) {
        uint64_t dec = (uint64_t)(-delta);
        if (dec > (g_brk_cur - g_brk_base)) {
            target = g_brk_base;
        } else {
            target = g_brk_cur - dec;
        }
    } else {
        target = g_brk_cur + (uint64_t)delta;
    }
    uint64_t result = arch_x86_64_vmem_brk(target);
    if (result != target) {
        return OPENOS_X86_64_MAP_FAILED; /* grow failed */
    }
    return prev;
}

uint32_t arch_x86_64_vmem_region_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < MAX_REGIONS; ++i) {
        if (g_regions[i].used) {
            ++n;
        }
    }
    return n;
}
