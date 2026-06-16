/* ============================================================
 * openos - Legacy IDE/ATA PIO block driver
 * ============================================================ */
#include "../include/ata.h"
#include "../include/blockdev.h"
#include "../fs/vfs.h"
#include "../include/io.h"
#include "../include/serial.h"
#include "../include/string.h"

#define ATA_SECTOR_SIZE 512u
#define ATA_MAX_DEVICES 4u

#define ATA_SR_BSY  0x80u
#define ATA_SR_DRDY 0x40u
#define ATA_SR_DF   0x20u
#define ATA_SR_DRQ  0x08u
#define ATA_SR_ERR  0x01u

#define ATA_CMD_READ_PIO   0x20u
#define ATA_CMD_WRITE_PIO  0x30u
#define ATA_CMD_CACHE_FLUSH 0xE7u
#define ATA_CMD_IDENTIFY   0xECu

typedef struct ata_channel {
    uint16_t io_base;
    uint16_t ctrl_base;
} ata_channel_t;

typedef struct ata_device {
    uint8_t present;
    uint8_t channel;
    uint8_t drive;
    uint32_t sectors;
    char name[16];
    blockdev_t *blockdev;
} ata_device_t;

static ata_channel_t ata_channels[2] = {
    { 0x1F0u, 0x3F6u },
    { 0x170u, 0x376u }
};
static ata_device_t ata_devices[ATA_MAX_DEVICES];
static blockdev_ops_t ata_ops;
static uint32_t ata_count;

static void ata_delay(const ata_channel_t *ch) {
    (void)inb(ch->ctrl_base);
    (void)inb(ch->ctrl_base);
    (void)inb(ch->ctrl_base);
    (void)inb(ch->ctrl_base);
}

static void ata_select(const ata_channel_t *ch, uint8_t drive, uint32_t lba) {
    outb((uint16_t)(ch->io_base + 6u), (uint8_t)(0xE0u | ((drive & 1u) << 4) | ((lba >> 24) & 0x0Fu)));
    ata_delay(ch);
}

static int ata_wait_not_busy(const ata_channel_t *ch) {
    uint32_t i;
    for (i = 0; i < 1000000u; i++) {
        if ((inb((uint16_t)(ch->io_base + 7u)) & ATA_SR_BSY) == 0) return 0;
    }
    return -1;
}

static int ata_wait_drq(const ata_channel_t *ch) {
    uint32_t i;
    for (i = 0; i < 1000000u; i++) {
        uint8_t status = inb((uint16_t)(ch->io_base + 7u));
        if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) return -1;
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ) != 0) return 0;
    }
    return -1;
}

static void ata_read_words(uint16_t port, void *buf, uint32_t words) {
    uint16_t *out = (uint16_t *)buf;
    uint32_t i;
    for (i = 0; i < words; i++) out[i] = inw(port);
}

static void ata_write_words(uint16_t port, const void *buf, uint32_t words) {
    const uint16_t *in = (const uint16_t *)buf;
    uint32_t i;
    for (i = 0; i < words; i++) outw(port, in[i]);
}

static int ata_identify(uint8_t channel, uint8_t drive, uint16_t *identify) {
    const ata_channel_t *ch = &ata_channels[channel];
    uint8_t status;
    uint32_t i;

    ata_select(ch, drive, 0);
    outb((uint16_t)(ch->io_base + 2u), 0);
    outb((uint16_t)(ch->io_base + 3u), 0);
    outb((uint16_t)(ch->io_base + 4u), 0);
    outb((uint16_t)(ch->io_base + 5u), 0);
    outb((uint16_t)(ch->io_base + 7u), ATA_CMD_IDENTIFY);
    ata_delay(ch);

    status = inb((uint16_t)(ch->io_base + 7u));
    if (status == 0) return -1;

    for (i = 0; i < 1000000u; i++) {
        status = inb((uint16_t)(ch->io_base + 7u));
        if ((status & ATA_SR_BSY) == 0) break;
    }
    if ((status & ATA_SR_BSY) != 0) return -1;

    if (inb((uint16_t)(ch->io_base + 4u)) != 0 || inb((uint16_t)(ch->io_base + 5u)) != 0) return -1;
    if (ata_wait_drq(ch) < 0) return -1;
    ata_read_words(ch->io_base, identify, 256);
    return 0;
}

static ata_device_t *ata_from_blockdev(blockdev_t *dev) {
    uint32_t i;
    for (i = 0; i < ATA_MAX_DEVICES; i++) {
        if (ata_devices[i].present && ata_devices[i].blockdev == dev) return &ata_devices[i];
    }
    return NULL;
}

