/* ============================================================
 * openos x86_64 ext2/ext4 只读文件系统驱动（阶段 M3.2）
 * 见 include/ext4_64.h 说明。
 * ============================================================ */
#include "ext4_64.h"

/* kmalloc / 日志 由内核提供 */
extern void  early_serial64_write(const char *s);

/* ---- 简易工具 ---- */
static void fs_log(const char *s) { early_serial64_write(s); }
static void u32_to_hex(uint32_t v, char *out) {
    const char *h = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) out[i] = h[(v >> ((7 - i) * 4)) & 0xF];
    out[8] = 0;
}
static void u32_to_dec(uint32_t v, char *out) {
    char tmp[12]; int n = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}
static void log_kv(const char *k, uint32_t v) {
    char buf[9]; u32_to_hex(v, buf);
    fs_log(k); fs_log("=0x"); fs_log(buf); fs_log("\n");
}
static void log_kd(const char *k, uint32_t v) {
    char buf[12]; u32_to_dec(v, buf);
    fs_log(k); fs_log("="); fs_log(buf); fs_log("\n");
}

/* ---- 磁盘结构（on-disk，小端）---- */

#define EXT_SUPER_MAGIC   0xEF53
#define EXT_SB_OFFSET     1024      /* 超级块固定在 1024 字节偏移 */
#define EXT_ROOT_INO      2         /* 根目录 inode 号固定为 2 */

/* s_feature_incompat 位 */
#define INCOMPAT_FILETYPE  0x0002   /* 目录项含 file_type 字节 */
#define INCOMPAT_EXTENTS   0x0040   /* ext4 extent 树 */
#define INCOMPAT_64BIT     0x0080   /* 64 位块号 / 64B 组描述符 */

/* i_mode 高 4 位类型 */
#define S_IFMT   0xF000
#define S_IFDIR  0x4000
#define S_IFREG  0x8000
#define S_IFLNK  0xA000

/* extent 魔数 */
#define EXT4_EXT_MAGIC   0xF30A

typedef struct __attribute__((packed)) {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;    /* 块大小1K时=1，否则=0 */
    uint32_t s_log_block_size;      /* 块大小 = 1024 << 该值 */
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;               /* 0xEF53 */
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;           /* 0=GOOD_OLD(inode128), 1=DYNAMIC */
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    /* -- rev1 dynamic -- */
    uint32_t s_first_ino;
    uint16_t s_inode_size;          /* inode 结构字节数 */
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    /* 后续字段（journal uuid 等）省略，用 desc_size 定位 */
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;           /* 64bit 时组描述符字节数 */
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    /* 其余省略 */
} ext_superblock_t;

/* 块组描述符（传统 32 字节 / ext4 64 字节，仅取低 32B 用到的字段） */
typedef struct __attribute__((packed)) {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;     /* inode 表起始块（低 32 位） */
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    /* 64bit 扩展（高 32 位）*/
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
    uint16_t bg_free_blocks_count_hi;
    uint16_t bg_free_inodes_count_hi;
    uint16_t bg_used_dirs_count_hi;
    uint16_t bg_itable_unused_hi;
    uint32_t bg_exclude_bitmap_hi;
    uint16_t bg_block_bitmap_csum_hi;
    uint16_t bg_inode_bitmap_csum_hi;
    uint32_t bg_reserved;
} ext_group_desc_t;

typedef struct __attribute__((packed)) {
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
    uint32_t i_block[15];           /* 直接/间接块 或 extent 树根(60B) */
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
    uint8_t  i_osd2[12];
    /* rev1 大 inode 后续省略 */
} ext_inode_t;

/* 目录项（ext2 rev>=0.5 含 file_type） */
typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    /* char name[]; 紧随其后 */
} ext_dirent_disk_t;

/* extent 树头 / 索引 / 叶 */
typedef struct __attribute__((packed)) {
    uint16_t eh_magic;              /* 0xF30A */
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;              /* 0=叶子层 */
    uint32_t eh_generation;
} ext4_extent_header_t;

typedef struct __attribute__((packed)) {
    uint32_t ei_block;              /* 覆盖的逻辑块起点 */
    uint32_t ei_leaf_lo;            /* 下一级物理块（低 32 位） */
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} ext4_extent_idx_t;

typedef struct __attribute__((packed)) {
    uint32_t ee_block;              /* 逻辑块起点 */
    uint16_t ee_len;                /* 块数（>32768 表示未初始化） */
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;           /* 物理块起点（低 32 位） */
} ext4_extent_t;

