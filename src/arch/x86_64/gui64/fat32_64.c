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
    fat32_write_fn write_fn;    /* 阶段 4-3：写回调（NULL=只读） */
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

/* ============================================================
 * 阶段 4-3：写入底层 helper
 * ============================================================ */
static int is_eoc(uint32_t c); /* 前置声明 */

/* 写一个扇区（绝对 LBA） */
static int write_sector(uint32_t lba, const void *buf) {
    if (!g_vol.write_fn) return -1;
    return g_vol.write_fn(lba, 1, buf);
}

/* 写整簇（buf 至少 cluster_bytes 字节） */
static int write_cluster(uint32_t cluster, const void *buf) {
    if (!g_vol.write_fn) return -1;
    if (cluster < 2) return -1;
    uint32_t lba = g_vol.data_begin_lba +
                   (cluster - 2) * g_vol.sectors_per_cluster;
    return g_vol.write_fn(lba, g_vol.sectors_per_cluster, buf);
}

/* 写 FAT 表项：把 cluster 处的项设为 value，同步写所有 FAT 副本。返回 0 成功。*/
static int fat_set_entry(uint32_t cluster, uint32_t value) {
    if (!g_vol.write_fn) return -1;
    uint32_t fat_offset = cluster * 4;
    uint32_t sec_index  = fat_offset / SECTOR_SIZE;
    uint32_t ent_offset = fat_offset % SECTOR_SIZE;
    uint8_t sec[SECTOR_SIZE];
    /* 先读第一个 FAT 副本对应扇区 */
    uint32_t base_lba = g_vol.fat_begin_lba + sec_index;
    if (read_sector(base_lba, sec) != 0) return -1;
    /* 保留高 4 位 */
    uint32_t old = *(uint32_t *)(sec + ent_offset);
    *(uint32_t *)(sec + ent_offset) = (old & 0xF0000000) | (value & 0x0FFFFFFF);
    /* 写入所有 FAT 副本 */
    for (uint32_t f = 0; f < g_vol.num_fats; f++) {
        uint32_t lba = g_vol.fat_begin_lba + f * g_vol.fat_size_sectors + sec_index;
        if (write_sector(lba, sec) != 0) return -1;
    }
    return 0;
}

/* 扫描 FAT 找一个空闲簇（值=0），标记为 EOC 并返回簇号；0 表示失败 */
static uint32_t alloc_cluster(void) {
    if (!g_vol.write_fn) return 0;
    /* 总簇数上限：FAT 表项总数 */
    uint32_t total_entries = g_vol.fat_size_sectors * (SECTOR_SIZE / 4);
    uint8_t sec[SECTOR_SIZE];
    for (uint32_t s = 0; s < g_vol.fat_size_sectors; s++) {
        uint32_t lba = g_vol.fat_begin_lba + s;
        if (read_sector(lba, sec) != 0) return 0;
        for (uint32_t off = 0; off < SECTOR_SIZE; off += 4) {
            uint32_t clus = s * (SECTOR_SIZE / 4) + off / 4;
            if (clus < 2) continue;
            if (clus >= total_entries) return 0;
            uint32_t val = (*(uint32_t *)(sec + off)) & 0x0FFFFFFF;
            if (val == 0) {
                /* 标记为 EOC */
                if (fat_set_entry(clus, 0x0FFFFFFF) != 0) return 0;
                return clus;
            }
        }
    }
    return 0;
}

/* 释放簇链（从 start 开始直到 EOC，全部置 0） */
static void free_chain(uint32_t start) {
    uint32_t c = start;
    while (!is_eoc(c)) {
        uint32_t next = fat_next_cluster(c);
        fat_set_entry(c, 0);
        c = next;
    }
}

