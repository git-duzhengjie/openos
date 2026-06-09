/* ============================================================
 * openos - exFAT 文件系统驱动 (Phase 3)
 *
 * 当前实现：挂载 + 目录树扫描 + 文件读取 + 已有文件写入/扩容。
 * 目标：先让块设备上的 exFAT 卷可以通过 VFS 的 ls/cat/write 访问。
 * ============================================================ */

#include "../include/exfat.h"
#include "../include/blockdev.h"
#include "../include/string.h"
#include "../include/serial.h"
#include "vfs.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#define EXFAT_FS_TYPE_OFFSET          3
#define EXFAT_FS_TYPE_LEN             8
#define EXFAT_PARTITION_OFFSET_OFF    64
#define EXFAT_VOLUME_LENGTH_OFF       72
#define EXFAT_FAT_OFFSET_OFF          80
#define EXFAT_FAT_LENGTH_OFF          84
#define EXFAT_CLUSTER_HEAP_OFFSET_OFF 88
#define EXFAT_CLUSTER_COUNT_OFF       92
#define EXFAT_ROOT_CLUSTER_OFF        96
#define EXFAT_VOLUME_SERIAL_OFF       100
#define EXFAT_REVISION_OFF            104
#define EXFAT_VOLUME_FLAGS_OFF        106
#define EXFAT_BYTES_PER_SECTOR_SHIFT  108
#define EXFAT_SECTORS_PER_CLUSTER_SHIFT 109
#define EXFAT_NUMBER_OF_FATS          110
#define EXFAT_DRIVE_SELECT            111
#define EXFAT_PERCENT_IN_USE          112
#define EXFAT_BOOT_SIGNATURE_OFF      510

#define EXFAT_BOOT_SIGNATURE          0xAA55
#define EXFAT_FAT_EOC                 0xFFFFFFFFu
#define EXFAT_FAT_BAD                 0xFFFFFFF7u
#define EXFAT_FIRST_DATA_CLUSTER      2

#define EXFAT_ENTRY_EOD               0x00
#define EXFAT_ENTRY_BITMAP            0x81
#define EXFAT_ENTRY_UPCASE            0x82
#define EXFAT_ENTRY_LABEL             0x83
#define EXFAT_ENTRY_FILE              0x85
#define EXFAT_ENTRY_STREAM            0xC0
#define EXFAT_ENTRY_NAME              0xC1

#define EXFAT_ATTR_DIRECTORY          0x0010
#define EXFAT_STREAM_NO_FAT_CHAIN     0x02

#define EXFAT_MAX_MOUNTS              4
#define EXFAT_MAX_NODES               128
#define EXFAT_MAX_CLUSTER_CHAIN       4096
#define EXFAT_TEST_SECTORS            2048

typedef struct exfat_boot_info {
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
} exfat_boot_info_t;

typedef struct exfat_mount exfat_mount_t;

typedef struct exfat_node {
    int used;
    exfat_mount_t *mount;
    uint32_t first_cluster;
    uint64_t size;
    uint16_t attr;
    uint8_t flags;
    uint32_t dir_cluster;
    uint32_t file_entry_offset;
    uint32_t stream_entry_offset;
} exfat_node_t;

struct exfat_mount {
    int used;
    blockdev_t *dev;
    exfat_boot_info_t boot;
    exfat_node_t nodes[EXFAT_MAX_NODES];
};

static exfat_mount_t exfat_mounts[EXFAT_MAX_MOUNTS];

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) {
    uint64_t lo = rd32(p);
    uint64_t hi = rd32(p + 4);
    return lo | (hi << 32);
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)(v & 0xFFFFFFFFu));
    wr32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t exfat_cluster_lba(exfat_mount_t *m, uint32_t cluster) {
    if (!m || cluster < EXFAT_FIRST_DATA_CLUSTER) return 0;
    return m->boot.cluster_heap_offset +
           (cluster - EXFAT_FIRST_DATA_CLUSTER) * m->boot.sectors_per_cluster;
}

static int exfat_read_sector(exfat_mount_t *m, uint32_t lba, void *buf) {
    if (!m || !m->dev || !buf) return -1;
    if (m->dev->sector_size != 512) return -1;
    return blockdev_read_blocks(m->dev, lba, 1, buf) == 1 ? 0 : -1;
}

