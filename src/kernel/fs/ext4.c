/* ============================================================
 * openos - EXT4 filesystem driver (read/write subset)
 * ============================================================ */

#include "../include/ext4.h"
#include "../include/blockdev.h"
#include "../include/string.h"
#include "../include/serial.h"
#include "vfs.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define EXT4_SUPERBLOCK_OFFSET       1024u
#define EXT4_SUPERBLOCK_SIZE         1024u
#define EXT4_SUPER_MAGIC             0xEF53u
#define EXT4_GOOD_OLD_INODE_SIZE     128u
#define EXT4_ROOT_INO                2u
#define EXT4_FS_MAGIC                 0xEF530004u

#define EXT4_N_BLOCKS                15u
#define EXT4_EXTENTS_FL              0x00080000u
#define EXT4_INDEX_FL                0x00001000u

#define EXT4_S_IFMT                  0xF000u
#define EXT4_S_IFREG                 0x8000u
#define EXT4_S_IFDIR                 0x4000u

#define EXT4_NAME_LEN                255u
#define EXT4_MAX_MOUNTS              2u
#define EXT4_MAX_NODES               96u
#define EXT4_MAX_BLOCK_SIZE          4096u
#define EXT4_MAX_EXTENT_DEPTH        5u
#define EXT4_MAX_SCAN_DEPTH          8u

#define EXT4_FT_REG_FILE             1u
#define EXT4_FT_DIR                  2u

#define EXT4_EXT_MAGIC               0xF30Au

#define EXT4_FEATURE_COMPAT_DIR_INDEX    0x0020u
#define EXT4_FEATURE_INCOMPAT_FILETYPE   0x0002u
#define EXT4_FEATURE_INCOMPAT_EXTENTS    0x0040u
#define EXT4_FEATURE_INCOMPAT_64BIT      0x0080u

#pragma pack(push, 1)
typedef struct ext4_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
} ext4_superblock_t;

typedef struct ext4_group_desc32 {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
} ext4_group_desc32_t;

typedef struct ext4_inode_disk {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT4_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
} ext4_inode_disk_t;

typedef struct ext4_extent_header {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} ext4_extent_header_t;

typedef struct ext4_extent_idx {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} ext4_extent_idx_t;

typedef struct ext4_extent {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} ext4_extent_t;

#pragma pack(pop)

typedef struct ext4_mount ext4_mount_t;

typedef struct ext4_node {
    int used;
    ext4_mount_t *mount;
    uint32_t inode_no;
    uint16_t mode;
    uint32_t flags;
    uint64_t size;
    uint32_t block[EXT4_N_BLOCKS];
} ext4_node_t;

struct ext4_mount {
    int used;
    blockdev_t *dev;
    ext4_superblock_t sb;
    uint32_t block_size;
    uint32_t sectors_per_block;
    uint32_t inode_size;
    uint32_t group_desc_size;
    uint32_t groups_count;
    ext4_node_t nodes[EXT4_MAX_NODES];
};

static ext4_mount_t ext4_mounts[EXT4_MAX_MOUNTS];

static uint16_t ext4_rd16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}

static uint32_t ext4_rd32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint64_t ext4_make64(uint32_t lo, uint32_t hi) {
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t ext4_div_u64_u32(uint64_t n, uint32_t d, uint32_t *rem) {
    uint64_t q = 0;
    uint64_t r = 0;
    int i;

    if (d == 0) {
        if (rem) *rem = 0;
        return 0;
    }

    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (1ULL << i);
        }
    }

    if (rem) *rem = (uint32_t)r;
    return q;
}

static ext4_mount_t *ext4_alloc_mount(void) {
    uint32_t i;
    for (i = 0; i < EXT4_MAX_MOUNTS; i++) {
        if (!ext4_mounts[i].used) {
            memset(&ext4_mounts[i], 0, sizeof(ext4_mount_t));
            ext4_mounts[i].used = 1;
            return &ext4_mounts[i];
        }
    }
    return NULL;
}

static ext4_node_t *ext4_alloc_node(ext4_mount_t *m) {
    uint32_t i;
    if (!m) return NULL;
    for (i = 0; i < EXT4_MAX_NODES; i++) {
        if (!m->nodes[i].used) {
            memset(&m->nodes[i], 0, sizeof(ext4_node_t));
            m->nodes[i].used = 1;
            m->nodes[i].mount = m;
            return &m->nodes[i];
        }
    }
    return NULL;
}

