/* ============================================================
 * openos - user memory access helpers
 *
 * These helpers validate user pointers against the current page tables before
 * the kernel reads from or writes to user memory. They are intentionally small:
 * current OpenOS still uses one shared address space, but checking PTE_USER and
 * PTE_PRESENT prevents syscall code from blindly touching kernel/unmapped pages.
 * ============================================================ */

#include "include/usermem.h"
#include "include/vmm.h"
#include "include/string.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

static int user_range_bounds_ok(uint32_t start, uint32_t len)
{
    uint32_t end;

    if (len == 0)
        return 1;
    if (start < USERMEM_PTR_MIN || start >= USERMEM_PTR_MAX)
        return 0;

    end = start + len - 1;
    if (end < start || end >= USERMEM_PTR_MAX)
        return 0;

    return 1;
}

static int user_page_ok(uint32_t addr, int write)
{
    uint32_t pgd_idx = addr >> 22;
    uint32_t pte_idx = (addr >> 12) & 0x3FF;
    volatile uint32_t *pde = (volatile uint32_t *)0xFFFFF000;
    volatile uint32_t *pte = (volatile uint32_t *)(0xFFC00000 + (pgd_idx << 12));
    uint32_t pde_flags = pde[pgd_idx];
    uint32_t pte_flags;

    if ((pde_flags & PTE_PRESENT) == 0)
        return 0;
    if ((pde_flags & PTE_USER) == 0)
        return 0;
    if (write && (pde_flags & PTE_RW) == 0)
        return 0;

    pte_flags = pte[pte_idx];
    if ((pte_flags & PTE_PRESENT) == 0)
        return 0;
    if ((pte_flags & PTE_USER) == 0)
        return 0;
    if (write && (pte_flags & PTE_RW) == 0)
        return 0;

    return 1;
}

int user_ptr_valid(const void *ptr, uint32_t len, int write)
{
    uint32_t start = (uint32_t)ptr;
    uint32_t end;
    uint32_t page;

    if (!user_range_bounds_ok(start, len))
        return 0;
    if (len == 0)
        return 1;

    end = start + len - 1;
    page = start & PAGE_MASK;
    while (page <= end) {
        if (!user_page_ok(page, write))
            return 0;
        if (page > UINT32_MAX - PAGE_SIZE)
            break;
        page += PAGE_SIZE;
    }

    return 1;
}

int copy_from_user(void *dst, const void *user_src, uint32_t len)
{
    if (len == 0)
        return 0;
    if (!dst || !user_ptr_valid(user_src, len, USERMEM_READ))
        return -1;

    memcpy(dst, user_src, len);
    return 0;
}

int copy_to_user(void *user_dst, const void *src, uint32_t len)
{
    if (len == 0)
        return 0;
    if (!src || !user_ptr_valid(user_dst, len, USERMEM_WRITE))
        return -1;

    memcpy(user_dst, src, len);
    return 0;
}

int strncpy_from_user(char *dst, const char *user_src, uint32_t maxlen)
{
    uint32_t i;

    if (!dst || !user_src || maxlen == 0)
        return -1;

    for (i = 0; i < maxlen; i++) {
        if (!user_ptr_valid(user_src + i, 1, USERMEM_READ)) {
            dst[0] = '\0';
            return -1;
        }
        dst[i] = user_src[i];
        if (dst[i] == '\0')
            return (int)i;
    }

    dst[maxlen - 1] = '\0';
    return -1;
}
