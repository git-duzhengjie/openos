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

#define BLOCK_CACHE_ENTRIES 64
#define BLOCK_CACHE_SECTOR_SIZE 512

typedef struct block_cache_entry {
    uint8_t valid;
    uint8_t dirty;
    uint32_t age;
    blockdev_t *dev;
    uint32_t lba;
    uint8_t data[BLOCK_CACHE_SECTOR_SIZE];
} block_cache_entry_t;

static block_cache_entry_t block_cache[BLOCK_CACHE_ENTRIES];
static uint32_t block_cache_clock = 1;
static blockdev_cache_stats_t block_cache_stats;

static int blockdev_raw_read_one(blockdev_t *dev, uint32_t lba, void *buf) {
    if (!dev || !buf || !dev->ops || !dev->ops->read_blocks) return -1;
    return dev->ops->read_blocks(dev, lba, 1, buf) == 1 ? 0 : -1;
}

static int blockdev_raw_write_one(blockdev_t *dev, uint32_t lba, const void *buf) {
    if (!dev || !buf || !dev->ops || !dev->ops->write_blocks) return -1;
    return dev->ops->write_blocks(dev, lba, 1, buf) == 1 ? 0 : -1;
}

static int block_cache_flush_entry(block_cache_entry_t *e) {
    if (!e || !e->valid || !e->dirty) return 0;
    if (blockdev_raw_write_one(e->dev, e->lba, e->data) < 0) return -1;
    e->dirty = 0;
    block_cache_stats.flushes++;
    return 0;
}

static block_cache_entry_t *block_cache_find(blockdev_t *dev, uint32_t lba) {
    uint32_t i;
    for (i = 0; i < BLOCK_CACHE_ENTRIES; i++) {
        if (block_cache[i].valid && block_cache[i].dev == dev && block_cache[i].lba == lba) {
            block_cache[i].age = block_cache_clock++;
            return &block_cache[i];
        }
    }
    return 0;
}

static block_cache_entry_t *block_cache_alloc(blockdev_t *dev, uint32_t lba) {
    uint32_t i;
    uint32_t victim = 0;
    uint32_t best_age = 0xFFFFFFFFu;

    for (i = 0; i < BLOCK_CACHE_ENTRIES; i++) {
        if (!block_cache[i].valid) {
            victim = i;
            best_age = 0;
            break;
        }
        if (block_cache[i].age < best_age) {
            best_age = block_cache[i].age;
            victim = i;
        }
    }

    if (best_age != 0) {
        if (block_cache_flush_entry(&block_cache[victim]) < 0) return 0;
        block_cache_stats.evictions++;
    }
    memset(&block_cache[victim], 0, sizeof(block_cache[victim]));
    block_cache[victim].valid = 1;
    block_cache[victim].dev = dev;
    block_cache[victim].lba = lba;
    block_cache[victim].age = block_cache_clock++;
    return &block_cache[victim];
}

static int block_cache_read_one(blockdev_t *dev, uint32_t lba, void *buf) {
    block_cache_entry_t *e;

    if (dev->sector_size != BLOCK_CACHE_SECTOR_SIZE) {
        return dev->ops->read_blocks(dev, lba, 1, buf) == 1 ? 0 : -1;
    }

    e = block_cache_find(dev, lba);
    if (e) {
        block_cache_stats.read_hits++;
    } else {
        block_cache_stats.read_misses++;
        e = block_cache_alloc(dev, lba);
        if (!e) return -1;
        if (blockdev_raw_read_one(dev, lba, e->data) < 0) {
            memset(e, 0, sizeof(*e));
            return -1;
        }
    }
    memcpy(buf, e->data, BLOCK_CACHE_SECTOR_SIZE);
    return 0;
}