/* 清零一个簇的内容 */
static int zero_cluster(uint32_t cluster) {
    uint8_t *z = (uint8_t *)arch_x86_64_kmalloc(g_vol.cluster_bytes);
    if (!z) return -1;
    for (uint32_t i = 0; i < g_vol.cluster_bytes; i++) z[i] = 0;
    int rc = write_cluster(cluster, z);
    arch_x86_64_kfree(z);
    return rc;
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


/* ============================================================
 * 阶段 4-3：FAT32 写入支持
 * ============================================================ */

/* 设置扇区写回调，使能写入 */
void fat32_set_write_fn(fat32_write_fn fn) {
    g_vol.write_fn = fn;
}

/* 是否可写（已挂载且设置了写回调） */
int fat32_writable(void) {
    return g_vol.mounted && g_vol.write_fn != 0;
}

/* 把文件名（可含扩展名）转成 11 字节 8.3 短名（大写、空格填充）。
 * 仅支持 8.3：主名<=8，扩展<=3。成功返回 0，非法返回 -1。 */
static int make_short_name(const char *name, uint8_t out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, o = 0;
    while (name[i] && name[i] != '.') {
        if (o >= 8) return -1;
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[o++] = (uint8_t)c;
        i++;
    }
    if (name[i] == '.') {
        i++;
        int e = 8;
        while (name[i]) {
            if (e >= 11) return -1;
            char c = name[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[e++] = (uint8_t)c;
            i++;
        }
    }
    if (out[0] == ' ') return -1;
    return 0;
}

/* ============================================================
 * M3.1 LFN（长文件名）写入基础设施
 * ============================================================ */

/* LFN 校验和：由 11 字节 8.3 短名计算，绑定 LFN 项与短名项 */
static uint8_t lfn_checksum(const uint8_t sfn[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + sfn[i]);
    return sum;
}

/* 判断名字是否为“纯 8.3”（可直接用短名，无需 LFN）：
 * 主名<=8、扩展<=3，且不含小写、空格及非法字符，点<=1。 */
static int name_is_pure_83(const char *name) {
    int i = 0, base = 0, ext = 0, dots = 0, in_ext = 0;
    while (name[i]) {
        char c = name[i];
        if (c == '.') { dots++; in_ext = 1; i++; continue; }
        if (c >= 'a' && c <= 'z') return 0;      /* 小写 → 需 LFN */
        if (c == ' ') return 0;                   /* 空格 → 需 LFN */
        if (in_ext) { if (++ext > 3) return 0; }
        else        { if (++base > 8) return 0; }
        i++;
    }
    if (dots > 1) return 0;
    if (base == 0) return 0;
    return 1;
}

/* 生成唯一短名 NAME~N（当长名不满足 8.3 时）。dir_clus=父目录首簇，
 * 用于探测冲突。成功返回 0，填充 out[11]。 */
/* 目录项定位结果：物理簇号 + 簇内字节偏移 */
typedef struct {
    uint32_t clus;
    uint32_t off;
    int      found;
    uint32_t old_first;
    int      is_dir;
    int      free_clus_valid;
    uint32_t free_clus;
    uint32_t free_off;
} dirent_loc_t;

static int locate_dirent(uint32_t dir_first_clus, const uint8_t sfn[11], dirent_loc_t *loc); /* 前置声明 */

static int gen_short_name(const char *longname, uint32_t dir_clus, uint8_t out[11]) {
    /* 1) 提取基名（字母数字大写，去空格/点，取前 6） */
    uint8_t basis[8]; int bl = 0;
    uint8_t ext[3];   int el = 0;
    int last_dot = -1, i;
    for (i = 0; longname[i]; i++) if (longname[i] == '.') last_dot = i;
    for (i = 0; longname[i] && bl < 6; i++) {
        if (i == last_dot) break;
        char c = longname[i];
        if (c == ' ' || c == '.') continue;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '~')
            basis[bl++] = (uint8_t)c;
        else
            basis[bl++] = '_';
    }
    if (bl == 0) basis[bl++] = '_';
    if (last_dot >= 0) {
        for (i = last_dot + 1; longname[i] && el < 3; i++) {
            char c = longname[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                ext[el++] = (uint8_t)c;
        }
    }
    /* 2) 尝试 ~1 ... ~99，直到无冲突 */
    for (int n = 1; n <= 99; n++) {
        uint8_t sfn[11];
        for (int k = 0; k < 11; k++) sfn[k] = ' ';
        char nbuf[4]; int nl = 0;
        int tmp = n;
        char rev[3]; int rc = 0;
        while (tmp > 0) { rev[rc++] = (char)('0' + tmp % 10); tmp /= 10; }
        for (int k = rc - 1; k >= 0; k--) nbuf[nl++] = rev[k];
        /* 主名 = basis 前 (6 - (nl-? )) ... 简化：basis 截到 (7 - nl) + '~' + num */
        int keep = 7 - nl;          /* 供 basis 的字符数（含 ~ 前） */
        if (keep > bl) keep = bl;
        int p = 0;
        for (int k = 0; k < keep; k++) sfn[p++] = basis[k];
        sfn[p++] = '~';
        for (int k = 0; k < nl; k++) sfn[p++] = (uint8_t)nbuf[k];
        for (int k = 0; k < el; k++) sfn[8 + k] = ext[k];
        /* 探测冲突 */
        dirent_loc_t probe;
        if (locate_dirent(dir_clus, sfn, &probe) != 0 || !probe.found) {
            for (int k = 0; k < 11; k++) out[k] = sfn[k];
            return 0;
        }
    }
    return -1;
}

/* 在父目录簇链中查找短名 sfn，同时记录第一个空闲槽位置。 */
static int locate_dirent(uint32_t dir_first_clus, const uint8_t sfn[11],
                         dirent_loc_t *loc) {
    uint8_t *clusbuf = (uint8_t *)arch_x86_64_kmalloc(g_vol.cluster_bytes);
    if (!clusbuf) return -1;
    uint32_t per = g_vol.cluster_bytes;
    uint32_t cluster = dir_first_clus;
    loc->found = 0;
    loc->free_clus_valid = 0;
    loc->free_clus = 0;
    loc->free_off = 0;
    loc->clus = 0;
    loc->off = 0;
    loc->old_first = 0;
    loc->is_dir = 0;
    while (!is_eoc(cluster)) {
        if (read_cluster(cluster, clusbuf) != 0) { arch_x86_64_kfree(clusbuf); return -1; }
        for (uint32_t off = 0; off < per; off += 32) {
            fat_dir_ent_t *e = (fat_dir_ent_t *)(clusbuf + off);
            uint8_t c0 = e->name[0];
            if (c0 == 0x00) {
                if (!loc->free_clus_valid) {
                    loc->free_clus_valid = 1;
                    loc->free_clus = cluster;
                    loc->free_off = off;
                }
                arch_x86_64_kfree(clusbuf);
                return 0;
            }
            if (c0 == 0xE5) {
                if (!loc->free_clus_valid) {
                    loc->free_clus_valid = 1;
                    loc->free_clus = cluster;
                    loc->free_off = off;
                }
                continue;
            }
            if ((e->attr & 0x0F) == 0x0F) continue;
            if (e->attr & 0x08) continue;
            int match = 1;
            for (int k = 0; k < 11; k++) {
                if (e->name[k] != sfn[k]) { match = 0; break; }
            }
            if (match) {
                loc->found = 1;
                loc->clus = cluster;
                loc->off = off;
                loc->is_dir = (e->attr & 0x10) ? 1 : 0;
                loc->old_first = ((uint32_t)e->fst_clus_hi << 16) |
                                 e->fst_clus_lo;
                arch_x86_64_kfree(clusbuf);
                return 0;
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    arch_x86_64_kfree(clusbuf);
    return 0;
}

/* ============================================================
 * M3.1 多槽目录项写入（N×LFN + 1×8.3）
 * ============================================================ */

/* 在父目录（首簇 dir_clus）写入一组目录项：若 longname 非纯8.3则前置
 * 多个 LFN 项，末尾为 8.3 项（短名 sfn、属性 attr、首簇 first、大小 size）。
 * 需要 need 个连续空槽，不够则扩展目录簇。成功返回 0。 */
static int place_dir_entry(uint32_t dir_clus, const char *longname,
                           const uint8_t sfn[11], uint8_t attr,
                           uint32_t first, uint32_t size) {
    /* 计算 LFN 项数 */
    int name_len = 0;
    while (longname[name_len]) name_len++;
    int use_lfn = !name_is_pure_83(longname);
    int lfn_cnt = 0;
    if (use_lfn) lfn_cnt = (name_len + 12) / 13;   /* 每项 13 字符 */
    int need = lfn_cnt + 1;
    uint8_t csum = lfn_checksum(sfn);

    /* 本地构造所有项（need 个 32B） */
    uint32_t cb = g_vol.cluster_bytes;
    uint8_t *ents = (uint8_t*)arch_x86_64_kmalloc((uint32_t)need * 32);
    if (!ents) return -1;
    for (int i = 0; i < need * 32; i++) ents[i] = 0;

    /* LFN 项（倒序：seq n..1，n 带 0x40） */
    for (int e = 0; e < lfn_cnt; e++) {
        uint8_t *ep = ents + e * 32;
        int seq = lfn_cnt - e;                 /* 首项在前，序号最大 */
        ep[0] = (uint8_t)(seq | ((e == 0) ? 0x40 : 0));
        ep[11] = 0x0F;                          /* LFN 属性 */
        ep[12] = 0;
        ep[13] = csum;
        ep[26] = 0; ep[27] = 0;                 /* first cluster = 0 */
        /* 填充 13 个 UCS-2 字符 */
        int char_base = (seq - 1) * 13;
        static const int slots[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
        for (int k = 0; k < 13; k++) {
            int ci = char_base + k;
            uint16_t wc;
            if (ci < name_len) wc = (uint16_t)(uint8_t)longname[ci];
            else if (ci == name_len) wc = 0x0000;
            else wc = 0xFFFF;
            ep[slots[k]]     = (uint8_t)(wc & 0xFF);
            ep[slots[k] + 1] = (uint8_t)(wc >> 8);
        }
    }
    /* 8.3 项 */
    {
        uint8_t *ep = ents + lfn_cnt * 32;
        for (int k = 0; k < 11; k++) ep[k] = sfn[k];
        ep[11] = attr;
        ep[20] = (uint8_t)((first >> 16) & 0xFF);
        ep[21] = (uint8_t)((first >> 24) & 0xFF);
        ep[26] = (uint8_t)(first & 0xFF);
        ep[27] = (uint8_t)((first >> 8) & 0xFF);
        ep[28] = (uint8_t)(size & 0xFF);
        ep[29] = (uint8_t)((size >> 8) & 0xFF);
        ep[30] = (uint8_t)((size >> 16) & 0xFF);
        ep[31] = (uint8_t)((size >> 24) & 0xFF);
    }

    /* 在目录簇链中找 need 个连续空槽（同一簇内）。
     * 简化策略：逐簇扫描，在单簇内找连续 run；不够则扩展新簇。 */
    uint8_t *buf = (uint8_t*)arch_x86_64_kmalloc(cb);
    if (!buf) { arch_x86_64_kfree(ents); return -1; }
    uint32_t ents_per = cb / 32;
    uint32_t clus = dir_clus, prev = 0;
    while (clus >= 2 && clus < 0x0FFFFFF8) {
        if (read_cluster(clus, buf) != 0) { arch_x86_64_kfree(buf); arch_x86_64_kfree(ents); return -1; }
        for (uint32_t s = 0; s + (uint32_t)need <= ents_per; s++) {
            int ok = 1;
            for (int j = 0; j < need; j++) {
                uint8_t f = buf[(s + j) * 32];
                if (!(f == 0x00 || f == 0xE5)) { ok = 0; break; }
            }
            if (ok) {
                for (int j = 0; j < need; j++)
                    for (int b = 0; b < 32; b++)
                        buf[(s + j) * 32 + b] = ents[j * 32 + b];
                int rc = write_cluster(clus, buf);
                arch_x86_64_kfree(buf); arch_x86_64_kfree(ents);
                return rc;
            }
        }
        prev = clus;
        clus = fat_next_cluster(clus);
    }
    /* 扩展新簇 */
    uint32_t nc = alloc_cluster();
    if (nc == 0) { arch_x86_64_kfree(buf); arch_x86_64_kfree(ents); return -1; }
    fat_set_entry(prev, nc);
    fat_set_entry(nc, 0x0FFFFFFF);
    for (uint32_t i = 0; i < cb; i++) buf[i] = 0;
    for (int j = 0; j < need; j++)
        for (int b = 0; b < 32; b++)
            buf[j * 32 + b] = ents[j * 32 + b];
    int rc = write_cluster(nc, buf);
    arch_x86_64_kfree(buf); arch_x86_64_kfree(ents);
    return rc;
}

/* 按名字（长名或短名，大小写不敏感）在父目录中删除一组目录项（LFN+8.3），
 * 全部标记 0xE5。输出首簇/是否目录。未找到返回 -1，找到返回 0。 */
static int remove_dir_entry_by_name(uint32_t dir_clus, const char *name,
                                    uint32_t *out_first, int *out_is_dir) {
    uint32_t cb = g_vol.cluster_bytes;
    uint32_t ents = cb / 32;
    uint8_t *buf = (uint8_t*)arch_x86_64_kmalloc(cb);
    if (!buf) return -1;

    /* 记录当前名字 run 的 LFN 项位置（cluster,index） */
    uint32_t run_clus[24]; uint32_t run_idx[24]; int run_n = 0;
    char lfn_buf[256]; int lfn_have = 0;

    uint32_t clus = dir_clus;
    while (clus >= 2 && clus < 0x0FFFFFF8) {
        if (read_cluster(clus, buf) != 0) { arch_x86_64_kfree(buf); return -1; }
        for (uint32_t i = 0; i < ents; i++) {
            uint8_t *e = buf + i * 32;
            uint8_t f0 = e[0];
            if (f0 == 0x00) { arch_x86_64_kfree(buf); return -1; }
            if (f0 == 0xE5) { run_n = 0; lfn_have = 0; continue; }
            uint8_t attr = e[11];
            if (attr == 0x0F) {
                fat_lfn_ent_t *l = (fat_lfn_ent_t*)e;
                uint8_t ord = l->ord & 0x3F;
                int pos = (ord - 1) * 13;
                lfn_extract(l, lfn_buf, &pos);
                if (l->ord & 0x40) lfn_buf[ord * 13] = 0;
                if (run_n < 24) { run_clus[run_n] = clus; run_idx[run_n] = i; run_n++; }
                lfn_have = 1;
                continue;
            }
            if (attr & 0x08) { run_n = 0; lfn_have = 0; continue; } /* 卷标 */

            /* 8.3 项 */
            fat_dir_ent_t *d = (fat_dir_ent_t*)e;
            char nm[256];
            if (lfn_have) { int k=0; for(;lfn_buf[k]&&k<255;k++) nm[k]=lfn_buf[k]; nm[k]=0; }
            else fmt_short_name(d->name, nm);

            if (str_eq_ci(nm, name)) {
                if (out_first) *out_first = ((uint32_t)d->fst_clus_hi << 16) | d->fst_clus_lo;
                if (out_is_dir) *out_is_dir = (d->attr & 0x10) ? 1 : 0;
                /* 标记 8.3 项 */
                e[0] = 0xE5;
                if (write_cluster(clus, buf) != 0) { arch_x86_64_kfree(buf); return -1; }
                /* 标记所有 LFN 项（可能在不同簇） */
                for (int r = 0; r < run_n; r++) {
                    if (run_clus[r] == clus) {
                        /* 同簇：重新读写（buf 已被写回，重读） */
                        if (read_cluster(run_clus[r], buf) != 0) { arch_x86_64_kfree(buf); return -1; }
                        buf[run_idx[r]*32] = 0xE5;
                        if (write_cluster(run_clus[r], buf) != 0) { arch_x86_64_kfree(buf); return -1; }
                    } else {
                        uint8_t *b2 = (uint8_t*)arch_x86_64_kmalloc(cb);
                        if (!b2) { arch_x86_64_kfree(buf); return -1; }
                        if (read_cluster(run_clus[r], b2) != 0) { arch_x86_64_kfree(b2); arch_x86_64_kfree(buf); return -1; }
                        b2[run_idx[r]*32] = 0xE5;
                        int wr = write_cluster(run_clus[r], b2);
                        arch_x86_64_kfree(b2);
                        if (wr != 0) { arch_x86_64_kfree(buf); return -1; }
                    }
                }
                arch_x86_64_kfree(buf);
                return 0;
            }
            run_n = 0; lfn_have = 0;
        }
        clus = fat_next_cluster(clus);
    }
    arch_x86_64_kfree(buf);
    return -1;
}

/* 覆盖写入文件：路径 path，数据 data，长度 len。
 * 仅支持 8.3 短名；已存在则覆盖，不存在则在父目录创建。返回 0 成功。 */
int fat32_write_file(const char *path, const void *data, uint32_t len) {
    if (!fat32_writable()) return -1;
    if (!path || path[0] != '/') return -1;

    char parent[256];
    const char *fname = path;
    int last_slash = -1;
    for (int i = 0; path[i]; i++) if (path[i] == '/') last_slash = i;
    if (last_slash < 0) return -1;
    fname = path + last_slash + 1;
    if (fname[0] == 0) return -1;
    if (last_slash == 0) {
        parent[0] = '/'; parent[1] = 0;
    } else {
        int j = 0;
        for (; j < last_slash && j < 255; j++) parent[j] = path[j];
        parent[j] = 0;
    }

    uint8_t sfn[11];
    int pure83 = name_is_pure_83(fname);

    uint32_t parent_clus;
    if (parent[0] == '/' && parent[1] == 0) {
        parent_clus = g_vol.root_cluster;
    } else {
        fat32_dirent_t pd;
        if (resolve_path(parent, &pd) != 0) return -1;
        if (!pd.is_dir) return -1;
        parent_clus = pd.first_cluster;
        if (parent_clus == 0) parent_clus = g_vol.root_cluster;
    }

    /* 生成短名：纯8.3 直接转；否则生成 NAME~N */
    if (pure83) {
        if (make_short_name(fname, sfn) != 0) return -1;
    }

    /* 若已存在同名项，先删除旧的（释放旧簇链） */
    {
        uint32_t old_first = 0; int old_is_dir = 0;
        if (remove_dir_entry_by_name(parent_clus, fname, &old_first, &old_is_dir) == 0) {
            if (old_is_dir) return -1;          /* 同名目录，拒绝 */
            if (old_first >= 2) free_chain(old_first);
        }
    }
    if (!pure83) {
        if (gen_short_name(fname, parent_clus, sfn) != 0) return -1;
    }

    uint32_t first_clus = 0;
    if (len > 0) {
        uint32_t per = g_vol.cluster_bytes;
        uint32_t nclus = (len + per - 1) / per;
        uint32_t prev = 0;
        const uint8_t *src = (const uint8_t *)data;
        uint8_t *wbuf = (uint8_t *)arch_x86_64_kmalloc(per);
        if (!wbuf) return -1;
        uint32_t remaining = len;
        for (uint32_t c = 0; c < nclus; c++) {
            uint32_t nc = alloc_cluster();
            if (nc == 0) { if (first_clus) free_chain(first_clus); arch_x86_64_kfree(wbuf); return -1; }
            if (c == 0) first_clus = nc;
            else fat_set_entry(prev, nc);
            fat_set_entry(nc, 0x0FFFFFFF);
            uint32_t chunk = remaining < per ? remaining : per;
            for (uint32_t k = 0; k < per; k++)
                wbuf[k] = (k < chunk) ? src[k] : 0;
            if (write_cluster(nc, wbuf) != 0) {
                free_chain(first_clus); arch_x86_64_kfree(wbuf); return -1;
            }
            src += chunk;
            remaining -= chunk;
            prev = nc;
        }
        fat_set_entry(prev, 0x0FFFFFFF);
        arch_x86_64_kfree(wbuf);
    }

    if (place_dir_entry(parent_clus, fname, sfn, 0x20, first_clus, len) != 0) {
        if (first_clus) free_chain(first_clus);
        return -1;
    }

    fs_log("[fat32] wrote file OK\n");
    return 0;
}

/* ============================================================
 * M3.1 fat32_mkdir / fat32_delete
 * ============================================================ */

/* 创建目录：分配一个新簇作为目录内容簇，写入 . 与 .. 项，
 * 在父目录中挂 LFN+8.3（attr=0x10）。 */
int fat32_mkdir(const char *path) {
    if (!g_vol.mounted || !fat32_writable()) return -1;
    if (!path || path[0] != '/') return -1;

    /* 拆分父路径与名字 */
    char parent[256]; char fname[256];
    int plen = 0; while (path[plen]) plen++;
    int last_slash = -1;
    for (int i = 0; i < plen; i++) if (path[i] == '/') last_slash = i;
    if (last_slash < 0) return -1;
    int j = 0;
    for (; j < last_slash && j < 255; j++) parent[j] = path[j];
    parent[j] = 0;
    if (parent[0] == 0) { parent[0] = '/'; parent[1] = 0; }
    int k = 0;
    for (int i = last_slash + 1; path[i] && k < 255; i++) fname[k++] = path[i];
    fname[k] = 0;
    if (fname[0] == 0) return -1;

    uint32_t parent_clus;
    if (parent[0] == '/' && parent[1] == 0) {
        parent_clus = g_vol.root_cluster;
    } else {
        fat32_dirent_t pd;
        if (resolve_path(parent, &pd) != 0) return -1;
        if (!pd.is_dir) return -1;
        parent_clus = pd.first_cluster;
        if (parent_clus == 0) parent_clus = g_vol.root_cluster;
    }

    /* 同名已存在？ */
    {
        fat32_dirent_t ex;
        if (resolve_path(path, &ex) == 0) return -1;
    }

    /* 分配目录内容簇并清零 */
    uint32_t dclus = alloc_cluster();
    if (dclus == 0) return -1;
    if (zero_cluster(dclus) != 0) { free_chain(dclus); return -1; }
    fat_set_entry(dclus, 0x0FFFFFFF);

    /* 写入 . 与 .. 两项 */
    uint32_t cb = g_vol.cluster_bytes;
    uint8_t *buf = (uint8_t*)arch_x86_64_kmalloc(cb);
    if (!buf) { free_chain(dclus); return -1; }
    for (uint32_t i = 0; i < cb; i++) buf[i] = 0;
    /* "." 首簇 = dclus */
    fat_dir_ent_t *dot = (fat_dir_ent_t*)buf;
    for (int i = 0; i < 11; i++) dot->name[i] = ' ';
    dot->name[0] = '.';
    dot->attr = 0x10;
    dot->fst_clus_hi = (uint16_t)((dclus >> 16) & 0xFFFF);
    dot->fst_clus_lo = (uint16_t)(dclus & 0xFFFF);
    /* ".." 首簇 = parent_clus（根目录约定为 0） */
    fat_dir_ent_t *dotdot = (fat_dir_ent_t*)(buf + 32);
    for (int i = 0; i < 11; i++) dotdot->name[i] = ' ';
    dotdot->name[0] = '.'; dotdot->name[1] = '.';
    dotdot->attr = 0x10;
    uint32_t pc = (parent_clus == g_vol.root_cluster) ? 0 : parent_clus;
    dotdot->fst_clus_hi = (uint16_t)((pc >> 16) & 0xFFFF);
    dotdot->fst_clus_lo = (uint16_t)(pc & 0xFFFF);
    if (write_cluster(dclus, buf) != 0) { arch_x86_64_kfree(buf); free_chain(dclus); return -1; }
    arch_x86_64_kfree(buf);

    /* 在父目录挂载目录项 */
    uint8_t sfn[11];
    if (name_is_pure_83(fname)) {
        if (make_short_name(fname, sfn) != 0) { free_chain(dclus); return -1; }
    } else {
        if (gen_short_name(fname, parent_clus, sfn) != 0) { free_chain(dclus); return -1; }
    }
    if (place_dir_entry(parent_clus, fname, sfn, 0x10, dclus, 0) != 0) {
        free_chain(dclus);
        return -1;
    }
    fs_log("[fat32] mkdir OK\n");
    return 0;
}

/* 删除文件或空目录：释放簇链 + 标记目录项 0xE5。
 * 非空目录拒绝。 */
int fat32_delete(const char *path) {
    if (!g_vol.mounted || !fat32_writable()) return -1;
    if (!path || path[0] != '/') return -1;

    /* 先解析目标 */
    fat32_dirent_t de;
    if (resolve_path(path, &de) != 0) return -1;

    /* 拆分父路径与名字 */
    char parent[256]; char fname[256];
    int plen = 0; while (path[plen]) plen++;
    int last_slash = -1;
    for (int i = 0; i < plen; i++) if (path[i] == '/') last_slash = i;
    if (last_slash < 0) return -1;
    int j = 0;
    for (; j < last_slash && j < 255; j++) parent[j] = path[j];
    parent[j] = 0;
    if (parent[0] == 0) { parent[0] = '/'; parent[1] = 0; }
    int k = 0;
    for (int i = last_slash + 1; path[i] && k < 255; i++) fname[k++] = path[i];
    fname[k] = 0;
    if (fname[0] == 0) return -1;

    uint32_t parent_clus;
    if (parent[0] == '/' && parent[1] == 0) {
        parent_clus = g_vol.root_cluster;
    } else {
        fat32_dirent_t pd;
        if (resolve_path(parent, &pd) != 0) return -1;
        parent_clus = pd.first_cluster;
        if (parent_clus == 0) parent_clus = g_vol.root_cluster;
    }

    /* 若为目录，确保为空（除 . 与 ..） */
    if (de.is_dir) {
        uint32_t cb = g_vol.cluster_bytes;
        uint32_t ents = cb / 32;
        uint8_t *buf = (uint8_t*)arch_x86_64_kmalloc(cb);
        if (!buf) return -1;
        uint32_t c = de.first_cluster;
        int has_child = 0;
        while (c >= 2 && c < 0x0FFFFFF8 && !has_child) {
            if (read_cluster(c, buf) != 0) { arch_x86_64_kfree(buf); return -1; }
            for (uint32_t i = 0; i < ents; i++) {
                uint8_t f0 = buf[i*32];
                if (f0 == 0x00) { has_child = 0; goto done_scan; }
                if (f0 == 0xE5) continue;
                uint8_t attr = buf[i*32 + 11];
                if (attr == 0x0F) continue;      /* LFN 项 */
                /* 跳过 . 与 .. */
                if (buf[i*32] == '.' &&
                    (buf[i*32+1] == ' ' || (buf[i*32+1] == '.' && buf[i*32+2] == ' ')))
                    continue;
                has_child = 1; break;
            }
            c = fat_next_cluster(c);
        }
done_scan:
        arch_x86_64_kfree(buf);
        if (has_child) { fs_log("[fat32] rmdir: not empty\n"); return -1; }
    }

    /* 从父目录删除目录项（含 LFN） */
    uint32_t first = 0; int is_dir = 0;
    if (remove_dir_entry_by_name(parent_clus, fname, &first, &is_dir) != 0) return -1;
    /* 释放簇链 */
    if (first >= 2) free_chain(first);
    fs_log("[fat32] delete OK\n");
    return 0;
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
    /* --- 阶段 4-3：写入自检 --- */
    if (fat32_writable()) {
        fs_log("[fat32][selftest] --- write /OSWRITE.TXT ---\n");
        const char *msg = "Hello from OPENOS FAT32 writer!\n";
        uint32_t mlen = 0; while (msg[mlen]) mlen++;
        int wrc = fat32_write_file("/OSWRITE.TXT", msg, mlen);
        st_num("[fat32][selftest] write rc=", wrc);
        if (wrc == 0) {
            int back = fat32_read_file("/OSWRITE.TXT", rbuf, sizeof(rbuf) - 1);
            if (back >= 0) {
                rbuf[back] = 0;
                st_num("[fat32][selftest] readback bytes=", back);
                fs_log("----8<----\n"); fs_log(rbuf); fs_log("\n---->8----\n");
                if (back == (int)mlen) fs_log("[fat32][selftest] WRITE VERIFY OK\n");
                else fs_log("[fat32][selftest] WRITE VERIFY size mismatch\n");
            } else {
                st_num("[fat32][selftest] readback failed rc=", back);
            }
        }

        /* --- M3.1: LFN 长文件名写入 --- */
        fs_log("[fat32][selftest] --- M3.1 write LFN 'My Long File Name.txt' ---\n");
        const char *lmsg = "long filename payload OK\n";
        uint32_t llen = 0; while (lmsg[llen]) llen++;
        int lwrc = fat32_write_file("/My Long File Name.txt", lmsg, llen);
        st_num("[fat32][selftest] LFN write rc=", lwrc);
        if (lwrc == 0) {
            int lb = fat32_read_file("/My Long File Name.txt", rbuf, sizeof(rbuf) - 1);
            if (lb >= 0) {
                rbuf[lb] = 0;
                st_num("[fat32][selftest] LFN readback bytes=", lb);
                fs_log("----8<----\n"); fs_log(rbuf); fs_log("\n---->8----\n");
                if (lb == (int)llen) fs_log("[fat32][selftest] LFN VERIFY OK\n");
                else fs_log("[fat32][selftest] LFN VERIFY size mismatch\n");
            } else st_num("[fat32][selftest] LFN readback failed rc=", lb);
        }

        /* --- M3.1: mkdir + 子目录写文件 --- */
        fs_log("[fat32][selftest] --- M3.1 mkdir /NEWDIR ---\n");
        int mrc = fat32_mkdir("/NEWDIR");
        st_num("[fat32][selftest] mkdir rc=", mrc);
        if (mrc == 0) {
            const char *smsg = "file inside newdir\n";
            uint32_t slen = 0; while (smsg[slen]) slen++;
            int swrc = fat32_write_file("/NEWDIR/inside.txt", smsg, slen);
            st_num("[fat32][selftest] subdir write rc=", swrc);
            if (swrc == 0) {
                int sb = fat32_read_file("/NEWDIR/inside.txt", rbuf, sizeof(rbuf) - 1);
                if (sb >= 0) {
                    rbuf[sb] = 0;
                    st_num("[fat32][selftest] subdir readback bytes=", sb);
                    if (sb == (int)slen) fs_log("[fat32][selftest] MKDIR+WRITE VERIFY OK\n");
                    else fs_log("[fat32][selftest] MKDIR+WRITE size mismatch\n");
                } else st_num("[fat32][selftest] subdir readback failed rc=", sb);
            }
        }

        /* --- M3.1: delete 文件 --- */
        fs_log("[fat32][selftest] --- M3.1 delete /OSWRITE.TXT ---\n");
        int drc = fat32_delete("/OSWRITE.TXT");
        st_num("[fat32][selftest] delete rc=", drc);
        if (drc == 0) {
            int chk = fat32_read_file("/OSWRITE.TXT", rbuf, sizeof(rbuf) - 1);
            if (chk < 0) fs_log("[fat32][selftest] DELETE VERIFY OK (gone)\n");
            else fs_log("[fat32][selftest] DELETE VERIFY FAIL (still exists)\n");
        }

        /* --- M3.1: delete 空目录（先删子文件再删目录） --- */
        fs_log("[fat32][selftest] --- M3.1 rmdir /NEWDIR ---\n");
        fat32_delete("/NEWDIR/inside.txt");
        int rdrc = fat32_delete("/NEWDIR");
        st_num("[fat32][selftest] rmdir rc=", rdrc);
        if (rdrc == 0) fs_log("[fat32][selftest] RMDIR VERIFY OK\n");
    }

    fs_log("[fat32][selftest] done\n");
}
