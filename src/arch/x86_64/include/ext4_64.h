/* ============================================================
 * openos x86_64 ext2/ext4 文件系统驱动（阶段 M3.2：只读）
 *
 * 设计要点：
 *   - 依赖注入：挂载时传入扇区读回调（ext4_read_fn），
 *     不直接绑定 ATA/AHCI，方便测试与更换后端。
 *   - 支持 ext2 / ext3 / ext4 只读：
 *       · Superblock（含 rev1 动态特性、大 inode）
 *       · Block Group Descriptor（32B 传统 + 64B ext4 扩展）
 *       · inode（直接块 + 一/二/三级间接块；ext4 extent 树）
 *       · 目录项线性遍历（linear directory）
 *   - 支持多级路径解析、目录遍历、文件读取。
 *
 * 只读约束：不实现写入 / 不解析 journal（ext3/4 日志按已 flush 处理）。
 *
 * 参考：The Second Extended File System (ext2)、
 *       ext4 Disk Layout (kernel.org Documentation/filesystems/ext4)
 * ============================================================ */
#ifndef EXT4_64_H
#define EXT4_64_H

#include "types.h"

/* 扇区读回调：从 lba 读 count 个 512 字节扇区到 buf。返回 0 成功，负数失败。
 * ext 文件系统块大小（1K/2K/4K）由驱动内部换算为多个 512B 扇区。 */
typedef int (*ext4_read_fn)(uint32_t lba, uint32_t count, void *buf);

/* 目录项（对外统一视图，与 fat32_dirent_t 语义对齐） */
typedef struct {
    char     name[256];   /* 文件名（UTF-8/ASCII，最长 255） */
    uint32_t size;        /* 文件字节数（目录为块占用大小） */
    uint32_t inode;       /* inode 号 */
    int      is_dir;      /* 1=目录，0=普通文件，2=符号链接，3=其它 */
} ext4_dirent_t;

/* 挂载 ext2/ext4。
 *   read_fn  : 扇区读回调（必填）
 *   part_lba : 分区起始 LBA；传 0 则自动探测 MBR 第一个 Linux 分区(0x83)，
 *              若无 MBR 分区表则按整盘 ext 解析。
 * 返回 0 成功，负数失败：
 *   -1 read_fn 为空  -2 读超级块失败  -3 magic 不匹配(非 ext)
 *   -4 不支持的特性  -5 块大小非法 */
int ext4_mount(ext4_read_fn read_fn, uint32_t part_lba);

/* 是否已成功挂载 */
int ext4_mounted(void);

/* 文件系统版本视图：2=ext2, 3=ext3(有 journal), 4=ext4(有 extent/64bit)。
 * 未挂载返回 0。 */
int ext4_version(void);

/* 列目录。path 如 "/" 或 "/dir/sub"。对每个条目调用 cb。
 * cb 返回非 0 可提前停止遍历。
 * 返回条目数，负数为错误。*/
int ext4_list(const char *path,
              int (*cb)(const ext4_dirent_t *ent, void *ud),
              void *ud);

/* 读文件内容到 buf（最多 max 字节）。返回实际读取字节数，负数错误。*/
int ext4_read_file(const char *path, void *buf, uint32_t max);

/* 查询单个路径的元信息。返回 0 成功并填充 out，负数错误。*/
int ext4_stat(const char *path, ext4_dirent_t *out);

/* 开机自检：打印超级块信息 + 列根目录 + 读文件，结果打到串口。*/
void ext4_selftest(void);

#endif /* EXT4_64_H */
