#ifndef OPENOS_ARCH_X86_64_INITRD64_H
#define OPENOS_ARCH_X86_64_INITRD64_H

#include <stdint.h>

#include "arch64_types.h"
#include "bootinfo.h"

#define OPENOS_X86_64_INITRD_MAX_FILES 32u
#define OPENOS_X86_64_INITRD_NAME_MAX  64u
#define OPENOS_X86_64_INITRD_MAGIC     0x494E52443634554CULL /* "INRD64UL" */

typedef struct x86_64_initrd_file {
    char name[OPENOS_X86_64_INITRD_NAME_MAX];
    const uint8_t *data;
    x86_64_size_t size;
    uint32_t mode;
} x86_64_initrd_file_t;

typedef struct x86_64_initrd_image {
    uint64_t magic;
    uint32_t file_count;
    const x86_64_initrd_file_t *files;
} x86_64_initrd_image_t;

void arch_x86_64_initrd_init(const openos_bootinfo_t *bootinfo);
const x86_64_initrd_image_t *arch_x86_64_initrd_get_image(void);
const x86_64_initrd_file_t *arch_x86_64_initrd_find(const char *path);
void arch_x86_64_initrd_print_status(void);

#endif /* OPENOS_ARCH_X86_64_INITRD64_H */