static int exfat_write_sector(exfat_mount_t *m, uint32_t lba, const void *buf) {
    if (!m || !m->dev || !buf) return -1;
    if (m->dev->sector_size != 512) return -1;
    return blockdev_write_blocks(m->dev, lba, 1, buf) == 1 ? 0 : -1;
}

static uint32_t exfat_next_cluster(exfat_mount_t *m, uint32_t cluster) {
    uint8_t sector[512];
    uint32_t fat_byte;
    uint32_t lba;
    uint32_t off;
    uint32_t next;

    if (!m || cluster < EXFAT_FIRST_DATA_CLUSTER) return EXFAT_FAT_EOC;
    if (cluster >= m->boot.cluster_count + EXFAT_FIRST_DATA_CLUSTER) return EXFAT_FAT_EOC;

    fat_byte = cluster * 4;
    lba = m->boot.fat_offset + (fat_byte / m->boot.bytes_per_sector);
    off = fat_byte % m->boot.bytes_per_sector;
    if (exfat_read_sector(m, lba, sector) < 0) return EXFAT_FAT_EOC;
    next = rd32(sector + off);
    if (next >= 0xFFFFFFF8u || next == EXFAT_FAT_BAD || next == 0) return EXFAT_FAT_EOC;
    return next;
}

static int exfat_read_cluster(exfat_mount_t *m, uint32_t cluster, void *buf) {
    uint8_t *out = (uint8_t *)buf;
    uint32_t base;
    uint32_t i;

    if (!m || !buf) return -1;
    base = exfat_cluster_lba(m, cluster);
    if (!base) return -1;
    for (i = 0; i < m->boot.sectors_per_cluster; i++) {
        if (exfat_read_sector(m, base + i, out + i * m->boot.bytes_per_sector) < 0) return -1;
    }
    return 0;
}

static int exfat_write_cluster(exfat_mount_t *m, uint32_t cluster, const void *buf) {
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t base;
    uint32_t i;

    if (!m || !buf) return -1;
    base = exfat_cluster_lba(m, cluster);
    if (!base) return -1;
    for (i = 0; i < m->boot.sectors_per_cluster; i++) {
        if (exfat_write_sector(m, base + i, in + i * m->boot.bytes_per_sector) < 0) return -1;
    }
    return 0;
}

static int exfat_set_next_cluster(exfat_mount_t *m, uint32_t cluster, uint32_t next) {
    uint8_t sector[512];
    uint32_t fat_byte;
    uint32_t lba;
    uint32_t off;

    if (!m || cluster < EXFAT_FIRST_DATA_CLUSTER) return -1;
    if (cluster >= m->boot.cluster_count + EXFAT_FIRST_DATA_CLUSTER) return -1;

    fat_byte = cluster * 4;
    lba = m->boot.fat_offset + (fat_byte / m->boot.bytes_per_sector);
    off = fat_byte % m->boot.bytes_per_sector;
    if (exfat_read_sector(m, lba, sector) < 0) return -1;
    wr32(sector + off, next);
    return exfat_write_sector(m, lba, sector) == 0 ? 0 : -1;
}

static uint32_t exfat_get_fat_entry(exfat_mount_t *m, uint32_t cluster) {
    uint8_t sector[512];
    uint32_t fat_byte;
    uint32_t lba;
    uint32_t off;

    if (!m || cluster < EXFAT_FIRST_DATA_CLUSTER) return EXFAT_FAT_EOC;
    if (cluster >= m->boot.cluster_count + EXFAT_FIRST_DATA_CLUSTER) return EXFAT_FAT_EOC;

    fat_byte = cluster * 4;
    lba = m->boot.fat_offset + (fat_byte / m->boot.bytes_per_sector);
    off = fat_byte % m->boot.bytes_per_sector;
    if (exfat_read_sector(m, lba, sector) < 0) return EXFAT_FAT_EOC;
    return rd32(sector + off);
}

static uint32_t exfat_find_free_cluster(exfat_mount_t *m) {
    uint32_t c;
    uint32_t end;

    if (!m) return 0;
    end = m->boot.cluster_count + EXFAT_FIRST_DATA_CLUSTER;
    for (c = EXFAT_FIRST_DATA_CLUSTER; c < end; c++) {
        if (exfat_get_fat_entry(m, c) == 0) return c;
    }
    return 0;
}

