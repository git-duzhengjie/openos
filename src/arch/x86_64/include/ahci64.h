/*
 * ahci64.h — AHCI/SATA 硬盘驱动对外接口（M2.1）
 *
 * 复用 M1.1 PCI 枚举 + pmm64 物理连续分配 + identity-map(phys==virt)。
 * MVP：单控制器、单端口、polling 模式、READ/WRITE DMA EXT (LBA48)。
 *
 * 对外接口风格对齐 ata64.h：
 *   ahci_init()          探测 AHCI 控制器 + 首个挂盘端口（IDENTIFY）
 *   ahci_present()       是否探测到可用 SATA 盘
 *   ahci_sector_count()  磁盘总扇区数（LBA48）
 *   ahci_read_sectors()  按 LBA 读若干 512 字节扇区
 *   ahci_write_sectors() 按 LBA 写若干 512 字节扇区
 *   ahci_flush()         FLUSH CACHE EXT，确保落盘
 */
#ifndef OPENOS_AHCI64_H
#define OPENOS_AHCI64_H

#include <stdint.h>

/* 探测并初始化第一个 AHCI 控制器上第一个挂盘的端口。返回 0 成功，负数失败。 */
int ahci_init(void);

/* 是否已成功初始化并探测到可用 SATA 盘。 */
int ahci_present(void);

/* 磁盘总扇区数（512 字节/扇区，LBA48）。未就绪返回 0。 */
uint64_t ahci_sector_count(void);

/* 从 lba 开始读 count 个扇区到 buf。返回 0 成功，负数失败。 */
int ahci_read_sectors(uint64_t lba, uint32_t count, void *buf);

/* 从 buf 写 count 个扇区到 lba 起始位置。返回 0 成功，负数失败。 */
int ahci_write_sectors(uint64_t lba, uint32_t count, const void *buf);

/* FLUSH CACHE EXT，确保写入落盘。返回 0 成功，负数失败。 */
int ahci_flush(void);

/* headless 自测：IDENTIFY 出容量 + 写一扇区读回校验。返回 0 全 PASS。 */
int ahci_selftest(void);

/* M2.1 MSI：延迟挂载中断。需在 LAPIC 就绪后调用（storage init 早于 LAPIC）。
 * 成功后驱动由 polling 切换为中断驱动（仍保留 polling 回退）。幂等。 */
void ahci_irq_install_late(void);

/* 返回中断触发次数（调试验证：>0 证明 MSI 中断路径真实生效） */
uint32_t ahci_irq_count(void);

#endif /* OPENOS_AHCI64_H */
