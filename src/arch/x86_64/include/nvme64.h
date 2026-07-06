/*
 * nvme64.h — NVMe 存储驱动对外接口（M2.2）
 *
 * 复用 M1.1 PCI 枚举 + pmm64 物理连续分配 + identity-map(phys==virt)
 * + M2.1 AHCI 打下的 MMIO 显式映射套路。
 * MVP：单控制器、单 namespace、polling 模式、1 个 IO 队列对、PRP。
 *
 * 对外接口风格对齐 ahci64.h：
 *   nvme_init()          探测 NVMe 控制器 + namespace 1（IDENTIFY）
 *   nvme_present()       是否探测到可用 NVMe namespace
 *   nvme_sector_count()  namespace 总逻辑块数（LBA）
 *   nvme_read_sectors()  按 LBA 读若干逻辑块
 *   nvme_write_sectors() 按 LBA 写若干逻辑块
 *   nvme_flush()         FLUSH，确保落盘
 */
#ifndef OPENOS_NVME64_H
#define OPENOS_NVME64_H

#include <stdint.h>

/* 探测并初始化第一个 NVMe 控制器上 namespace 1。返回 0 成功，负数失败。 */
int nvme_init(void);

/* 是否已成功初始化并探测到可用 namespace。 */
int nvme_present(void);

/* namespace 总逻辑块数。未就绪返回 0。 */
uint64_t nvme_sector_count(void);

/* 每个逻辑块字节数（通常 512 或 4096）。未就绪返回 0。 */
uint32_t nvme_block_size(void);

/* 从 lba 开始读 count 个逻辑块到 buf。返回 0 成功，负数失败。 */
int nvme_read_sectors(uint64_t lba, uint32_t count, void *buf);

/* 从 buf 写 count 个逻辑块到 lba 起始位置。返回 0 成功，负数失败。 */
int nvme_write_sectors(uint64_t lba, uint32_t count, const void *buf);

/* FLUSH，确保写入落盘。返回 0 成功，负数失败。 */
int nvme_flush(void);

/* FAT32 blockdev 适配器（512B 扇区语义，lba 收窄为 uint32）。
 * 仅当原生块大小=512 时可用，否则返回 -1 交由上层回退 ATA。 */
int nvme_fat_read(uint32_t lba, uint32_t count, void *buf);
int nvme_fat_write(uint32_t lba, uint32_t count, const void *buf);

/* headless 自测：IDENTIFY 出容量 + 写一块读回校验。返回 0 全 PASS。 */
int nvme_selftest(void);

#endif /* OPENOS_NVME64_H */