static uint32_t exfat_cluster_count_for_size(exfat_mount_t *m, uint64_t size) {
    uint32_t size32;
    uint32_t bpc;

    if (!m || m->boot.bytes_per_cluster == 0) return 0;
    if (size == 0) return 0;

    /*
     * 内核以 -nostdlib 链接，不能让 GCC 生成 64 位除法辅助符号
     * __udivdi3。当前 VFS/file_t offset 仍是 32 位，exfat_write_at()
     * 也限制写入目标大小不超过 4GiB，因此这里安全降为 32 位计算。
     */
    if (size > 0xFFFFFFFFULL) return 0xFFFFFFFFu;

    size32 = (uint32_t)size;
    bpc = m->boot.bytes_per_cluster;
    return ((size32 - 1) / bpc) + 1;
}

static uint32_t exfat_nth_cluster(exfat_node_t *node, uint32_t index) {
    exfat_mount_t *m;
    uint32_t c;
    uint32_t i;

    if (!node || !node->mount || node->first_cluster < EXFAT_FIRST_DATA_CLUSTER) return EXFAT_FAT_EOC;
    m = node->mount;
    c = node->first_cluster;
    for (i = 0; i < index; i++) {
        if (node->flags & EXFAT_STREAM_NO_FAT_CHAIN) c++;
        else c = exfat_next_cluster(m, c);
        if (c == EXFAT_FAT_EOC) return EXFAT_FAT_EOC;
    }
    return c;
}

static int exfat_zero_cluster(exfat_mount_t *m, uint32_t cluster) {
    uint8_t zero[4096];
    if (!m || m->boot.bytes_per_cluster > sizeof(zero)) return -1;
    memset(zero, 0, sizeof(zero));
    return exfat_write_cluster(m, cluster, zero);
}

static int exfat_update_stream_entry(exfat_node_t *node) {
    exfat_mount_t *m;
    uint8_t cluster_buf[4096];
    uint8_t *stream;

    if (!node || !node->mount) return -1;
    if (node->stream_entry_offset >= node->mount->boot.bytes_per_cluster) return -1;
    m = node->mount;
    if (m->boot.bytes_per_cluster > sizeof(cluster_buf)) return -1;
    if (exfat_read_cluster(m, node->dir_cluster, cluster_buf) < 0) return -1;
    stream = cluster_buf + node->stream_entry_offset;
    if (stream[0] != EXFAT_ENTRY_STREAM) return -1;
    stream[1] = node->flags;
    wr64(stream + 8, node->size);   /* ValidDataLength */
    wr32(stream + 20, node->first_cluster);
    wr64(stream + 24, node->size);  /* DataLength */
    return exfat_write_cluster(m, node->dir_cluster, cluster_buf);
}

static int exfat_ensure_clusters(exfat_node_t *node, uint64_t target_size) {
    exfat_mount_t *m;
    uint32_t have;
    uint32_t need;
    uint32_t last;

    if (!node || !node->mount) return -1;
    m = node->mount;
    have = exfat_cluster_count_for_size(m, node->size);
    need = exfat_cluster_count_for_size(m, target_size);
    if (need <= have) return 0;
    if (node->first_cluster < EXFAT_FIRST_DATA_CLUSTER) return -1;

    if (have == 0) have = 1;
    last = exfat_nth_cluster(node, have - 1);
    if (last == EXFAT_FAT_EOC) return -1;

    if (node->flags & EXFAT_STREAM_NO_FAT_CHAIN) {
        uint32_t i;
        node->flags &= (uint8_t)~EXFAT_STREAM_NO_FAT_CHAIN;
        for (i = 0; i < have - 1; i++) {
            if (exfat_set_next_cluster(m, node->first_cluster + i, node->first_cluster + i + 1) < 0) return -1;
        }
        if (exfat_set_next_cluster(m, last, EXFAT_FAT_EOC) < 0) return -1;
    }

    while (have < need) {
        uint32_t freec = exfat_find_free_cluster(m);
        if (!freec) return -1;
        if (exfat_set_next_cluster(m, last, freec) < 0) return -1;
        if (exfat_set_next_cluster(m, freec, EXFAT_FAT_EOC) < 0) return -1;
        if (exfat_zero_cluster(m, freec) < 0) return -1;
        last = freec;
        have++;
    }
    return 0;
}