static int ext4_read_bytes(ext4_mount_t *m, uint64_t offset, uint32_t size, void *buf) {
    uint8_t sector[512];
    uint8_t *out = (uint8_t *)buf;
    uint32_t done = 0;

    if (!m || !m->dev || !buf) return -1;
    if (m->dev->sector_size != 512) return -1;

    while (done < size) {
        uint64_t abs = offset + done;
        uint32_t lba = (uint32_t)(abs >> 9);
        uint32_t off = (uint32_t)(abs & 511u);
        uint32_t chunk = 512u - off;
        if (chunk > size - done) chunk = size - done;
        if (blockdev_read_blocks(m->dev, lba, 1, sector) != 1) return -1;
        memcpy(out + done, sector + off, chunk);
        done += chunk;
    }
    return 0;
}

static int ext4_read_block(ext4_mount_t *m, uint64_t block_no, void *buf) {
    if (!m || !buf) return -1;
    if (m->block_size > EXT4_MAX_BLOCK_SIZE) return -1;
    if (block_no > 0xFFFFFFFFu) return -1;
    if (blockdev_read_blocks(m->dev, (uint32_t)block_no * m->sectors_per_block,
                             m->sectors_per_block, buf) != (int)m->sectors_per_block) return -1;
    return 0;
}

static int ext4_write_block(ext4_mount_t *m, uint64_t block_no, const void *buf) {
    if (!m || !m->dev || !buf) return -1;
    if (!m->dev->ops || !m->dev->ops->write_blocks) return -1;
    if (m->block_size > EXT4_MAX_BLOCK_SIZE) return -1;
    if (block_no > 0xFFFFFFFFu) return -1;
    if (blockdev_write_blocks(m->dev, (uint32_t)block_no * m->sectors_per_block,
                              m->sectors_per_block, buf) != (int)m->sectors_per_block) return -1;
    return 0;
}

static uint64_t ext4_blocks_count(ext4_mount_t *m) {
    return (uint64_t)m->sb.s_blocks_count_lo;
}

static int ext4_parse_super(ext4_mount_t *m) {
    uint8_t buf[EXT4_SUPERBLOCK_SIZE];
    uint64_t blocks;

    if (!m) return -1;
    if (ext4_read_bytes(m, EXT4_SUPERBLOCK_OFFSET, EXT4_SUPERBLOCK_SIZE, buf) < 0) return -1;
    memcpy(&m->sb, buf, sizeof(ext4_superblock_t));

    if (m->sb.s_magic != EXT4_SUPER_MAGIC) return -1;
    if (m->sb.s_log_block_size > 2) return -1;
    if (m->sb.s_blocks_per_group == 0 || m->sb.s_inodes_per_group == 0) return -1;
    if (m->sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) {
        serial_write("[EXT4] 64-bit block groups are not supported yet\n");
        return -1;
    }

    m->block_size = 1024u << m->sb.s_log_block_size;
    if (m->block_size < 1024u || m->block_size > EXT4_MAX_BLOCK_SIZE) return -1;
    if ((m->block_size % 512u) != 0) return -1;
    m->sectors_per_block = m->block_size / 512u;
    m->inode_size = m->sb.s_inode_size ? m->sb.s_inode_size : EXT4_GOOD_OLD_INODE_SIZE;
    if (m->inode_size < EXT4_GOOD_OLD_INODE_SIZE || m->inode_size > m->block_size) return -1;
    m->group_desc_size = sizeof(ext4_group_desc32_t);

    blocks = ext4_blocks_count(m);
    m->groups_count = (uint32_t)ext4_div_u64_u32(blocks + m->sb.s_blocks_per_group - 1,
                                                 m->sb.s_blocks_per_group,
                                                 NULL);
    if (m->groups_count == 0) return -1;
    return 0;
}

static uint64_t ext4_group_desc_table_block(ext4_mount_t *m) {
    return (m->block_size == 1024u) ? 2u : 1u;
}

static int ext4_read_group_desc(ext4_mount_t *m, uint32_t group, ext4_group_desc32_t *gd) {
    uint8_t block[EXT4_MAX_BLOCK_SIZE];
    uint64_t table_block;
    uint32_t descs_per_block;
    uint32_t block_index;
    uint32_t offset;

    if (!m || !gd || group >= m->groups_count) return -1;
    table_block = ext4_group_desc_table_block(m);
    descs_per_block = m->block_size / m->group_desc_size;
    if (descs_per_block == 0) return -1;
    block_index = group / descs_per_block;
    offset = (group % descs_per_block) * m->group_desc_size;

    if (ext4_read_block(m, table_block + block_index, block) < 0) return -1;
    memcpy(gd, block + offset, sizeof(ext4_group_desc32_t));
    return 0;
}

