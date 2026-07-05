/* ============================================================
 * openos x86_64 ATA PIO 驱动实现 (LBA28, primary master)
 * ============================================================ */

#include "ata64.h"

extern void early_serial64_write(const char *text);
#define ata_log(s) early_serial64_write((s))

/* ---- secondary IDE bus 寄存器（与 primary 上的 UEFI 引导盘隔离）---- */
#define ATA_IO_BASE   0x170
#define ATA_CTRL_BASE 0x376

#define ATA_REG_DATA      (ATA_IO_BASE + 0) /* 16-bit 数据 */
#define ATA_REG_ERROR     (ATA_IO_BASE + 1)
#define ATA_REG_FEATURES  (ATA_IO_BASE + 1)
#define ATA_REG_SECCOUNT  (ATA_IO_BASE + 2)
#define ATA_REG_LBA_LO    (ATA_IO_BASE + 3)
#define ATA_REG_LBA_MID   (ATA_IO_BASE + 4)
#define ATA_REG_LBA_HI    (ATA_IO_BASE + 5)
#define ATA_REG_DRIVE     (ATA_IO_BASE + 6)
#define ATA_REG_STATUS    (ATA_IO_BASE + 7)
#define ATA_REG_COMMAND   (ATA_IO_BASE + 7)
#define ATA_REG_CTRL      (ATA_CTRL_BASE + 0) /* 0x3F6 alt-status/device-ctrl */

/* status 位 */
#define ATA_SR_BSY  0x80  /* busy */
#define ATA_SR_DRDY 0x40  /* drive ready */
#define ATA_SR_DRQ  0x08  /* data request ready */
#define ATA_SR_ERR  0x01  /* error */

/* 命令 */
#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_FLUSH      0xE7
#define ATA_CMD_IDENTIFY   0xEC

/* ---- 端口 IO ---- */
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ __volatile__("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* ---- 驱动状态 ---- */
static int      g_ata_present = 0;
static uint32_t g_ata_sectors = 0;

/* 400ns 延时：读 alt-status 4 次 */
static void ata_io_delay(void) {
    (void)inb(ATA_REG_CTRL);
    (void)inb(ATA_REG_CTRL);
    (void)inb(ATA_REG_CTRL);
    (void)inb(ATA_REG_CTRL);
}

/* 忙等待 BSY 清除，返回最终 status；带超时防止死锁 */
static int ata_wait_not_busy(void) {
    int timeout = 100000000;
    while (timeout-- > 0) {
        uint8_t st = inb(ATA_REG_STATUS);
        if (!(st & ATA_SR_BSY)) return st;
    }
    return -1;
}

/* 等待 DRQ 置位（可以传输数据），带超时 */
static int ata_wait_drq(void) {
    int timeout = 100000000;
    while (timeout-- > 0) {
        uint8_t st = inb(ATA_REG_STATUS);
        if (st & ATA_SR_ERR) return -1;
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

int ata_present(void)         { return g_ata_present; }
uint32_t ata_sector_count(void) { return g_ata_sectors; }

int ata_init(void) {
    g_ata_present = 0;
    g_ata_sectors = 0;

    /* 选择 master (drive/head = 0xA0) */
    outb(ATA_REG_DRIVE, 0xA0);
    ata_io_delay();

    /* 若 status = 0xFF，浮空总线，无设备 */
    uint8_t st = inb(ATA_REG_STATUS);
    if (st == 0xFF) {
        ata_log("[ATA] no drive (floating bus)\n");
        return 0;
    }

    /* IDENTIFY：清 LBA 寄存器后发命令 */
    outb(ATA_REG_SECCOUNT, 0);
    outb(ATA_REG_LBA_LO, 0);
    outb(ATA_REG_LBA_MID, 0);
    outb(ATA_REG_LBA_HI, 0);
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_delay();

    st = inb(ATA_REG_STATUS);
    if (st == 0) {
        ata_log("[ATA] no drive (status 0)\n");
        return 0;
    }

    if (ata_wait_not_busy() < 0) {
        ata_log("[ATA] IDENTIFY BSY timeout\n");
        return 0;
    }

    /* 非 ATA 设备可能在 LBA_MID/HI 返回非零签名（ATAPI/SATA）*/
    if (inb(ATA_REG_LBA_MID) != 0 || inb(ATA_REG_LBA_HI) != 0) {
        ata_log("[ATA] not an ATA drive (signature mismatch)\n");
        return 0;
    }

    if (ata_wait_drq() < 0) {
        ata_log("[ATA] IDENTIFY DRQ timeout/err\n");
        return 0;
    }

    /* 读取 256 words 的 IDENTIFY 数据 */
    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_REG_DATA);

    /* word 60-61: LBA28 可寻址扇区总数 */
    uint32_t sectors = ((uint32_t)id[61] << 16) | (uint32_t)id[60];
    if (sectors == 0) sectors = 0;

    g_ata_present = 1;
    g_ata_sectors = sectors;
    ata_log("[ATA] secondary master detected, LBA28 sectors ready\n");
    return 1;
}

int ata_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    if (!g_ata_present) return -1;
    if (count == 0) return 0;
    uint16_t *ptr = (uint16_t *)buf;

    for (uint32_t s = 0; s < count; s++) {
        uint32_t cur = lba + s;
        if (ata_wait_not_busy() < 0) return -2;

        /* master + LBA28 高 4 位 */
        outb(ATA_REG_DRIVE, 0xE0 | ((cur >> 24) & 0x0F));
        outb(ATA_REG_FEATURES, 0);
        outb(ATA_REG_SECCOUNT, 1);
        outb(ATA_REG_LBA_LO,  (uint8_t)(cur & 0xFF));
        outb(ATA_REG_LBA_MID, (uint8_t)((cur >> 8) & 0xFF));
        outb(ATA_REG_LBA_HI,  (uint8_t)((cur >> 16) & 0xFF));
        outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO);

        if (ata_wait_drq() < 0) return -3;

        /* 读 256 words = 512 字节 */
        for (int i = 0; i < 256; i++) *ptr++ = inw(ATA_REG_DATA);
        ata_io_delay();
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    if (!g_ata_present) return -1;
    if (count == 0) return 0;
    const uint16_t *ptr = (const uint16_t *)buf;

    for (uint32_t s = 0; s < count; s++) {
        uint32_t cur = lba + s;
        if (ata_wait_not_busy() < 0) return -2;

        outb(ATA_REG_DRIVE, 0xE0 | ((cur >> 24) & 0x0F));
        outb(ATA_REG_FEATURES, 0);
        outb(ATA_REG_SECCOUNT, 1);
        outb(ATA_REG_LBA_LO,  (uint8_t)(cur & 0xFF));
        outb(ATA_REG_LBA_MID, (uint8_t)((cur >> 8) & 0xFF));
        outb(ATA_REG_LBA_HI,  (uint8_t)((cur >> 16) & 0xFF));
        outb(ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

        if (ata_wait_drq() < 0) return -3;

        /* 写 256 words = 512 字节 */
        for (int i = 0; i < 256; i++) outw(ATA_REG_DATA, *ptr++);
        ata_io_delay();

        /* 每扇区后 flush 保证顺序 */
        outb(ATA_REG_COMMAND, ATA_CMD_FLUSH);
        if (ata_wait_not_busy() < 0) return -4;
    }
    return 0;
}

int ata_flush(void) {
    if (!g_ata_present) return -1;
    if (ata_wait_not_busy() < 0) return -2;
    outb(ATA_REG_COMMAND, ATA_CMD_FLUSH);
    if (ata_wait_not_busy() < 0) return -3;
    return 0;
}