static int exfat_read_at(exfat_node_t *node, uint32_t offset, void *buf, uint32_t count) {
    exfat_mount_t *m;
    uint8_t cluster_buf[4096];
    uint8_t *out = (uint8_t *)buf;
    uint32_t cluster;
    uint32_t cluster_index;
    uint32_t skip_clusters;
    uint32_t in_cluster;
    uint32_t done = 0;

    if (!node || !node->mount || !buf) return -1;
    m = node->mount;
    if (m->boot.bytes_per_cluster > sizeof(cluster_buf)) return -1;
    if ((uint64_t)offset >= node->size) return 0;
    if ((uint64_t)count > node->size - offset) count = (uint32_t)(node->size - offset);

    cluster = node->first_cluster;
    skip_clusters = offset / m->boot.bytes_per_cluster;
    in_cluster = offset % m->boot.bytes_per_cluster;

    for (cluster_index = 0; cluster_index < skip_clusters; cluster_index++) {
        if (node->flags & EXFAT_STREAM_NO_FAT_CHAIN) cluster++;
        else cluster = exfat_next_cluster(m, cluster);
        if (cluster == EXFAT_FAT_EOC) return (int)done;
    }

    while (done < count && cluster != EXFAT_FAT_EOC) {
        uint32_t chunk;
        if (exfat_read_cluster(m, cluster, cluster_buf) < 0) return done ? (int)done : -1;
        chunk = m->boot.bytes_per_cluster - in_cluster;
        if (chunk > count - done) chunk = count - done;
        memcpy(out + done, cluster_buf + in_cluster, chunk);
        done += chunk;
        in_cluster = 0;
        if (done < count) {
            if (node->flags & EXFAT_STREAM_NO_FAT_CHAIN) cluster++;
            else cluster = exfat_next_cluster(m, cluster);
        }
    }

    return (int)done;
}

static int exfat_file_read(file_t *f, void *buf, uint32_t count) {
    exfat_node_t *node;
    int ret;

    if (!f || !f->inode || !f->inode->fs_data) return -1;
    node = (exfat_node_t *)f->inode->fs_data;
    ret = exfat_read_at(node, f->offset, buf, count);
    if (ret > 0) f->offset += (uint32_t)ret;
    return ret;
}

static int exfat_file_seek(file_t *f, int offset, int whence) {
    uint32_t new_off;

    if (!f || !f->inode) return -1;
    switch (whence) {
    case SEEK_SET:
        if (offset < 0) return -1;
        new_off = (uint32_t)offset;
        break;
    case SEEK_CUR:
        if (offset < 0 && (uint32_t)(-offset) > f->offset) return -1;
        new_off = f->offset + offset;
        break;
    case SEEK_END:
        if (offset < 0 && (uint32_t)(-offset) > f->inode->size) return -1;
        new_off = f->inode->size + offset;
        break;
    default:
        return -1;
    }
    f->offset = new_off;
    return (int)new_off;
}