/* ---- 卷状态 ---- */
#define SECTOR_SIZE   512
#define MAX_BLOCK     4096          /* 支持最大块 4K */

typedef struct {
    int          mounted;
    ext4_read_fn read;
    uint32_t     part_lba;          /* 分区起始扇区 */
    uint32_t     block_size;        /* 1024/2048/4096 */
    uint32_t     inode_size;        /* 128/256... */
    uint32_t     inodes_per_group;
    uint32_t     blocks_per_group;
    uint32_t     first_data_block;
    uint32_t     groups_count;
    uint32_t     desc_size;         /* 组描述符字节数 32 或 64 */
    uint32_t     feature_incompat;
    uint32_t     rev_level;
    int          version;           /* 2/3/4 */
    uint32_t     inode_table_blk[64]; /* 各组 inode 表起始块（缓存前 64 组） */
    ext_superblock_t sb;
} ext_vol_t;

static ext_vol_t g_ev;

/* ---- 块读基础设施 ---- */

/* 读一个 fs 块（block_size 字节）到 buf */
static int read_block(uint32_t blk, void *buf) {
    uint32_t spb = g_ev.block_size / SECTOR_SIZE;   /* sectors per block */
    uint32_t lba = g_ev.part_lba + blk * spb;
    return g_ev.read(lba, spb, buf);
}

/* 读组描述符（组 g）到 out（拷贝低 32/64 字节） */
static int read_group_desc(uint32_t g, ext_group_desc_t *out) {
    /* GDT 紧跟超级块所在块之后：块大小1K时 SB 在块1，GDT 从块2；否则 SB 在块0，GDT 从块1 */
    uint32_t gdt_start = g_ev.first_data_block + 1;
    uint32_t per_block = g_ev.block_size / g_ev.desc_size;
    uint32_t blk = gdt_start + g / per_block;
    uint32_t idx = g % per_block;
    static uint8_t tmp[MAX_BLOCK];
    if (read_block(blk, tmp) != 0) return -1;
    /* 清零 out 再拷贝实际 desc_size 字节 */
    uint8_t *dst = (uint8_t *)out;
    for (uint32_t i = 0; i < sizeof(ext_group_desc_t); i++) dst[i] = 0;
    uint8_t *src = tmp + idx * g_ev.desc_size;
    uint32_t n = g_ev.desc_size < sizeof(ext_group_desc_t) ? g_ev.desc_size : sizeof(ext_group_desc_t);
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
    return 0;
}

/* ---- MBR 分区探测 ---- */
/* 从 LBA0 的 MBR 找第一个 Linux(0x83) 分区起始 LBA。无则返回 0。 */
static uint32_t probe_mbr_linux(ext4_read_fn read) {
    static uint8_t mbr[SECTOR_SIZE];
    if (read(0, 1, mbr) != 0) return 0;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return 0;   /* 无 MBR 签名 */
    for (int i = 0; i < 4; i++) {
        uint8_t *e = mbr + 446 + i * 16;
        uint8_t type = e[4];
        if (type == 0x83) {
            uint32_t lba = e[8] | (e[9] << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24);
            return lba;
        }
    }
    return 0;
}

