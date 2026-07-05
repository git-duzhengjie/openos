/* ============================================================
 * openos x86_64 FAT32 只读驱动实现（阶段 4-1）
 * 见 include/fat32_64.h 说明。
 * ============================================================ */
#include "fat32_64.h"

/* kmalloc / 日志 由内核提供 */
extern void *arch_x86_64_kmalloc(unsigned long size);
extern void  arch_x86_64_kfree(void *p);
extern void  early_serial64_write(const char *s);

/* ---- 简易工具 ---- */
static void fs_log(const char *s) { early_serial64_write(s); }

static void u32_to_hex(uint32_t v, char *out) {
    const char *h = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) out[i] = h[(v >> ((7 - i) * 4)) & 0xF];
    out[8] = 0;
}
static void log_kv(const char *k, uint32_t v) {
    char buf[9]; u32_to_hex(v, buf);
    fs_log(k); fs_log("=0x"); fs_log(buf); fs_log("\n");
}

static int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* ---- FAT32 挂载状态 ---- */
#define SECTOR_SIZE 512

typedef struct {
    fat32_read_fn read_fn;
    uint32_t part_lba;          /* 分区起始 LBA */
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint32_t fat_size_sectors;  /* 每个 FAT 占用扇区数 */
    uint32_t total_sectors;
    uint32_t root_cluster;
    uint32_t fat_begin_lba;     /* 第一个 FAT 的起始 LBA（绝对） */
    uint32_t data_begin_lba;    /* 数据区第一个簇的起始 LBA（绝对） */
    uint32_t cluster_bytes;
    int      mounted;
} fat32_vol_t;

static fat32_vol_t g_vol;

/* 读一个扇区（绝对 LBA） */
static int read_sector(uint32_t lba, void *buf) {
    return g_vol.read_fn(lba, 1, buf);
}

/* 读取整簇到 buf（buf 至少 cluster_bytes 字节） */
static int read_cluster(uint32_t cluster, void *buf) {
    if (cluster < 2) return -1;
    uint32_t lba = g_vol.data_begin_lba +
                   (cluster - 2) * g_vol.sectors_per_cluster;
    return g_vol.read_fn(lba, g_vol.sectors_per_cluster, buf);
}

/* 读 FAT 表项，得到下一个簇号 */
static uint32_t fat_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = g_vol.fat_begin_lba + (fat_offset / SECTOR_SIZE);
    uint32_t ent_offset = fat_offset % SECTOR_SIZE;
    uint8_t sec[SECTOR_SIZE];
    if (read_sector(fat_sector, sec) != 0) return 0x0FFFFFFF;
    uint32_t val = *(uint32_t *)(sec + ent_offset);
    return val & 0x0FFFFFFF;
}

/* 簇号是否为链尾/无效 */
static int is_eoc(uint32_t c) { return (c >= 0x0FFFFFF8) || (c < 2); }

/* ---- MBR 分区探测 ---- */
static uint32_t probe_mbr_first_fat(fat32_read_fn read_fn) {
    uint8_t sec[SECTOR_SIZE];
    if (read_fn(0, 1, sec) != 0) return 0;
    /* MBR 签名 */
    if (sec[510] != 0x55 || sec[511] != 0xAA) return 0;
    /* 4 个分区项，偏移 446，每项 16 字节 */
    for (int i = 0; i < 4; i++) {
        uint8_t *p = sec + 446 + i * 16;
        uint8_t type = p[4];
        uint32_t start = *(uint32_t *)(p + 8);
        /* 0x0B / 0x0C = FAT32 */
        if ((type == 0x0B || type == 0x0C) && start != 0) return start;
    }
    return 0;
}