static int ext4_inode_location(ext4_mount_t *m, uint32_t inode_no, uint64_t *block_no, uint32_t *block_off) {
    ext4_group_desc32_t gd;
    uint32_t index;
    uint32_t group;
    uint32_t index_in_group;
    uint64_t inode_table;
    uint64_t byte_offset;

    if (!m || !block_no || !block_off || inode_no == 0) return -1;
    index = inode_no - 1;
    group = index / m->sb.s_inodes_per_group;
    index_in_group = index % m->sb.s_inodes_per_group;
    if (ext4_read_group_desc(m, group, &gd) < 0) return -1;

    inode_table = gd.bg_inode_table_lo;
    byte_offset = (uint64_t)index_in_group * m->inode_size;
    *block_no = inode_table + ext4_div_u64_u32(byte_offset, m->block_size, block_off);
    if (*block_off + sizeof(ext4_inode_disk_t) > m->block_size) return -1;
    return 0;
}

static int ext4_read_inode(ext4_mount_t *m, uint32_t inode_no, ext4_inode_disk_t *inode) {
    uint8_t block[EXT4_MAX_BLOCK_SIZE];
    uint64_t block_no;
    uint32_t block_off;

    if (!m || !inode || inode_no == 0) return -1;
    if (ext4_inode_location(m, inode_no, &block_no, &block_off) < 0) return -1;
    if (ext4_read_block(m, block_no, block) < 0) return -1;
    memcpy(inode, block + block_off, sizeof(ext4_inode_disk_t));
    return 0;
}

static int ext4_write_inode_size(ext4_node_t *node, uint64_t size) {
    uint8_t block[EXT4_MAX_BLOCK_SIZE];
    uint8_t *ino;
    uint64_t block_no;
    uint32_t block_off;
    uint32_t sectors;

    if (!node || !node->mount || size > 0xFFFFFFFFu) return -1;
    if (ext4_inode_location(node->mount, node->inode_no, &block_no, &block_off) < 0) return -1;
    if (ext4_read_block(node->mount, block_no, block) < 0) return -1;

    ino = block + block_off;
    sectors = (uint32_t)((size + 511u) >> 9);
    ino[4] = (uint8_t)((uint32_t)size & 0xFFu);
    ino[5] = (uint8_t)(((uint32_t)size >> 8) & 0xFFu);
    ino[6] = (uint8_t)(((uint32_t)size >> 16) & 0xFFu);
    ino[7] = (uint8_t)(((uint32_t)size >> 24) & 0xFFu);
    ino[28] = (uint8_t)(sectors & 0xFFu);
    ino[29] = (uint8_t)((sectors >> 8) & 0xFFu);
    ino[30] = (uint8_t)((sectors >> 16) & 0xFFu);
    ino[31] = (uint8_t)((sectors >> 24) & 0xFFu);
    ino[108] = 0;
    ino[109] = 0;
    ino[110] = 0;
    ino[111] = 0;

    if (ext4_write_block(node->mount, block_no, block) < 0) return -1;
    node->size = size;
    return 0;
}

static uint64_t ext4_inode_size(const ext4_inode_disk_t *inode) {
    if (!inode) return 0;
    if ((inode->i_mode & EXT4_S_IFMT) == EXT4_S_IFREG) {
        return ext4_make64(inode->i_size_lo, inode->i_size_high);
    }
    return inode->i_size_lo;
}

static int ext4_is_dir(const ext4_inode_disk_t *inode) {
    return inode && ((inode->i_mode & EXT4_S_IFMT) == EXT4_S_IFDIR);
}

static int ext4_is_file(const ext4_inode_disk_t *inode) {
    return inode && ((inode->i_mode & EXT4_S_IFMT) == EXT4_S_IFREG);
}

static uint64_t ext4_extent_start(const ext4_extent_t *ex) {
    return ((uint64_t)ex->ee_start_hi << 32) | ex->ee_start_lo;
}

static uint16_t ext4_extent_len(const ext4_extent_t *ex) {
    uint16_t len = ex->ee_len;
    if (len & 0x8000u) len = (uint16_t)(len & 0x7FFFu);
    return len;
}

