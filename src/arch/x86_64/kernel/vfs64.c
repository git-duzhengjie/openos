#include "../include/vfs64.h"

#include <stddef.h>

#include "../include/early_console64.h"

static x86_64_vfs_node_t mounted_nodes[OPENOS_X86_64_INITRD_MAX_FILES];
static uint32_t mounted_node_count;
static uint8_t vfs_ready;
static uint8_t initrd_mounted;
static uint64_t lookup_count;
static uint64_t read_count;

static int vfs_streq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

void arch_x86_64_vfs_init(void) {
    uint32_t i;
    vfs_ready = 1u;
    initrd_mounted = 0;
    mounted_node_count = 0;
    lookup_count = 0;
    read_count = 0;
    for (i = 0; i < OPENOS_X86_64_INITRD_MAX_FILES; ++i) {
        mounted_nodes[i].path = NULL;
        mounted_nodes[i].data = NULL;
        mounted_nodes[i].size = 0;
        mounted_nodes[i].mode = 0;
    }
}

int arch_x86_64_vfs_mount_initrd(const x86_64_initrd_image_t *image) {
    uint32_t i;
    if (!vfs_ready || image == NULL || image->magic != OPENOS_X86_64_INITRD_MAGIC || image->files == NULL) {
        return -1;
    }
    if (image->file_count > OPENOS_X86_64_INITRD_MAX_FILES) {
        return -2;
    }

    mounted_node_count = image->file_count;
    for (i = 0; i < mounted_node_count; ++i) {
        mounted_nodes[i].path = image->files[i].name;
        mounted_nodes[i].data = image->files[i].data;
        mounted_nodes[i].size = image->files[i].size;
        mounted_nodes[i].mode = image->files[i].mode;
    }
    initrd_mounted = 1u;
    return 0;
}

const x86_64_vfs_node_t *arch_x86_64_vfs_lookup(const char *path) {
    uint32_t i;
    ++lookup_count;
    if (!vfs_ready || !initrd_mounted || path == NULL) {
        return NULL;
    }
    for (i = 0; i < mounted_node_count; ++i) {
        if (vfs_streq(path, mounted_nodes[i].path)) {
            return &mounted_nodes[i];
        }
    }
    return NULL;
}

int arch_x86_64_vfs_read_all(const char *path, const uint8_t **data, x86_64_size_t *size) {
    const x86_64_vfs_node_t *node;
    ++read_count;
    if (data == NULL || size == NULL) {
        return -1;
    }
    node = arch_x86_64_vfs_lookup(path);
    if (node == NULL) {
        *data = NULL;
        *size = 0;
        return -2;
    }
    *data = node->data;
    *size = node->size;
    return 0;
}

void arch_x86_64_vfs_print_status(void) {
    early_console64_write("[x86_64][vfs] ready=");
    early_console64_write_hex64(vfs_ready);
    early_console64_write(" initrd_mounted=");
    early_console64_write_hex64(initrd_mounted);
    early_console64_write(" nodes=");
    early_console64_write_hex64(mounted_node_count);
    early_console64_write(" lookups=");
    early_console64_write_hex64(lookup_count);
    early_console64_write(" reads=");
    early_console64_write_hex64(read_count);
    early_console64_write("\n");
}
