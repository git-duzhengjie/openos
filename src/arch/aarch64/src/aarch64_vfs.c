#include "aarch64_vfs.h"

#include "aarch64_uart.h"

static aarch64_vfs_node_t aarch64_vfs_nodes[OPENOS_AARCH64_INITRD_MAX_FILES];
static uint32_t aarch64_vfs_node_count;
static uint8_t aarch64_vfs_ready;

static int aarch64_streq(const char *lhs, const char *rhs) {
    if (!lhs || !rhs) {
        return 0;
    }

    while (*lhs && *rhs) {
        if (*lhs != *rhs) {
            return 0;
        }
        ++lhs;
        ++rhs;
    }

    return *lhs == *rhs;
}

void aarch64_vfs_init(void) {
    for (uint32_t i = 0; i < OPENOS_AARCH64_INITRD_MAX_FILES; ++i) {
        aarch64_vfs_nodes[i].path = 0;
        aarch64_vfs_nodes[i].data = 0;
        aarch64_vfs_nodes[i].size = 0;
        aarch64_vfs_nodes[i].mode = 0;
    }
    aarch64_vfs_node_count = 0;
    aarch64_vfs_ready = 1;
}

int aarch64_vfs_mount_initrd(const aarch64_initrd_image_t *image) {
    if (!aarch64_vfs_ready || !image || image->magic != OPENOS_AARCH64_INITRD_MAGIC || !image->files) {
        return -1;
    }

    if (image->file_count > OPENOS_AARCH64_INITRD_MAX_FILES) {
        return -2;
    }

    for (uint32_t i = 0; i < image->file_count; ++i) {
        aarch64_vfs_nodes[i].path = image->files[i].name;
        aarch64_vfs_nodes[i].data = image->files[i].data;
        aarch64_vfs_nodes[i].size = image->files[i].size;
        aarch64_vfs_nodes[i].mode = image->files[i].mode;
    }
    aarch64_vfs_node_count = image->file_count;
    return 0;
}

const aarch64_vfs_node_t *aarch64_vfs_lookup(const char *path) {
    if (!aarch64_vfs_ready || !path) {
        return 0;
    }

    for (uint32_t i = 0; i < aarch64_vfs_node_count; ++i) {
        if (aarch64_streq(aarch64_vfs_nodes[i].path, path)) {
            return &aarch64_vfs_nodes[i];
        }
    }

    return 0;
}

int aarch64_vfs_read_all(const char *path, const uint8_t **data, size_t *size) {
    const aarch64_vfs_node_t *node = aarch64_vfs_lookup(path);
    if (!node || !data || !size) {
        return -1;
    }

    *data = node->data;
    *size = node->size;
    return 0;
}

void aarch64_vfs_print_status(void) {
    aarch64_uart_write("[aarch64][vfs] mounted nodes=");
    char count = (char)('0' + aarch64_vfs_node_count);
    aarch64_uart_putc(count);
    aarch64_uart_write("\n");
}
