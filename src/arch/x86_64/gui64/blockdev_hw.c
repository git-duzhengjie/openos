/* ============================================================
 * openos - x86_64 硬件块设备适配层 (M2.2)
 * ------------------------------------------------------------
 * 将底层具体驱动（NVMe / AHCI / ATA primary / ATA slave）包装为
 * 统一的 blockdev_ops_t，并在探测到设备后注册进块设备抽象层。
 *
 * 设备命名约定：
 *   nvme0   - NVMe 命名空间 1
 *   sda     - AHCI 端口 0
 *   hda     - ATA primary master
 *   hdb     - ATA primary slave（数据盘）
 *
 * 说明：底层驱动均为单实例全局状态，故各 ops 的 dev 形参仅用于
 * 取 sector_size 等元信息，实际 I/O 直接转调驱动全局函数。
 * ============================================================ */

#include "blockdev.h"
#include "nvme64.h"
#include "ata64.h"
#include "ahci64.h"

#define BD_IOCTL_FLUSH 0x0001u

/* 主设备号分配 */
#define MAJOR_NVME 0x01u
#define MAJOR_AHCI 0x02u
#define MAJOR_ATA  0x03u

/* ---------------- NVMe 适配 ---------------- */
static int nvme_bd_read(blockdev_t *d, uint32_t lba, uint32_t count, void *buf) {
    (void)d; return nvme_read_sectors((uint64_t)lba, count, buf);
}
static int nvme_bd_write(blockdev_t *d, uint32_t lba, uint32_t count, const void *buf) {
    (void)d; return nvme_write_sectors((uint64_t)lba, count, buf);
}
static int nvme_bd_ioctl(blockdev_t *d, uint32_t req, void *arg) {
    (void)d; (void)arg;
    if (req == BD_IOCTL_FLUSH) return nvme_flush();
    return -1;
}
static blockdev_ops_t g_nvme_ops = {
    0, 0, nvme_bd_read, nvme_bd_write, nvme_bd_ioctl
};

/* ---------------- AHCI 适配 ---------------- */
static int ahci_bd_read(blockdev_t *d, uint32_t lba, uint32_t count, void *buf) {
    (void)d; return ahci_read_sectors((uint64_t)lba, count, buf);
}
static int ahci_bd_write(blockdev_t *d, uint32_t lba, uint32_t count, const void *buf) {
    (void)d; return ahci_write_sectors((uint64_t)lba, count, buf);
}
static int ahci_bd_ioctl(blockdev_t *d, uint32_t req, void *arg) {
    (void)d; (void)arg;
    if (req == BD_IOCTL_FLUSH) return ahci_flush();
    return -1;
}
static blockdev_ops_t g_ahci_ops = {
    0, 0, ahci_bd_read, ahci_bd_write, ahci_bd_ioctl
};

/* ---------------- ATA primary master 适配 ---------------- */
static int ata_bd_read(blockdev_t *d, uint32_t lba, uint32_t count, void *buf) {
    (void)d; return ata_read_sectors(lba, count, buf);
}
static int ata_bd_write(blockdev_t *d, uint32_t lba, uint32_t count, const void *buf) {
    (void)d; return ata_write_sectors(lba, count, buf);
}
static int ata_bd_ioctl(blockdev_t *d, uint32_t req, void *arg) {
    (void)d; (void)arg;
    if (req == BD_IOCTL_FLUSH) return ata_flush();
    return -1;
}
static blockdev_ops_t g_ata_ops = {
    0, 0, ata_bd_read, ata_bd_write, ata_bd_ioctl
};

/* ---------------- ATA slave（数据盘）适配 ---------------- */
static int ataS_bd_read(blockdev_t *d, uint32_t lba, uint32_t count, void *buf) {
    (void)d; return ata_slave_read_sectors(lba, count, buf);
}
static int ataS_bd_write(blockdev_t *d, uint32_t lba, uint32_t count, const void *buf) {
    (void)d; return ata_slave_write_sectors(lba, count, buf);
}
static blockdev_ops_t g_ata_slave_ops = {
    0, 0, ataS_bd_read, ataS_bd_write, 0
};

/*
 * 探测并注册所有已就绪的硬件块设备。
 * 前置条件：调用方已完成各驱动的 init/present 探测流程，
 * 本函数依据 *_present() 结果决定是否注册。
 * 返回成功注册的设备数量。
 */
int blockdev_register_hw_devices(void) {
    int n = 0;

    if (nvme_present()) {
        uint32_t bs = nvme_block_size();
        uint64_t sc = nvme_sector_count();
        if (bs == 0) bs = BLOCKDEV_SECTOR_SIZE_DEFAULT;
        if (blockdev_register("nvme0", MAJOR_NVME, 0, bs, (uint32_t)sc,
                              &g_nvme_ops, 0) >= 0)
            n++;
    }

    if (ahci_present()) {
        uint64_t sc = ahci_sector_count();
        if (blockdev_register("sda", MAJOR_AHCI, 0, BLOCKDEV_SECTOR_SIZE_DEFAULT,
                              (uint32_t)sc, &g_ahci_ops, 0) >= 0)
            n++;
    }

    if (ata_present()) {
        uint32_t sc = ata_sector_count();
        if (blockdev_register("hda", MAJOR_ATA, 0, BLOCKDEV_SECTOR_SIZE_DEFAULT,
                              sc, &g_ata_ops, 0) >= 0)
            n++;
    }

    if (ata_slave_present()) {
        if (blockdev_register("hdb", MAJOR_ATA, 1, BLOCKDEV_SECTOR_SIZE_DEFAULT,
                              0, &g_ata_slave_ops, 0) >= 0)
            n++;
    }

    return n;
}

/* blockdev.h 声明的内置注册入口：此处等价转发到硬件探测注册。 */
void blockdev_register_builtin_devices(void) {
    blockdev_init();
    blockdev_register_hw_devices();
}
