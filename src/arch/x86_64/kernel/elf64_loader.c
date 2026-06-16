#include "../include/early_console64.h"
#include "../include/elf64_loader.h"
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

static elf64_loader_status_t map_segment(const uint8_t *image,
                                         x86_64_size_t image_size,
                                         const elf64_phdr_t *phdr,
                                         elf64_load_result_t *result) {
    x86_64_virt_addr_t seg_start;
    x86_64_virt_addr_t seg_end;
    x86_64_virt_addr_t va;
    uint64_t flags;

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

    seg_start = align_down_addr(phdr->p_vaddr);
    seg_end = align_up_addr(phdr->p_vaddr + phdr->p_memsz);
    flags = flags_from_phdr(phdr);

    for (va = seg_start; va < seg_end; va += OPENOS_X86_64_VMM_PAGE_SIZE) {
        x86_64_phys_addr_t phys = arch_x86_64_pmm_alloc_page();
        if (phys == 0) {
            return ELF64_LOADER_ERR_NO_MEMORY;
        }
        if (arch_x86_64_vmm_map_page(va, phys, flags) != 0) {
            arch_x86_64_pmm_free_page(phys);
            return ELF64_LOADER_ERR_MAP_FAILED;
        }
        ++elf64_loader_info.mapped_pages;
    }

    mem_zero((void *)(uintptr_t)phdr->p_vaddr, (x86_64_size_t)phdr->p_memsz);
    mem_copy((void *)(uintptr_t)phdr->p_vaddr,
             image + phdr->p_offset,
             (x86_64_size_t)phdr->p_filesz);

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
        status = map_segment(bytes, image_size, phdr, &result);
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
