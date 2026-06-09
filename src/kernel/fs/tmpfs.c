/* ============================================================
 * openos - 临时内存文件系统实现 (tmpfs)
 * ============================================================ */

#include "tmpfs.h"
#include "ramfs.h"
#include "serial.h"
#include "pmm.h"
#include "string.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#define TMPFS_MAX_MOUNTS 8

extern file_ops_t ramfs_file_ops;

static file_ops_t tmpfs_dir_ops = {
    .open = NULL,
    .close = NULL,
    .read = NULL,
    .write = NULL,
    .seek = NULL,
    .truncate = NULL,
    .readdir = NULL,
};

typedef struct tmpfs_instance {
    fs_type_t fs;
    inode_t root_inode;
    int used;
} tmpfs_instance_t;

static tmpfs_instance_t tmpfs_instances[TMPFS_MAX_MOUNTS];

static inode_t *tmpfs_lookup(fs_type_t *fs, const char *path) {
    if (!fs || !path) return NULL;
    if (strcmp(path, "/") != 0) return NULL;

    tmpfs_instance_t *inst = (tmpfs_instance_t *)fs->data;
    if (!inst || !inst->used) return NULL;
    return &inst->root_inode;
}

static fs_type_t tmpfs_base_type = {
    .name = "tmpfs",
    .magic = TMPFS_MAGIC,
    .lookup = tmpfs_lookup,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .data = NULL,
};

static tmpfs_instance_t *tmpfs_alloc_instance(void) {
    for (int i = 0; i < TMPFS_MAX_MOUNTS; i++) {
        if (!tmpfs_instances[i].used) {
            tmpfs_instance_t *inst = &tmpfs_instances[i];
            memset(inst, 0, sizeof(tmpfs_instance_t));
            inst->used = 1;

            memcpy(&inst->fs, &tmpfs_base_type, sizeof(fs_type_t));
            inst->fs.data = inst;

            inst->root_inode.ino = 0;
            inst->root_inode.mode = FS_DIR | S_IRUSR | S_IWUSR;
            inst->root_inode.size = 0;
            inst->root_inode.nlinks = 1;
            inst->root_inode.ref_count = 1;
            tmpfs_setup_inode(&inst->root_inode, inst->root_inode.mode);
            return inst;
        }
    }
    return NULL;
}

void tmpfs_setup_inode(inode_t *ip, uint32_t mode) {
    if (!ip) return;
    ip->fs_type = TMPFS_MAGIC;
    if ((mode & FS_DIR) == FS_DIR) {
        ip->ops = &tmpfs_dir_ops;
    } else {
        ip->ops = &ramfs_file_ops;
    }
}

fs_type_t *tmpfs_get_fs_type(void) {
    return &tmpfs_base_type;
}

void tmpfs_init(void) {
    memset(tmpfs_instances, 0, sizeof(tmpfs_instances));
    serial_write("[OK] tmpfs initialized\n");
}

int tmpfs_mount(const char *path) {
    if (!path) return -1;

    tmpfs_instance_t *inst = tmpfs_alloc_instance();
    if (!inst) return -1;

    if (vfs_mount(path, &inst->fs) < 0) {
        memset(inst, 0, sizeof(tmpfs_instance_t));
        return -1;
    }

    serial_write("[TMPFS] mounted at ");
    serial_write(path);
    serial_write("\n");
    return 0;
}