/* ---- 挂载 ---- */
int ext4_mount(ext4_read_fn read_fn, uint32_t part_lba) {
    if (!read_fn) return -1;
    for (uint32_t i = 0; i < sizeof(g_ev); i++) ((uint8_t *)&g_ev)[i] = 0;
    g_ev.read = read_fn;

    /* 分区探测 */
    if (part_lba == 0) {
        uint32_t p = probe_mbr_linux(read_fn);
        part_lba = p;   /* 0 表示整盘 */
    }
    g_ev.part_lba = part_lba;

    /* 读超级块：固定在分区偏移 1024 字节 = 扇区 (part_lba + 2) */
    static uint8_t sbbuf[SECTOR_SIZE * 2];
    if (read_fn(part_lba + (EXT_SB_OFFSET / SECTOR_SIZE), 2, sbbuf) != 0)
        return -2;
    ext_superblock_t *sb = (ext_superblock_t *)sbbuf;
    if (sb->s_magic != EXT_SUPER_MAGIC) {
        fs_log("[EXT] bad magic (not ext2/3/4)\n");
        return -3;
    }

    /* 拷贝超级块到卷状态 */
    for (uint32_t i = 0; i < sizeof(ext_superblock_t); i++)
        ((uint8_t *)&g_ev.sb)[i] = sbbuf[i];

    /* 块大小 */
    if (sb->s_log_block_size > 2) return -5;    /* 仅支持 1K/2K/4K */
    g_ev.block_size = 1024u << sb->s_log_block_size;
    g_ev.first_data_block = sb->s_first_data_block;
    g_ev.blocks_per_group = sb->s_blocks_per_group;
    g_ev.inodes_per_group = sb->s_inodes_per_group;
    g_ev.rev_level = sb->s_rev_level;
    g_ev.feature_incompat = (sb->s_rev_level >= 1) ? sb->s_feature_incompat : 0;

    /* inode 大小与组描述符大小 */
    if (sb->s_rev_level >= 1) {
        g_ev.inode_size = sb->s_inode_size ? sb->s_inode_size : 128;
        if (g_ev.feature_incompat & INCOMPAT_64BIT)
            g_ev.desc_size = sb->s_desc_size ? sb->s_desc_size : 64;
        else
            g_ev.desc_size = 32;
    } else {
        g_ev.inode_size = 128;
        g_ev.desc_size = 32;
    }

    /* 组数 = ceil(blocks_count / blocks_per_group) */
    uint32_t blocks = sb->s_blocks_count_lo;
    g_ev.groups_count = (blocks - g_ev.first_data_block + g_ev.blocks_per_group - 1)
                        / g_ev.blocks_per_group;
    if (g_ev.groups_count == 0) g_ev.groups_count = 1;

    /* 版本判定 */
    if (g_ev.feature_incompat & INCOMPAT_EXTENTS) g_ev.version = 4;
    else if (sb->s_feature_compat & 0x0004 /* HAS_JOURNAL */) g_ev.version = 3;
    else g_ev.version = 2;

    /* 缓存前 64 组的 inode 表起始块 */
    uint32_t cache_n = g_ev.groups_count < 64 ? g_ev.groups_count : 64;
    for (uint32_t g = 0; g < cache_n; g++) {
        ext_group_desc_t gd;
        if (read_group_desc(g, &gd) != 0) return -2;
        g_ev.inode_table_blk[g] = gd.bg_inode_table_lo;
    }

    g_ev.mounted = 1;
    return 0;
}

int ext4_mounted(void) { return g_ev.mounted; }
int ext4_version(void) { return g_ev.mounted ? g_ev.version : 0; }

/* ---- inode 读取 ---- */
/* 读 inode 号 ino（从 1 起）到 out。返回 0 成功。 */
static int read_inode(uint32_t ino, ext_inode_t *out) {
    if (ino == 0) return -1;
    uint32_t g = (ino - 1) / g_ev.inodes_per_group;
    uint32_t idx = (ino - 1) % g_ev.inodes_per_group;
    uint32_t itbl;
    if (g < 64) {
        itbl = g_ev.inode_table_blk[g];
    } else {
        ext_group_desc_t gd;
        if (read_group_desc(g, &gd) != 0) return -1;
        itbl = gd.bg_inode_table_lo;
    }
    /* inode 在表内字节偏移 */
    uint32_t byte_off = idx * g_ev.inode_size;
    uint32_t blk = itbl + byte_off / g_ev.block_size;
    uint32_t in_blk = byte_off % g_ev.block_size;
    static uint8_t buf[MAX_BLOCK];
    if (read_block(blk, buf) != 0) return -1;
    uint8_t *src = buf + in_blk;
    uint8_t *dst = (uint8_t *)out;
    for (uint32_t i = 0; i < sizeof(ext_inode_t); i++) dst[i] = src[i];
    return 0;
}

/* ---- 逻辑块 -> 物理块 映射 ---- */

/* extent 树：在 i_block(60字节) 或间接块中查找逻辑块 lblk 对应物理块。
 * hdr 指向 extent header。返回物理块号，0 表示空洞/未找到。 */