static int ext4_find_extent_block(ext4_mount_t *m, const uint8_t *tree, uint32_t logical, uint32_t depth, uint64_t *phys) {
    const ext4_extent_header_t *eh = (const ext4_extent_header_t *)tree;
    uint16_t entries;
    uint16_t i;

    if (!m || !tree || !phys) return -1;
    if (eh->eh_magic != EXT4_EXT_MAGIC) return -1;
    if (depth > EXT4_MAX_EXTENT_DEPTH) return -1;

    entries = eh->eh_entries;
    if (entries == 0) return -1;

    if (eh->eh_depth == 0) {
        const ext4_extent_t *ext = (const ext4_extent_t *)(tree + sizeof(ext4_extent_header_t));
        for (i = 0; i < entries; i++) {
            uint32_t first = ext[i].ee_block;
            uint32_t len = ext4_extent_len(&ext[i]);
            if (logical >= first && logical < first + len) {
                *phys = ext4_extent_start(&ext[i]) + (logical - first);
                return 0;
            }
        }
        return -1;
    } else {
        const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(tree + sizeof(ext4_extent_header_t));
        uint16_t best = 0;
        uint8_t block[EXT4_MAX_BLOCK_SIZE];
        uint64_t child;

        for (i = 0; i < entries; i++) {
            if (idx[i].ei_block <= logical) best = i;
            else break;
        }
        child = ((uint64_t)idx[best].ei_leaf_hi << 32) | idx[best].ei_leaf_lo;
        if (ext4_read_block(m, child, block) < 0) return -1;
        return ext4_find_extent_block(m, block, logical, depth + 1, phys);
    }
}

static int ext4_inode_logical_to_phys(ext4_node_t *node, uint32_t logical, uint64_t *phys) {
    uint32_t i;

    if (!node || !phys) return -1;
    if (node->flags & EXT4_EXTENTS_FL) {
        return ext4_find_extent_block(node->mount, (const uint8_t *)node->block, logical, 0, phys);
    }

    /* 兼容老式 direct block，暂不实现 indirect block。 */
    if (logical < 12u) {
        if (node->block[logical] == 0) return -1;
        *phys = node->block[logical];
        return 0;
    }

    for (i = 12u; i < EXT4_N_BLOCKS; i++) {
        if (node->block[i] != 0) break;
    }
    return -1;
}

static int ext4_file_read(file_t *file, void *buf, uint32_t size) {
    ext4_node_t *node;
    uint8_t block[EXT4_MAX_BLOCK_SIZE];
    uint8_t *out = (uint8_t *)buf;
    uint32_t done = 0;
    uint32_t block_size;
    uint32_t offset;

    if (!file || !file->inode || !buf) return -1;
    node = (ext4_node_t *)file->inode->fs_data;
    if (!node || !node->mount) return -1;
    offset = file->offset;
    if (offset >= node->size) return 0;
    if (size > node->size - offset) size = (uint32_t)(node->size - offset);

    block_size = node->mount->block_size;
    while (done < size) {
        uint32_t logical = (offset + done) / block_size;
        uint32_t in_block = (offset + done) % block_size;
        uint32_t chunk = block_size - in_block;
        uint64_t phys;

        if (chunk > size - done) chunk = size - done;
        if (ext4_inode_logical_to_phys(node, logical, &phys) < 0) {
            memset(out + done, 0, chunk);
        } else {
            if (ext4_read_block(node->mount, phys, block) < 0) return -1;
            memcpy(out + done, block + in_block, chunk);
        }
        done += chunk;
    }
    file->offset += done;
    return (int)done;
}

static int ext4_file_write(file_t *file, const void *buf, uint32_t size) {
    ext4_node_t *node;
    const uint8_t *in = (const uint8_t *)buf;
    uint8_t block[EXT4_MAX_BLOCK_SIZE];
    uint32_t done = 0;
    uint32_t block_size;
    uint32_t offset;
    uint64_t end_pos;

    if (!file || !file->inode || !buf) return -1;
    node = (ext4_node_t *)file->inode->fs_data;
    if (!node || !node->mount) return -1;
    if ((node->mode & EXT4_S_IFMT) != EXT4_S_IFREG) return -1;
    if (size == 0) return 0;

    block_size = node->mount->block_size;
    offset = file->offset;
    end_pos = (uint64_t)offset + size;
    if (end_pos > 0xFFFFFFFFu) return -1;

    while (done < size) {
        uint32_t logical = (offset + done) / block_size;
        uint32_t in_block = (offset + done) % block_size;
        uint32_t chunk = block_size - in_block;
        uint64_t phys;

        if (chunk > size - done) chunk = size - done;
        if (ext4_inode_logical_to_phys(node, logical, &phys) < 0) return done ? (int)done : -1;
        if (ext4_read_block(node->mount, phys, block) < 0) return -1;
        memcpy(block + in_block, in + done, chunk);
        if (ext4_write_block(node->mount, phys, block) < 0) return -1;
        done += chunk;
    }

    file->offset += done;
    if ((uint64_t)file->offset > node->size) {
        if (ext4_write_inode_size(node, file->offset) < 0) return -1;
        file->inode->size = file->offset;
    }
    return (int)done;
}