/* ---- 挂载 ---- */
int fat32_mount(fat32_read_fn read_fn, uint32_t part_lba) {
    if (!read_fn) return -1;
    g_vol.mounted = 0;
    g_vol.read_fn = read_fn;

    if (part_lba == 0) {
        uint32_t p = probe_mbr_first_fat(read_fn);
        part_lba = p; /* 若为 0 则按整盘 FAT32 从 LBA0 解析 */
    }
    g_vol.part_lba = part_lba;

    uint8_t bpb[SECTOR_SIZE];
    if (read_fn(part_lba, 1, bpb) != 0) {
        fs_log("[fat32] read BPB failed\n");
        return -2;
    }

    g_vol.bytes_per_sector    = *(uint16_t *)(bpb + 11);
    g_vol.sectors_per_cluster = bpb[13];
    g_vol.reserved_sectors    = *(uint16_t *)(bpb + 14);
    g_vol.num_fats            = bpb[16];
    uint16_t total16          = *(uint16_t *)(bpb + 19);
    g_vol.fat_size_sectors    = *(uint32_t *)(bpb + 36); /* FAT32: BPB_FATSz32 */
    g_vol.total_sectors       = total16 ? total16 : *(uint32_t *)(bpb + 32);
    g_vol.root_cluster        = *(uint32_t *)(bpb + 44);

    /* 合法性校验 */
    if (g_vol.bytes_per_sector != SECTOR_SIZE ||
        g_vol.sectors_per_cluster == 0 ||
        g_vol.num_fats == 0 ||
        g_vol.fat_size_sectors == 0) {
        fs_log("[fat32] invalid BPB\n");
        log_kv("  bps", g_vol.bytes_per_sector);
        log_kv("  spc", g_vol.sectors_per_cluster);
        log_kv("  fatsz", g_vol.fat_size_sectors);
        return -3;
    }

    g_vol.fat_begin_lba  = part_lba + g_vol.reserved_sectors;
    g_vol.data_begin_lba = g_vol.fat_begin_lba +
                           (uint32_t)g_vol.num_fats * g_vol.fat_size_sectors;
    g_vol.cluster_bytes  = (uint32_t)g_vol.sectors_per_cluster * SECTOR_SIZE;
    g_vol.mounted = 1;

    fs_log("[fat32] mounted OK\n");
    log_kv("  part_lba", part_lba);
    log_kv("  spc", g_vol.sectors_per_cluster);
    log_kv("  root_clus", g_vol.root_cluster);
    log_kv("  data_lba", g_vol.data_begin_lba);
    return 0;
}

int fat32_mounted(void) { return g_vol.mounted; }

/* ============================================================
 * 目录项解析
 * ============================================================ */
/* FAT 目录项（32 字节） */
typedef struct {
    uint8_t  name[11];      /* 8.3 名 */
    uint8_t  attr;          /* 属性 */
    uint8_t  nt_res;
    uint8_t  crt_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} __attribute__((packed)) fat_dir_ent_t;

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LFN       0x0F

/* 将 8.3 名转成可读字符串 */
static void fmt_short_name(const uint8_t *raw, char *out) {
    int o = 0;
    for (int i = 0; i < 8; i++) {
        if (raw[i] == ' ') break;
        out[o++] = raw[i];
    }
    /* 扩展名 */
    if (raw[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11; i++) {
            if (raw[i] == ' ') break;
            out[o++] = raw[i];
        }
    }
    out[o] = 0;
}

/* LFN 项（32 字节，与 dir_ent 复用同一区域） */
typedef struct {
    uint8_t  ord;
    uint8_t  name1[10];   /* 5 个 UTF-16 字符 */
    uint8_t  attr;        /* == 0x0F */
    uint8_t  type;
    uint8_t  checksum;
    uint8_t  name2[12];   /* 6 个 UTF-16 字符 */
    uint16_t fst_clus_lo; /* 恒为 0 */
    uint8_t  name3[4];    /* 2 个 UTF-16 字符 */
} __attribute__((packed)) fat_lfn_ent_t;

/* 从 LFN 项抽取字符（UTF-16 → ASCII 截断），追加到 dst[pos] */
static void lfn_extract(const fat_lfn_ent_t *l, char *dst, int *pos) {
    const uint8_t *parts[3] = { l->name1, l->name2, l->name3 };
    int counts[3] = { 5, 6, 2 };
    for (int p = 0; p < 3; p++) {
        for (int i = 0; i < counts[p]; i++) {
            uint16_t ch = *(uint16_t *)(parts[p] + i * 2);
            if (ch == 0x0000 || ch == 0xFFFF) { dst[*pos] = 0; return; }
            dst[(*pos)++] = (ch < 128) ? (char)ch : '?';
        }
    }
}

/* ============================================================
 * 目录遍历（核心）
 * 对目录 dir_cluster 的每个有效条目调用 cb。
 * cb 返回非 0 则提前停止遍历。
 * 返回：遍历到的条目数，负数为错误。
 * ============================================================ */
