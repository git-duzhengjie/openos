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
        /* PT: entries point to user data pages — free them, except BORROWED. */
        for (i = 0; i < AS_ENTRIES_PER_TABLE; ++i) {
            uint64_t e = table[i];
            if (!(e & OPENOS_X86_64_AS_FLAG_P)) {
                continue;
            }
            if (e & OPENOS_X86_64_AS_FLAG_BORROWED) {
                /* Shared page (e.g. bootstrap user stack) — do NOT free. */
                early_console64_write("[free_subtree] skip borrowed pa=");
                early_console64_write_hex64(entry_phys(e));
                early_console64_write("\n");
                continue;
            }
            arch_x86_64_pmm_free_page(entry_phys(e));
        }
        return;
    }
    for (i = 0; i < AS_ENTRIES_PER_TABLE; ++i) {
        uint64_t e = table[i];
        if (!(e & OPENOS_X86_64_AS_FLAG_P)) {
            continue;
        }
        early_console64_write("[free_subtree] lvl=");
        early_console64_write_hex64((uint64_t)level);
        early_console64_write(" idx=");
        early_console64_write_hex64(i);
        early_console64_write(" child_pa=");
        early_console64_write_hex64(entry_phys(e));
        early_console64_write("\n");
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
    x86_64_address_space_t *as = NULL;
    x86_64_phys_addr_t pml4_phys;
    uint64_t *pml4_va;
    uint64_t *boot_va;
    uint64_t i;

    /* γ.2.b: recycle a freed slot (pml4_va == NULL) before growing. */
    for (uint64_t s = 0; s < pool_used; ++s) {
        if (pool[s].pml4_va == NULL) { as = &pool[s]; break; }
    }
    if (as == NULL) {
        if (pool_used >= (sizeof(pool) / sizeof(pool[0]))) {
            return NULL;
        }
        as = &pool[pool_used];
    }

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
    early_console64_write("[x86_64][as] destroy as=");
    early_console64_write_hex64((uint64_t)(uintptr_t)as);
    early_console64_write(" pml4_pa=");
    early_console64_write_hex64(as ? (uint64_t)as->pml4_phys : 0);
    early_console64_write("\n");
    uint64_t i;
    uint64_t *pml4_va;
    if (as == NULL || as->pml4_va == NULL) {
        return;
    }
    pml4_va = as->pml4_va;
    /*
     * H.5b.3 fix: as_create pointer-copies PML4[0], PML4[2..511] from the
     * boot PML4 (kernel low identity + kernel high half). Those PDPTs are
     * SHARED — walking them here and calling pmm_free_page would double-free
     * kernel page tables and later hand the same physical page out as a new
     * user PML4, corrupting a live parent AS. Only PML4[1] is owned by this
     * AS (user high half, zero-init on create) and must be freed on destroy.
     */
    (void)i;
    {
        uint64_t e = pml4_va[1];
        if (e & OPENOS_X86_64_AS_FLAG_P) {
            free_subtree(phys_to_va(entry_phys(e)), 2); /* child is PDPT */
            arch_x86_64_pmm_free_page(entry_phys(e));
            pml4_va[1] = 0;
        }
    }
    arch_x86_64_pmm_free_page(as->pml4_phys);
    as->pml4_va = NULL;
    as->pml4_phys = 0;
}

void arch_x86_64_as_activate(x86_64_address_space_t *as) {
    /*
     * A2.P4.a — defensive: NULL or torn-down AS routes to the boot PML4.
     *
     * Rationale: callers (e.g. proc teardown, ENOEXEC rollback) may pass
     * NULL when they have no replacement AS in hand. Returning silently
     * leaves CR3 pointing at whatever AS we are about to destroy, and a
     * subsequent TLB miss would walk freed page tables → triple fault.
     * Switching to the boot PML4 keeps the kernel half live (it covers
     * the kernel image + HHK + identity 0..1 GiB) and is always safe.
     */
    if (as == NULL || as->pml4_phys == 0) {
        arch_x86_64_as_activate_boot();
        return;
    }
    as->generation++;
    __asm__ __volatile__("mov %0, %%cr3" :: "r"((uint64_t)as->pml4_phys) : "memory");
}

/*
 * H.5b.3: deep-clone an entire AS for fork().
 *
 * Strategy:
 *   1. allocate a fresh AS via as_create() -- that already copies
 *      PML4[0,2..511] from boot (shared boot identity + HHK).
 *   2. PML4[1] subtree: recursively descend parent. For each non-leaf
 *      level (PDPT/PD/PT) PMM-alloc a fresh table and copy entries
 *      after recursion. For each present PT leaf, PMM-alloc a fresh
 *      4 KiB user data frame, byte-copy the parent frame into it via
 *      boot 0..4 GiB identity (PMM frames live there), and rewrite the
 *      leaf entry's phys field while preserving the flag bits exactly.
 *   3. On any OOM unwind: tear down whatever was built via as_destroy(),
 *      which already knows to free PML4[1..511] recursively.
 */

static void as_memcpy_page(uint64_t dst_phys, uint64_t src_phys) {
    /* Boot identity covers physical [0, 4 GiB). PMM frames live there,
     * so phys==va. Copy a full 4 KiB page word-by-word. */
    uint64_t *d = phys_to_va(dst_phys);
    uint64_t *s = phys_to_va(src_phys);
    uint64_t i;
    for (i = 0; i < (AS_PAGE_SIZE / sizeof(uint64_t)); ++i) {
        d[i] = s[i];
    }
}

/* Clone a present PT (leaf table). Returns 0 on success.
 * Writes the new PT's table phys into *out_phys, *_pages_added is
 * incremented for each newly-allocated leaf 4 KiB frame. */
