#ifndef OPENOS_ARCH_AARCH64_VFS_H
#define OPENOS_ARCH_AARCH64_VFS_H

#include <stddef.h>
#include <stdint.h>

#include "aarch64_initrd.h"

typedef struct aarch64_vfs_node {
    const char *path;
    const uint8_t *data;
    size_t size;
    uint32_t mode;
} aarch64_vfs_node_t;

void aarch64_vfs_init(void);
int aarch64_vfs_mount_initrd(const aarch64_initrd_image_t *image);
const aarch64_vfs_node_t *aarch64_vfs_lookup(const char *path);
int aarch64_vfs_read_all(const char *path, const uint8_t **data, size_t *size);
void aarch64_vfs_print_status(void);

#endif /* OPENOS_ARCH_AARCH64_VFS_H */
