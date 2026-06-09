/* ============================================================
 * openos - 块设备框架与 RAM disk (Phase 3)
 * ============================================================ */

#include "../include/blockdev.h"
#include "../include/string.h"
#include "../include/serial.h"
#include "../include/pmm.h"
#include "../include/devmgr.h"
#include "../fs/vfs.h"

static blockdev_t blockdev_table[BLOCKDEV_MAX];
static uint32_t blockdev_table_count = 0;

static void blockdev_copy_name(char *dst, const char *src) {
    uint32_t i = 0;
    if (!dst) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (; i < BLOCKDEV_NAME_MAX - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void blockdev_init(void) {
    memset(blockdev_table, 0, sizeof(blockdev_table));
    blockdev_table_count = 0;
}

int blockdev_register(const char *name, uint32_t major, uint32_t minor,
                      uint32_t sector_size, uint32_t sector_count,
                      blockdev_ops_t *ops, void *private_data) {
    blockdev_t *dev;

    if (!name || !ops) return -1;
    if (!ops->read_blocks || !ops->write_blocks) return -1;
    if (sector_size == 0 || sector_count == 0) return -1;
    if (blockdev_table_count >= BLOCKDEV_MAX) return -1;
    if (blockdev_find(name)) return -1;
    if (blockdev_find_by_devno(major, minor)) return -1;

    dev = &blockdev_table[blockdev_table_count++];
    memset(dev, 0, sizeof(blockdev_t));
    blockdev_copy_name(dev->name, name);
    dev->major = major;
    dev->minor = minor;
    dev->sector_size = sector_size;
    dev->sector_count = sector_count;
    dev->ops = ops;
    dev->private_data = private_data;
    dev->ref_count = 0;
    devmgr_register(name, "platform", DEVMGR_TYPE_BLOCK, major, minor, 0, dev);
    return 0;
}

int blockdev_unregister(const char *name) {
    uint32_t i;

    if (!name) return -1;
    for (i = 0; i < blockdev_table_count; i++) {
        if (strcmp(blockdev_table[i].name, name) == 0) {
            if (blockdev_table[i].ref_count != 0) return -1;
            devmgr_unregister(blockdev_table[i].name);
            if (i + 1 < blockdev_table_count) {
                memcpy(&blockdev_table[i], &blockdev_table[blockdev_table_count - 1], sizeof(blockdev_t));
            }
            memset(&blockdev_table[blockdev_table_count - 1], 0, sizeof(blockdev_t));
            blockdev_table_count--;
            return 0;
        }
    }
    return -1;
}

blockdev_t *blockdev_find(const char *name) {
    uint32_t i;

    if (!name) return 0;
    for (i = 0; i < blockdev_table_count; i++) {
        if (strcmp(blockdev_table[i].name, name) == 0) {
            return &blockdev_table[i];
        }
    }
    return 0;
}

blockdev_t *blockdev_find_by_devno(uint32_t major, uint32_t minor) {
    uint32_t i;

    for (i = 0; i < blockdev_table_count; i++) {
        if (blockdev_table[i].major == major && blockdev_table[i].minor == minor) {
            return &blockdev_table[i];
        }
    }
    return 0;
}

blockdev_t *blockdev_get_by_index(uint32_t index) {
    if (index >= blockdev_table_count) return 0;
    return &blockdev_table[index];
}

uint32_t blockdev_count(void) {
    return blockdev_table_count;
}

int blockdev_open(blockdev_t *dev) {
    int ret;

    if (!dev) return -1;
    if (dev->ops && dev->ops->open) {
        ret = dev->ops->open(dev);
        if (ret < 0) return ret;
    }
    dev->ref_count++;
    return 0;
}

int blockdev_close(blockdev_t *dev) {
    int ret = 0;

    if (!dev) return -1;
    if (dev->ops && dev->ops->close) {
        ret = dev->ops->close(dev);
    }
    if (dev->ref_count > 0) dev->ref_count--;
    return ret;
}

int blockdev_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    if (!dev || !buf) return -1;
    if (!dev->ops || !dev->ops->read_blocks) return -1;
    if (count == 0) return 0;
    if (lba >= dev->sector_count) return -1;
    if (count > dev->sector_count - lba) return -1;
    return dev->ops->read_blocks(dev, lba, count, buf);
}

int blockdev_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    if (!dev || !buf) return -1;
    if (!dev->ops || !dev->ops->write_blocks) return -1;
    if (count == 0) return 0;
    if (lba >= dev->sector_count) return -1;
    if (count > dev->sector_count - lba) return -1;
    return dev->ops->write_blocks(dev, lba, count, buf);
}

int blockdev_ioctl(blockdev_t *dev, uint32_t request, void *arg) {
    if (!dev) return -1;
    if (!dev->ops || !dev->ops->ioctl) return -1;
    return dev->ops->ioctl(dev, request, arg);
}

uint32_t blockdev_size_bytes(blockdev_t *dev) {
    if (!dev) return 0;
    return dev->sector_size * dev->sector_count;
}

