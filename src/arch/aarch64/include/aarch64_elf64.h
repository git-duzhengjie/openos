#ifndef OPENOS_ARCH_AARCH64_ELF64_H
#define OPENOS_ARCH_AARCH64_ELF64_H

#include <stddef.h>
#include <stdint.h>

#define AARCH64_ELF64_MAX_LOAD_SEGMENTS 8u

typedef struct aarch64_elf64_segment {
    uintptr_t virtual_address;
    uintptr_t memory_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint32_t flags;
} aarch64_elf64_segment_t;

typedef struct aarch64_elf64_image {
    uintptr_t entry;
    uint32_t segment_count;
    aarch64_elf64_segment_t segments[AARCH64_ELF64_MAX_LOAD_SEGMENTS];
} aarch64_elf64_image_t;

int aarch64_elf64_load(const void *elf_data, size_t elf_size, aarch64_elf64_image_t *image);
void aarch64_elf64_print_image(const aarch64_elf64_image_t *image);

#endif /* OPENOS_ARCH_AARCH64_ELF64_H */
