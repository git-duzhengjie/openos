#include "aarch64_elf64.h"

#include "aarch64_memory.h"
#include "aarch64_uart.h"

#define EI_NIDENT 16u
#define PT_LOAD 1u
#define EM_AARCH64 183u
#define ET_EXEC 2u
#define ET_DYN 3u
#define ELFMAG0 0x7fu
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2u
#define ELFDATA2LSB 1u

typedef struct elf64_ehdr {
    unsigned char e_ident[EI_NIDENT];
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

static void *copy_bytes(void *dst, const void *src, size_t len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
    return dst;
}

static void zero_bytes(void *dst, size_t len)
{
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < len; ++i) {
        d[i] = 0;
    }
}

static int validate_header(const elf64_ehdr_t *hdr, size_t elf_size)
{
    if (hdr == 0 || elf_size < sizeof(*hdr)) {
        return -1;
    }
    if (hdr->e_ident[0] != ELFMAG0 || hdr->e_ident[1] != ELFMAG1 ||
        hdr->e_ident[2] != ELFMAG2 || hdr->e_ident[3] != ELFMAG3) {
        return -2;
    }
    if (hdr->e_ident[4] != ELFCLASS64 || hdr->e_ident[5] != ELFDATA2LSB) {
        return -3;
    }
    if (hdr->e_machine != EM_AARCH64) {
        return -4;
    }
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) {
        return -5;
    }
    if (hdr->e_phentsize != sizeof(elf64_phdr_t)) {
        return -6;
    }
    if (hdr->e_phoff + ((uint64_t)hdr->e_phnum * hdr->e_phentsize) > elf_size) {
        return -7;
    }
    return 0;
}

int aarch64_elf64_load(const void *elf_data, size_t elf_size, aarch64_elf64_image_t *image)
{
    const elf64_ehdr_t *hdr = (const elf64_ehdr_t *)elf_data;
    int rc = validate_header(hdr, elf_size);

    if (image == 0) {
        return -20;
    }
    image->entry = 0;
    image->segment_count = 0;

    if (rc != 0) {
        return rc;
    }

    image->entry = (uintptr_t)hdr->e_entry;

    for (uint16_t i = 0; i < hdr->e_phnum; ++i) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)((const unsigned char *)elf_data + hdr->e_phoff + ((uint64_t)i * hdr->e_phentsize));
        void *segment_mem;

        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (image->segment_count >= AARCH64_ELF64_MAX_LOAD_SEGMENTS) {
            return -8;
        }
        if (ph->p_offset + ph->p_filesz > elf_size || ph->p_memsz < ph->p_filesz) {
            return -9;
        }

        segment_mem = aarch64_heap_alloc((size_t)ph->p_memsz, ph->p_align >= 16u ? (size_t)ph->p_align : 16u);
        if (segment_mem == 0) {
            return -10;
        }

        copy_bytes(segment_mem, (const unsigned char *)elf_data + ph->p_offset, (size_t)ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            zero_bytes((unsigned char *)segment_mem + ph->p_filesz, (size_t)(ph->p_memsz - ph->p_filesz));
        }

        image->segments[image->segment_count].virtual_address = (uintptr_t)ph->p_vaddr;
        image->segments[image->segment_count].memory_address = (uintptr_t)segment_mem;
        image->segments[image->segment_count].file_size = ph->p_filesz;
        image->segments[image->segment_count].memory_size = ph->p_memsz;
        image->segments[image->segment_count].flags = ph->p_flags;

        if ((uint64_t)hdr->e_entry >= ph->p_vaddr && (uint64_t)hdr->e_entry < ph->p_vaddr + ph->p_memsz) {
            image->entry = (uintptr_t)segment_mem + (uintptr_t)((uint64_t)hdr->e_entry - ph->p_vaddr);
        }

        ++image->segment_count;
    }

    if (image->segment_count == 0u || image->entry == 0u) {
        return -11;
    }

    return 0;
}

void aarch64_elf64_print_image(const aarch64_elf64_image_t *image)
{
    if (image == 0) {
        aarch64_uart_write("A5.5: ELF64 image=null\n");
        return;
    }
    aarch64_uart_write("A5.5: ELF64 loader entry/segments ready\n");
}
