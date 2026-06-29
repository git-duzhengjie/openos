/*
 * H.5b.3.A: address-space deep-clone self-test.
 *
 * Builds a "parent" AS with exactly one user 4 KiB leaf mapped at
 * USER_VBASE + 0x10000, stamps a magic into the parent physical frame,
 * runs arch_x86_64_as_clone(), and verifies via boot 0..4 GiB identity
 * that the child has its own deep-copied frame (different phys, same
 * bytes) and that mutations on either side do not leak across.
 *
 * Runs on the boot identity map only -- never activates the test ASes.
 */

#include "../include/as_selftest64.h"

#include "../include/address_space64.h"
#include "../include/pmm64.h"
#include "../include/early_console64.h"
#include "../include/arch64_types.h"

#define AS_SELFTEST_USER_VA  0x0000008000010000ULL
#define AS_SELFTEST_MAGIC_P  0xCAFEBABEDEADBEEFULL
#define AS_SELFTEST_MAGIC_C  0x1234567890ABCDEFULL

static void ast_log(const char *s) { early_console64_write(s); }
static void ast_hex(uint64_t v)    { early_console64_write_hex64(v); }

/* Walk parent or child PML4[1] subtree down to the leaf entry for the
 * given va, returning the physical address of the data frame. Returns 0
 * if any level is not present. */
static uint64_t walk_leaf_phys(const x86_64_address_space_t *as, uint64_t va) {
    uint64_t pml4_i = (va >> 39) & 0x1FFu;
    uint64_t pdpt_i = (va >> 30) & 0x1FFu;
    uint64_t pd_i   = (va >> 21) & 0x1FFu;
    uint64_t pt_i   = (va >> 12) & 0x1FFu;
    const uint64_t *t;
    uint64_t e;

    if (as == 0 || as->pml4_va == 0) return 0;
    e = as->pml4_va[pml4_i];
    if (!(e & OPENOS_X86_64_AS_FLAG_P)) return 0;
    t = (const uint64_t *)(uintptr_t)(e & 0x000FFFFFFFFFF000ULL);
    e = t[pdpt_i];
    if (!(e & OPENOS_X86_64_AS_FLAG_P)) return 0;
    t = (const uint64_t *)(uintptr_t)(e & 0x000FFFFFFFFFF000ULL);
    e = t[pd_i];
    if (!(e & OPENOS_X86_64_AS_FLAG_P)) return 0;
    t = (const uint64_t *)(uintptr_t)(e & 0x000FFFFFFFFFF000ULL);
    e = t[pt_i];
    if (!(e & OPENOS_X86_64_AS_FLAG_P)) return 0;
    return e & 0x000FFFFFFFFFF000ULL;
}