/* ---------------- 内置设备：ram0 ----------------
 * 不使用 1MiB 静态数组，避免把 kernel.bin/BSS 突然撑大，导致
 * 早期 bootloader 固定加载扇区不足或覆盖内存布局。RAM disk 按页
 * 从 PMM 分配，读写时按字节偏移映射到对应页。
 */
#define RAMDISK0_SECTOR_SIZE  512
#define RAMDISK0_SECTOR_COUNT 2048
#define RAMDISK0_SIZE         (RAMDISK0_SECTOR_SIZE * RAMDISK0_SECTOR_COUNT)
#define RAMDISK0_PAGE_COUNT   ((RAMDISK0_SIZE + PAGE_SIZE - 1) / PAGE_SIZE)

static uint8_t *ramdisk0_pages[RAMDISK0_PAGE_COUNT];
static int ramdisk0_ready = 0;

static int ramdisk_copy_out(uint32_t byte_offset, void *buf, uint32_t bytes) {
    uint8_t *out = (uint8_t *)buf;
    uint32_t done = 0;

    if (!buf) return -1;
    if (byte_offset > RAMDISK0_SIZE) return -1;
    if (bytes > RAMDISK0_SIZE - byte_offset) return -1;

    while (done < bytes) {
        uint32_t pos = byte_offset + done;
        uint32_t page_index = pos / PAGE_SIZE;
        uint32_t page_off = pos % PAGE_SIZE;
        uint32_t chunk = PAGE_SIZE - page_off;

        if (chunk > bytes - done) chunk = bytes - done;
        if (page_index >= RAMDISK0_PAGE_COUNT || !ramdisk0_pages[page_index]) return -1;

        memcpy(out + done, ramdisk0_pages[page_index] + page_off, chunk);
        done += chunk;
    }
    return 0;
}

static int ramdisk_copy_in(uint32_t byte_offset, const void *buf, uint32_t bytes) {
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t done = 0;

    if (!buf) return -1;
    if (byte_offset > RAMDISK0_SIZE) return -1;
    if (bytes > RAMDISK0_SIZE - byte_offset) return -1;

    while (done < bytes) {
        uint32_t pos = byte_offset + done;
        uint32_t page_index = pos / PAGE_SIZE;
        uint32_t page_off = pos % PAGE_SIZE;
        uint32_t chunk = PAGE_SIZE - page_off;

        if (chunk > bytes - done) chunk = bytes - done;
        if (page_index >= RAMDISK0_PAGE_COUNT || !ramdisk0_pages[page_index]) return -1;

        memcpy(ramdisk0_pages[page_index] + page_off, in + done, chunk);
        done += chunk;
    }
    return 0;
}

static int ramdisk_alloc_pages(void) {
    uint32_t i;

    if (ramdisk0_ready) return 0;

    for (i = 0; i < RAMDISK0_PAGE_COUNT; i++) {
        ramdisk0_pages[i] = (uint8_t *)pmm_alloc_page();
        if (!ramdisk0_pages[i]) return -1;
        memset(ramdisk0_pages[i], 0, PAGE_SIZE);
    }

    ramdisk0_ready = 1;
    return 0;
}

static int ramdisk_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    uint32_t offset;
    uint32_t bytes;
    (void)dev;

    if (!ramdisk0_ready) return -1;
    offset = lba * RAMDISK0_SECTOR_SIZE;
    bytes = count * RAMDISK0_SECTOR_SIZE;
    if (ramdisk_copy_out(offset, buf, bytes) < 0) return -1;
    return (int)count;
}

static int ramdisk_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    uint32_t offset;
    uint32_t bytes;
    (void)dev;

    if (!ramdisk0_ready) return -1;
    offset = lba * RAMDISK0_SECTOR_SIZE;
    bytes = count * RAMDISK0_SECTOR_SIZE;
    if (ramdisk_copy_in(offset, buf, bytes) < 0) return -1;
    return (int)count;
}

static blockdev_ops_t ramdisk_ops = {
    0,
    0,
    ramdisk_read_blocks,
    ramdisk_write_blocks,
    0
};

void blockdev_register_builtin_devices(void) {
    serial_write("[BLOCKDEV] builtin begin\n");
    vfs_mkdir("/dev", 0755);

    serial_write("[BLOCKDEV] alloc ram0 pages\n");
    if (ramdisk_alloc_pages() < 0) {
        serial_write("[ERR] ram0 alloc failed\n");
        return;
    }

    serial_write("[BLOCKDEV] register ram0\n");
    if (blockdev_register("ram0", 1, 0,
                          RAMDISK0_SECTOR_SIZE,
                          RAMDISK0_SECTOR_COUNT,
                          &ramdisk_ops,
                          ramdisk0_pages) < 0) {
        serial_write("[ERR] ram0 register failed\n");
        return;
    }

    serial_write("[BLOCKDEV] create /dev/ram0\n");
    if (vfs_mknod("/dev/ram0", FS_BLOCK_DEVICE | 0666, "ram0") < 0) {
        serial_write("[ERR] /dev/ram0 create failed\n");
        return;
    }

    serial_write("[OK] block devices registered\n");
}