static int exfat_write_at(exfat_node_t *node, uint32_t offset, const void *buf, uint32_t count) {
    exfat_mount_t *m;
    uint8_t cluster_buf[4096];
    const uint8_t *in = (const uint8_t *)buf;
    uint64_t target_size;
    uint32_t cluster;
    uint32_t cluster_index;
    uint32_t skip_clusters;
    uint32_t in_cluster;
    uint32_t done = 0;

    if (!node || !node->mount || !buf) return -1;
    if (count == 0) return 0;
    m = node->mount;
    if (m->boot.bytes_per_cluster > sizeof(cluster_buf)) return -1;

    target_size = (uint64_t)offset + count;
    if (target_size > 0xFFFFFFFFu) return -1;
    if (target_size > node->size) {
        if (exfat_ensure_clusters(node, target_size) < 0) return -1;
    }

    cluster = node->first_cluster;
    skip_clusters = offset / m->boot.bytes_per_cluster;
    in_cluster = offset % m->boot.bytes_per_cluster;

    for (cluster_index = 0; cluster_index < skip_clusters; cluster_index++) {
        if (node->flags & EXFAT_STREAM_NO_FAT_CHAIN) cluster++;
        else cluster = exfat_next_cluster(m, cluster);
        if (cluster == EXFAT_FAT_EOC) return done ? (int)done : -1;
    }

    while (done < count && cluster != EXFAT_FAT_EOC) {
        uint32_t chunk;
        if (exfat_read_cluster(m, cluster, cluster_buf) < 0) return done ? (int)done : -1;
        chunk = m->boot.bytes_per_cluster - in_cluster;
        if (chunk > count - done) chunk = count - done;
        memcpy(cluster_buf + in_cluster, in + done, chunk);
        if (exfat_write_cluster(m, cluster, cluster_buf) < 0) return done ? (int)done : -1;
        done += chunk;
        in_cluster = 0;
        if (done < count) {
            if (node->flags & EXFAT_STREAM_NO_FAT_CHAIN) cluster++;
            else cluster = exfat_next_cluster(m, cluster);
        }
    }

    if ((uint64_t)offset + done > node->size) {
        node->size = (uint64_t)offset + done;
        if (exfat_update_stream_entry(node) < 0) return done ? (int)done : -1;
    }

    return (int)done;
}

static int exfat_file_write(file_t *f, const void *buf, uint32_t count) {
    exfat_node_t *node;
    int ret;

    if (!f || !f->inode || !f->inode->fs_data) return -1;
    node = (exfat_node_t *)f->inode->fs_data;
    ret = exfat_write_at(node, f->offset, buf, count);
    if (ret > 0) f->offset += (uint32_t)ret;
    if (f->inode->size < node->size) f->inode->size = node->size;
    return ret;
}

static int exfat_file_truncate(inode_t *inode, uint32_t size) {
    exfat_node_t *node;
    exfat_mount_t *m;
    uint32_t bpc;

    if (!inode || !inode->fs_data) return -1;
    node = (exfat_node_t *)inode->fs_data;
    if (node->attr & EXFAT_ATTR_DIRECTORY) return -1;
    if (!node->mount) return -1;
    m = node->mount;
    bpc = m->boot.bytes_per_cluster;
    if (bpc == 0) return -1;

    if ((uint64_t)size > node->size) {
        if (exfat_ensure_clusters(node, size) < 0) return -1;
    } else if ((uint64_t)size < node->size && size > 0 && (size % bpc) != 0) {
        uint8_t cluster_buf[4096];
        uint32_t cluster = exfat_nth_cluster(node, size / bpc);
        uint32_t off = size % bpc;
        if (bpc > sizeof(cluster_buf)) return -1;
        if (cluster != EXFAT_FAT_EOC) {
            if (exfat_read_cluster(m, cluster, cluster_buf) < 0) return -1;
            memset(cluster_buf + off, 0, bpc - off);
            if (exfat_write_cluster(m, cluster, cluster_buf) < 0) return -1;
        }
    }

    node->size = size;
    inode->size = size;
    return exfat_update_stream_entry(node);
}

static file_ops_t exfat_file_ops = {
    0,
    0,
    exfat_file_read,
    exfat_file_write,
    exfat_file_seek,
    exfat_file_truncate,
    0
};

static exfat_node_t *exfat_alloc_node(exfat_mount_t *m) {
    uint32_t i;
    if (!m) return NULL;
    for (i = 0; i < EXFAT_MAX_NODES; i++) {
        if (!m->nodes[i].used) {
            memset(&m->nodes[i], 0, sizeof(exfat_node_t));
            m->nodes[i].used = 1;
            m->nodes[i].mount = m;
            return &m->nodes[i];
        }
    }
    return NULL;
}

static exfat_mount_t *exfat_alloc_mount(void) {
    uint32_t i;
    for (i = 0; i < EXFAT_MAX_MOUNTS; i++) {
        if (!exfat_mounts[i].used) {
            memset(&exfat_mounts[i], 0, sizeof(exfat_mount_t));
            exfat_mounts[i].used = 1;
            return &exfat_mounts[i];
        }
    }
    return NULL;
}