static int ext4_file_seek(file_t *file, int offset, int whence) {
    uint32_t base;
    uint32_t next;
    if (!file || !file->inode) return -1;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = file->offset;
    else if (whence == SEEK_END) base = file->inode->size;
    else return -1;
    if (offset < 0 && (uint32_t)(-offset) > base) return -1;
    next = offset < 0 ? base - (uint32_t)(-offset) : base + (uint32_t)offset;
    if (next > file->inode->size) return -1;
    file->offset = next;
    return (int)next;
}

static int ext4_zero_range(ext4_node_t *node, uint32_t from, uint32_t to) {
    uint8_t block[EXT4_MAX_BLOCK_SIZE];
    uint32_t block_size;

    if (!node || !node->mount || from > to) return -1;
    block_size = node->mount->block_size;
    while (from < to) {
        uint32_t logical = from / block_size;
        uint32_t in_block = from % block_size;
        uint32_t chunk = block_size - in_block;
        uint64_t phys;

        if (chunk > to - from) chunk = to - from;
        if (ext4_inode_logical_to_phys(node, logical, &phys) < 0) return -1;
        if (ext4_read_block(node->mount, phys, block) < 0) return -1;
        memset(block + in_block, 0, chunk);
        if (ext4_write_block(node->mount, phys, block) < 0) return -1;
        from += chunk;
    }
    return 0;
}

static int ext4_file_truncate(inode_t *inode, uint32_t size) {
    ext4_node_t *node;
    uint32_t block_size;
    uint32_t old_blocks;
    uint32_t new_blocks;

    if (!inode) return -1;
    node = (ext4_node_t *)inode->fs_data;
    if (!node || !node->mount) return -1;
    if ((node->mode & EXT4_S_IFMT) != EXT4_S_IFREG) return -1;

    block_size = node->mount->block_size;
    old_blocks = (uint32_t)ext4_div_u64_u32(node->size + block_size - 1u, block_size, NULL);
    new_blocks = (size + block_size - 1u) / block_size;
    if (new_blocks > old_blocks) return -1;
    if (size > node->size && ext4_zero_range(node, (uint32_t)node->size, size) < 0) return -1;

    if (ext4_write_inode_size(node, size) < 0) return -1;
    inode->size = size;
    return 0;
}

static file_ops_t ext4_file_ops = {
    0,
    0,
    ext4_file_read,
    ext4_file_write,
    ext4_file_seek,
    ext4_file_truncate,
    0,
    0
};

static ext4_node_t *ext4_node_from_inode(ext4_mount_t *m, uint32_t inode_no, const ext4_inode_disk_t *disk) {
    ext4_node_t *node = ext4_alloc_node(m);
    if (!node || !disk) return NULL;
    node->inode_no = inode_no;
    node->mode = disk->i_mode;
    node->flags = disk->i_flags;
    node->size = ext4_inode_size(disk);
    memcpy(node->block, disk->i_block, sizeof(node->block));
    return node;
}

static int ext4_copy_name(char *dst, const uint8_t *src, uint32_t len) {
    uint32_t i;
    if (!dst || !src) return -1;
    if (len == 0 || len >= MAX_NAME) return -1;
    for (i = 0; i < len && i < MAX_NAME - 1; i++) {
        char c = (char)src[i];
        dst[i] = c;
    }
    dst[i] = '\0';
    return 0;
}