static uint32_t extent_lookup(uint8_t *node, uint32_t lblk) {
    ext4_extent_header_t *h = (ext4_extent_header_t *)node;
    if (h->eh_magic != EXT4_EXT_MAGIC) return 0;
    if (h->eh_depth == 0) {
        /* 叶子：extent 数组 */
        ext4_extent_t *ee = (ext4_extent_t *)(node + sizeof(ext4_extent_header_t));
        for (int i = 0; i < h->eh_entries; i++) {
            uint32_t len = ee[i].ee_len;
            if (len > 32768) len -= 32768;  /* 未初始化 extent */
            if (lblk >= ee[i].ee_block && lblk < ee[i].ee_block + len) {
                uint32_t phys = ee[i].ee_start_lo + (lblk - ee[i].ee_block);
                return phys;
            }
        }
        return 0;
    } else {
        /* 索引节点：递归下钻 */
        ext4_extent_idx_t *ix = (ext4_extent_idx_t *)(node + sizeof(ext4_extent_header_t));
        int sel = -1;
        for (int i = 0; i < h->eh_entries; i++) {
            if (lblk >= ix[i].ei_block) sel = i; else break;
        }
        if (sel < 0) return 0;
        static uint8_t child[MAX_BLOCK * 4];  /* 支持多层递归共用？开局部静态不行 */
        uint8_t *cb = child;
        if (read_block(ix[sel].ei_leaf_lo, cb) != 0) return 0;
        return extent_lookup(cb, lblk);
    }
}

/* 传统直接/间接块映射。返回物理块号，0=空洞。 */
static uint32_t indirect_lookup(ext_inode_t *in, uint32_t lblk) {
    uint32_t ppb = g_ev.block_size / 4;   /* 每块指针数 */
    static uint8_t ib[MAX_BLOCK];
    /* 0..11 直接 */
    if (lblk < 12) return in->i_block[lblk];
    lblk -= 12;
    /* 一级间接 */
    if (lblk < ppb) {
        if (in->i_block[12] == 0) return 0;
        if (read_block(in->i_block[12], ib) != 0) return 0;
        return ((uint32_t *)ib)[lblk];
    }
    lblk -= ppb;
    /* 二级间接 */
    if (lblk < ppb * ppb) {
        if (in->i_block[13] == 0) return 0;
        if (read_block(in->i_block[13], ib) != 0) return 0;
        uint32_t l1 = ((uint32_t *)ib)[lblk / ppb];
        if (l1 == 0) return 0;
        if (read_block(l1, ib) != 0) return 0;
        return ((uint32_t *)ib)[lblk % ppb];
    }
    lblk -= ppb * ppb;
    /* 三级间接 */
    if (in->i_block[14] == 0) return 0;
    if (read_block(in->i_block[14], ib) != 0) return 0;
    uint32_t l1 = ((uint32_t *)ib)[lblk / (ppb * ppb)];
    if (l1 == 0) return 0;
    if (read_block(l1, ib) != 0) return 0;
    uint32_t l2 = ((uint32_t *)ib)[(lblk / ppb) % ppb];
    if (l2 == 0) return 0;
    if (read_block(l2, ib) != 0) return 0;
    return ((uint32_t *)ib)[lblk % ppb];
}

#define EXT4_INODE_FLAG_EXTENTS  0x80000

/* 统一入口：inode 的逻辑块 lblk -> 物理块 */
static uint32_t inode_bmap(ext_inode_t *in, uint32_t lblk) {
    if (in->i_flags & EXT4_INODE_FLAG_EXTENTS)
        return extent_lookup((uint8_t *)in->i_block, lblk);
    return indirect_lookup(in, lblk);
}

/* 读 inode 第 lblk 个逻辑块到 buf（block_size 字节）。空洞则置零。 */
static int read_inode_block(ext_inode_t *in, uint32_t lblk, void *buf) {
    uint32_t phys = inode_bmap(in, lblk);
    if (phys == 0) {
        uint8_t *b = (uint8_t *)buf;
        for (uint32_t i = 0; i < g_ev.block_size; i++) b[i] = 0;
        return 0;
    }
    return read_block(phys, buf);
}

