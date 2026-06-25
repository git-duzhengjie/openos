/* ============================================================
 * openos - RAM 文件系统 (ramfs) - Phase 3
 * 
 * 内存文件系统，文件内容存储在物理页中
 * ============================================================ */

#ifndef KERNEL_FS_RAMFS_H
#define KERNEL_FS_RAMFS_H

#include "vfs.h"

/* ramfs 文件系统标识和文件数据块 */
#define RAMFS_MAGIC       0x858458F6u
#define RAMFS_BLOCK_SIZE  4096
/* 每文件最大 2MB。ramfs_file_t 由单页保存元数据，512 个块指针仍小于 4KB。 */
#define RAMFS_MAX_BLOCKS  512

typedef struct ramfs_file {
    uint32_t blocks[RAMFS_MAX_BLOCKS];  /* 物理页地址 */
    uint32_t nblocks;                    /* 已分配块数 */
    uint32_t size;                       /* 实际大小 */
} ramfs_file_t;

/* 初始化 ramfs 并挂载到 / */
void ramfs_init(void);

/* 刷新 ramfs ops，避免早期 .data/.bss 清理或内存覆盖导致函数表为空 */
void ramfs_refresh_ops(void);

/* 设置 inode 的 ramfs ops */
void ramfs_setup_inode(inode_t *ip, uint32_t mode);

/* 获取 ramfs 文件系统类型 */
fs_type_t *ramfs_get_fs_type(void);

/* ramfs file ops (用于调试) */
extern file_ops_t ramfs_file_ops;

/* 直接调用 ramfs 写逻辑，避免跨模块通过可变 ops 表间接跳转 */
int ramfs_write_fallback(file_t *f, const void *buf, uint32_t count);

#endif /* KERNEL_FS_RAMFS_H */