static int block_cache_write_one(blockdev_t *dev, uint32_t lba, const void *buf) {
    block_cache_entry_t *e;

    if (dev->sector_size != BLOCK_CACHE_SECTOR_SIZE) {
        return dev->ops->write_blocks(dev, lba, 1, buf) == 1 ? 0 : -1;
    }

    e = block_cache_find(dev, lba);
    if (e) {
        block_cache_stats.write_hits++;
    } else {
        block_cache_stats.write_misses++;
        e = block_cache_alloc(dev, lba);
        if (!e) return -1;
    }
    memcpy(e->data, buf, BLOCK_CACHE_SECTOR_SIZE);
    e->dirty = 1;
    e->age = block_cache_clock++;
    return 0;
}

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
    memset(block_cache, 0, sizeof(block_cache));
    memset(&block_cache_stats, 0, sizeof(block_cache_stats));
    blockdev_table_count = 0;
    block_cache_clock = 1;
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
            if (blockdev_flush(&blockdev_table[i]) < 0) return -1;
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
    uint8_t *out = (uint8_t *)buf;
    uint32_t i;

    if (!dev || !buf) return -1;
    if (!dev->ops || !dev->ops->read_blocks) return -1;
    if (count == 0) return 0;
    if (lba >= dev->sector_count) return -1;
    if (count > dev->sector_count - lba) return -1;

    for (i = 0; i < count; i++) {
        if (block_cache_read_one(dev, lba + i, out + i * dev->sector_size) < 0) {
            return i == 0 ? -1 : (int)i;
        }
    }
    return (int)count;
}

int blockdev_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t i;

    if (!dev || !buf) return -1;
    if (!dev->ops || !dev->ops->write_blocks) return -1;
    if (count == 0) return 0;
    if (lba >= dev->sector_count) return -1;
    if (count > dev->sector_count - lba) return -1;

    for (i = 0; i < count; i++) {
        if (block_cache_write_one(dev, lba + i, in + i * dev->sector_size) < 0) {
            return i == 0 ? -1 : (int)i;
        }
    }
    return (int)count;
}

int blockdev_flush(blockdev_t *dev) {
    uint32_t i;
    int ret = 0;

    if (!dev) return -1;
    for (i = 0; i < BLOCK_CACHE_ENTRIES; i++) {
        if (block_cache[i].valid && block_cache[i].dev == dev) {
            if (block_cache_flush_entry(&block_cache[i]) < 0) ret = -1;
        }
    }
    return ret;
}

int blockdev_flush_all(void) {
    uint32_t i;
    int ret = 0;

    for (i = 0; i < BLOCK_CACHE_ENTRIES; i++) {
        if (block_cache_flush_entry(&block_cache[i]) < 0) ret = -1;
    }
    return ret;
}

int blockdev_invalidate(blockdev_t *dev) {
    uint32_t i;

    if (!dev) return -1;
    if (blockdev_flush(dev) < 0) return -1;
    for (i = 0; i < BLOCK_CACHE_ENTRIES; i++) {
        if (block_cache[i].valid && block_cache[i].dev == dev) {
            memset(&block_cache[i], 0, sizeof(block_cache[i]));
        }
    }
    return 0;
}

int blockdev_invalidate_all(void) {
    if (blockdev_flush_all() < 0) return -1;
    memset(block_cache, 0, sizeof(block_cache));
    return 0;
}

void blockdev_cache_get_stats(blockdev_cache_stats_t *stats) {
    uint32_t i;

    if (!stats) return;
    *stats = block_cache_stats;
    stats->entries = BLOCK_CACHE_ENTRIES;
    stats->valid_entries = 0;
    stats->dirty_entries = 0;
    for (i = 0; i < BLOCK_CACHE_ENTRIES; i++) {
        if (block_cache[i].valid) {
            stats->valid_entries++;
            if (block_cache[i].dirty) stats->dirty_entries++;
        }
    }
}

void blockdev_cache_reset_stats(void) {
    memset(&block_cache_stats, 0, sizeof(block_cache_stats));
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

static blockdev_ops_t ramdisk_ops;

static void ramdisk_refresh_ops(void) {
    ramdisk_ops.open = 0;
    ramdisk_ops.close = 0;
    ramdisk_ops.read_blocks = ramdisk_read_blocks;
    ramdisk_ops.write_blocks = ramdisk_write_blocks;
    ramdisk_ops.ioctl = 0;
}


typedef struct block_partition_private {
    blockdev_t *parent;
    uint32_t start_lba;
    uint32_t sector_count;
} block_partition_private_t;

static block_partition_private_t partition_private_table[BLOCKDEV_MAX];
static blockdev_ops_t partition_ops;
static uint32_t partition_private_count = 0;

static int partition_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    block_partition_private_t *part;

    if (!dev || !buf || !dev->private_data) return -1;
    part = (block_partition_private_t *)dev->private_data;
    if (!part->parent) return -1;
    if (count == 0) return 0;
    if (lba >= part->sector_count) return -1;
    if (count > part->sector_count - lba) return -1;
    return blockdev_read_blocks(part->parent, part->start_lba + lba, count, buf);
}

