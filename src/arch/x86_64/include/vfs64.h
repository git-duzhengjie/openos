#ifndef OPENOS_ARCH_X86_64_VFS64_H
#define OPENOS_ARCH_X86_64_VFS64_H

#include <stdint.h>

#include "arch64_types.h"
#include "initrd64.h"

typedef struct x86_64_vfs_node {
    const char *path;
    const uint8_t *data;
    x86_64_size_t size;
    uint32_t mode;
} x86_64_vfs_node_t;

void arch_x86_64_vfs_init(void);
int arch_x86_64_vfs_mount_initrd(const x86_64_initrd_image_t *image);
const x86_64_vfs_node_t *arch_x86_64_vfs_lookup(const char *path);
int arch_x86_64_vfs_read_all(const char *path, const uint8_t **data, x86_64_size_t *size);
void arch_x86_64_vfs_print_status(void);

#endif /* OPENOS_ARCH_X86_64_VFS64_H */
