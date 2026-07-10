#include "../include/early_console64.h"
#include "../include/elf64_loader.h"
#include "../include/address_space64.h"
#include "../include/pmm64.h"
#include "../include/vmm64.h"

#define ELF64_EI_NIDENT 16U
#define ELF64_LOAD_ALIGN OPENOS_X86_64_VMM_PAGE_SIZE

typedef struct elf64_ehdr {
    unsigned char e_ident[ELF64_EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

static elf64_loader_info_t elf64_loader_info;

static x86_64_virt_addr_t align_down_addr(x86_64_virt_addr_t value) {
    return value & ~(ELF64_LOAD_ALIGN - 1ULL);
}

static x86_64_virt_addr_t align_up_addr(x86_64_virt_addr_t value) {
    return (value + ELF64_LOAD_ALIGN - 1ULL) & ~(ELF64_LOAD_ALIGN - 1ULL);
}

static int add_overflows_u64(uint64_t a, uint64_t b) {
    return a > UINT64_MAX - b;
}

static int range_in_image(uint64_t offset, uint64_t size, x86_64_size_t image_size) {
    if (add_overflows_u64(offset, size)) {
        return 0;
    }
    return offset + size <= image_size;
}

static int range_in_user(x86_64_virt_addr_t addr, x86_64_size_t size) {
    if (size == 0 || add_overflows_u64(addr, size)) {
        return 0;
    }
    return addr >= OPENOS_X86_64_USER_BASE && addr + size <= OPENOS_X86_64_USER_TOP;
}

static void mem_zero(void *dst, x86_64_size_t size) {
    uint8_t *out = (uint8_t *)dst;
    x86_64_size_t i;
    for (i = 0; i < size; ++i) {
        out[i] = 0;
    }
}

static void mem_copy(void *dst, const void *src, x86_64_size_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    x86_64_size_t i;
    for (i = 0; i < size; ++i) {
        out[i] = in[i];
    }
}

static uint64_t flags_from_phdr(const elf64_phdr_t *phdr) {
    uint64_t flags = OPENOS_X86_64_VMM_USER_FLAGS;
    if ((phdr->p_flags & OPENOS_ELF64_PF_X) == 0) {
        flags |= OPENOS_X86_64_PTE_NX;
    }
    return flags;
}

static elf64_loader_status_t validate_header(const elf64_ehdr_t *ehdr, x86_64_size_t image_size) {
    if (image_size < sizeof(*ehdr)) {
        return ELF64_LOADER_ERR_TRUNCATED;
    }
    if (ehdr->e_ident[0] != OPENOS_ELF64_MAGIC0 ||
        ehdr->e_ident[1] != OPENOS_ELF64_MAGIC1 ||
        ehdr->e_ident[2] != OPENOS_ELF64_MAGIC2 ||
        ehdr->e_ident[3] != OPENOS_ELF64_MAGIC3) {
        return ELF64_LOADER_ERR_BAD_MAGIC;
    }
    if (ehdr->e_ident[4] != OPENOS_ELF64_CLASS64 ||
        ehdr->e_ident[5] != OPENOS_ELF64_DATA_LSB ||
        ehdr->e_ident[6] != OPENOS_ELF64_VERSION_CURRENT ||
        ehdr->e_machine != OPENOS_ELF64_EM_X86_64 ||
        ehdr->e_version != OPENOS_ELF64_VERSION_CURRENT ||
        (ehdr->e_type != OPENOS_ELF64_ET_EXEC && ehdr->e_type != OPENOS_ELF64_ET_DYN) ||
        ehdr->e_phentsize != sizeof(elf64_phdr_t) ||
        ehdr->e_phnum == 0) {
        return ELF64_LOADER_ERR_UNSUPPORTED;
    }
    if (!range_in_image(ehdr->e_phoff, (uint64_t)ehdr->e_phentsize * ehdr->e_phnum, image_size)) {
        return ELF64_LOADER_ERR_TRUNCATED;
    }
    if (!range_in_user(ehdr->e_entry, 1)) {
        return ELF64_LOADER_ERR_BAD_SEGMENT;
    }
    return ELF64_LOADER_OK;
}

static uint64_t as_flags_from_phdr(const elf64_phdr_t *phdr) {
    uint64_t flags = OPENOS_X86_64_AS_FLAG_US;
    if ((phdr->p_flags & OPENOS_ELF64_PF_W) != 0) {
        flags |= OPENOS_X86_64_AS_FLAG_RW;
    }
    if ((phdr->p_flags & OPENOS_ELF64_PF_X) == 0) {
        flags |= OPENOS_X86_64_AS_FLAG_NX;
    }
    return flags;
}

static elf64_loader_status_t map_segment(const uint8_t *image,
                                         x86_64_size_t image_size,
                                         const elf64_phdr_t *phdr,
                                         elf64_load_result_t *result,
                                         struct x86_64_address_space *target_as) {
    x86_64_virt_addr_t seg_start;
    x86_64_virt_addr_t seg_end;

    if (phdr->p_memsz == 0) {
        return ELF64_LOADER_OK;
    }
    if (phdr->p_filesz > phdr->p_memsz || !range_in_image(phdr->p_offset, phdr->p_filesz, image_size)) {
        return ELF64_LOADER_ERR_TRUNCATED;
    }
    if (!range_in_user(phdr->p_vaddr, phdr->p_memsz)) {
        return ELF64_LOADER_ERR_BAD_SEGMENT;
    }
    if (phdr->p_align != 0 && phdr->p_align != 1 && (phdr->p_align & (phdr->p_align - 1ULL)) != 0) {
        return ELF64_LOADER_ERR_BAD_SEGMENT;
    }
    if (phdr->p_align > 1 && ((phdr->p_vaddr - phdr->p_offset) & (phdr->p_align - 1ULL)) != 0) {
        return ELF64_LOADER_ERR_BAD_SEGMENT;
    }

    /*
     * H.5b.2 step B / γ.exec-fix: PT_LOADs sit at USER_VBASE+0x400000
     * (high-half, PML4[1]). Historically we mapped user VA to the fixed
     * phys = va - USER_VBASE, which caused *every* user ELF (launcher,
     * hello64_v2, hello_fork...) to share the same physical footprint
     * around 0x400000+. As soon as two AS destroy in sequence (execve or
     * process exit) we hit pmm double-free on those pages.
     *
     * Fix (plan A): each load_segment page allocates a *fresh* phys via
     * pmm_alloc_page(), stages the image bytes through the boot 0..4GiB
     * identity window (phys_to_va(phys) == phys), then binds the user VA
     * to that private phys in target_as. free_subtree() on as_destroy now
     * frees each ELF's own pages exactly once -- no cross-AS aliasing.
     */
    if (phdr->p_vaddr < OPENOS_X86_64_USER_VBASE) {
        return ELF64_LOADER_ERR_BAD_SEGMENT;
    }
    if (target_as == NULL) {
        /* Loading an ELF without a target AS would still trigger the
         * legacy aliasing bug. All live call sites pass a non-NULL AS
         * (kernel64 boot path + execve). Refuse instead of silently
         * corrupting the PMM. */
        return ELF64_LOADER_ERR_BAD_ARGUMENT;
    }

    seg_start = align_down_addr(phdr->p_vaddr);
    seg_end = align_up_addr(phdr->p_vaddr + phdr->p_memsz);
    (void)flags_from_phdr;  /* boot-vmm mirror retired in step B */
    early_console64_write("[elf64] seg va=");
    early_console64_write_hex64(phdr->p_vaddr);
    early_console64_write(" memsz=");
    early_console64_write_hex64(phdr->p_memsz);
    early_console64_write(" pages=");
    early_console64_write_hex64((seg_end - seg_start) / OPENOS_X86_64_VMM_PAGE_SIZE);
    early_console64_write("\n");

    /*
     * Walk pages in one pass: allocate a fresh phys, zero+copy the slice of
     * file bytes belonging to this page through the identity window, then
     * map user VA -> phys in target_as.
     */
    {
        x86_64_virt_addr_t page_va;
        const uint8_t *file_src = image + phdr->p_offset;
        x86_64_virt_addr_t file_lo = phdr->p_vaddr;
        x86_64_virt_addr_t file_hi = phdr->p_vaddr + phdr->p_filesz;

        for (page_va = seg_start; page_va < seg_end; page_va += OPENOS_X86_64_VMM_PAGE_SIZE) {
            x86_64_phys_addr_t phys = arch_x86_64_pmm_alloc_page();
            if (phys == 0) {
                return ELF64_LOADER_ERR_BAD_SEGMENT;
            }
            ++elf64_loader_info.mapped_pages;

            /* Zero the whole page (fresh from PMM has undefined contents). */
            mem_zero((void *)(uintptr_t)phys, OPENOS_X86_64_VMM_PAGE_SIZE);

            /* Compute intersection [page_va, page_va+PAGE) ∩ [file_lo, file_hi)
             * and copy that slice from the ELF image into phys+off. */
            x86_64_virt_addr_t page_end = page_va + OPENOS_X86_64_VMM_PAGE_SIZE;
            x86_64_virt_addr_t copy_lo = page_va > file_lo ? page_va : file_lo;
            x86_64_virt_addr_t copy_hi = page_end < file_hi ? page_end : file_hi;
            if (copy_lo < copy_hi) {
                uint64_t page_off = copy_lo - page_va;
                uint64_t src_off = copy_lo - file_lo;
                mem_copy((void *)(uintptr_t)(phys + page_off),
                         file_src + src_off,
                         (x86_64_size_t)(copy_hi - copy_lo));
            }

            if (arch_x86_64_as_map_user(target_as, page_va, phys,
                                     OPENOS_X86_64_VMM_PAGE_SIZE,
                                     as_flags_from_phdr(phdr)) != 0) {
                early_console64_write("[elf64] map_user FAIL va=");
                early_console64_write_hex64(page_va);
                early_console64_write(" phys=");
                early_console64_write_hex64(phys);
                early_console64_write("\n");
                return ELF64_LOADER_ERR_BAD_SEGMENT;
            }
            if (copy_lo < copy_hi) {
                early_console64_write("[elf64] page va=");
                early_console64_write_hex64(page_va);
                early_console64_write(" phys=");
                early_console64_write_hex64(phys);
                early_console64_write(" first8=");
                early_console64_write_hex64(*(volatile uint64_t *)(uintptr_t)phys);
                early_console64_write("\n");
            }
        }
    }

    if (result->load_segments == 0 || seg_start < result->low_addr) {
        result->low_addr = seg_start;
    }
    if (seg_end > result->high_addr) {
        result->high_addr = seg_end;
    }
    result->brk_start = result->high_addr;
    ++result->load_segments;
    return ELF64_LOADER_OK;
}

void arch_x86_64_elf64_loader_init(void) {
    elf64_loader_info.attempted_loads = 0;
    elf64_loader_info.successful_loads = 0;
    elf64_loader_info.failed_loads = 0;
    elf64_loader_info.mapped_pages = 0;
    elf64_loader_info.last_status = ELF64_LOADER_OK;
}

elf64_load_result_t arch_x86_64_elf64_load_image(const void *image, x86_64_size_t image_size) {
    return arch_x86_64_elf64_load_image_into(image, image_size, NULL);
}

elf64_load_result_t arch_x86_64_elf64_load_image_into(
    const void *image, x86_64_size_t image_size,
    struct x86_64_address_space *target_as) {
    const uint8_t *bytes = (const uint8_t *)image;
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)image;
    elf64_load_result_t result;
    elf64_loader_status_t status;
    uint16_t i;

    result.entry = 0;
    result.low_addr = 0;
    result.high_addr = 0;
    result.brk_start = 0;
    result.load_segments = 0;
    result.status = ELF64_LOADER_ERR_BAD_ARGUMENT;

    ++elf64_loader_info.attempted_loads;
    if (!image || image_size == 0) {
        status = ELF64_LOADER_ERR_BAD_ARGUMENT;
        goto finish;
    }

    status = validate_header(ehdr, image_size);
    if (status != ELF64_LOADER_OK) {
        goto finish;
    }

    for (i = 0; i < ehdr->e_phnum; ++i) {
        const elf64_phdr_t *phdr = (const elf64_phdr_t *)(const void *)(bytes + ehdr->e_phoff + ((uint64_t)i * ehdr->e_phentsize));
        if (phdr->p_type != OPENOS_ELF64_PT_LOAD) {
            continue;
        }
        status = map_segment(bytes, image_size, phdr, &result, target_as);
        if (status != ELF64_LOADER_OK) {
            goto finish;
        }
    }

    if (result.load_segments == 0) {
        status = ELF64_LOADER_ERR_BAD_SEGMENT;
        goto finish;
    }

    result.entry = ehdr->e_entry;
    result.status = ELF64_LOADER_OK;
    status = ELF64_LOADER_OK;

finish:
    elf64_loader_info.last_status = status;
    if (status == ELF64_LOADER_OK) {
        ++elf64_loader_info.successful_loads;
    } else {
        ++elf64_loader_info.failed_loads;
        result.status = status;
    }
    return result;
}

const elf64_loader_info_t *arch_x86_64_elf64_loader_get_info(void) {
    return &elf64_loader_info;
}

void arch_x86_64_elf64_loader_print_status(void) {
    early_console64_write("[x86_64][ELF64] loads=");
    early_console64_write_hex64(elf64_loader_info.attempted_loads);
    early_console64_write(" ok=");
    early_console64_write_hex64(elf64_loader_info.successful_loads);
    early_console64_write(" fail=");
    early_console64_write_hex64(elf64_loader_info.failed_loads);
    early_console64_write(" mapped_pages=");
    early_console64_write_hex64(elf64_loader_info.mapped_pages);
    early_console64_write(" last=");
    early_console64_write_hex64((uint64_t)(int64_t)elf64_loader_info.last_status);
    early_console64_write("\n");
}