static int exfat_parse_boot(exfat_mount_t *m) {
    uint8_t sector[512];
    uint16_t sig;

    if (!m) return -1;
    if (exfat_read_sector(m, 0, sector) < 0) return -1;
    if (memcmp(sector + EXFAT_FS_TYPE_OFFSET, "EXFAT   ", EXFAT_FS_TYPE_LEN) != 0) return -1;

    sig = rd16(sector + EXFAT_BOOT_SIGNATURE_OFF);
    if (sig != EXFAT_BOOT_SIGNATURE) return -1;

    m->boot.fat_offset = rd32(sector + EXFAT_FAT_OFFSET_OFF);
    m->boot.fat_length = rd32(sector + EXFAT_FAT_LENGTH_OFF);
    m->boot.cluster_heap_offset = rd32(sector + EXFAT_CLUSTER_HEAP_OFFSET_OFF);
    m->boot.cluster_count = rd32(sector + EXFAT_CLUSTER_COUNT_OFF);
    m->boot.root_cluster = rd32(sector + EXFAT_ROOT_CLUSTER_OFF);
    m->boot.bytes_per_sector = 1u << sector[EXFAT_BYTES_PER_SECTOR_SHIFT];
    m->boot.sectors_per_cluster = 1u << sector[EXFAT_SECTORS_PER_CLUSTER_SHIFT];
    m->boot.bytes_per_cluster = m->boot.bytes_per_sector * m->boot.sectors_per_cluster;

    if (m->boot.bytes_per_sector != 512) return -1;
    if (m->boot.sectors_per_cluster == 0) return -1;
    if (m->boot.bytes_per_cluster == 0 || m->boot.bytes_per_cluster > 4096) return -1;
    if (m->boot.root_cluster < EXFAT_FIRST_DATA_CLUSTER) return -1;
    return 0;
}

static void exfat_utf16_to_ascii_name(char *dst, const uint8_t *name_entries, uint32_t name_len) {
    uint32_t out = 0;
    uint32_t pos = 0;

    if (!dst) return;
    dst[0] = '\0';
    while (pos < name_len && out < MAX_NAME - 1) {
        const uint8_t *entry = name_entries + (pos / 15) * 32;
        uint32_t slot = pos % 15;
        uint16_t ch = rd16(entry + 2 + slot * 2);
        if (ch == 0) break;
        dst[out++] = (ch < 0x80) ? (char)ch : '?';
        pos++;
    }
    dst[out] = '\0';
}

