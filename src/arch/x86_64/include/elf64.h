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
                                                const uint8_t *buffer, uint64_t size)
{
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)buffer;
    const elf64_phdr_t *phdrs;
    uint64_t i;
    efi_status_t status;
    uint32_t memory_type = 0x00000002U; /* EfiLoaderCode */

    if (!elf64_validate(buffer, size)) {
        return EFI_LOAD_ERROR;
    }

    phdrs = (const elf64_phdr_t *)(buffer + ehdr->phoff);

    for (i = 0; i < ehdr->phnum; ++i) {
        const elf64_phdr_t *ph = &phdrs[i];
        uint8_t *dest;

        if (ph->type != ELF64_PT_LOAD) {
            continue;
        }

        /* UEFI 下使用 PhysAddr 加载（内核高半区 vaddr=0xFFFFFFFF80000000 需
         * paddr=0x200000 才能被 UEFI identity map 访问）。在 boot64.asm
         * 启用分页后高半区 vaddr 会被映射到该物理页。 */
        uint64_t load_addr = (ph->paddr != 0) ? ph->paddr : ph->vaddr;
        uint64_t aligned_addr = load_addr & ~0xFFFULL;
        uint64_t aligned_size = ((load_addr - aligned_addr + ph->memsz + 0xFFFULL) & ~0xFFFULL);
        uint64_t pages = aligned_size >> 12;

        /* 分配内存 - 先试 AllocateAddress（保留 ELF 原定 paddr），失败则
         * 回退 AllocateAnyPages。为了内核 boot64.asm 该都在开分页前只依赖
         * UEFI identity map，AnyPages 后的地址仍能被访问。 */
        status = system_table->boot_services->allocate_pages(
            1, /* AllocateAddress */
            memory_type,
            pages,
            &aligned_addr);
        if (status != EFI_SUCCESS) {
            /* 回退：AnyPages，让 UEFI 选位置 */
            status = system_table->boot_services->allocate_pages(
                0, /* AllocateAnyPages */
                memory_type,
                pages,
                &aligned_addr);
            if (status != EFI_SUCCESS) {
                return status;
            }
            /* AnyPages 下 load_addr 不再是原始 paddr，调整 dest */
            load_addr = aligned_addr + (load_addr & 0xFFFULL);
        }

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

    return EFI_SUCCESS;
}

#endif /* OPENOS_ARCH_X86_64_BOOT_ELF64_H */