int arch_x86_64_as_selftest_clone_run(void) {
    x86_64_address_space_t *parent;
    x86_64_address_space_t *child;
    x86_64_phys_addr_t      pframe;
    uint64_t                p_leaf, c_leaf;
    uint64_t               *pp, *cp;
    int                     rv = 0;

    ast_log("[x86_64][as.selftest] BEGIN clone test\n");

    /* ---- build parent AS with one user page ---- */
    parent = arch_x86_64_as_create();
    if (parent == 0) {
        ast_log("[x86_64][as.selftest] FAIL: as_create(parent)\n");
        return -1;
    }
    pframe = arch_x86_64_pmm_alloc_page();
    if (pframe == 0) {
        ast_log("[x86_64][as.selftest] FAIL: pmm_alloc parent frame\n");
        arch_x86_64_as_destroy(parent);
        return -2;
    }
    /* Stamp the parent frame via boot 0..4 GiB identity (PMM frames live
     * there, so phys == va in the kernel AS we're currently running on). */
    pp = (uint64_t *)(uintptr_t)pframe;
    pp[0] = AS_SELFTEST_MAGIC_P;
    pp[1] = ~AS_SELFTEST_MAGIC_P;
    pp[2] = 0xA5A5A5A5A5A5A5A5ULL;

    if (arch_x86_64_as_map_user(parent, AS_SELFTEST_USER_VA, pframe, 0x1000,
                                OPENOS_X86_64_AS_FLAG_P |
                                OPENOS_X86_64_AS_FLAG_RW |
                                OPENOS_X86_64_AS_FLAG_US) != 0) {
        ast_log("[x86_64][as.selftest] FAIL: as_map_user(parent)\n");
        arch_x86_64_pmm_free_page(pframe);
        arch_x86_64_as_destroy(parent);
        return -3;
    }

    /* ---- clone ---- */
    child = arch_x86_64_as_clone(parent);
    if (child == 0) {
        ast_log("[x86_64][as.selftest] FAIL: as_clone returned NULL\n");
        arch_x86_64_as_destroy(parent);
        return -4;
    }

    /* ---- structural checks ---- */
    if (child->pml4_phys == parent->pml4_phys) {
        ast_log("[x86_64][as.selftest] FAIL: child shares parent PML4 frame\n");
        rv = -5; goto out;
    }
    if (child->pml4_va == 0 ||
        !(child->pml4_va[1] & OPENOS_X86_64_AS_FLAG_P)) {
        ast_log("[x86_64][as.selftest] FAIL: child PML4[1] not present\n");
        rv = -6; goto out;
    }
    if ((child->pml4_va[1] & 0x000FFFFFFFFFF000ULL) ==
        (parent->pml4_va[1] & 0x000FFFFFFFFFF000ULL)) {
        ast_log("[x86_64][as.selftest] FAIL: child shares parent PDPT frame\n");
        rv = -7; goto out;
    }

    /* ---- leaf-frame deep-copy & isolation ---- */
    p_leaf = walk_leaf_phys(parent, AS_SELFTEST_USER_VA);
    c_leaf = walk_leaf_phys(child,  AS_SELFTEST_USER_VA);
    if (p_leaf == 0 || c_leaf == 0) {
        ast_log("[x86_64][as.selftest] FAIL: leaf walk parent=");
        ast_hex(p_leaf); ast_log(" child="); ast_hex(c_leaf);
        ast_log("\n");
        rv = -8; goto out;
    }
    if (p_leaf == c_leaf) {
        ast_log("[x86_64][as.selftest] FAIL: child leaf shares parent frame ");
        ast_hex(p_leaf); ast_log("\n");
        rv = -9; goto out;
    }
    pp = (uint64_t *)(uintptr_t)p_leaf;
    cp = (uint64_t *)(uintptr_t)c_leaf;
    if (cp[0] != AS_SELFTEST_MAGIC_P ||
        cp[1] != ~AS_SELFTEST_MAGIC_P ||
        cp[2] != 0xA5A5A5A5A5A5A5A5ULL) {
        ast_log("[x86_64][as.selftest] FAIL: child leaf bytes mismatch cp[0]=");
        ast_hex(cp[0]); ast_log("\n");
        rv = -10; goto out;
    }

    /* mutate child, parent must stay unchanged */
    cp[0] = AS_SELFTEST_MAGIC_C;
    if (pp[0] != AS_SELFTEST_MAGIC_P) {
        ast_log("[x86_64][as.selftest] FAIL: parent corrupted by child write\n");
        rv = -11; goto out;
    }
    /* mutate parent, child must keep its mutation */
    pp[0] = 0x1111111111111111ULL;
    if (cp[0] != AS_SELFTEST_MAGIC_C) {
        ast_log("[x86_64][as.selftest] FAIL: child corrupted by parent write\n");
        rv = -12; goto out;
    }

    ast_log("[x86_64][as.selftest] OK clone parent_pa=");
    ast_hex((uint64_t)parent->pml4_phys);
    ast_log(" child_pa=");
    ast_hex((uint64_t)child->pml4_phys);
    ast_log(" p_leaf=");
    ast_hex(p_leaf);
    ast_log(" c_leaf=");
    ast_hex(c_leaf);
    ast_log("\n");

out:
    /* as_destroy walks PT leaves and frees the user data frames, so
     * destroying both ASes also returns the parent's original frame and
     * the cloned child frame to the PMM -- no manual pmm_free here. */
    arch_x86_64_as_destroy(child);
    arch_x86_64_as_destroy(parent);
    return rv;
}