static int walk_dir(uint32_t dir_cluster,
                    int (*cb)(const fat32_dirent_t *ent, void *ud),
                    void *ud) {
    if (!g_vol.mounted) return -1;
    uint8_t *clusbuf = (uint8_t *)arch_x86_64_kmalloc(g_vol.cluster_bytes);
    if (!clusbuf) return -2;

    char lfn_buf[256];
    int  lfn_have = 0;   /* 是否已结果 LFN */
    int  count = 0;
    uint32_t cluster = dir_cluster;

    while (!is_eoc(cluster)) {
        if (read_cluster(cluster, clusbuf) != 0) { arch_x86_64_kfree(clusbuf); return -3; }
        uint32_t ents = g_vol.cluster_bytes / 32;
        for (uint32_t i = 0; i < ents; i++) {
            uint8_t *e = clusbuf + i * 32;
            uint8_t first = e[0];
            if (first == 0x00) { arch_x86_64_kfree(clusbuf); return count; } /* 目录结束 */
            if (first == 0xE5) { lfn_have = 0; continue; }                   /* 已删除 */
            uint8_t attr = e[11];
            if (attr == ATTR_LFN) {
                fat_lfn_ent_t *l = (fat_lfn_ent_t *)e;
                uint8_t ord = l->ord & 0x3F;
                /* LFN 逆序存储：ord 越大越靠前。按 (ord-1)*13 定位 */
                int pos = (ord - 1) * 13;
                lfn_extract(l, lfn_buf, &pos);
                /* 记录最大长度继续在后面断尾 */
                if (l->ord & 0x40) lfn_buf[ord * 13] = 0; /* 首项（最高序号）：预置终止 */
                lfn_have = 1;
                continue;
            }
            if (attr & ATTR_VOLUME_ID) { lfn_have = 0; continue; } /* 卷标 */

            /* 普通 8.3 项 */
            fat_dir_ent_t *d = (fat_dir_ent_t *)e;
            fat32_dirent_t out;
            if (lfn_have) {
                int k = 0; for (; lfn_buf[k] && k < 255; k++) out.name[k] = lfn_buf[k];
                out.name[k] = 0;
            } else {
                fmt_short_name(d->name, out.name);
            }
            lfn_have = 0;
            out.size = d->file_size;
            out.first_cluster = ((uint32_t)d->fst_clus_hi << 16) | d->fst_clus_lo;
            out.is_dir = (d->attr & ATTR_DIRECTORY) ? 1 : 0;
            count++;
            if (cb && cb(&out, ud)) { arch_x86_64_kfree(clusbuf); return count; }
        }
        cluster = fat_next_cluster(cluster);
    }
    arch_x86_64_kfree(clusbuf);
    return count;
}

/* ============================================================
 * 路径解析：在 dir_cluster 中查找名为 name 的条目
 * ============================================================ */
typedef struct { const char *want; fat32_dirent_t found; int hit; } lookup_ctx_t;
static int lookup_cb(const fat32_dirent_t *ent, void *ud) {
    lookup_ctx_t *c = (lookup_ctx_t *)ud;
    if (str_eq_ci(ent->name, c->want)) { c->found = *ent; c->hit = 1; return 1; }
    return 0;
}

/* 解析完整路径，得到最终条目。返回 0 成功。*/
static int resolve_path(const char *path, fat32_dirent_t *out) {
    if (!g_vol.mounted) return -1;
    /* 根目录 */
    if (!path || path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
        out->name[0] = '/'; out->name[1] = 0;
        out->size = 0; out->first_cluster = g_vol.root_cluster; out->is_dir = 1;
        return 0;
    }
    uint32_t cur_cluster = g_vol.root_cluster;
    fat32_dirent_t cur; cur.first_cluster = cur_cluster; cur.is_dir = 1;
    const char *p = path;
    while (*p == '/') p++;
    char seg[256];
    while (*p) {
        int s = 0;
        while (*p && *p != '/') { if (s < 255) seg[s++] = *p; p++; }
        seg[s] = 0;
        while (*p == '/') p++;
        if (s == 0) continue;
        lookup_ctx_t ctx; ctx.want = seg; ctx.hit = 0;
        walk_dir(cur.first_cluster, lookup_cb, &ctx);
        if (!ctx.hit) return -2;
        cur = ctx.found;
        if (*p && !cur.is_dir) return -3; /* 中间路径不是目录 */
    }
    *out = cur;
    return 0;
}

