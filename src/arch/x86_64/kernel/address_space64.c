#include "../include/address_space64.h"

#include "../include/early_console64.h"
#include "../include/pmm64.h"

/*
 * H.5b: per-process address space implementation.
 *
 * We rely on the boot-time identity map of physical [0, 4 GiB) installed
 * by entry64.S, which means every PMM page we allocate is reachable via
 * its physical address as a kernel virtual pointer. This lets us walk
 * and edit any AS's page tables without bringing it into the active CR3.
 *
 * The active boot PML4 (which the kernel boots into) is captured on the
 * first call to arch_x86_64_as_boot_pml4() by reading CR3. All freshly
 * created ASes copy PML4[0] from that boot PML4 — that is how the
 * kernel's identity mapping and the boot user stack stay visible after
 * we activate the new AS.
 *
 * Locking: ASes are not yet shared between CPUs (we only switch in/out
 * on the current CPU). When SMP enters the picture for fork(), we will
 * revisit ipi-driven shootdown.
 */

#define AS_PAGE_SIZE 0x1000ULL
#define AS_ENTRIES_PER_TABLE 512ULL

#define AS_FLAG_ALL_NON_LEAF (OPENOS_X86_64_AS_FLAG_P | \
                              OPENOS_X86_64_AS_FLAG_RW | \
                              OPENOS_X86_64_AS_FLAG_US)

static x86_64_phys_addr_t boot_pml4_cached = 0;

static inline uint64_t *phys_to_va(x86_64_phys_addr_t phys) {
    /* Boot identity covers physical [0, 4 GiB). PMM frames live there. */
    return (uint64_t *)(uintptr_t)phys;
}

static inline uint64_t pml4_index(x86_64_virt_addr_t va) { return (va >> 39) & 0x1FFULL; }
static inline uint64_t pdpt_index(x86_64_virt_addr_t va) { return (va >> 30) & 0x1FFULL; }
static inline uint64_t pd_index(x86_64_virt_addr_t va)   { return (va >> 21) & 0x1FFULL; }
static inline uint64_t pt_index(x86_64_virt_addr_t va)   { return (va >> 12) & 0x1FFULL; }

static inline x86_64_phys_addr_t entry_phys(uint64_t entry) {
    return entry & 0x000FFFFFFFFFF000ULL;
}

static x86_64_phys_addr_t alloc_table(void) {
    x86_64_phys_addr_t phys = arch_x86_64_pmm_alloc_page();
    uint64_t *va;
    uint64_t i;

    if (phys == 0) {
        return 0;
    }
    va = phys_to_va(phys);
    for (i = 0; i < AS_ENTRIES_PER_TABLE; ++i) {
        va[i] = 0;
    }
    return phys;
}

static uint64_t *walk_or_alloc(uint64_t *table, uint64_t idx, uint64_t flags) {
    uint64_t entry = table[idx];
    x86_64_phys_addr_t child_phys;

    if (entry & OPENOS_X86_64_AS_FLAG_P) {
        return phys_to_va(entry_phys(entry));
    }
    child_phys = alloc_table();
    if (child_phys == 0) {
        return NULL;
    }
    table[idx] = child_phys | flags;
    return phys_to_va(child_phys);
}

static void free_subtree(uint64_t *table, int level) {
    /* level: 3=PML4 subtree we're walking, 2=PDPT, 1=PD, 0=PT (leaf parent). */
    uint64_t i;
    if (table == NULL) {
        return;
    }
    if (level == 0) {
        /* PT: entries point to user data pages — free them. */
        for (i = 0; i < AS_ENTRIES_PER_TABLE; ++i) {
            uint64_t e = table[i];
            if (e & OPENOS_X86_64_AS_FLAG_P) {
                arch_x86_64_pmm_free_page(entry_phys(e));
            }
        }
        return;
    }
    for (i = 0; i < AS_ENTRIES_PER_TABLE; ++i) {
        uint64_t e = table[i];
        if (!(e & OPENOS_X86_64_AS_FLAG_P)) {
            continue;
        }
        free_subtree(phys_to_va(entry_phys(e)), level - 1);
        arch_x86_64_pmm_free_page(entry_phys(e));
    }
}

x86_64_phys_addr_t arch_x86_64_as_boot_pml4(void) {
    if (boot_pml4_cached == 0) {
        uint64_t cr3;
        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        boot_pml4_cached = (x86_64_phys_addr_t)(cr3 & 0x000FFFFFFFFFF000ULL);
    }
    return boot_pml4_cached;
}

void arch_x86_64_as_activate_boot(void) {
    x86_64_phys_addr_t boot = arch_x86_64_as_boot_pml4();
    __asm__ __volatile__("mov %0, %%cr3" :: "r"((uint64_t)boot) : "memory");
}

