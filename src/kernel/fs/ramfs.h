/* ============================================================
 * openos - RAM 文件系统 (ramfs) - Phase 3
 * 
 * 内存文件系统，文件内容存储在物理页中
 * ============================================================ */

#ifndef KERNEL_FS_RAMFS_H
#define KERNEL_FS_RAMFS_H

#include "vfs.h"

/* ramfs 文件数据块 */
#define RAMFS_BLOCK_SIZE  4096
#define RAMFS_MAX_BLOCKS  64   /* 每文件最大 256KB */

typedef struct ramfs_file {
    uint32_t blocks[RAMFS_MAX_BLOCKS];  /* 物理页地址 */
    uint32_t nblocks;                    /* 已分配块数 */
    uint32_t size;                       /* 实际大小 */
} ramfs_file_t;

/* 初始化 ramfs 并挂载到 / */
void ramfs_init(void);

/* 设置 inode 的 ramfs ops */
void ramfs_setup_inode(inode_t *ip, uint32_t mode);

/* 获取 ramfs 文件系统类型 */
fs_type_t *ramfs_get_fs_type(void);

#endif /* KERNEL_FS_RAMFS_H */