static int ext4_scan_directory(ext4_mount_t *m, dentry_t *parent, ext4_node_t *dir_node, int depth) {
    uint8_t block[EXT4_MAX_BLOCK_SIZE];
    uint64_t pos = 0;

    if (!m || !parent || !dir_node) return -1;
    if ((uint32_t)depth > EXT4_MAX_SCAN_DEPTH) return 0;

    while (pos < dir_node->size) {
        uint32_t logical = (uint32_t)ext4_div_u64_u32(pos, m->block_size, NULL);
        uint64_t phys;
        uint32_t off = 0;

        if (ext4_inode_logical_to_phys(dir_node, logical, &phys) < 0) {
            pos += m->block_size;
            continue;
        }
        if (ext4_read_block(m, phys, block) < 0) return -1;

        while (off + 8u <= m->block_size && pos + off < dir_node->size) {
            uint8_t *de = block + off;
            uint32_t ino = ext4_rd32(de + 0);
            uint16_t rec_len = ext4_rd16(de + 4);
            uint8_t name_len = de[6];
            uint8_t file_type = de[7];
            char name[MAX_NAME];
            ext4_inode_disk_t child_inode;
            ext4_node_t *child_node;
            dentry_t *child;

            if (rec_len < 8u || off + rec_len > m->block_size) break;
            if (ino != 0 && name_len > 0 && 8u + name_len <= rec_len) {
                if (!(name_len == 1 && de[8] == '.') && !(name_len == 2 && de[8] == '.' && de[9] == '.')) {
                    if (ext4_copy_name(name, de + 8, name_len) == 0 && ext4_read_inode(m, ino, &child_inode) == 0) {
                        child_node = ext4_node_from_inode(m, ino, &child_inode);
                        if (!child_node) return -1;

                        if (file_type == EXT4_FT_DIR || ext4_is_dir(&child_inode)) {
                            child = vfs_create_node_under(parent, name, FS_DIR | 0755, 0, child_node, (uint32_t)child_node->size);
                            if (child && child->inode) child->inode->fs_type = EXT4_FS_MAGIC;
                            if (!child) return -1;
                            ext4_scan_directory(m, child, child_node, depth + 1);
                        } else if (file_type == EXT4_FT_REG_FILE || ext4_is_file(&child_inode)) {
                            child = vfs_create_node_under(parent, name, FS_FILE | 0644, &ext4_file_ops, child_node, (uint32_t)child_node->size);
                            if (child && child->inode) child->inode->fs_type = EXT4_FS_MAGIC;
                            if (!child) return -1;
                        }
                    }
                }
            }
            off += rec_len;
        }
        pos += m->block_size;
    }
    return 0;
}

int ext4_mount(const char *dev_name, const char *mount_path) {
    blockdev_t *dev;
    ext4_mount_t *m;
    dentry_t *mount_dentry;
    ext4_inode_disk_t root_inode;
    ext4_node_t *root_node;

    if (!dev_name || !mount_path) return -1;
    serial_write("[EXT4] mount begin\n");

    dev = blockdev_find(dev_name);
    if (!dev) {
        serial_write("[EXT4] block device not found\n");
        return -1;
    }
    if (dev->sector_size != 512) {
        serial_write("[EXT4] only 512-byte sector devices are supported\n");
        return -1;
    }

    mount_dentry = vfs_path_lookup(mount_path);
    if (!mount_dentry) {
        if (vfs_mkdir(mount_path, 0755) < 0) {
            serial_write("[EXT4] create mount dir failed\n");
            return -1;
        }
        mount_dentry = vfs_path_lookup(mount_path);
    }
    if (!mount_dentry) return -1;

    m = ext4_alloc_mount();
    if (!m) return -1;
    m->dev = dev;

    if (blockdev_open(dev) < 0) {
        m->used = 0;
        return -1;
    }
    if (ext4_parse_super(m) < 0) {
        serial_write("[EXT4] invalid EXT4 volume\n");
        blockdev_close(dev);
        m->used = 0;
        return -1;
    }
    if (ext4_read_inode(m, EXT4_ROOT_INO, &root_inode) < 0 || !ext4_is_dir(&root_inode)) {
        serial_write("[EXT4] root inode invalid\n");
        blockdev_close(dev);
        m->used = 0;
        return -1;
    }

    root_node = ext4_node_from_inode(m, EXT4_ROOT_INO, &root_inode);
    if (!root_node) return -1;
    mount_dentry->inode->fs_data = root_node;
    mount_dentry->inode->fs_type = EXT4_FS_MAGIC;

    if (ext4_scan_directory(m, mount_dentry, root_node, 0) < 0) {
        serial_write("[EXT4] scan root failed\n");
        return -1;
    }

    serial_write("[OK] EXT4 mounted\n");
    return 0;
}