x86_64_address_space_t *arch_x86_64_as_create(void) {
    static x86_64_address_space_t pool[16];
    static uint64_t pool_used = 0;
    x86_64_address_space_t *as;
    x86_64_phys_addr_t pml4_phys;
    uint64_t *pml4_va;
    uint64_t *boot_va;
    uint64_t i;

    if (pool_used >= (sizeof(pool) / sizeof(pool[0]))) {
        return NULL;
    }
    as = &pool[pool_used];

    pml4_phys = alloc_table();
    if (pml4_phys == 0) {
        return NULL;
    }
    pml4_va = phys_to_va(pml4_phys);

    /* Copy PML4[0] from the boot PML4 — that is where the entire
     * 0..4 GiB identity map (kernel + boot user stack + PMM frames) lives.
     * Everything else stays zero. */
    boot_va = phys_to_va(arch_x86_64_as_boot_pml4());
    /*
     * H.5b.2 step B: pointer-copy every PML4 slot except PML4[1] from
     * the boot PML4. Rationale:
     *   PML4[0]    : boot 0..4 GiB identity (kernel low half + boot user
     *                stack + PMM frames + initrd) -- must stay reachable.
     *   PML4[1]    : user-space high half, owned by this AS (zero-init).
     *   PML4[2..255] : currently empty in the boot PML4 (no canonical),
     *                  but pointer-copy is harmless.
     *   PML4[256..511]: kernel high half. The kernel image itself is
     *                   linked at -2 GiB (PML4[511]), so failing to copy
     *                   these slots would triple-fault on the very next
     *                   instruction after `mov cr3`.
     */
    for (i = 0; i < AS_ENTRIES_PER_TABLE; ++i) {
        if (i == 1U) {
            pml4_va[i] = 0;
        } else {
            pml4_va[i] = boot_va[i];
        }
    }

    as->pml4_phys = pml4_phys;
    as->pml4_va = pml4_va;
    as->user_pages = 0;
    as->generation = 0;
    pool_used++;
    early_console64_write("[x86_64][as] create pml4_pa=");
    early_console64_write_hex64((uint64_t)pml4_phys);
    early_console64_write(" pool_used=");
    early_console64_write_hex64(pool_used);
    early_console64_write("\n");
    return as;
}

void arch_x86_64_as_destroy(x86_64_address_space_t *as) {
    uint64_t i;
    uint64_t *pml4_va;
    if (as == NULL || as->pml4_va == NULL) {
        return;
    }
    pml4_va = as->pml4_va;
    /* Walk only PML4[1..511]; PML4[0] is shared (boot identity) — never free it. */
    for (i = 1; i < AS_ENTRIES_PER_TABLE; ++i) {
        uint64_t e = pml4_va[i];
        if (!(e & OPENOS_X86_64_AS_FLAG_P)) {
            continue;
        }
        free_subtree(phys_to_va(entry_phys(e)), 2); /* child is PDPT */
        arch_x86_64_pmm_free_page(entry_phys(e));
        pml4_va[i] = 0;
    }
    arch_x86_64_pmm_free_page(as->pml4_phys);
    as->pml4_va = NULL;
    as->pml4_phys = 0;
}

void arch_x86_64_as_activate(x86_64_address_space_t *as) {
    if (as == NULL || as->pml4_phys == 0) {
        return;
    }
    as->generation++;
    __asm__ __volatile__("mov %0, %%cr3" :: "r"((uint64_t)as->pml4_phys) : "memory");
}

int arch_x86_64_as_map_user(x86_64_address_space_t *as,
                            x86_64_virt_addr_t va,
                            x86_64_phys_addr_t pa,
                            x86_64_size_t size,
                            uint64_t flags) {
    x86_64_virt_addr_t cursor;
    x86_64_virt_addr_t end;
    x86_64_phys_addr_t pcur;

    if (as == NULL || as->pml4_va == NULL) {
        return -1;
    }
    if ((va & (AS_PAGE_SIZE - 1)) != 0) return -1;
    if ((pa & (AS_PAGE_SIZE - 1)) != 0) return -1;
    if ((size & (AS_PAGE_SIZE - 1)) != 0) return -1;
    if (size == 0) return -1;
    if (va < OPENOS_X86_64_USER_VBASE) return -1;
    end = va + size;
    if (end > OPENOS_X86_64_USER_VTOP || end < va) return -1;

    /* Force P|US bits on the leaf; caller controls W/X/etc. */
    flags |= OPENOS_X86_64_AS_FLAG_P | OPENOS_X86_64_AS_FLAG_US;

    cursor = va;
    pcur = pa;
    while (cursor < end) {
        uint64_t *pdpt;
        uint64_t *pd;
        uint64_t *pt;
        uint64_t pti;

        pdpt = walk_or_alloc(as->pml4_va, pml4_index(cursor), AS_FLAG_ALL_NON_LEAF);
        if (!pdpt) return -1;
        pd = walk_or_alloc(pdpt, pdpt_index(cursor), AS_FLAG_ALL_NON_LEAF);
        if (!pd) return -1;
        pt = walk_or_alloc(pd, pd_index(cursor), AS_FLAG_ALL_NON_LEAF);
        if (!pt) return -1;

        pti = pt_index(cursor);
        if (!(pt[pti] & OPENOS_X86_64_AS_FLAG_P)) {
            as->user_pages++;
        }
        pt[pti] = (pcur & 0x000FFFFFFFFFF000ULL) | flags;

        cursor += AS_PAGE_SIZE;
        pcur += AS_PAGE_SIZE;
    }
    return 0;
}