static int exfat_scan_directory(exfat_mount_t *m, dentry_t *parent, uint32_t start_cluster, uint64_t dir_size, uint8_t dir_flags, int depth) {
    uint8_t cluster_buf[4096];
    uint8_t name_entries[17 * 32];
    uint32_t cluster = start_cluster;
    uint64_t consumed = 0;

    if (!m || !parent || depth > 8) return -1;

    while (cluster != EXFAT_FAT_EOC && consumed < dir_size) {
        uint32_t off;
        if (exfat_read_cluster(m, cluster, cluster_buf) < 0) return -1;

        for (off = 0; off + 32 <= m->boot.bytes_per_cluster && consumed < dir_size; off += 32, consumed += 32) {
            uint8_t *e = cluster_buf + off;
            if (e[0] == EXFAT_ENTRY_EOD) return 0;

            if (e[0] == EXFAT_ENTRY_FILE) {
                uint8_t secondary_count = e[1];
                uint16_t attr = rd16(e + 4);
                uint32_t entry_bytes = (uint32_t)(secondary_count + 1) * 32;
                uint8_t stream[32];
                uint32_t name_len = 0;
                uint32_t first_cluster = 0;
                uint64_t data_len = 0;
                uint8_t flags = 0;
                char name[MAX_NAME];
                uint32_t nbytes = 0;
                uint32_t s;
                dentry_t *child;
                exfat_node_t *node;

                memset(stream, 0, sizeof(stream));
                memset(name_entries, 0, sizeof(name_entries));
                name[0] = '\0';

                for (s = 1; s <= secondary_count && off + s * 32 + 32 <= m->boot.bytes_per_cluster; s++) {
                    uint8_t *se = cluster_buf + off + s * 32;
                    if (se[0] == EXFAT_ENTRY_STREAM) {
                        memcpy(stream, se, 32);
                        flags = se[1];
                        name_len = se[3];
                        first_cluster = rd32(se + 20);
                        data_len = rd64(se + 24);
                    } else if (se[0] == EXFAT_ENTRY_NAME) {
                        if (nbytes + 32 <= sizeof(name_entries)) {
                            memcpy(name_entries + nbytes, se, 32);
                            nbytes += 32;
                        }
                    }
                }

                exfat_utf16_to_ascii_name(name, name_entries, name_len);
                if (name[0] && first_cluster >= EXFAT_FIRST_DATA_CLUSTER) {
                    node = exfat_alloc_node(m);
                    if (!node) return -1;
                    node->first_cluster = first_cluster;
                    node->size = data_len;
                    node->attr = attr;
                    node->flags = flags;
                    node->dir_cluster = cluster;
                    node->file_entry_offset = off;
                    node->stream_entry_offset = off + 32;

                    if (attr & EXFAT_ATTR_DIRECTORY) {
                        child = vfs_create_node_under(parent, name, FS_DIR | 0555, 0, node, (uint32_t)data_len);
                        if (!child) return -1;
                        exfat_scan_directory(m, child, first_cluster, data_len, flags, depth + 1);
                    } else {
                        child = vfs_create_node_under(parent, name, FS_FILE | 0644, &exfat_file_ops, node, (uint32_t)data_len);
                        if (!child) return -1;
                    }
                }

                off += entry_bytes - 32;
                consumed += entry_bytes - 32;
            }
        }

        if (dir_flags & EXFAT_STREAM_NO_FAT_CHAIN) cluster++;
        else cluster = exfat_next_cluster(m, cluster);
    }
    return 0;
}

int exfat_mount(const char *dev_name, const char *mount_path) {
    blockdev_t *dev;
    exfat_mount_t *m;
    dentry_t *mount_dentry;
    exfat_node_t *root_node;

    if (!dev_name || !mount_path) return -1;
    serial_write("[EXFAT] mount begin\n");

    dev = blockdev_find(dev_name);
    if (!dev) {
        serial_write("[EXFAT] block device not found\n");
        return -1;
    }

    mount_dentry = vfs_path_lookup(mount_path);
    if (!mount_dentry) {
        if (vfs_mkdir(mount_path, 0755) < 0) {
            serial_write("[EXFAT] create mount dir failed\n");
            return -1;
        }
        mount_dentry = vfs_path_lookup(mount_path);
    }
    if (!mount_dentry) return -1;

    m = exfat_alloc_mount();
    if (!m) return -1;
    m->dev = dev;

    if (blockdev_open(dev) < 0) return -1;
    if (exfat_parse_boot(m) < 0) {
        serial_write("[EXFAT] invalid exFAT volume\n");
        blockdev_close(dev);
        m->used = 0;
        return -1;
    }

    root_node = exfat_alloc_node(m);
    if (!root_node) return -1;
    root_node->first_cluster = m->boot.root_cluster;
    root_node->size = m->boot.bytes_per_cluster;
    root_node->attr = EXFAT_ATTR_DIRECTORY;
    root_node->flags = 0;
    mount_dentry->inode->fs_data = root_node;

    if (exfat_scan_directory(m, mount_dentry, m->boot.root_cluster, m->boot.bytes_per_cluster, 0, 0) < 0) {
        serial_write("[EXFAT] scan root failed\n");
        return -1;
    }

    serial_write("[OK] exFAT mounted\n");
    return 0;
}

static void exfat_set_name_entry(uint8_t *entry, const char *name, uint32_t start, uint32_t len) {
    uint32_t i;
    memset(entry, 0, 32);
    entry[0] = EXFAT_ENTRY_NAME;
    entry[1] = 0;
    for (i = 0; i < 15; i++) {
        uint16_t ch = 0;
        if (start + i < len) ch = (uint16_t)(uint8_t)name[start + i];
        wr16(entry + 2 + i * 2, ch);
    }
}

