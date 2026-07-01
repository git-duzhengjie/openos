#ifndef OPENOS_ARCH_X86_64_ADDRESS_SPACE64_H
#define OPENOS_ARCH_X86_64_ADDRESS_SPACE64_H

#include <stdint.h>

#include "arch64_types.h"
#include "vmm64.h"

/*
 * H.5b: per-process address space (independent PML4).
 *
 * Layout decision (Plan B):
 *   - PML4[0]   : shared with the kernel "boot" PML4 (the 0..4 GiB
 *                 identity-mapped low region holding all of the kernel's
 *                 .text/.data/.bss/heap/PMM frames + boot user_stack).
 *                 We simply copy the entry pointer; no clone.
 *   - PML4[1]   : per-process *private* user subtree, anchored at the
 *                 canonical user virtual base 0x0000_0080_0000_0000.
 *                 ELF segments, user stack, argv/envp strings all live
 *                 here. Each AS owns its own PDPT/PD/PT chain.
 *   - PML4[256..511] : reserved for future kernel high-half (HHK).
 *                 H.5b leaves them zero; they will be cloned by pointer
 *                 once the kernel migrates above the canonical hole.
 *
 * Lifecycle:
 *   create  -> alloc PML4 page, copy slot 0, zero the rest.
 *   map     -> walk-or-alloc PDPT/PD/PT in PML4[1] subtree, install
 *              4 KiB leaf with caller-supplied U/W/X flags.
 *   activate-> load CR3 with the PML4 phys.
 *   destroy -> walk PML4[1] subtree, free leaf pages, page tables,
 *              and the PML4 itself. PML4[0] is NEVER freed (shared).
 */

#define OPENOS_X86_64_USER_VBASE 0x0000008000000000ULL  /* PML4[1] start    */
#define OPENOS_X86_64_USER_VTOP  0x0000010000000000ULL  /* PML4[2] start    */

/*
 * Page flag bits (mirrors vmm64.c locals; we re-declare so callers don't
 * have to pull internal headers).
 */
#define OPENOS_X86_64_AS_FLAG_P  (1ULL << 0)
#define OPENOS_X86_64_AS_FLAG_RW (1ULL << 1)
#define OPENOS_X86_64_AS_FLAG_US (1ULL << 2)
#define OPENOS_X86_64_AS_FLAG_NX (1ULL << 63)

/*
 * BORROWED (AVL bit 9): leaf page is *not* owned by this AS. free_subtree
 * must NOT return the physical page to PMM when tearing down. Used for
 * pages that are shared across address spaces (e.g. the bootstrap user
 * stack in usermode64.c). Ignored by the CPU (AVL).
 */
#define OPENOS_X86_64_AS_FLAG_BORROWED (1ULL << 9)

typedef struct x86_64_address_space {
    x86_64_phys_addr_t pml4_phys;   /* physical address of the PML4 page */
    uint64_t *pml4_va;              /* identity-mapped virtual pointer    */
    uint64_t  user_pages;           /* leaf user pages currently mapped   */
    uint64_t  generation;           /* bumped on activate (debug)         */
} x86_64_address_space_t;

/* Create a fresh AS. Returns NULL on OOM. PML4[0] is shared with the
 * boot identity tables; PML4[1..511] start zero. */
x86_64_address_space_t *arch_x86_64_as_create(void);

/* Tear down an AS: free every page table under PML4[1] plus the PML4
 * itself. Caller must ensure the AS is NOT currently active on any CPU
 * (CR3 must point elsewhere). Safe to call with NULL. */
void arch_x86_64_as_destroy(x86_64_address_space_t *as);

/* Load CR3 with as->pml4_phys. Bumps generation. */
void arch_x86_64_as_activate(x86_64_address_space_t *as);

/* Map [va, va+size) -> [pa, pa+size) inside this AS with the given flags
 * (P|RW|US|NX). va, pa, size must be 4 KiB aligned. va must lie in
 * [USER_VBASE, USER_VTOP). Returns 0 on success, -1 on bad args / OOM. */
int arch_x86_64_as_map_user(x86_64_address_space_t *as,
                            x86_64_virt_addr_t va,
                            x86_64_phys_addr_t pa,
                            x86_64_size_t size,
                            uint64_t flags);

/* Deep-clone an existing AS for fork(): allocate a brand new PML4,
 * pointer-copy PML4[0,2..511] (shared boot identity + future HHK), then
 * recursively duplicate the entire PML4[1] subtree (PDPT/PD/PT pages
 * each get fresh PMM frames, and every present leaf 4 KiB user page is
 * copied byte-for-byte to a freshly allocated frame). Page flags
 * (P/RW/US/NX/...) are preserved on every leaf. On any OOM the partial
 * clone is fully torn down and NULL is returned. Returns NULL if parent
 * is NULL or has no PML4. */
x86_64_address_space_t *arch_x86_64_as_clone(const x86_64_address_space_t *parent);

/* Query: physical of currently-active boot PML4 (kernel identity). Used
 * by execve to flip back to the kernel AS before destroying the old. */
x86_64_phys_addr_t arch_x86_64_as_boot_pml4(void);

/* Switch CR3 back to the boot/kernel PML4. */
void arch_x86_64_as_activate_boot(void);

#endif /* OPENOS_ARCH_X86_64_ADDRESS_SPACE64_H */
