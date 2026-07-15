#ifndef OPENOS_ARCH_X86_64_BOOT_ELF64_H
#define OPENOS_ARCH_X86_64_BOOT_ELF64_H

#include <stdint.h>
#include "uefi64.h"

/* ELF64 基础类型 */
typedef uint64_t elf64_addr_t;
typedef uint64_t elf64_off_t;
typedef uint16_t elf64_half_t;
typedef uint32_t elf64_word_t;
typedef int32_t  elf64_sword_t;
typedef uint64_t elf64_xword_t;
typedef int64_t  elf64_sxword_t;

#define ELF64_MAGIC 0x464C457F /* 0x7F + "ELF" */
#define ELF64_CLASS_64 2
#define ELF64_DATA_LSB 1
#define ELF64_TYPE_EXEC 2
#define ELF64_MACHINE_X86_64 62

#define ELF64_PT_LOAD 1
#define ELF64_PF_X (1 << 0)
#define ELF64_PF_W (1 << 1)
#define ELF64_PF_R (1 << 2)

/* ELF64 标识 */
typedef struct {
    uint8_t  magic[4];
    uint8_t  class;
    uint8_t  data;
    uint8_t  version;
    uint8_t  osabi;
    uint8_t  abiversion;
    uint8_t  pad[7];
} elf64_ident_t;

/* ELF64 头部 */
typedef struct {
    elf64_ident_t  ident;
    elf64_half_t   type;
    elf64_half_t   machine;
    elf64_word_t   version;
    elf64_addr_t   entry;
    elf64_off_t    phoff;
    elf64_off_t    shoff;
    elf64_word_t   flags;
    elf64_half_t   ehsize;
    elf64_half_t   phentsize;
    elf64_half_t   phnum;
    elf64_half_t   shentsize;
    elf64_half_t   shnum;
    elf64_half_t   shstrndx;
} elf64_ehdr_t;

/* ELF64 程序头 */
typedef struct {
    elf64_word_t   type;
    elf64_word_t   flags;
    elf64_off_t    offset;
    elf64_addr_t   vaddr;
    elf64_addr_t   paddr;
    elf64_xword_t  filesz;
    elf64_xword_t  memsz;
    elf64_xword_t  align;
} elf64_phdr_t;

static inline int elf64_validate(const uint8_t *buffer, uint64_t size)
{
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)buffer;

    if (size < sizeof(elf64_ehdr_t)) {
        return 0;
    }
    if (*(const uint32_t *)ehdr->ident.magic != ELF64_MAGIC) {
        return 0;
    }
    if (ehdr->ident.class != ELF64_CLASS_64) {
        return 0;
    }
    if (ehdr->ident.data != ELF64_DATA_LSB) {
        return 0;
    }
    if (ehdr->machine != ELF64_MACHINE_X86_64) {
        return 0;
    }
    if (ehdr->type != ELF64_TYPE_EXEC) {
        return 0;
    }

    return 1;
}

static inline uint64_t elf64_get_entry(const uint8_t *buffer)
{
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)buffer;
    const elf64_phdr_t *phdrs = (const elf64_phdr_t *)(buffer + ehdr->phoff);
    uint64_t entry = ehdr->entry;

    /* 如果 entry 是高半区虚地址（未开分页不可访问），按首个 LOAD
     * 段的 vaddr->paddr 差调到物理地址。内核启动初期代码必须用
     * paddr 运行，启用分页后再跳到 vaddr。 */
    if ((entry >> 48) == 0xFFFF) {
        for (uint16_t i = 0; i < ehdr->phnum; ++i) {
            const elf64_phdr_t *ph = &phdrs[i];
            if (ph->type == ELF64_PT_LOAD && entry >= ph->vaddr && entry < ph->vaddr + ph->memsz) {
                return (entry - ph->vaddr) + ph->paddr;
            }
        }
    }
    return entry;
}