/* ============================================================
 * 对外 API
 * ============================================================ */
int fat32_list(const char *path,
               int (*cb)(const fat32_dirent_t *ent, void *ud),
               void *ud) {
    fat32_dirent_t d;
    if (resolve_path(path, &d) != 0) return -1;
    if (!d.is_dir) return -2;
    return walk_dir(d.first_cluster, cb, ud);
}

int fat32_stat(const char *path, fat32_dirent_t *out) {
    return resolve_path(path, out);
}

int fat32_read_file(const char *path, void *buf, uint32_t max) {
    fat32_dirent_t d;
    if (resolve_path(path, &d) != 0) return -1;
    if (d.is_dir) return -2;
    uint32_t remain = d.size;
    if (remain > max) remain = max;
    uint8_t *out = (uint8_t *)buf;
    uint8_t *clusbuf = (uint8_t *)arch_x86_64_kmalloc(g_vol.cluster_bytes);
    if (!clusbuf) return -3;
    uint32_t got = 0;
    uint32_t cluster = d.first_cluster;
    while (!is_eoc(cluster) && got < remain) {
        if (read_cluster(cluster, clusbuf) != 0) { arch_x86_64_kfree(clusbuf); return -4; }
        uint32_t chunk = g_vol.cluster_bytes;
        if (chunk > remain - got) chunk = remain - got;
        for (uint32_t i = 0; i < chunk; i++) out[got + i] = clusbuf[i];
        got += chunk;
        cluster = fat_next_cluster(cluster);
    }
    arch_x86_64_kfree(clusbuf);
    return (int)got;
}

/* ============================================================
 * 开机自检：验证列目录 / LFN / 子目录 / 读文件（阶段 4-1）
 * ============================================================ */
static void st_num(const char *k, int v) {
    /* 打印十进制小数（支持负数） */
    char buf[16]; int n = 0; int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) buf[n++] = '0';
    while (v > 0 && n < 12) { buf[n++] = '0' + (v % 10); v /= 10; }
    fs_log(k);
    char out[20]; int o = 0;
    if (neg) out[o++] = '-';
    for (int i = n - 1; i >= 0; i--) out[o++] = buf[i];
    out[o++] = '\n'; out[o] = 0;
    fs_log(out);
}

static int st_list_cb(const fat32_dirent_t *ent, void *ud) {
    (void)ud;
    fs_log("    ");
    fs_log(ent->is_dir ? "[D] " : "[F] ");
    fs_log(ent->name);
    fs_log("\n");
    return 0;
}

void fat32_selftest(void) {
    if (!g_vol.mounted) return;
    fs_log("[fat32][selftest] --- list / ---\n");
    int n = fat32_list("/", st_list_cb, 0);
    st_num("[fat32][selftest] root entries=", n);

    fs_log("[fat32][selftest] --- list /DOCS ---\n");
    int n2 = fat32_list("/DOCS", st_list_cb, 0);
    st_num("[fat32][selftest] DOCS entries=", n2);

    fs_log("[fat32][selftest] --- read /HELLO.TXT ---\n");
    static char rbuf[512];
    int rn = fat32_read_file("/HELLO.TXT", rbuf, sizeof(rbuf) - 1);
    if (rn >= 0) {
        rbuf[rn] = 0;
        st_num("[fat32][selftest] bytes=", rn);
        fs_log("----8<----\n");
        fs_log(rbuf);
        fs_log("\n---->8----\n");
    } else {
        st_num("[fat32][selftest] read failed rc=", rn);
    }

    fs_log("[fat32][selftest] --- read /DOCS/nested file.txt (LFN+subdir) ---\n");
    int rn2 = fat32_read_file("/DOCS/nested file.txt", rbuf, sizeof(rbuf) - 1);
    if (rn2 >= 0) {
        rbuf[rn2] = 0;
        st_num("[fat32][selftest] bytes=", rn2);
        fs_log("----8<----\n"); fs_log(rbuf); fs_log("\n---->8----\n");
    } else {
        st_num("[fat32][selftest] read failed rc=", rn2);
    }
    fs_log("[fat32][selftest] done\n");
}
