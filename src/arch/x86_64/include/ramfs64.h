#ifndef OPENOS_ARCH_X86_64_RAMFS64_H
#define OPENOS_ARCH_X86_64_RAMFS64_H

/* ============================================================
 * openos x86_64 RAMFS —— 内存树形可读写文件系统
 *
 * 阶段一目标：提供一个真正的分层 VFS + 内存文件系统实现，
 * 取代原先 gui64_stubs.c 中基于 initrd 全路径前缀匹配的只读桩。
 *
 * 设计要点：
 *   - 真正的 parent/child/sibling 目录树（dentry_t）
 *   - 每个节点一个 inode_t（复用 core/fs/vfs.h 的结构定义）
 *   - 文件数据用 kmalloc 动态分配，支持读/写/追加/截断
 *   - 启动时把内嵌 initrd 的全路径文件解包成真实树节点
 *   - 对上仍导出 gui.c 依赖的 vfs_* 接口，签名 100% 不变
 * ============================================================ */

#include "types.h"
#include "core/fs/vfs.h"

/* 初始化 RAMFS：建根目录，导入 initrd 文件到内存树。
 * 由内核启动流程调用一次（在 initrd_init 之后）。 */
void ramfs_init(void);

/* ============================================================
 * 阶段二：磁盘快照持久化（配合 ata64 块设备）
 * ============================================================ */

/* 将当前 RAMFS 整棵树序列化写入磁盘快照。
 * 返回 0 成功，负数失败（无磁盘/写错误）。 */
int ramfs_snapshot_save(void);

/* 从磁盘快照恢复 RAMFS 树（覆盖当前内容）。
 * 必须在 ramfs_init() 之后调用。
 * 返回 1=成功恢复，0=无有效快照(保留initrd内容)，负数=错误。 */
int ramfs_snapshot_load(void);

#endif /* OPENOS_ARCH_X86_64_RAMFS64_H */
