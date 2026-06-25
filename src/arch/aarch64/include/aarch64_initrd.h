#ifndef OPENOS_ARCH_AARCH64_INITRD_H
#define OPENOS_ARCH_AARCH64_INITRD_H

#include <stddef.h>
#include <stdint.h>

#define OPENOS_AARCH64_INITRD_MAGIC 0x41495244494e4954ULL /* "AIRDINIT" */
#define OPENOS_AARCH64_INITRD_MAX_FILES 8u

typedef struct aarch64_initrd_file {
    const char *name;
    const uint8_t *data;
    size_t size;
    uint32_t mode;
} aarch64_initrd_file_t;

typedef struct aarch64_initrd_image {
    uint64_t magic;
    uint32_t file_count;
    const aarch64_initrd_file_t *files;
} aarch64_initrd_image_t;

void aarch64_initrd_init(void);
const aarch64_initrd_image_t *aarch64_initrd_get_image(void);
const aarch64_initrd_file_t *aarch64_initrd_find(const char *path);
void aarch64_initrd_print_status(void);

#endif /* OPENOS_ARCH_AARCH64_INITRD_H */
