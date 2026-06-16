#include "fat32.h"
#include "vfs.h"
#include "blockdev.h"
#include "heap.h"
#include "string.h"
#include "serial.h"

#define FAT32_SECTOR_SIZE      512u
#define FAT32_EOC              0x0FFFFFF8u
#define FAT32_FREE_CLUSTER     0u
#define FAT32_ATTR_READ_ONLY   0x01u
#define FAT32_ATTR_HIDDEN      0x02u
#define FAT32_ATTR_SYSTEM      0x04u
#define FAT32_ATTR_VOLUME_ID   0x08u
#define FAT32_ATTR_DIRECTORY   0x10u
#define FAT32_ATTR_ARCHIVE     0x20u
#define FAT32_ATTR_LFN         0x0Fu
#define FAT32_MAX_CLUSTERS     4096u
#define FAT32_MAX_DEPTH        8u

typedef struct fat32_bpb {
    uint8_t jump[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors16;
    uint8_t media;
    uint16_t fat_size16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
    uint32_t fat_size32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct fat32_dirent {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} __attribute__((packed)) fat32_dirent_t;

typedef struct fat32_fs {
    blockdev_t *dev;
    fs_type_t vfs_fs;
    dentry_t *mount_root;
    uint32_t total_sectors;
    uint32_t fat_start_lba;
    uint32_t fat_size;
    uint32_t data_start_lba;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t cluster_count;
} fat32_fs_t;

typedef struct fat32_node {
    fat32_fs_t *fs;
    uint32_t first_cluster;
    uint32_t size;
    uint8_t is_dir;
} fat32_node_t;

static fat32_fs_t g_fat32_mounts[2];
static uint8_t g_fat32_mount_used[2];

static uint16_t rd16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static uint32_t rd32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void wr16(void *p, uint16_t v) {
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr32(void *p, uint32_t v) {
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)((v >> 8) & 0xFFu);
    b[2] = (uint8_t)((v >> 16) & 0xFFu);
    b[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int fat32_read_sector(fat32_fs_t *fs, uint32_t lba, void *buf) {
    if (!fs || !fs->dev || !buf) return -1;
    if (lba >= fs->dev->sector_count) return -1;
    return blockdev_read_blocks(fs->dev, lba, 1, buf);
}

static int fat32_write_sector(blockdev_t *dev, uint32_t lba, const void *buf) {
    if (!dev || !buf || lba >= dev->sector_count) return -1;
    return blockdev_write_blocks(dev, lba, 1, buf);
}

static uint32_t fat32_cluster_lba(fat32_fs_t *fs, uint32_t cluster) {
    if (!fs || cluster < 2) return 0;
    return fs->data_start_lba + ((cluster - 2u) * fs->sectors_per_cluster);
}

static int fat32_read_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t *next) {
    uint8_t sector[FAT32_SECTOR_SIZE];
    uint32_t fat_offset;
    uint32_t lba;
    uint32_t offset;

    if (!fs || !next || cluster >= fs->cluster_count + 2u) return -1;
    fat_offset = cluster * 4u;
    lba = fs->fat_start_lba + (fat_offset / FAT32_SECTOR_SIZE);
    offset = fat_offset % FAT32_SECTOR_SIZE;
    if (fat32_read_sector(fs, lba, sector) < 0) return -1;
    *next = rd32(&sector[offset]) & 0x0FFFFFFFu;
    return 0;
}

static uint32_t fat32_nth_cluster(fat32_fs_t *fs, uint32_t first, uint32_t index) {
    uint32_t cur = first;
    uint32_t next;
    uint32_t guard = 0;

    if (!fs || cur < 2) return 0;
    while (index > 0) {
        if (guard++ > fs->cluster_count) return 0;
        if (fat32_read_fat_entry(fs, cur, &next) < 0) return 0;
        if (next >= FAT32_EOC || next < 2) return 0;
        cur = next;
        index--;
    }
    return cur;
}

static int fat32_file_read(file_t *f, void *buf, uint32_t count) {
    fat32_node_t *node;
    uint8_t sector[FAT32_SECTOR_SIZE];
    uint8_t *out = (uint8_t *)buf;
    uint32_t done = 0;

    if (!f || !buf || !f->inode || !f->inode->fs_data) return -1;
    node = (fat32_node_t *)f->inode->fs_data;
    if (node->is_dir) return -1;
    if (f->offset >= node->size) return 0;
    if (count > node->size - f->offset) count = node->size - f->offset;

    while (done < count) {
        uint32_t abs_off = f->offset + done;
        uint32_t cluster_index = abs_off / node->fs->bytes_per_cluster;
        uint32_t cluster_off = abs_off % node->fs->bytes_per_cluster;
        uint32_t sector_in_cluster = cluster_off / FAT32_SECTOR_SIZE;
        uint32_t sector_off = cluster_off % FAT32_SECTOR_SIZE;
        uint32_t cluster = fat32_nth_cluster(node->fs, node->first_cluster, cluster_index);
        uint32_t lba;
        uint32_t chunk;

        if (cluster < 2) break;
        lba = fat32_cluster_lba(node->fs, cluster) + sector_in_cluster;
        if (fat32_read_sector(node->fs, lba, sector) < 0) break;
        chunk = FAT32_SECTOR_SIZE - sector_off;
        if (chunk > count - done) chunk = count - done;
        memcpy(out + done, sector + sector_off, chunk);
        done += chunk;
    }

    f->offset += done;
    return (int)done;
}

static int fat32_file_write(file_t *f, const void *buf, uint32_t count) {
    (void)f;
    (void)buf;
    (void)count;
    return -1;
}

static int fat32_truncate(inode_t *inode, uint32_t size) {
    (void)inode;
    (void)size;
    return -1;
}

static file_ops_t fat32_file_ops = {
    .open = 0,
    .close = 0,
    .read = fat32_file_read,
    .write = fat32_file_write,
    .seek = 0,
    .truncate = fat32_truncate,
    .readdir = 0,
    .poll = 0,
};

static void fat32_short_name(const uint8_t raw[11], char *out, uint32_t out_size) {
    uint32_t pos = 0;
    int base_end = 7;
    int ext_end = 10;
    int i;

    if (!out || out_size == 0) return;
    while (base_end >= 0 && raw[base_end] == ' ') base_end--;
    while (ext_end >= 8 && raw[ext_end] == ' ') ext_end--;

    for (i = 0; i <= base_end && pos + 1 < out_size; i++) {
        char c = (char)raw[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[pos++] = c;
    }
    if (ext_end >= 8 && pos + 1 < out_size) out[pos++] = '.';
    for (i = 8; i <= ext_end && pos + 1 < out_size; i++) {
        char c = (char)raw[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[pos++] = c;
    }
    out[pos] = '\0';
}

static uint32_t fat32_entry_cluster(const fat32_dirent_t *de) {
    return ((uint32_t)rd16(&de->first_cluster_hi) << 16) | rd16(&de->first_cluster_lo);
}

static fat32_node_t *fat32_alloc_node(fat32_fs_t *fs, uint32_t first_cluster, uint32_t size, uint8_t is_dir) {
    fat32_node_t *node = (fat32_node_t *)kmalloc(sizeof(fat32_node_t));
    if (!node) return 0;
    memset(node, 0, sizeof(*node));
    node->fs = fs;
    node->first_cluster = first_cluster;
    node->size = size;
    node->is_dir = is_dir;
    return node;
}

static int fat32_load_dir(fat32_fs_t *fs, dentry_t *parent, uint32_t first_cluster, uint32_t depth) {
    uint8_t sector[FAT32_SECTOR_SIZE];
    uint32_t cluster = first_cluster;
    uint32_t guard = 0;

    if (!fs || !parent || first_cluster < 2 || depth > FAT32_MAX_DEPTH) return -1;

    while (cluster >= 2 && cluster < FAT32_EOC) {
        uint32_t s;
        for (s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t lba = fat32_cluster_lba(fs, cluster) + s;
            uint32_t off;
            if (fat32_read_sector(fs, lba, sector) < 0) return -1;
            for (off = 0; off < FAT32_SECTOR_SIZE; off += sizeof(fat32_dirent_t)) {
                fat32_dirent_t *de = (fat32_dirent_t *)(sector + off);
                char name[MAX_NAME];
                uint32_t child_cluster;
                uint32_t mode;
                fat32_node_t *node;
                dentry_t *child;

                if (de->name[0] == 0x00u) return 0;
                if (de->name[0] == 0xE5u) continue;
                if ((de->attr & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) continue;
                if (de->attr & FAT32_ATTR_VOLUME_ID) continue;
                if (de->name[0] == '.') continue;

                fat32_short_name(de->name, name, sizeof(name));
                if (name[0] == '\0') continue;
                child_cluster = fat32_entry_cluster(de);
                if (de->attr & FAT32_ATTR_DIRECTORY) {
                    mode = FS_DIR | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
                    node = fat32_alloc_node(fs, child_cluster, 0, 1);
                } else {
                    mode = FS_FILE | S_IRUSR | S_IRGRP | S_IROTH;
                    node = fat32_alloc_node(fs, child_cluster, rd32(&de->file_size), 0);
                }
                if (!node) return -1;
                child = vfs_create_node_under(parent, name, mode, node->is_dir ? 0 : &fat32_file_ops, node, node->size);
                if (!child) {
                    kfree(node);
                    return -1;
                }
                child->inode->fs_type = FAT32_MAGIC;
                child->inode->uid = 0;
                child->inode->gid = 0;
                if (node->is_dir && child_cluster >= 2) {
                    if (fat32_load_dir(fs, child, child_cluster, depth + 1u) < 0) return -1;
                }
            }
        }
        if (guard++ > fs->cluster_count) return -1;
        if (fat32_read_fat_entry(fs, cluster, &cluster) < 0) return -1;
        if (cluster >= FAT32_EOC) break;
    }
    return 0;
}

static int fat32_probe(fat32_fs_t *fs, blockdev_t *dev) {
    uint8_t sector[FAT32_SECTOR_SIZE];
    fat32_bpb_t *bpb;
    uint32_t total;
    uint32_t data_sectors;

    if (!fs || !dev) return -1;
    memset(fs, 0, sizeof(*fs));
    fs->dev = dev;
    if (fat32_read_sector(fs, 0, sector) < 0) return -1;
    bpb = (fat32_bpb_t *)sector;
    if (sector[510] != 0x55u || sector[511] != 0xAAu) return -1;
    if (rd16(&bpb->bytes_per_sector) != FAT32_SECTOR_SIZE) return -1;
    if (bpb->sectors_per_cluster == 0) return -1;
    if (bpb->fat_count == 0 || rd32(&bpb->fat_size32) == 0) return -1;
    total = rd16(&bpb->total_sectors16);
    if (total == 0) total = rd32(&bpb->total_sectors32);
    if (total == 0 || total > dev->sector_count) return -1;

    fs->total_sectors = total;
    fs->fat_start_lba = rd16(&bpb->reserved_sectors);
    fs->fat_size = rd32(&bpb->fat_size32);
    fs->root_cluster = rd32(&bpb->root_cluster);
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->bytes_per_cluster = fs->sectors_per_cluster * FAT32_SECTOR_SIZE;
    fs->data_start_lba = fs->fat_start_lba + ((uint32_t)bpb->fat_count * fs->fat_size);
    if (fs->data_start_lba >= fs->total_sectors || fs->root_cluster < 2) return -1;
    data_sectors = fs->total_sectors - fs->data_start_lba;
    fs->cluster_count = data_sectors / fs->sectors_per_cluster;
    if (fs->cluster_count == 0 || fs->cluster_count > FAT32_MAX_CLUSTERS) return -1;
    return 0;
}

static inode_t *fat32_fs_lookup(fs_type_t *vfs_fs, const char *path) {
    (void)vfs_fs;
    (void)path;
    return 0;
}

static inode_t *fat32_fs_create(fs_type_t *vfs_fs, const char *name, uint32_t mode) {
    (void)vfs_fs;
    (void)name;
    (void)mode;
    return 0;
}

static int fat32_fs_mkdir(fs_type_t *vfs_fs, const char *name) {
    (void)vfs_fs;
    (void)name;
    return -1;
}

static int fat32_fs_unlink(fs_type_t *vfs_fs, const char *name) {
    (void)vfs_fs;
    (void)name;
    return -1;
}

static fat32_fs_t *fat32_alloc_mount(void) {
    uint32_t i;
    for (i = 0; i < sizeof(g_fat32_mounts) / sizeof(g_fat32_mounts[0]); i++) {
        if (!g_fat32_mount_used[i]) {
            g_fat32_mount_used[i] = 1;
            memset(&g_fat32_mounts[i], 0, sizeof(g_fat32_mounts[i]));
            return &g_fat32_mounts[i];
        }
    }
    return 0;
}

static void fat32_release_mount(fat32_fs_t *fs) {
    uint32_t i;
    if (!fs) return;
    for (i = 0; i < sizeof(g_fat32_mounts) / sizeof(g_fat32_mounts[0]); i++) {
        if (&g_fat32_mounts[i] == fs) {
            memset(fs, 0, sizeof(*fs));
            g_fat32_mount_used[i] = 0;
            return;
        }
    }
}

int fat32_mount(const char *path, const char *dev_name) {
    blockdev_t *dev;
    fat32_fs_t *fs;
    dentry_t *mp;
    fat32_node_t *root_node;

    if (!path || !dev_name) return -1;
    serial_write("[FAT32] mount begin\n");

    dev = blockdev_find(dev_name);
    if (!dev) {
        serial_write("[FAT32] block device not found\n");
        return -1;
    }
    if (dev->sector_size != FAT32_SECTOR_SIZE) {
        serial_write("[FAT32] only 512-byte sector devices are supported\n");
        return -1;
    }

    mp = vfs_path_lookup(path);
    if (!mp) {
        if (vfs_mkdir(path, 0755) < 0) {
            serial_write("[FAT32] create mount dir failed\n");
            return -1;
        }
        mp = vfs_path_lookup(path);
    }
    if (!mp) return -1;

    fs = fat32_alloc_mount();
    if (!fs) return -1;
    if (blockdev_open(dev) < 0) {
        fat32_release_mount(fs);
        return -1;
    }
    if (fat32_probe(fs, dev) < 0) {
        serial_write("[FAT32] invalid FAT32 volume\n");
        blockdev_close(dev);
        fat32_release_mount(fs);
        return -1;
    }

    strcpy(fs->vfs_fs.name, "fat32");
    fs->vfs_fs.magic = FAT32_MAGIC;
    fs->vfs_fs.lookup = fat32_fs_lookup;
    fs->vfs_fs.create = fat32_fs_create;
    fs->vfs_fs.mkdir = fat32_fs_mkdir;
    fs->vfs_fs.unlink = fat32_fs_unlink;
    fs->vfs_fs.data = fs;

    if (vfs_mount(path, &fs->vfs_fs) < 0) {
        blockdev_close(dev);
        fat32_release_mount(fs);
        return -1;
    }
    mp = vfs_path_lookup(path);
    if (!mp || !mp->mount) {
        blockdev_close(dev);
        fat32_release_mount(fs);
        return -1;
    }
    fs->mount_root = mp->mount;
    root_node = fat32_alloc_node(fs, fs->root_cluster, 0, 1);
    if (!root_node) {
        blockdev_close(dev);
        fat32_release_mount(fs);
        return -1;
    }
    fs->mount_root->inode->fs_data = root_node;
    fs->mount_root->inode->fs_type = FAT32_MAGIC;
    fs->mount_root->inode->mode = FS_DIR | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    fs->mount_root->inode->uid = 0;
    fs->mount_root->inode->gid = 0;

    if (fat32_load_dir(fs, fs->mount_root, fs->root_cluster, 0) < 0) {
        blockdev_close(dev);
        fat32_release_mount(fs);
        return -1;
    }
    serial_write("[FAT32] mounted ");
    serial_write(dev_name);
    serial_write(" at ");
    serial_write(path);
    serial_write("\n");
    return 0;
}

static void fat32_make_short_name(uint8_t out[11], const char *base, const char *ext) {
    uint32_t i;
    for (i = 0; i < 11; i++) out[i] = ' ';
    for (i = 0; i < 8 && base && base[i]; i++) out[i] = (uint8_t)base[i];
    for (i = 0; i < 3 && ext && ext[i]; i++) out[8 + i] = (uint8_t)ext[i];
}

int fat32_format_demo(const char *dev_name) {
    blockdev_t *dev;
    uint8_t sector[FAT32_SECTOR_SIZE];
    fat32_bpb_t *bpb;
    fat32_dirent_t *de;
    const char *msg = "Hello from FAT32 on openos.\n";
    uint32_t i;

    if (!dev_name) return -1;
    dev = blockdev_find(dev_name);
    if (!dev || dev->sector_count < 128) return -1;

    memset(sector, 0, sizeof(sector));
    bpb = (fat32_bpb_t *)sector;
    bpb->jump[0] = 0xEBu;
    bpb->jump[1] = 0x58u;
    bpb->jump[2] = 0x90u;
    memcpy(bpb->oem, "OPENOS  ", 8);
    wr16(&bpb->bytes_per_sector, FAT32_SECTOR_SIZE);
    bpb->sectors_per_cluster = 1;
    wr16(&bpb->reserved_sectors, 32);
    bpb->fat_count = 1;
    bpb->media = 0xF8u;
    wr32(&bpb->total_sectors32, dev->sector_count);
    wr32(&bpb->fat_size32, 8);
    wr32(&bpb->root_cluster, 2);
    wr16(&bpb->fs_info, 1);
    wr16(&bpb->backup_boot_sector, 6);
    bpb->drive_number = 0x80u;
    bpb->boot_signature = 0x29u;
    wr32(&bpb->volume_id, 0x1234ABCDu);
    memcpy(bpb->volume_label, "OPENOS FAT ", 11);
    memcpy(bpb->fs_type, "FAT32   ", 8);
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
    if (fat32_write_sector(dev, 0, sector) < 0) return -1;

    memset(sector, 0, sizeof(sector));
    wr32(&sector[0], 0x0FFFFFF8u);
    wr32(&sector[4], 0x0FFFFFFFu);
    wr32(&sector[8], 0x0FFFFFFFu);
    wr32(&sector[12], 0x0FFFFFFFu);
    if (fat32_write_sector(dev, 32, sector) < 0) return -1;
    memset(sector, 0, sizeof(sector));
    for (i = 33; i < 40; i++) {
        if (fat32_write_sector(dev, i, sector) < 0) return -1;
    }

    memset(sector, 0, sizeof(sector));
    de = (fat32_dirent_t *)sector;
    fat32_make_short_name(de->name, "README", "TXT");
    de->attr = FAT32_ATTR_ARCHIVE;
    wr16(&de->first_cluster_lo, 3);
    wr32(&de->file_size, strlen(msg));
    if (fat32_write_sector(dev, 40, sector) < 0) return -1;

    memset(sector, 0, sizeof(sector));
    memcpy(sector, msg, strlen(msg));
    if (fat32_write_sector(dev, 41, sector) < 0) return -1;

    serial_write("[FAT32] demo formatted ");
    serial_write(dev_name);
    serial_write("\n");
    return 0;
}
