/* ============================================================
 * openos - 临时内存文件系统 (tmpfs)
 *
 * tmpfs 是基于页分配器的易失性内存文件系统，适合挂载到 /tmp。
 * 当前实现复用 ramfs 的文件读写块管理能力，并提供独立的
 * tmpfs 文件系统类型、挂载入口和 inode 标记。
 * ============================================================ */

#ifndef KERNEL_FS_TMPFS_H
#define KERNEL_FS_TMPFS_H

#include "vfs.h"

#define TMPFS_MAGIC 0x01021994u

/* 初始化 tmpfs 子系统 */
void tmpfs_init(void);

/* 挂载一个新的 tmpfs 实例到指定目录，目录必须存在 */
int tmpfs_mount(const char *path);

/* 设置 inode 的 tmpfs 类型和操作表 */
void tmpfs_setup_inode(inode_t *ip, uint32_t mode);

/* 获取基础 tmpfs 文件系统类型，主要用于调试 */
fs_type_t *tmpfs_get_fs_type(void);

#endif /* KERNEL_FS_TMPFS_H */