static inline efi_status_t elf64_load_segments(efi_system_table64_t *system_table,
                                                const uint8_t *buffer, uint64_t size,
                                                uint64_t *out_reloc_delta)
{
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)buffer;
    const elf64_phdr_t *phdrs;
    uint64_t i;
    efi_status_t status;
    uint32_t memory_type = 0x00000002U; /* EfiLoaderCode */
    uint64_t reloc_delta = 0; /* phys offset applied if we relocate */

    if (out_reloc_delta) *out_reloc_delta = 0;

    if (!elf64_validate(buffer, size)) {
        return EFI_LOAD_ERROR;
    }

    phdrs = (const elf64_phdr_t *)(buffer + ehdr->phoff);

    /* -----------------------------------------------------------------
     * M6.11.3-fix: reserve the WHOLE kernel physical span as ONE
     * contiguous block, instead of per-segment AllocateAddress (which,
     * once the image grew past a certain size, could fail for a higher
     * segment and silently fall back to AnyPages — scattering that
     * segment to a random physical page while entry64.S's boot page
     * tables assume all PT_LOAD segments sit contiguously from
     * _start64's phys base. That mismatch made the kernel read rodata
     * (embedded ELFs, jump tables) from a blank page → bogus function
     * pointer → RIP flew to 0x000a0004 right after the higher-half jump.
     *
     * Strategy: compute [min_paddr, max_paddr+memsz), 2MiB-align the
     * base (entry64.S requires 2MiB alignment via `andq $-0x200000`),
     * AllocateAddress the entire span at ONCE (not per-segment). If the
     * linked base is occupied by firmware, fall back to a SINGLE
     * AllocateAnyPages of the same 2MiB-aligned span and RELOCATE every
     * segment by the same delta. This is safe because entry64.S derives
     * kernel_phys_base from its own runtime RIP (`phys(_start64) =
     * vaddr - r10`) and hard-maps pd_hi from there, so it self-heals to
     * whatever contiguous base we picked. We publish the delta via
     * *out_reloc_delta so the caller can add it to the entry point.
     * Allocating as ONE block preserves the contiguity invariant the
     * boot page tables assume.
     * --------------------------------------------------------------- */
    {
        uint64_t min_paddr = ~0ULL;
        uint64_t max_end   = 0;
        for (i = 0; i < ehdr->phnum; ++i) {
            const elf64_phdr_t *ph = &phdrs[i];
            if (ph->type != ELF64_PT_LOAD) continue;
            uint64_t p = (ph->paddr != 0) ? ph->paddr : ph->vaddr;
            uint64_t e = p + ph->memsz;
            if (p < min_paddr) min_paddr = p;
            if (e > max_end)   max_end   = e;
        }
        if (min_paddr == ~0ULL) {
            return EFI_LOAD_ERROR; /* no PT_LOAD */
        }
        /* 2MiB-align base down; align total span up to 2MiB */
        uint64_t span_base = min_paddr & ~0x1FFFFFULL;
        uint64_t span_end  = (max_end + 0x1FFFFFULL) & ~0x1FFFFFULL;
        uint64_t span_pages = (span_end - span_base) >> 12;
        uint64_t span_addr = span_base;

        status = system_table->boot_services->allocate_pages(
            1, /* AllocateAddress */ memory_type, span_pages, &span_addr);
        if (status == EFI_SUCCESS) {
            reloc_delta = 0;
            uefi64_serial_write("[UEFI][elf] reserved kernel span (fixed) base=");
            uefi64_serial_write_hex64(span_base);
            uefi64_serial_write(" end=");
            uefi64_serial_write_hex64(span_end);
            uefi64_serial_write(" pages=");
            uefi64_serial_write_hex64(span_pages);
            uefi64_serial_write("\n");
        } else {
            /* Linked base occupied -> relocate. Over-allocate by one 2MiB
             * slack page so we can 2MiB-align the AnyPages result upward. */
            uint64_t any_pages = span_pages + (0x200000ULL >> 12);
            uint64_t any_addr  = 0;
            status = system_table->boot_services->allocate_pages(
                0, /* AllocateAnyPages */ memory_type, any_pages, &any_addr);
            if (status != EFI_SUCCESS) {
                uefi64_serial_write("[UEFI][elf] AnyPages fallback FAILED pages=");
                uefi64_serial_write_hex64(any_pages);
                uefi64_serial_write(" status=");
                uefi64_serial_write_hex64(status);
                uefi64_serial_write(" -- FATAL\n");
                return status;
            }
            uint64_t new_base = (any_addr + 0x1FFFFFULL) & ~0x1FFFFFULL;
            reloc_delta = new_base - span_base;
            uefi64_serial_write("[UEFI][elf] linked base occupied; RELOCATED. any_addr=");
            uefi64_serial_write_hex64(any_addr);
            uefi64_serial_write(" new_base=");
            uefi64_serial_write_hex64(new_base);
            uefi64_serial_write(" delta=");
            uefi64_serial_write_hex64(reloc_delta);
            uefi64_serial_write("\n");
        }
    }

    for (i = 0; i < ehdr->phnum; ++i) {
        const elf64_phdr_t *ph = &phdrs[i];
        uint8_t *dest;

        if (ph->type != ELF64_PT_LOAD) {
            continue;
        }

        /* Physical memory for the whole image is already reserved above.
         * Each segment lands at its linked paddr + reloc_delta (contiguity
         * invariant preserved), so we just zero memsz then copy filesz. */
        uint64_t load_addr = ((ph->paddr != 0) ? ph->paddr : ph->vaddr) + reloc_delta;

        /* 清零 bss 部分 */
        dest = (uint8_t *)(uintptr_t)load_addr;
        for (uint64_t j = 0; j < ph->memsz; ++j) {
            dest[j] = 0;
        }

        /* 复制数据 */
        for (uint64_t j = 0; j < ph->filesz; ++j) {
            dest[j] = buffer[ph->offset + j];
        }
    }

    if (out_reloc_delta) *out_reloc_delta = reloc_delta;
    return EFI_SUCCESS;
}

#endif /* OPENOS_ARCH_X86_64_BOOT_ELF64_H */