static int clone_pt(const uint64_t *parent_pt,
                    x86_64_phys_addr_t *out_phys,
                    uint64_t *pages_added) {
    x86_64_phys_addr_t new_pt_phys;
    uint64_t *new_pt;
    uint64_t i;

    new_pt_phys = alloc_table();
    if (new_pt_phys == 0) return -1;
    new_pt = phys_to_va(new_pt_phys);

    for (i = 0; i < AS_ENTRIES_PER_TABLE; ++i) {
        uint64_t pe = parent_pt[i];
        x86_64_phys_addr_t new_leaf;
        if (!(pe & OPENOS_X86_64_AS_FLAG_P)) {
            new_pt[i] = 0;
            continue;
        }
        new_leaf = arch_x86_64_pmm_alloc_page();
        if (new_leaf == 0) {
            /* unwind: free leaves already copied in *this* PT */
            uint64_t j;
            for (j = 0; j < i; ++j) {
                uint64_t ne = new_pt[j];
                if (ne & OPENOS_X86_64_AS_FLAG_P) {
                    arch_x86_64_pmm_free_page(entry_phys(ne));
                }
            }
            arch_x86_64_pmm_free_page(new_pt_phys);
            return -1;
        }
        as_memcpy_page((uint64_t)new_leaf, (uint64_t)entry_phys(pe));
        /* Preserve every flag bit exactly (US/RW/NX/PAT/G/...) -- only
         * swap the physical frame field. The new leaf is *independently*
         * allocated for the child, so it is owned (not borrowed): strip
         * the BORROWED marker from the copied flag set. */
        new_pt[i] = ((uint64_t)new_leaf & 0x000FFFFFFFFFF000ULL) |
                    (pe & ~0x000FFFFFFFFFF000ULL &
                     ~OPENOS_X86_64_AS_FLAG_BORROWED);
        (*pages_added)++;
    }
    *out_phys = new_pt_phys;
    return 0;
}

/* Generic recursive clone of a non-leaf table.
 * level: 2=PDPT, 1=PD, 0=PT(leaf). */
static int clone_subtree(const uint64_t *parent_table,
                         int level,
                         x86_64_phys_addr_t *out_phys,
                         uint64_t *pages_added) {
    x86_64_phys_addr_t new_phys;
    uint64_t *new_table;
    uint64_t i;

    if (level == 0) {
        return clone_pt(parent_table, out_phys, pages_added);
    }

    new_phys = alloc_table();
    if (new_phys == 0) return -1;
    new_table = phys_to_va(new_phys);

    for (i = 0; i < AS_ENTRIES_PER_TABLE; ++i) {
        uint64_t pe = parent_table[i];
        x86_64_phys_addr_t child_phys;
        if (!(pe & OPENOS_X86_64_AS_FLAG_P)) {
            new_table[i] = 0;
            continue;
        }
        if (clone_subtree(phys_to_va(entry_phys(pe)),
                          level - 1,
                          &child_phys,
                          pages_added) != 0) {
            /* unwind: free already-cloned children in this table */
            uint64_t j;
            for (j = 0; j < i; ++j) {
                uint64_t ne = new_table[j];
                if (ne & OPENOS_X86_64_AS_FLAG_P) {
                    free_subtree(phys_to_va(entry_phys(ne)), level - 1);
                    arch_x86_64_pmm_free_page(entry_phys(ne));
                }
            }
            arch_x86_64_pmm_free_page(new_phys);
            return -1;
        }
        /* preserve flags on non-leaf entry */
        new_table[i] = ((uint64_t)child_phys & 0x000FFFFFFFFFF000ULL) |
                       (pe & ~0x000FFFFFFFFFF000ULL);
    }
    *out_phys = new_phys;
    return 0;
}

x86_64_address_space_t *arch_x86_64_as_clone(const x86_64_address_space_t *parent) {
    x86_64_address_space_t *child;
    uint64_t parent_pml4_1;
    x86_64_phys_addr_t child_pdpt_phys;
    uint64_t pages_added = 0;

    if (parent == NULL || parent->pml4_va == NULL) {
        return NULL;
    }

    child = arch_x86_64_as_create();
    if (child == NULL) return NULL;

    parent_pml4_1 = parent->pml4_va[1];
    if (!(parent_pml4_1 & OPENOS_X86_64_AS_FLAG_P)) {
        /* parent has no user mappings, child stays bare */
        early_console64_write("[x86_64][as] clone parent_pml4[1] empty, child=");
        early_console64_write_hex64((uint64_t)child->pml4_phys);
        early_console64_write("\n");
        return child;
    }

    if (clone_subtree(phys_to_va(entry_phys(parent_pml4_1)),
                      2, /* PDPT level */
                      &child_pdpt_phys,
                      &pages_added) != 0) {
        arch_x86_64_as_destroy(child);
        return NULL;
    }

    /* hook the freshly cloned PDPT into PML4[1] preserving parent flags */
    child->pml4_va[1] = ((uint64_t)child_pdpt_phys & 0x000FFFFFFFFFF000ULL) |
                        (parent_pml4_1 & ~0x000FFFFFFFFFF000ULL);
    child->user_pages = pages_added;

    early_console64_write("[x86_64][as] clone parent_pa=");
    early_console64_write_hex64((uint64_t)parent->pml4_phys);
    early_console64_write(" child_pa=");
    early_console64_write_hex64((uint64_t)child->pml4_phys);
    early_console64_write(" pages=");
    early_console64_write_hex64(pages_added);
    early_console64_write("\n");
    return child;
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