static int partition_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    block_partition_private_t *part;

    if (!dev || !buf || !dev->private_data) return -1;
    part = (block_partition_private_t *)dev->private_data;
    if (!part->parent) return -1;
    if (count == 0) return 0;
    if (lba >= part->sector_count) return -1;
    if (count > part->sector_count - lba) return -1;
    return blockdev_write_blocks(part->parent, part->start_lba + lba, count, buf);
}

static int partition_ioctl(blockdev_t *dev, uint32_t request, void *arg) {
    block_partition_private_t *part;

    if (!dev || !dev->private_data) return -1;
    part = (block_partition_private_t *)dev->private_data;
    if (!part->parent) return -1;
    return blockdev_ioctl(part->parent, request, arg);
}

static void partition_refresh_ops(void) {
    partition_ops.open = 0;
    partition_ops.close = 0;
    partition_ops.read_blocks = partition_read_blocks;
    partition_ops.write_blocks = partition_write_blocks;
    partition_ops.ioctl = partition_ioctl;
}

static void partition_make_name(const char *base, int index, char *out) {
    uint32_t pos = 0;
    uint32_t i;

    if (!out) return;
    if (!base) base = "blk";
    for (i = 0; base[i] && pos < BLOCKDEV_NAME_MAX - 1; i++) out[pos++] = base[i];
    if (pos < BLOCKDEV_NAME_MAX - 1) out[pos++] = 'p';
    if (index >= 10 && pos < BLOCKDEV_NAME_MAX - 1) out[pos++] = (char)('0' + (index / 10));
    if (pos < BLOCKDEV_NAME_MAX - 1) out[pos++] = (char)('0' + (index % 10));
    out[pos] = 0;
}

static void partition_make_path(const char *name, char *out) {
    uint32_t pos = 0;
    uint32_t i;
    const char prefix[] = "/dev/";

    if (!out) return;
    for (i = 0; prefix[i] && pos < 31; i++) out[pos++] = prefix[i];
    if (name) {
        for (i = 0; name[i] && pos < 31; i++) out[pos++] = name[i];
    }
    out[pos] = 0;
}

static int partition_register_child(blockdev_t *parent, int index, uint32_t start_lba, uint32_t sector_count) {
    block_partition_private_t *priv;
    char dev_name[BLOCKDEV_NAME_MAX];
    char dev_path[32];

    if (!parent || sector_count == 0) return -1;
    if (start_lba >= parent->sector_count) return -1;
    if (sector_count > parent->sector_count - start_lba) return -1;
    if (partition_private_count >= BLOCKDEV_MAX) return -1;

    partition_make_name(parent->name, index, dev_name);
    partition_make_path(dev_name, dev_path);
    if (blockdev_find(dev_name)) return -1;

    priv = &partition_private_table[partition_private_count++];
    priv->parent = parent;
    priv->start_lba = start_lba;
    priv->sector_count = sector_count;

    if (blockdev_register(dev_name, parent->major, (uint32_t)index,
                          parent->sector_size, sector_count,
                          &partition_ops, priv) < 0) {
        partition_private_count--;
        memset(priv, 0, sizeof(*priv));
        return -1;
    }

    if (vfs_mknod(dev_path, FS_BLOCK_DEVICE | 0666, dev_name) < 0) {
        serial_write("[WARN] partition node create failed: ");
        serial_write(dev_path);
        serial_write("\n");
    }

    serial_write("[BLOCKDEV] found partition ");
    serial_write(dev_name);
    serial_write(" LBA: 0x");
    serial_write_hex(start_lba);
    serial_write(" sectors: 0x");
    serial_write_hex(sector_count);
    serial_write("\n");
    return 0;
}

typedef struct mbr_part_entry {
    uint8_t boot_flag;
    uint8_t chs_start[3];
    uint8_t type;
    uint8_t chs_end[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed)) mbr_part_entry_t;

typedef struct mbr_boot_sector {
    uint8_t boot_code[446];
    mbr_part_entry_t partitions[4];
    uint16_t signature;
} __attribute__((packed)) mbr_boot_sector_t;

