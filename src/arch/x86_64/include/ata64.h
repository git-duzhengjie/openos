/* ============================================================
 * openos x86_64 ATA PIO 驱动 —— 精简 LBA28 磁盘读写
 *
 * 面向阶段二"磁盘持久化"，提供最小可用的扇区级读写：
 *   ata_init()          探测 primary/master 硬盘（IDENTIFY）
 *   ata_present()       是否探测到可用磁盘
 *   ata_sector_count()  磁盘总扇区数（LBA28 上限 2^28）
 *   ata_read_sectors()  按 LBA 读若干 512 字节扇区
 *   ata_write_sectors() 按 LBA 写若干 512 字节扇区
 *   ata_flush()         FLUSH CACHE，确保落盘
 *
 * 仅支持 IDE primary bus (I/O 0x1F0-0x1F7, ctrl 0x3F6) 的 master。
 * 需要 QEMU 以 i440fx(pc) 机型 + if=ide 挂载磁盘。
 * ============================================================ */

#ifndef ATA64_H
#define ATA64_H

#include "types.h"

#define ATA_SECTOR_SIZE 512

/* 初始化并探测 primary master。返回 1=发现磁盘，0=无磁盘 */
int ata_init(void);

/* 是否有可用磁盘 */
int ata_present(void);

/* 磁盘总扇区数（IDENTIFY 得到，LBA28） */
uint32_t ata_sector_count(void);

/* 读取 count 个扇区到 buf。返回 0 成功，负数失败 */
int ata_read_sectors(uint32_t lba, uint32_t count, void *buf);

/* 从 buf 写入 count 个扇区。返回 0 成功，负数失败 */
int ata_write_sectors(uint32_t lba, uint32_t count, const void *buf);

/* 刷新磁盘缓存，确保数据真正落盘。返回 0 成功 */
int ata_flush(void);

/* ---- secondary slave（FAT32 盘，与 master 数据盘隔离）---- */
/* 初始化并探测 secondary slave。返回 1=发现磁盘，0=无磁盘 */
int ata_slave_init(void);
/* slave 是否可用 */
int ata_slave_present(void);
/* 从 slave 读取 count 个扇区到 buf。返回 0 成功，负数失败 */
int ata_slave_read_sectors(uint32_t lba, uint32_t count, void *buf);
/* 向 slave 写入 count 个扇区。返回 0 成功，负数失败 */
int ata_slave_write_sectors(uint32_t lba, uint32_t count, const void *buf);

#endif /* ATA64_H */