/* ---- 字符串工具 ---- */
static int str_eq_n(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static uint32_t str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

/* 在目录 inode dir 中查名为 name(长 len) 的条目，返回其 inode 号（0=未找到）。
 * 若 out_type 非空，写入 file_type。 */
static uint32_t dir_find(ext_inode_t *dir, const char *name, uint32_t len, uint8_t *out_type) {
    uint32_t size = dir->i_size_lo;
    uint32_t nblk = (size + g_ev.block_size - 1) / g_ev.block_size;
    static uint8_t blk[MAX_BLOCK];
    for (uint32_t b = 0; b < nblk; b++) {
        if (read_inode_block(dir, b, blk) != 0) continue;
        uint32_t off = 0;
        while (off < g_ev.block_size) {
            ext_dirent_disk_t *de = (ext_dirent_disk_t *)(blk + off);
            if (de->rec_len < 8) break;
            if (de->inode != 0 && de->name_len == len) {
                char *nm = (char *)(blk + off + sizeof(ext_dirent_disk_t));
                if (str_eq_n(nm, name, len)) {
                    if (out_type) *out_type = de->file_type;
                    return de->inode;
                }
            }
            off += de->rec_len;
        }
    }
    return 0;
}

/* 路径解析：从根开始逐级查找 path，返回目标 inode 号（0=未找到）。 */
static uint32_t resolve_path(const char *path) {
    uint32_t ino = EXT_ROOT_INO;
    const char *p = path;
    while (*p == '/') p++;
    while (*p) {
        /* 提取一段 */
        const char *start = p;
        while (*p && *p != '/') p++;
        uint32_t seg = (uint32_t)(p - start);
        if (seg == 0) break;
        ext_inode_t in;
        if (read_inode(ino, &in) != 0) return 0;
        if ((in.i_mode & S_IFMT) != S_IFDIR) return 0;
        uint32_t next = dir_find(&in, start, seg, 0);
        if (next == 0) return 0;
        ino = next;
        while (*p == '/') p++;
    }
    return ino;
}

/* file_type -> is_dir 视图（1=dir,0=reg,2=lnk,3=other） */
static int ftype_view(uint8_t ft, uint16_t mode) {
    if (ft == 2) return 1;         /* EXT4_FT_DIR */
    if (ft == 1) return 0;         /* EXT4_FT_REG_FILE */
    if (ft == 7) return 2;         /* EXT4_FT_SYMLINK */
    if (ft != 0) return 3;
    /* 无 file_type，靠 mode */
    if ((mode & S_IFMT) == S_IFDIR) return 1;
    if ((mode & S_IFMT) == S_IFREG) return 0;
    if ((mode & S_IFMT) == S_IFLNK) return 2;
    return 3;
}

/* ---- 公共 API ---- */

int ext4_list(const char *path, int (*cb)(const ext4_dirent_t *, void *), void *ud) {
    if (!g_ev.mounted) return -1;
    uint32_t ino = resolve_path(path);
    if (ino == 0) return -2;
    ext_inode_t dir;
    if (read_inode(ino, &dir) != 0) return -3;
    if ((dir.i_mode & S_IFMT) != S_IFDIR) return -4;

    uint32_t size = dir.i_size_lo;
    uint32_t nblk = (size + g_ev.block_size - 1) / g_ev.block_size;
    static uint8_t blk[MAX_BLOCK];
    int count = 0;
    for (uint32_t b = 0; b < nblk; b++) {
        if (read_inode_block(&dir, b, blk) != 0) continue;
        uint32_t off = 0;
        while (off < g_ev.block_size) {
            ext_dirent_disk_t *de = (ext_dirent_disk_t *)(blk + off);
            if (de->rec_len < 8) break;
            if (de->inode != 0 && de->name_len > 0) {
                ext4_dirent_t out;
                uint32_t nl = de->name_len;
                if (nl > 255) nl = 255;
                char *nm = (char *)(blk + off + sizeof(ext_dirent_disk_t));
                for (uint32_t i = 0; i < nl; i++) out.name[i] = nm[i];
                out.name[nl] = 0;
                out.inode = de->inode;
                /* 取子 inode 拿 size/mode */
                ext_inode_t ci;
                uint16_t mode = 0; uint32_t fsize = 0;
                if (read_inode(de->inode, &ci) == 0) { mode = ci.i_mode; fsize = ci.i_size_lo; }
                out.size = fsize;
                out.is_dir = ftype_view(de->file_type, mode);
                count++;
                if (cb && cb(&out, ud)) return count;
            }
            off += de->rec_len;
        }
    }
    return count;
}

int ext4_read_file(const char *path, void *buf, uint32_t max) {
    if (!g_ev.mounted) return -1;
    uint32_t ino = resolve_path(path);
    if (ino == 0) return -2;
    ext_inode_t in;
    if (read_inode(ino, &in) != 0) return -3;
    if ((in.i_mode & S_IFMT) != S_IFREG) return -4;

    uint32_t fsize = in.i_size_lo;
    if (fsize > max) fsize = max;
    uint8_t *dst = (uint8_t *)buf;
    static uint8_t blk[MAX_BLOCK];
    uint32_t done = 0;
    uint32_t lblk = 0;
    while (done < fsize) {
        if (read_inode_block(&in, lblk, blk) != 0) break;
        uint32_t chunk = g_ev.block_size;
        if (chunk > fsize - done) chunk = fsize - done;
        for (uint32_t i = 0; i < chunk; i++) dst[done + i] = blk[i];
        done += chunk;
        lblk++;
    }
    return (int)done;
}

int ext4_stat(const char *path, ext4_dirent_t *out) {
    if (!g_ev.mounted || !out) return -1;
    uint32_t ino = resolve_path(path);
    if (ino == 0) return -2;
    ext_inode_t in;
    if (read_inode(ino, &in) != 0) return -3;
    /* 取最后一段作为名字 */
    const char *p = path; const char *last = path;
    while (*p) { if (*p == '/') last = p + 1; p++; }
    uint32_t nl = str_len(last); if (nl > 255) nl = 255;
    for (uint32_t i = 0; i < nl; i++) out->name[i] = last[i];
    out->name[nl] = 0;
    out->inode = ino;
    out->size = in.i_size_lo;
    out->is_dir = ftype_view(0, in.i_mode);
    return 0;
}

/* ---- 自检 ---- */
static int selftest_list_cb(const ext4_dirent_t *e, void *ud) {
    (void)ud;
    fs_log("  [");
    fs_log(e->is_dir == 1 ? "D" : (e->is_dir == 2 ? "L" : "F"));
    fs_log("] ");
    fs_log(e->name);
    fs_log(" ("); { char b[12]; u32_to_dec(e->size, b); fs_log(b); } fs_log("B)\n");
    return 0;
}

void ext4_selftest(void) {
    fs_log("\n=== EXT2/4 SELFTEST ===\n");
    if (!g_ev.mounted) { fs_log("[EXT] not mounted, skip\n"); return; }

    fs_log("[EXT] version=ext");
    { char b[12]; u32_to_dec((uint32_t)g_ev.version, b); fs_log(b); } fs_log("\n");
    log_kd("[EXT] block_size", g_ev.block_size);
    log_kd("[EXT] inode_size", g_ev.inode_size);
    log_kd("[EXT] groups", g_ev.groups_count);
    log_kd("[EXT] inodes/group", g_ev.inodes_per_group);
    log_kd("[EXT] blocks/group", g_ev.blocks_per_group);
    log_kv("[EXT] feat_incompat", g_ev.feature_incompat);

    fs_log("[EXT] root dir listing:\n");
    int n = ext4_list("/", selftest_list_cb, 0);
    log_kd("[EXT] root entries", (uint32_t)(n < 0 ? 0 : n));

    /* 试读 /hello.txt */
    static char rbuf[512];
    int r = ext4_read_file("/hello.txt", rbuf, sizeof(rbuf) - 1);
    if (r > 0) {
        rbuf[r] = 0;
        fs_log("[EXT] /hello.txt (");
        { char b[12]; u32_to_dec((uint32_t)r, b); fs_log(b); } fs_log("B): ");
        fs_log(rbuf); fs_log("\n");
        fs_log("[EXT] READ VERIFY OK\n");
    } else {
        fs_log("[EXT] /hello.txt not found (ok if absent)\n");
    }

    /* 子目录遍历（验证多级路径解析） */
    fs_log("[EXT] /subdir listing:\n");
    int sn = ext4_list("/subdir", selftest_list_cb, 0);
    if (sn > 0) fs_log("[EXT] SUBDIR VERIFY OK\n");
    else fs_log("[EXT] subdir empty/absent\n");

    /* 子目录内文件读取（多级路径 + 文件读） */
    int r2 = ext4_read_file("/subdir/inside.txt", rbuf, sizeof(rbuf) - 1);
    if (r2 > 0) {
        rbuf[r2] = 0;
        fs_log("[EXT] /subdir/inside.txt: "); fs_log(rbuf); fs_log("\n");
        fs_log("[EXT] NESTED READ VERIFY OK\n");
    } else {
        fs_log("[EXT] /subdir/inside.txt not found\n");
    }

    /* 大文件间接块读取验证（big.dat 40000B > 12*1024，触发一级间接） */
    static char big[40008];
    int rb = ext4_read_file("/big.dat", big, sizeof(big));
    if (rb > 0) {
        /* 验证全为 'A'（mkfs 脚本填 40000 个 'A'） */
        int ok = (rb == 40000);
        for (int i = 0; ok && i < rb; i++) if (big[i] != 'A') ok = 0;
        log_kd("[EXT] big.dat bytes", (uint32_t)rb);
        if (ok) fs_log("[EXT] INDIRECT-BLOCK VERIFY OK\n");
        else fs_log("[EXT] big.dat content MISMATCH\n");
    } else {
        fs_log("[EXT] /big.dat not found\n");
    }
    fs_log("=== EXT SELFTEST DONE ===\n");
}