int exfat_format_test_volume(const char *dev_name) {
    blockdev_t *dev;
    exfat_mount_t temp;
    uint8_t sector[512];
    uint8_t hello[] = "Hello from openos exFAT!\n";
    const char *fname = "HELLO.TXT";
    uint32_t name_len = 9;

    if (!dev_name) return -1;
    dev = blockdev_find(dev_name);
    if (!dev) return -1;
    if (dev->sector_size != 512 || dev->sector_count < EXFAT_TEST_SECTORS) return -1;

    memset(&temp, 0, sizeof(temp));
    temp.dev = dev;
    temp.boot.bytes_per_sector = 512;
    temp.boot.sectors_per_cluster = 1;
    temp.boot.bytes_per_cluster = 512;
    temp.boot.fat_offset = 128;
    temp.boot.fat_length = 16;
    temp.boot.cluster_heap_offset = 144;
    temp.boot.cluster_count = 256;
    temp.boot.root_cluster = 2;

    memset(sector, 0, sizeof(sector));
    sector[0] = 0xEB;
    sector[1] = 0x76;
    sector[2] = 0x90;
    memcpy(sector + EXFAT_FS_TYPE_OFFSET, "EXFAT   ", 8);
    wr64(sector + EXFAT_PARTITION_OFFSET_OFF, 0);
    wr64(sector + EXFAT_VOLUME_LENGTH_OFF, dev->sector_count);
    wr32(sector + EXFAT_FAT_OFFSET_OFF, temp.boot.fat_offset);
    wr32(sector + EXFAT_FAT_LENGTH_OFF, temp.boot.fat_length);
    wr32(sector + EXFAT_CLUSTER_HEAP_OFFSET_OFF, temp.boot.cluster_heap_offset);
    wr32(sector + EXFAT_CLUSTER_COUNT_OFF, temp.boot.cluster_count);
    wr32(sector + EXFAT_ROOT_CLUSTER_OFF, temp.boot.root_cluster);
    wr32(sector + EXFAT_VOLUME_SERIAL_OFF, 0x20260609);
    wr16(sector + EXFAT_REVISION_OFF, 0x0100);
    wr16(sector + EXFAT_VOLUME_FLAGS_OFF, 0);
    sector[EXFAT_BYTES_PER_SECTOR_SHIFT] = 9;
    sector[EXFAT_SECTORS_PER_CLUSTER_SHIFT] = 0;
    sector[EXFAT_NUMBER_OF_FATS] = 1;
    sector[EXFAT_DRIVE_SELECT] = 0x80;
    sector[EXFAT_PERCENT_IN_USE] = 1;
    wr16(sector + EXFAT_BOOT_SIGNATURE_OFF, EXFAT_BOOT_SIGNATURE);
    if (blockdev_write_blocks(dev, 0, 1, sector) != 1) return -1;

    memset(sector, 0, sizeof(sector));
    wr32(sector + 0, 0xFFFFFFF8u);
    wr32(sector + 4, 0xFFFFFFFFu);
    wr32(sector + 8, 0xFFFFFFFFu);   /* cluster 2 root */
    wr32(sector + 12, 0xFFFFFFFFu);  /* cluster 3 HELLO.TXT */
    if (blockdev_write_blocks(dev, temp.boot.fat_offset, 1, sector) != 1) return -1;

    memset(sector, 0, sizeof(sector));
    sector[0] = EXFAT_ENTRY_FILE;
    sector[1] = 2;
    wr16(sector + 4, 0x0020);

    sector[32] = EXFAT_ENTRY_STREAM;
    sector[33] = EXFAT_STREAM_NO_FAT_CHAIN;
    sector[35] = (uint8_t)name_len;
    wr32(sector + 32 + 20, 3);
    wr64(sector + 32 + 24, sizeof(hello) - 1);

    exfat_set_name_entry(sector + 64, fname, 0, name_len);
    sector[96] = EXFAT_ENTRY_EOD;
    if (blockdev_write_blocks(dev, exfat_cluster_lba(&temp, 2), 1, sector) != 1) return -1;

    memset(sector, 0, sizeof(sector));
    memcpy(sector, hello, sizeof(hello) - 1);
    if (blockdev_write_blocks(dev, exfat_cluster_lba(&temp, 3), 1, sector) != 1) return -1;

    serial_write("[OK] exFAT test volume formatted\n");
    return 0;
}
