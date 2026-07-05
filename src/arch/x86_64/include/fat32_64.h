/* ============================================================
 * openos x86_64 FAT32 文件系统驱动（阶段 4-1：只读）
 *
 * 设计要点：
 *   - 依赖注入：挂载时传入扇区读回调（fat32_read_fn），
 *     不直接绑定 ATA，方便测试与更换后端。
 *   - 支持 MBR 分区表探测（part_lba 传 0 表示整盘无分区）。
 *   - 支持 8.3 短名 + LFN 长文件名（UTF-16 → ASCII 截断）。
 *   - 支持多级路径解析、目录遍历、文件读取。
 *
 * 参考：Microsoft FAT32 File System Specification (fatgen103)
 * ============================================================ */
#ifndef FAT32_64_H
#define FAT32_64_H

#include "types.h"

/* 扇区读回调：从 lba 读 count 个 512 字节扇区到 buf。返回 0 成功，负数失败 */
typedef int (*fat32_read_fn)(uint32_t lba, uint32_t count, void *buf);

/* 扇区写回调：把 buf 中 count 个 512 字节扇区写到 lba。返回 0 成功，负数失败。
 * （阶段 4-3 写入支持；不设置则文件系统为只读） */
typedef int (*fat32_write_fn)(uint32_t lba, uint32_t count, const void *buf);

/* 目录项（对外统一视图） */
typedef struct {
    char     name[256];        /* 文件名（LFN 或 8.3） */
    uint32_t size;             /* 文件字节数（目录为 0） */
    uint32_t first_cluster;    /* 起始簇号 */
    int      is_dir;           /* 1=目录，0=文件 */
} fat32_dirent_t;

/* 挂载 FAT32。
 *   read_fn  : 扇区读回调（必填）
 *   part_lba : 分区起始 LBA；传 0 则自动探测 MBR 第一个 FAT32 分区，
 *              若无 MBR 分区表则按整盘 FAT32 解析。
 * 返回 0 成功，负数失败。*/
int fat32_mount(fat32_read_fn read_fn, uint32_t part_lba);

/* 是否已成功挂载 */
int fat32_mounted(void);

/* 列目录。path 如 "/" 或 "/DIR/SUB"。对每个条目调用 cb。
 * cb 返回非 0 可提前停止遍历。
 * 返回条目数，负数为错误。*/
int fat32_list(const char *path,
               int (*cb)(const fat32_dirent_t *ent, void *ud),
               void *ud);

/* 读文件内容到 buf（最多 max 字节）。返回实际读取字节数，负数错误。*/
int fat32_read_file(const char *path, void *buf, uint32_t max);

/* ---- 阶段 4-3 / 4-4：写入支持（8.3 短名 + LFN 长文件名） ---- */

/* 注册扇区写回调。挂载后调用一次即可开启写能力。*/
void fat32_set_write_fn(fat32_write_fn write_fn);

/* 是否可写（已挂载且已注册写回调） */
int fat32_writable(void);

/* 写文件（覆盖已有内容 / 不存在则新建）。
 *   path : 完整路径，末段为文件名。8.3 短名直接写；含小写/长名/特殊
 *          字符时自动生成 LFN 长文件名项 + 短名别名（NAME~1.EXT）。
 *   buf  : 数据；size : 字节数（可为 0，表示清空文件）。
 * 返回 0 成功，负数错误：
 *   -1 未挂载/不可写  -2 路径非法  -3 父目录不存在
 *   -4 目标是目录     -5 簇分配失败 -6 目录项写入失败  -7 内存不足 */
int fat32_write_file(const char *path, const void *buf, uint32_t size);

/* 创建目录（含 LFN 支持）。
 *   path : 完整路径（如 /NEWDIR、/DOCS/子目录）。父目录须存在。
 * 返回 0 成功，负数错误（-4 已存在同名项）。*/
int fat32_mkdir(const char *path);

/* 删除文件或空目录（含前置 LFN 项一并抹除，释放簇链）。
 *   path : 完整路径。删除目录时目录必须为空。
 * 返回 0 成功，负数错误：
 *   -1 未挂载/不可写  -2 路径非法/根目录  -3 不存在
 *   -4 目录非空       -6 写入失败 */
int fat32_delete(const char *path);

/* 查询单个路径的元信息。返回 0 成功并填充 out，负数错误。*/
int fat32_stat(const char *path, fat32_dirent_t *out);

/* 开机自检：列目录 + 读文件，结果打到串口。*/
void fat32_selftest(void);

#endif /* FAT32_64_H */