typedef struct gpt_header {
    uint8_t signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t sizeof_partition_entry;
    uint32_t partition_entry_array_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct gpt_entry_min {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
} __attribute__((packed)) gpt_entry_min_t;

static int gpt_guid_is_zero(const uint8_t *guid) {
    uint32_t i;
    if (!guid) return 1;
    for (i = 0; i < 16; i++) {
        if (guid[i] != 0) return 0;
    }
    return 1;
}

static int blockdev_scan_gpt(blockdev_t *parent, int first_part_index) {
    uint8_t sector[BLOCK_CACHE_SECTOR_SIZE];
    gpt_header_t *hdr;
    uint32_t i;
    int part_index = first_part_index;

    if (!parent || parent->sector_size != BLOCK_CACHE_SECTOR_SIZE) return first_part_index;
    if (parent->sector_count < 2) return first_part_index;
    if (blockdev_read_blocks(parent, 1, 1, sector) != 1) return first_part_index;

    hdr = (gpt_header_t *)sector;
    if (memcmp(hdr->signature, "EFI PART", 8) != 0) return first_part_index;
    if (hdr->sizeof_partition_entry < sizeof(gpt_entry_min_t)) return first_part_index;
    if (hdr->partition_entry_lba == 0 || hdr->partition_entry_lba > 0xFFFFFFFFull) return first_part_index;

    serial_write("[BLOCKDEV] scanning GPT partitions\n");
    for (i = 0; i < hdr->num_partition_entries && part_index < BLOCKDEV_MAX; i++) {
        uint64_t byte_off = (uint64_t)i * hdr->sizeof_partition_entry;
        uint32_t entry_sector = (uint32_t)hdr->partition_entry_lba + (uint32_t)(byte_off / parent->sector_size);
        uint32_t entry_off = (uint32_t)(byte_off % parent->sector_size);
        gpt_entry_min_t *entry;
        uint64_t count64;

        if (entry_sector >= parent->sector_count) break;
        if (entry_off + sizeof(gpt_entry_min_t) > parent->sector_size) continue;
        if (blockdev_read_blocks(parent, entry_sector, 1, sector) != 1) break;

        entry = (gpt_entry_min_t *)(sector + entry_off);
        if (gpt_guid_is_zero(entry->type_guid)) continue;
        if (entry->last_lba < entry->first_lba) continue;
        if (entry->first_lba > 0xFFFFFFFFull || entry->last_lba > 0xFFFFFFFFull) continue;

        count64 = entry->last_lba - entry->first_lba + 1;
        if (count64 == 0 || count64 > 0xFFFFFFFFull) continue;
        if (partition_register_child(parent, part_index, (uint32_t)entry->first_lba, (uint32_t)count64) == 0) {
            part_index++;
        }
    }
    return part_index;
}

static int blockdev_scan_mbr(blockdev_t *parent, int first_part_index, int *has_protective_mbr) {
    mbr_boot_sector_t mbr;
    int part_index = first_part_index;
    int i;

    if (has_protective_mbr) *has_protective_mbr = 0;
    if (!parent || parent->sector_size != BLOCK_CACHE_SECTOR_SIZE) return first_part_index;
    if (blockdev_read_blocks(parent, 0, 1, &mbr) != 1 || mbr.signature != 0xAA55) return first_part_index;

    serial_write("[BLOCKDEV] scanning MBR partitions\n");
    for (i = 0; i < 4 && part_index < BLOCKDEV_MAX; i++) {
        if (mbr.partitions[i].type == 0 || mbr.partitions[i].sector_count == 0) continue;
        if (mbr.partitions[i].type == 0xEE) {
            if (has_protective_mbr) *has_protective_mbr = 1;
            continue;
        }
        if (partition_register_child(parent, part_index,
                                     mbr.partitions[i].lba_start,
                                     mbr.partitions[i].sector_count) == 0) {
            part_index++;
        }
    }
    return part_index;
}

static void blockdev_scan_partitions(blockdev_t *parent) {
    int next_index;
    int has_protective_mbr;

    if (!parent) return;
    partition_refresh_ops();
    next_index = blockdev_scan_mbr(parent, 1, &has_protective_mbr);
    if (has_protective_mbr || next_index == 1) {
        blockdev_scan_gpt(parent, next_index);
    }
}

void blockdev_register_builtin_devices(void) {
    serial_write("[BLOCKDEV] builtin begin\n");
    vfs_mkdir("/dev", 0755);

    serial_write("[BLOCKDEV] alloc ram0 pages\n");
    if (ramdisk_alloc_pages() < 0) {
        serial_write("[ERR] ram0 alloc failed\n");
        return;
    }

    ramdisk_refresh_ops();

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

    blockdev_scan_partitions(&blockdev_table[0]);

    serial_write("[OK] block devices registered\n");
}