#define EXT4_MKFS_BLOCK_SIZE        1024u
#define EXT4_MKFS_SECTORS_PER_BLOCK (EXT4_MKFS_BLOCK_SIZE / BLOCKDEV_SECTOR_SIZE_DEFAULT)
#define EXT4_MKFS_INODE_SIZE        128u
#define EXT4_MKFS_INODES            128u
#define EXT4_MKFS_ROOT_BLOCK        11u
#define EXT4_MKFS_RW_INO             12u
#define EXT4_MKFS_RW_BLOCK           12u
#define EXT4_MKFS_RW_FILE_NAME       "rw.txt"
#define EXT4_MKFS_RW_FILE_DATA       "openos ext4 writable file\n"

static void ext4_set_bit(uint8_t *bitmap, uint32_t bit) {
    bitmap[bit >> 3] = (uint8_t)(bitmap[bit >> 3] | (uint8_t)(1u << (bit & 7u)));
}

static void ext4_w16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void ext4_w32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int ext4_mkfs_write_block(blockdev_t *dev, uint32_t block, const uint8_t *buf) {
    return blockdev_write_blocks(dev, block * EXT4_MKFS_SECTORS_PER_BLOCK,
                                 EXT4_MKFS_SECTORS_PER_BLOCK, buf);
}

static void ext4_mkfs_write_dirent(uint8_t *p, uint32_t inode, uint16_t rec_len,
                                   uint8_t name_len, uint8_t file_type, const char *name) {
    uint8_t i;
    ext4_w32(p + 0, inode);
    ext4_w16(p + 4, rec_len);
    p[6] = name_len;
    p[7] = file_type;
    for (i = 0; i < name_len; i++) p[8u + i] = (uint8_t)name[i];
}

static void ext4_mkfs_write_inode(uint8_t *table, uint32_t inode_no, uint16_t mode,
                                  uint32_t size, uint32_t block_no, uint32_t flags) {
    uint8_t *ino = table + ((inode_no - 1u) * EXT4_MKFS_INODE_SIZE);
    uint32_t sectors = (size + 511u) >> 9;

    ext4_w16(ino + 0, mode);
    ext4_w32(ino + 4, size);
    ext4_w16(ino + 26, (mode & EXT4_S_IFDIR) ? 2u : 1u);
    ext4_w32(ino + 28, sectors);
    ext4_w32(ino + 32, flags);

    if (flags & EXT4_EXTENTS_FL) {
        uint8_t *eh = ino + 40;
        ext4_w16(eh + 0, EXT4_EXT_MAGIC);
        ext4_w16(eh + 2, 1);
        ext4_w16(eh + 4, 4);
        ext4_w16(eh + 6, 0);
        ext4_w32(eh + 12, 0);
        ext4_w16(eh + 16, 1);
        ext4_w16(eh + 18, 0);
        ext4_w32(eh + 20, block_no);
        ext4_w16(eh + 24, 0);
    } else if (block_no != 0) {
        ext4_w32(ino + 40, block_no);
    }
}