static int ata_pio_rw(ata_device_t *adev, uint32_t lba, uint32_t count, void *buf, int write) {
    const ata_channel_t *ch;
    uint8_t *ptr = (uint8_t *)buf;
    uint32_t i;

    if (!adev || !buf || count == 0) return count == 0 ? 0 : -1;
    if (lba >= adev->sectors || count > adev->sectors - lba) return -1;
    if (count > 255u) return -1;

    ch = &ata_channels[adev->channel];
    ata_select(ch, adev->drive, lba);
    if (ata_wait_not_busy(ch) < 0) return -1;

    outb((uint16_t)(ch->io_base + 1u), 0);
    outb((uint16_t)(ch->io_base + 2u), (uint8_t)count);
    outb((uint16_t)(ch->io_base + 3u), (uint8_t)(lba & 0xFFu));
    outb((uint16_t)(ch->io_base + 4u), (uint8_t)((lba >> 8) & 0xFFu));
    outb((uint16_t)(ch->io_base + 5u), (uint8_t)((lba >> 16) & 0xFFu));
    outb((uint16_t)(ch->io_base + 7u), write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);

    for (i = 0; i < count; i++) {
        if (ata_wait_drq(ch) < 0) return (i == 0) ? -1 : (int)i;
        if (write) ata_write_words(ch->io_base, ptr + i * ATA_SECTOR_SIZE, 256);
        else ata_read_words(ch->io_base, ptr + i * ATA_SECTOR_SIZE, 256);
        ata_delay(ch);
    }

    if (write) {
        outb((uint16_t)(ch->io_base + 7u), ATA_CMD_CACHE_FLUSH);
        (void)ata_wait_not_busy(ch);
    }
    return (int)count;
}

static int ata_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    ata_device_t *adev = ata_from_blockdev(dev);
    return ata_pio_rw(adev, lba, count, buf, 0);
}

static int ata_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    ata_device_t *adev = ata_from_blockdev(dev);
    return ata_pio_rw(adev, lba, count, (void *)buf, 1);
}

static void ata_make_name(char *out, uint32_t index) {
    out[0] = 'a'; out[1] = 't'; out[2] = 'a';
    out[3] = (char)('0' + (char)index);
    out[4] = '\0';
}

static void ata_make_dev_path(char *out, const char *name) {
    out[0] = '/'; out[1] = 'd'; out[2] = 'e'; out[3] = 'v'; out[4] = '/';
    strcpy(out + 5, name);
}

static void ata_register_detected(uint8_t channel, uint8_t drive, const uint16_t *identify) {
    ata_device_t *adev;
    char path[32];
    uint32_t sectors;

    if (ata_count >= ATA_MAX_DEVICES) return;

    sectors = ((uint32_t)identify[61] << 16) | identify[60];
    if (sectors == 0) return;

    adev = &ata_devices[ata_count];
    memset(adev, 0, sizeof(*adev));
    adev->present = 1;
    adev->channel = channel;
    adev->drive = drive;
    adev->sectors = sectors;
    ata_make_name(adev->name, ata_count);

    if (blockdev_register(adev->name, 3, ata_count,
                          ATA_SECTOR_SIZE, sectors,
                          &ata_ops, adev) < 0) {
        memset(adev, 0, sizeof(*adev));
        return;
    }
    adev->blockdev = blockdev_find(adev->name);
    if (!adev->blockdev) {
        (void)blockdev_unregister(adev->name);
        memset(adev, 0, sizeof(*adev));
        return;
    }

    ata_make_dev_path(path, adev->name);
    (void)vfs_mknod(path, FS_BLOCK_DEVICE | 0660, adev->name);

    serial_write("[ATA] registered ");
    serial_write(adev->name);
    serial_write(" sectors=0x");
    serial_write_hex(sectors);
    serial_write("\n");

    ata_count++;
}

uint32_t ata_device_count(void) {
    return ata_count;
}

void ata_init(void) {
    uint16_t identify[256];
    uint32_t channel;
    uint32_t drive;

    memset(ata_devices, 0, sizeof(ata_devices));
    memset(&ata_ops, 0, sizeof(ata_ops));
    ata_ops.read_blocks = ata_read_blocks;
    ata_ops.write_blocks = ata_write_blocks;

    serial_write("[ATA] probing legacy IDE channels\n");
    for (channel = 0; channel < 2; channel++) {
        for (drive = 0; drive < 2; drive++) {
            memset(identify, 0, sizeof(identify));
            if (ata_identify((uint8_t)channel, (uint8_t)drive, identify) == 0) {
                ata_register_detected((uint8_t)channel, (uint8_t)drive, identify);
            }
        }
    }
    serial_write("[ATA] probe complete\n");
}