int ext4_format_test_volume(const char *dev_name) {
    blockdev_t *dev;
    uint32_t total_blocks;
    uint32_t used_blocks;
    uint32_t free_blocks;
    static uint8_t block[EXT4_MKFS_BLOCK_SIZE];
    static uint8_t inode_table[EXT4_MKFS_BLOCK_SIZE * 4u];
    uint32_t i;

    if (!dev_name) dev_name = "ram0";
    dev = blockdev_find(dev_name);
    if (!dev || !dev->ops || !dev->ops->write_blocks) {
        serial_write("[EXT4] block device is not writable\n");
        return -1;
    }
    if (dev->sector_size != BLOCKDEV_SECTOR_SIZE_DEFAULT || dev->sector_count < 64u) {
        serial_write("[EXT4] unsupported block device geometry\n");
        return -1;
    }
    if (blockdev_open(dev) < 0) return -1;

    total_blocks = dev->sector_count / EXT4_MKFS_SECTORS_PER_BLOCK;
    if (total_blocks > 8192u) total_blocks = 8192u;
    if (total_blocks < 32u) {
        blockdev_close(dev);
        return -1;
    }
    used_blocks = EXT4_MKFS_RW_BLOCK + 1u;
    free_blocks = total_blocks - used_blocks;

    memset(block, 0, sizeof(block));
    for (i = 0; i < total_blocks; i++) {
        if (ext4_mkfs_write_block(dev, i, block) < 0) {
            blockdev_close(dev);
            return -1;
        }
    }

    /* Superblock is located at byte offset 1024, i.e. block 1 for 1KiB block size. */
    memset(block, 0, sizeof(block));
    ext4_w32(block + 0, EXT4_MKFS_INODES);
    ext4_w32(block + 4, total_blocks);
    ext4_w32(block + 8, 0);
    ext4_w32(block + 12, free_blocks);
    ext4_w32(block + 16, EXT4_MKFS_INODES - 12u);
    ext4_w32(block + 20, 1);
    ext4_w32(block + 24, 0);
    ext4_w32(block + 32, total_blocks);
    ext4_w32(block + 40, EXT4_MKFS_INODES);
    ext4_w16(block + 56, EXT4_SUPER_MAGIC);
    ext4_w16(block + 58, 1);
    ext4_w16(block + 60, 1);
    ext4_w32(block + 84, 11);
    ext4_w16(block + 88, EXT4_MKFS_INODE_SIZE);
    ext4_w32(block + 92, 0);
    ext4_w32(block + 96, EXT4_FEATURE_COMPAT_DIR_INDEX);
    ext4_w32(block + 100, EXT4_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_FILETYPE);
    ext4_w32(block + 104, 0);
    if (ext4_mkfs_write_block(dev, 1, block) < 0) {
        blockdev_close(dev);
        return -1;
    }

    memset(block, 0, sizeof(block));
    ext4_w32(block + 0, 3);
    ext4_w32(block + 4, 4);
    ext4_w32(block + 8, 5);
    ext4_w16(block + 12, (uint16_t)free_blocks);
    ext4_w16(block + 14, (uint16_t)(EXT4_MKFS_INODES - 12u));
    ext4_w16(block + 16, 2);
    if (ext4_mkfs_write_block(dev, 2, block) < 0) {
        blockdev_close(dev);
        return -1;
    }

    memset(block, 0, sizeof(block));
    for (i = 0; i < used_blocks; i++) ext4_set_bit(block, i);
    for (i = total_blocks; i < 8192u; i++) ext4_set_bit(block, i);
    if (ext4_mkfs_write_block(dev, 3, block) < 0) {
        blockdev_close(dev);
        return -1;
    }

    memset(block, 0, sizeof(block));
    for (i = 0; i < 12u; i++) ext4_set_bit(block, i);
    if (ext4_mkfs_write_block(dev, 4, block) < 0) {
        blockdev_close(dev);
        return -1;
    }

    memset(inode_table, 0, sizeof(inode_table));
    ext4_mkfs_write_inode(inode_table, EXT4_ROOT_INO, 0x41EDu, EXT4_MKFS_BLOCK_SIZE,
                          EXT4_MKFS_ROOT_BLOCK, EXT4_EXTENTS_FL);
    ext4_mkfs_write_inode(inode_table, 11u, 0x41C0u, 0u, 0u, 0u);
    ext4_mkfs_write_inode(inode_table, EXT4_MKFS_RW_INO, 0x81A4u,
                          (uint32_t)(sizeof(EXT4_MKFS_RW_FILE_DATA) - 1u),
                          EXT4_MKFS_RW_BLOCK, EXT4_EXTENTS_FL);
    for (i = 0; i < 4u; i++) {
        if (ext4_mkfs_write_block(dev, 5u + i, inode_table + i * EXT4_MKFS_BLOCK_SIZE) < 0) {
            blockdev_close(dev);
            return -1;
        }
    }

    memset(block, 0, sizeof(block));
    ext4_mkfs_write_dirent(block, EXT4_ROOT_INO, 12u, 1u, 2u, ".");
    ext4_mkfs_write_dirent(block + 12u, EXT4_ROOT_INO, 12u, 2u, 2u, "..");
    ext4_mkfs_write_dirent(block + 24u, EXT4_MKFS_RW_INO,
                           (uint16_t)(EXT4_MKFS_BLOCK_SIZE - 24u),
                           (uint8_t)(sizeof(EXT4_MKFS_RW_FILE_NAME) - 1u),
                           EXT4_FT_REG_FILE, EXT4_MKFS_RW_FILE_NAME);
    if (ext4_mkfs_write_block(dev, EXT4_MKFS_ROOT_BLOCK, block) < 0) {
        blockdev_close(dev);
        return -1;
    }

    memset(block, 0, sizeof(block));
    memcpy(block, EXT4_MKFS_RW_FILE_DATA, sizeof(EXT4_MKFS_RW_FILE_DATA) - 1u);
    if (ext4_mkfs_write_block(dev, EXT4_MKFS_RW_BLOCK, block) < 0) {
        blockdev_close(dev);
        return -1;
    }

    blockdev_close(dev);
    serial_write("[OK] EXT4 formatted\n");
    return 0;
}
