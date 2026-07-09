/* ============================================================
 * openos x86_64 RAMFS —— 内存树形可读写文件系统
 *
 * 取代 gui64_stubs.c 中基于 initrd 全路径前缀匹配的只读桩。
 * 提供真正的目录树 (parent/child/sibling)，文件数据动态分配，
 * 支持 open/read/write/seek/close/truncate/mkdir/rmdir/unlink/
 * readdir/stat/rename。对上导出 gui.c 依赖的 vfs_* 接口。
 * ============================================================ */

#include "ramfs64.h"
#include "ata64.h"
#include "initrd64.h"

/* ---- 外部依赖 ---- */
extern void *arch_x86_64_kmalloc(unsigned long size);
extern void  arch_x86_64_kfree(void *ptr);
extern const x86_64_initrd_image_t *arch_x86_64_initrd_get_image(void);
extern void  early_serial64_write(const char *text);
#define ramfs_log(s) early_serial64_write((s))

#define RAMFS_KMALLOC(sz) arch_x86_64_kmalloc((unsigned long)(sz))
#define RAMFS_KFREE(p)    arch_x86_64_kfree((void *)(p))

/* ---- 节点池 ---- */
#define RAMFS_MAX_NODES   512   /* inode + dentry 成对，最多 512 个文件/目录 */

typedef struct ramfs_node {
    inode_t   inode;            /* 复用 core/fs/vfs.h 的 inode_t（含 mode/size 等）*/
    dentry_t  dentry;           /* 复用 dentry_t（含 name/inode 指针）*/
    struct ramfs_node *parent;  /* 父目录节点，根节点为 NULL */
    struct ramfs_node *child;   /* 第一个子节点（仅目录有效）*/
    struct ramfs_node *sibling; /* 下一个兄弟节点 */
    char     *data;             /* 文件数据缓冲（kmalloc），目录为 NULL */
    unsigned long cap;          /* data 缓冲容量 */
    int       used;             /* 该槽是否被占用 */
} ramfs_node_t;

static ramfs_node_t g_nodes[RAMFS_MAX_NODES];
static ramfs_node_t *g_root = 0;   /* 根目录 "/" */
static int g_ramfs_ready = 0;

/* ---- 小工具：字符串 ---- */
static unsigned long rstrlen(const char *s) {
    unsigned long n = 0; if (!s) return 0; while (s[n]) n++; return n;
}
static void rstrncpy(char *dst, const char *src, unsigned long cap) {
    unsigned long i = 0;
    if (!dst || cap == 0) return;
    if (src) { for (; src[i] && i + 1 < cap; i++) dst[i] = src[i]; }
    dst[i] = 0;
}
static void rmemcpy(void *d, const void *s, unsigned long n) {
    unsigned char *dd = (unsigned char *)d; const unsigned char *ss = (const unsigned char *)s;
    for (unsigned long i = 0; i < n; i++) dd[i] = ss[i];
}
static void rmemset(void *d, int c, unsigned long n) {
    unsigned char *dd = (unsigned char *)d;
    for (unsigned long i = 0; i < n; i++) dd[i] = (unsigned char)c;
}

/* ---- 节点分配/释放 ---- */
static ramfs_node_t *node_alloc(void) {
    for (int i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!g_nodes[i].used) {
            ramfs_node_t *n = &g_nodes[i];
            rmemset(n, 0, sizeof(*n));
            n->used = 1;
            return n;
        }
    }
    return 0;
}

static void node_free(ramfs_node_t *n) {
    if (!n) return;
    if (n->data) { RAMFS_KFREE(n->data); n->data = 0; }
    n->cap = 0;
    n->used = 0;
}

/* ---- 建立一个节点，挂到 parent 下 ---- */
static ramfs_node_t *node_create(ramfs_node_t *parent, const char *name, uint32_t mode) {
    ramfs_node_t *n = node_alloc();
    if (!n) return 0;
    rstrncpy(n->dentry.name, name, MAX_NAME);
    n->dentry.inode = &n->inode;
    n->inode.mode = mode;
    n->inode.size = 0;
    n->inode.nlinks = 1;
    n->parent  = parent;
    n->child   = 0;
    n->sibling = 0;
    n->data    = 0;
    n->cap     = 0;
    if (parent) {
        /* 挂到 parent 子链表尾部 */
        if (!parent->child) {
            parent->child = n;
        } else {
            ramfs_node_t *c = parent->child;
            while (c->sibling) c = c->sibling;
            c->sibling = n;
        }
    }
    return n;
}

/* ---- 从 parent 子链表中摘除 n ---- */
static void node_detach(ramfs_node_t *n) {
    ramfs_node_t *p = n ? n->parent : 0;
    if (!p) return;
    if (p->child == n) { p->child = n->sibling; return; }
    ramfs_node_t *c = p->child;
    while (c && c->sibling != n) c = c->sibling;
    if (c) c->sibling = n->sibling;
}

/* ---- 在目录 dir 下按名字查子节点 ---- */
static ramfs_node_t *dir_lookup(ramfs_node_t *dir, const char *name, unsigned long nlen) {
    if (!dir) return 0;
    for (ramfs_node_t *c = dir->child; c; c = c->sibling) {
        if (rstrlen(c->dentry.name) == nlen) {
            int eq = 1;
            for (unsigned long i = 0; i < nlen; i++) {
                if (c->dentry.name[i] != name[i]) { eq = 0; break; }
            }
            if (eq) return c;
        }
    }
    return 0;
}

/* ---- 路径解析：按 '/' 逐级查找。
 * 返回目标节点，找不到返回 NULL。
 * 若 want_parent!=0，则返回最后一级的父目录，并把最后一段名字写入 last_name。 ---- */
static ramfs_node_t *path_resolve(const char *path, int want_parent,
                                  char *last_name, unsigned long last_cap) {
    if (!path || !g_root) return 0;
    /* 跳过前导 '/' */
    const char *p = path;
    while (*p == '/') p++;
    ramfs_node_t *cur = g_root;
    if (*p == 0) {
        /* 纯 "/" */
        if (want_parent) return 0; /* 根无父 */
        return g_root;
    }
    while (*p) {
        /* 取一段 */
        const char *seg = p;
        unsigned long slen = 0;
        while (p[slen] && p[slen] != '/') slen++;
        /* 跳到下一段起点 */
        const char *next = seg + slen;
        while (*next == '/') next++;
        int is_last = (*next == 0);

        if (is_last && want_parent) {
            if (last_name && last_cap) {
                unsigned long i = 0;
                for (; i < slen && i + 1 < last_cap; i++) last_name[i] = seg[i];
                last_name[i] = 0;
            }
            return cur; /* cur 即父目录 */
        }

        /* 处理 . 和 .. */
        if (slen == 1 && seg[0] == '.') {
            /* 当前目录，不动 */
        } else if (slen == 2 && seg[0] == '.' && seg[1] == '.') {
            if (cur->parent) cur = cur->parent;
        } else {
            ramfs_node_t *nx = dir_lookup(cur, seg, slen);
            if (!nx) return 0;
            cur = nx;
        }

        p = next;
        if (is_last) break;
    }
    return cur;
}

/* ---- 判断节点是否目录 ---- */
static int node_is_dir(ramfs_node_t *n) {
    return n && (n->inode.mode & FS_DIR);
}

/* ============================================================
 * 文件描述符表
 * ============================================================ */
#define RAMFS_MAX_FD  64

typedef struct {
    int           used;
    ramfs_node_t *node;   /* 指向 RAMFS 节点 */
    unsigned long pos;    /* 当前读写偏移 */
    int           flags;  /* 打开标志 */
} ramfs_file_t;

static ramfs_file_t g_fds[RAMFS_MAX_FD];

static int fd_alloc(ramfs_node_t *node, int flags) {
    for (int i = 0; i < RAMFS_MAX_FD; i++) {
        if (!g_fds[i].used) {
            g_fds[i].used  = 1;
            g_fds[i].node  = node;
            g_fds[i].pos   = 0;
            g_fds[i].flags = flags;
            return i;
        }
    }
    return -1;
}

static ramfs_file_t *fd_get(int fd) {
    if (fd < 0 || fd >= RAMFS_MAX_FD) return 0;
    if (!g_fds[fd].used) return 0;
    return &g_fds[fd];
}

/* ============================================================
 * FAT32 挂载点分发层（阶段 4-2：只读接入用户终端）
 *   挂载点：/mnt/fat  → 转发到 fat32_64 驱动
 *   - vfs_open   ：把整文件读进 kmalloc 缓冲，返回 fat fd
 *   - vfs_read   ：从缓冲返回
 *   - vfs_readdir：fat32_list
 *   - vfs_stat   ：fat32_stat
 * ============================================================ */
#include "fat32_64.h"
#include "ext4_64.h"

#define FAT_MNT      "/mnt/fat"
#define FAT_MNT_LEN  8
#define RAMFS_FAT_FD_BASE 4096   /* fat fd 与 ramfs fd 区分：>=4096 */
#define RAMFS_MAX_FAT_FD  16

typedef struct {
    int           used;
    char         *data;    /* 整文件缓冲 */
    unsigned long size;    /* 文件字节数 */
    unsigned long pos;     /* 当前读偏移 */
} ramfs_fat_fd_t;

static ramfs_fat_fd_t g_fat_fds[RAMFS_MAX_FAT_FD];

/* 判断 path 是否落在 /mnt/fat 挂载点下。
 * 返回：0=不是；1=正好是挂载点根；2=挂载点下的子路径（*sub 指向 fat 内部路径，以 '/' 开头） */
static int fat_match(const char *path, const char **sub) {
    if (!path) return 0;
    /* 前缀匹配 /mnt/fat */
    for (int i = 0; i < FAT_MNT_LEN; i++) {
        if (path[i] != FAT_MNT[i]) return 0;
    }
    char c = path[FAT_MNT_LEN];
    if (c == 0) { if (sub) *sub = "/"; return 1; }          /* 正好 /mnt/fat */
    if (c == '/') { if (sub) *sub = path + FAT_MNT_LEN; return 2; } /* /mnt/fat/... */
    return 0; /* 例如 /mnt/fatx 之类，不匹配 */
}

static int fat_fd_alloc(void) {
    for (int i = 0; i < RAMFS_MAX_FAT_FD; i++) {
        if (!g_fat_fds[i].used) { g_fat_fds[i].used = 1; return i; }
    }
    return -1;
}

static ramfs_fat_fd_t *fat_fd_get(int fd) {
    if (fd < RAMFS_FAT_FD_BASE) return 0;
    int i = fd - RAMFS_FAT_FD_BASE;
    if (i < 0 || i >= RAMFS_MAX_FAT_FD) return 0;
    if (!g_fat_fds[i].used) return 0;
    return &g_fat_fds[i];
}

/* fat32_list 回调：抓取第 want 个条目。 */
typedef struct {
    int             want;
    int             cur;
    fat32_dirent_t *out;
    int            *found;
} fat_pick_ctx_t;

static int fat_pick_cb(const fat32_dirent_t *ent, void *ud) {
    fat_pick_ctx_t *c = (fat_pick_ctx_t *)ud;
    if (c->cur == c->want) {
        *(c->out)   = *ent;
        *(c->found) = 1;
        return 1; /* 停止遍历 */
    }
    c->cur++;
    return 0;
}

/* 取目录 path 的第 index 个条目；命中则 *found=1 并填 out。 */
static void fat32_list_pick(const char *path, int index,
                            fat32_dirent_t *out, int *found) {
    fat_pick_ctx_t c;
    c.want = index; c.cur = 0; c.out = out; c.found = found;
    *found = 0;
    fat32_list(path, fat_pick_cb, &c);
}

/* ============================================================
 * ext2/ext4 挂载点分发（M3.3 统一 VFS 多类型挂载）
 * 与 /mnt/fat 完全对称：/mnt/ext 路由到 ext4_64 只读驱动。
 * ext fd 使用独立编号区间（>=8192），与 ramfs(<4096)、fat([4096,8192)) 区分。
 * ============================================================ */
#define EXT_MNT      "/mnt/ext"
#define EXT_MNT_LEN  8
#define RAMFS_EXT_FD_BASE 8192
#define RAMFS_MAX_EXT_FD  16

typedef struct {
    int           used;
    char         *data;    /* 整文件缓冲 */
    unsigned long size;    /* 文件字节数 */
    unsigned long pos;     /* 当前读偏移 */
} ramfs_ext_fd_t;

static ramfs_ext_fd_t g_ext_fds[RAMFS_MAX_EXT_FD];

/* 判断 path 是否落在 /mnt/ext 挂载点下（语义同 fat_match）。 */
static int ext_match(const char *path, const char **sub) {
    if (!path) return 0;
    for (int i = 0; i < EXT_MNT_LEN; i++) {
        if (path[i] != EXT_MNT[i]) return 0;
    }
    char c = path[EXT_MNT_LEN];
    if (c == 0) { if (sub) *sub = "/"; return 1; }
    if (c == '/') { if (sub) *sub = path + EXT_MNT_LEN; return 2; }
    return 0;
}

static int ext_fd_alloc(void) {
    for (int i = 0; i < RAMFS_MAX_EXT_FD; i++) {
        if (!g_ext_fds[i].used) { g_ext_fds[i].used = 1; return i; }
    }
    return -1;
}

static ramfs_ext_fd_t *ext_fd_get(int fd) {
    if (fd < RAMFS_EXT_FD_BASE) return 0;
    int i = fd - RAMFS_EXT_FD_BASE;
    if (i < 0 || i >= RAMFS_MAX_EXT_FD) return 0;
    if (!g_ext_fds[i].used) return 0;
    return &g_ext_fds[i];
}

/* ext4_list 回调：抓取第 want 个条目。 */
typedef struct {
    int             want;
    int             cur;
    ext4_dirent_t  *out;
    int            *found;
} ext_pick_ctx_t;

static int ext_pick_cb(const ext4_dirent_t *ent, void *ud) {
    ext_pick_ctx_t *c = (ext_pick_ctx_t *)ud;
    if (c->cur == c->want) {
        *(c->out)   = *ent;
        *(c->found) = 1;
        return 1;
    }
    c->cur++;
    return 0;
}

static void ext4_list_pick(const char *path, int index,
                           ext4_dirent_t *out, int *found) {
    ext_pick_ctx_t c;
    c.want = index; c.cur = 0; c.out = out; c.found = found;
    *found = 0;
    ext4_list(path, ext_pick_cb, &c);
}

/* ---- 确保文件数据缓冲至少有 need 字节容量（按块扩容）---- */
static int file_ensure_cap(ramfs_node_t *n, unsigned long need) {
    if (need <= n->cap) return 0;
    /* 向上取整到 64 字节的整数倍，减少频繁 realloc */
    unsigned long newcap = (need + 63UL) & ~63UL;
    if (newcap < 64UL) newcap = 64UL;
    char *nb = (char *)RAMFS_KMALLOC(newcap);
    if (!nb) return -1;
    rmemset(nb, 0, newcap);
    if (n->data && n->inode.size > 0) {
        rmemcpy(nb, n->data, n->inode.size);
    }
    if (n->data) RAMFS_KFREE(n->data);
    n->data = nb;
    n->cap  = newcap;
    return 0;
}

/* ============================================================
 * VFS 文件接口实现
 * ============================================================ */

int vfs_open(const char *path, int flags, int mode) {
    (void)mode;
    if (!path || !g_ramfs_ready) return -1;

    /* ---- FAT32 挂载点分发（只读）---- */
    {
        const char *sub = 0;
        int m = fat_match(path, &sub);
        if (m && fat32_mounted()) {
            if (m == 1) return -1; /* /mnt/fat 本身是目录，不能当文件打开 */
            /* 不支持写入 */
            if ((flags & O_CREAT) || (flags & O_TRUNC) || ((flags & 3) != O_RDONLY))
                return -1;
            fat32_dirent_t st;
            if (fat32_stat(sub, &st) != 0) return -1;
            if (st.is_dir) return -1;
            int slot = fat_fd_alloc();
            if (slot < 0) return -1;
            ramfs_fat_fd_t *ff = &g_fat_fds[slot];
            ff->size = st.size;
            ff->pos  = 0;
            ff->data = 0;
            if (st.size > 0) {
                ff->data = (char *)RAMFS_KMALLOC(st.size);
                if (!ff->data) { ff->used = 0; return -1; }
                int rn = fat32_read_file(sub, ff->data, (uint32_t)st.size);
                if (rn < 0) { RAMFS_KFREE(ff->data); ff->data = 0; ff->used = 0; return -1; }
                ff->size = (unsigned long)rn;
            }
            return RAMFS_FAT_FD_BASE + slot;
        }
    }

    /* ---- ext2/ext4 挂载点分发（只读）---- */
    {
        const char *sub = 0;
        int m = ext_match(path, &sub);
        if (m && ext4_mounted()) {
            if (m == 1) return -1; /* /mnt/ext 本身是目录，不能当文件打开 */
            if ((flags & O_CREAT) || (flags & O_TRUNC) || ((flags & 3) != O_RDONLY))
                return -1; /* ext 驱动为只读 */
            ext4_dirent_t st;
            if (ext4_stat(sub, &st) != 0) return -1;
            if (st.is_dir) return -1;
            int slot = ext_fd_alloc();
            if (slot < 0) return -1;
            ramfs_ext_fd_t *ef = &g_ext_fds[slot];
            ef->size = st.size;
            ef->pos  = 0;
            ef->data = 0;
            if (st.size > 0) {
                ef->data = (char *)RAMFS_KMALLOC(st.size);
                if (!ef->data) { ef->used = 0; return -1; }
                int rn = ext4_read_file(sub, ef->data, (uint32_t)st.size);
                if (rn < 0) { RAMFS_KFREE(ef->data); ef->data = 0; ef->used = 0; return -1; }
                ef->size = (unsigned long)rn;
            }
            return RAMFS_EXT_FD_BASE + slot;
        }
    }

    ramfs_node_t *n = (ramfs_node_t *)0;
    /* 先尝试直接解析 */
    {
        ramfs_node_t *tgt = path_resolve(path, 0, 0, 0);
        n = tgt;
    }

    if (!n) {
        /* 不存在：若带 O_CREAT 则创建普通文件 */
        if (flags & O_CREAT) {
            char name[MAX_NAME];
            ramfs_node_t *parent = path_resolve(path, 1, name, sizeof(name));
            if (!parent || !node_is_dir(parent)) return -1;
            if (name[0] == 0) return -1;
            n = node_create(parent, name, FS_FILE | 0644);
            if (!n) return -1;
        } else {
            return -1;
        }
    }

    if (node_is_dir(n)) return -1; /* 不能以文件方式打开目录 */

    /* O_TRUNC：清空内容 */
    if (flags & O_TRUNC) {
        n->inode.size = 0;
    }

    return fd_alloc(n, flags);
}

int vfs_close(int fd) {
    ramfs_fat_fd_t *ff = fat_fd_get(fd);
    if (ff) {
        if (ff->data) RAMFS_KFREE(ff->data);
        ff->data = 0; ff->size = 0; ff->pos = 0; ff->used = 0;
        return 0;
    }
    ramfs_ext_fd_t *ef = ext_fd_get(fd);
    if (ef) {
        if (ef->data) RAMFS_KFREE(ef->data);
        ef->data = 0; ef->size = 0; ef->pos = 0; ef->used = 0;
        return 0;
    }
    ramfs_file_t *f = fd_get(fd);
    if (!f) return -1;
    f->used = 0;
    f->node = 0;
    f->pos  = 0;
    return 0;
}

int vfs_read(int fd, void *buf, uint32_t count) {
    ramfs_fat_fd_t *ff = fat_fd_get(fd);
    if (ff) {
        if (!buf) return -1;
        if (ff->pos >= ff->size) return 0;
        unsigned long avail = ff->size - ff->pos;
        unsigned long want  = count;
        if (want > avail) want = avail;
        if (want > 0 && ff->data) rmemcpy(buf, ff->data + ff->pos, want);
        ff->pos += want;
        return (int)want;
    }
    ramfs_ext_fd_t *ef = ext_fd_get(fd);
    if (ef) {
        if (!buf) return -1;
        if (ef->pos >= ef->size) return 0;
        unsigned long avail = ef->size - ef->pos;
        unsigned long want  = count;
        if (want > avail) want = avail;
        if (want > 0 && ef->data) rmemcpy(buf, ef->data + ef->pos, want);
        ef->pos += want;
        return (int)want;
    }
    ramfs_file_t *f = fd_get(fd);
    if (!f || !buf) return -1;
    ramfs_node_t *n = f->node;
    if (!n || node_is_dir(n)) return -1;
    unsigned long sz = n->inode.size;
    if (f->pos >= sz) return 0;
    unsigned long avail = sz - f->pos;
    unsigned long want  = count;
    if (want > avail) want = avail;
    if (want > 0 && n->data) {
        rmemcpy(buf, n->data + f->pos, want);
    }
    f->pos += want;
    return (int)want;
}

int vfs_write(int fd, const void *buf, uint32_t count) {
    ramfs_file_t *f = fd_get(fd);
    if (!f || !buf) return -1;
    ramfs_node_t *n = f->node;
    if (!n || node_is_dir(n)) return -1;
    if ((f->flags & 3) == O_RDONLY) return -1; /* 只读打开不允许写 */
    if (count == 0) return 0;
    unsigned long end = f->pos + count;
    if (file_ensure_cap(n, end) != 0) return -1;
    rmemcpy(n->data + f->pos, buf, count);
    f->pos += count;
    if (f->pos > n->inode.size) n->inode.size = f->pos;
    return (int)count;
}

int vfs_seek(int fd, int offset, int whence) {
    ramfs_fat_fd_t *ff = fat_fd_get(fd);
    if (ff) {
        long base = 0;
        if (whence == SEEK_SET)      base = 0;
        else if (whence == SEEK_CUR) base = (long)ff->pos;
        else if (whence == SEEK_END) base = (long)ff->size;
        else return -1;
        long np = base + offset;
        if (np < 0) np = 0;
        ff->pos = (unsigned long)np;
        return (int)ff->pos;
    }
    ramfs_ext_fd_t *ef = ext_fd_get(fd);
    if (ef) {
        long base = 0;
        if (whence == SEEK_SET)      base = 0;
        else if (whence == SEEK_CUR) base = (long)ef->pos;
        else if (whence == SEEK_END) base = (long)ef->size;
        else return -1;
        long np = base + offset;
        if (np < 0) np = 0;
        ef->pos = (unsigned long)np;
        return (int)ef->pos;
    }
    ramfs_file_t *f = fd_get(fd);
    if (!f) return -1;
    ramfs_node_t *n = f->node;
    if (!n) return -1;
    long base = 0;
    if (whence == SEEK_SET)      base = 0;
    else if (whence == SEEK_CUR) base = (long)f->pos;
    else if (whence == SEEK_END) base = (long)n->inode.size;
    else return -1;
    long np = base + offset;
    if (np < 0) np = 0;
    f->pos = (unsigned long)np;
    return (int)f->pos;
}

int vfs_truncate(const char *path, uint32_t length) {
    if (!path || !g_ramfs_ready) return -1;
    ramfs_node_t *n = path_resolve(path, 0, 0, 0);
    if (!n || node_is_dir(n)) return -1;
    if (length == 0) {
        n->inode.size = 0;
        return 0;
    }
    if (file_ensure_cap(n, length) != 0) return -1;
    if (length > n->inode.size) {
        rmemset(n->data + n->inode.size, 0, length - n->inode.size);
    }
    n->inode.size = length;
    return 0;
}

/* ============================================================
 * VFS 目录接口实现
 * ============================================================ */

int vfs_mkdir(const char *path, int mode) {
    (void)mode;
    if (!path || !g_ramfs_ready) return -1;
    /* 已存在则失败 */
    if (path_resolve(path, 0, 0, 0)) return -1;
    char name[MAX_NAME];
    ramfs_node_t *parent = path_resolve(path, 1, name, sizeof(name));
    if (!parent || !node_is_dir(parent)) return -1;
    if (name[0] == 0) return -1;
    ramfs_node_t *n = node_create(parent, name, FS_DIR | 0755);
    if (!n) return -1;
    return 0;
}

int vfs_rmdir(const char *path) {
    if (!path || !g_ramfs_ready) return -1;
    ramfs_node_t *n = path_resolve(path, 0, 0, 0);
    if (!n || !node_is_dir(n)) return -1;
    if (n == g_root) return -1;      /* 不能删根 */
    if (n->child) return -1;         /* 非空目录不删 */
    node_detach(n);
    node_free(n);
    return 0;
}

int vfs_unlink(const char *path) {
    if (!path || !g_ramfs_ready) return -1;
    ramfs_node_t *n = path_resolve(path, 0, 0, 0);
    if (!n) return -1;
    if (node_is_dir(n)) return -1;   /* 目录请用 rmdir */
    node_detach(n);
    node_free(n);
    return 0;
}

/* readdir：按 index 返回 path 目录下第 index 个子项的 dentry。
 * 越界或错误返回 NULL。 */
dentry_t *vfs_readdir(const char *path, int index) {
    if (!path || !g_ramfs_ready || index < 0) return 0;

    /* ---- FAT32 挂载点分发 ---- */
    {
        const char *sub = 0;
        int m = fat_match(path, &sub);
        if (m && fat32_mounted()) {
            static dentry_t fat_de;
            static inode_t  fat_in;
            const char *fsub = (m == 1) ? "/" : sub;
            /* 回调选择器：抓取第 index 个条目 */
            fat32_dirent_t hit;
            int found = 0;
            fat32_list_pick(fsub, index, &hit, &found);
            if (!found) return 0;
            fat32_dirent_t *e = &hit;
            /* 填 dentry */
            {
                int i = 0;
                for (; i + 1 < MAX_NAME && e->name[i]; i++) fat_de.name[i] = e->name[i];
                fat_de.name[i] = 0;
            }
            fat_in.ino     = 0;
            fat_in.mode    = e->is_dir ? (FS_DIR | 0555) : (FS_FILE | 0444);
            fat_in.size    = e->size;
            fat_in.nlinks  = 1;
            fat_in.fs_type = 0;
            fat_de.inode   = &fat_in;
            fat_de.parent  = 0;
            fat_de.child   = 0;
            fat_de.sibling = 0;
            fat_de.mount   = 0;
            return &fat_de;
        }
    }

    /* ---- ext2/ext4 挂载点分发（只读）---- */
    {
        const char *sub = 0;
        int m = ext_match(path, &sub);
        if (m && ext4_mounted()) {
            static dentry_t ext_de;
            static inode_t  ext_in;
            const char *esub = (m == 1) ? "/" : sub;
            ext4_dirent_t hit;
            int found = 0;
            ext4_list_pick(esub, index, &hit, &found);
            if (!found) return 0;
            ext4_dirent_t *e = &hit;
            {
                int i = 0;
                for (; i + 1 < MAX_NAME && e->name[i]; i++) ext_de.name[i] = e->name[i];
                ext_de.name[i] = 0;
            }
            ext_in.ino     = 0;
            ext_in.mode    = e->is_dir ? (FS_DIR | 0555) : (FS_FILE | 0444);
            ext_in.size    = e->size;
            ext_in.nlinks  = 1;
            ext_in.fs_type = 0;
            ext_de.inode   = &ext_in;
            ext_de.parent  = 0;
            ext_de.child   = 0;
            ext_de.sibling = 0;
            ext_de.mount   = 0;
            return &ext_de;
        }
    }

    ramfs_node_t *dir = path_resolve(path, 0, 0, 0);
    if (!dir || !node_is_dir(dir)) return 0;
    int i = 0;
    for (ramfs_node_t *c = dir->child; c; c = c->sibling, i++) {
        if (i == index) return &c->dentry;
    }
    return 0;
}

int vfs_stat(const char *path, inode_t *st) {
    if (!path || !st || !g_ramfs_ready) return -1;

    /* ---- FAT32 挂载点分发 ---- */
    {
        const char *sub = 0;
        int m = fat_match(path, &sub);
        if (m && fat32_mounted()) {
            if (m == 1) {
                /* /mnt/fat 根目录 */
                st->ino = 0; st->mode = FS_DIR | 0555; st->size = 0;
                st->nlinks = 1; st->fs_type = 0;
                return 0;
            }
            fat32_dirent_t e;
            if (fat32_stat(sub, &e) != 0) return -1;
            st->ino = 0;
            st->mode = e.is_dir ? (FS_DIR | 0555) : (FS_FILE | 0444);
            st->size = e.size;
            st->nlinks = 1;
            st->fs_type = 0;
            return 0;
        }
    }

    /* ---- ext2/ext4 挂载点分发（只读）---- */
    {
        const char *sub = 0;
        int m = ext_match(path, &sub);
        if (m && ext4_mounted()) {
            if (m == 1) {
                st->ino = 0; st->mode = FS_DIR | 0555; st->size = 0;
                st->nlinks = 1; st->fs_type = 0;
                return 0;
            }
            ext4_dirent_t e;
            if (ext4_stat(sub, &e) != 0) return -1;
            st->ino = 0;
            st->mode = e.is_dir ? (FS_DIR | 0555) : (FS_FILE | 0444);
            st->size = e.size;
            st->nlinks = 1;
            st->fs_type = 0;
            return 0;
        }
    }

    ramfs_node_t *n = path_resolve(path, 0, 0, 0);
    if (!n) return -1;
    *st = n->inode;   /* 结构拷贝（mode/size/nlink 等）*/
    return 0;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath || !g_ramfs_ready) return -1;
    ramfs_node_t *n = path_resolve(oldpath, 0, 0, 0);
    if (!n || n == g_root) return -1;
    if (path_resolve(newpath, 0, 0, 0)) return -1; /* 目标已存在 */
    char name[MAX_NAME];
    ramfs_node_t *newparent = path_resolve(newpath, 1, name, sizeof(name));
    if (!newparent || !node_is_dir(newparent)) return -1;
    if (name[0] == 0) return -1;
    /* 防止把目录移到它自己的子树下 */
    for (ramfs_node_t *a = newparent; a; a = a->parent) {
        if (a == n) return -1;
    }
    node_detach(n);
    /* 改名 */
    rstrncpy(n->dentry.name, name, MAX_NAME);
    /* 挂到新父目录 */
    n->parent = newparent;
    n->sibling = 0;
    if (!newparent->child) {
        newparent->child = n;
    } else {
        ramfs_node_t *c = newparent->child;
        while (c->sibling) c = c->sibling;
        c->sibling = n;
    }
    return 0;
}

/* ============================================================
 * 初始化：建根目录 + 导入 initrd 文件
 * ============================================================ */

/* 沿着全路径逐级确保中间目录存在，返回最后一级的父目录，
 * 并把最后一段（文件名）写入 leaf。 */
static ramfs_node_t *ensure_dirs(const char *path, char *leaf, unsigned long leaf_cap) {
    const char *p = path;
    while (*p == '/') p++;
    ramfs_node_t *cur = g_root;
    while (*p) {
        const char *seg = p;
        unsigned long slen = 0;
        while (p[slen] && p[slen] != '/') slen++;
        const char *next = seg + slen;
        while (*next == '/') next++;
        int is_last = (*next == 0);

        if (is_last) {
            unsigned long i = 0;
            for (; i < slen && i + 1 < leaf_cap; i++) leaf[i] = seg[i];
            leaf[i] = 0;
            return cur;
        }
        /* 中间段：查找或创建目录 */
        ramfs_node_t *nx = dir_lookup(cur, seg, slen);
        if (!nx) {
            char nm[MAX_NAME];
            unsigned long i = 0;
            for (; i < slen && i + 1 < MAX_NAME; i++) nm[i] = seg[i];
            nm[i] = 0;
            nx = node_create(cur, nm, FS_DIR | 0755);
            if (!nx) return 0;
        }
        cur = nx;
        p = next;
    }
    /* 路径以 '/' 结尾或为空 */
    if (leaf && leaf_cap) leaf[0] = 0;
    return cur;
}

void ramfs_init(void) {
    if (g_ramfs_ready) return;

    /* 清零节点池 */
    for (int i = 0; i < RAMFS_MAX_NODES; i++) g_nodes[i].used = 0;
    for (int i = 0; i < RAMFS_MAX_FD; i++)    g_fds[i].used  = 0;

    /* 建根目录 */
    g_root = node_alloc();
    if (!g_root) { ramfs_log("[RAMFS] root alloc failed\n"); return; }
    rstrncpy(g_root->dentry.name, "/", MAX_NAME);
    g_root->dentry.inode = &g_root->inode;
    g_root->inode.mode = FS_DIR | 0755;
    g_root->inode.size = 0;
    g_root->inode.nlinks = 1;
    g_root->parent = 0;
    g_root->child = 0;
    g_root->sibling = 0;
    g_ramfs_ready = 1;

    /* 导入 initrd 文件 */
    const x86_64_initrd_image_t *img = arch_x86_64_initrd_get_image();
    if (!img || !img->files) {
        ramfs_log("[RAMFS] no initrd image, empty tree\n");
        return;
    }
    for (uint32_t fi = 0; fi < img->file_count; fi++) {
        const x86_64_initrd_file_t *f = &img->files[fi];
        if (!f->name[0]) continue;
        char leaf[MAX_NAME];
        ramfs_node_t *parent = ensure_dirs(f->name, leaf, sizeof(leaf));
        if (!parent) continue;
        if (leaf[0] == 0) continue; /* 目录项，已在 ensure_dirs 建好 */
        /* 创建文件节点 */
        ramfs_node_t *n = dir_lookup(parent, leaf, rstrlen(leaf));
        if (!n) {
            uint32_t md = f->mode ? f->mode : 0644u;
            n = node_create(parent, leaf, FS_FILE | (md & 0777));
            if (!n) continue;
        }
        /* 拷贝数据 */
        if (f->size > 0 && f->data) {
            if (file_ensure_cap(n, (unsigned long)f->size) == 0) {
                rmemcpy(n->data, f->data, (unsigned long)f->size);
                n->inode.size = (unsigned long)f->size;
            }
        }
    }
    ramfs_log("[RAMFS] init done, initrd imported\n");

    /* 阶段 4-2：创建 FAT32 挂载点占位目录 /mnt/fat，使其在 ls /mnt 中可见。
     * 实际 /mnt/fat/* 的访问由 vfs_* 分发层转发到 fat32 驱动。 */
    {
        char leaf[MAX_NAME];
        ramfs_node_t *mp = ensure_dirs("/mnt/fat/", leaf, sizeof(leaf));
        if (mp) ramfs_log("[RAMFS] mountpoint /mnt/fat ready\n");
    }
}

/* ============================================================
 * 磁盘快照持久化 (RAMFS <-> ATA secondary master)
 *
 * 磁盘布局 (512B 扇区):
 *   扇区 0        : superblock
 *   扇区 1..N     : 记录流 (record stream)
 * 每条记录 (紧凑排列, 小端):
 *   uint32 type   : 1=目录 2=文件
 *   uint32 mode   : inode.mode 低位权限
 *   uint32 pathlen: 路径字节数 (不含结尾)
 *   uint32 datalen: 文件数据字节数 (目录为0)
 *   char   path[pathlen]
 *   char   data[datalen]
 * 记录之间无对齐填充; 记录流以一条 type=0 的终止记录结束。
 * ========================================================== */

#define RAMFS_SNAP_MAGIC   0x52414d46u   /* 'RAMF' */
#define RAMFS_SNAP_VERSION 1u
#define RAMFS_SECTOR       512u
#define RAMFS_SNAP_MAXSEC  16384u        /* 最多 8 MiB 快照 */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t node_count;   /* 记录数(不含终止记录) */
    uint32_t total_bytes;  /* 记录流总字节(含终止记录) */
    uint32_t sector_count; /* 记录流占用扇区数 */
    uint32_t reserved[3];
} ramfs_snap_super_t;

/* 写游标: 把记录流累积到扇区缓冲, 满 512B 就落盘 */
typedef struct {
    uint32_t lba;          /* 当前写入的扇区号 */
    uint32_t off;          /* buf 内偏移 */
    uint32_t total;        /* 已写字节总数 */
    int      err;
    uint8_t  buf[RAMFS_SECTOR];
} ramfs_wcursor_t;

static void snap_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}
static uint32_t snap_get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 向写游标推入一段字节, 自动按扇区落盘 */
static void wcursor_push(ramfs_wcursor_t *wc, const void *src, uint32_t len) {
    const uint8_t *s = (const uint8_t *)src;
    while (len > 0 && !wc->err) {
        uint32_t space = RAMFS_SECTOR - wc->off;
        uint32_t n = (len < space) ? len : space;
        rmemcpy(wc->buf + wc->off, s, n);
        wc->off   += n;
        wc->total += n;
        s         += n;
        len       -= n;
        if (wc->off == RAMFS_SECTOR) {
            if (wc->lba >= 1u + RAMFS_SNAP_MAXSEC) { wc->err = -1; return; }
            if (ata_write_sectors(wc->lba, 1, wc->buf) != 0) { wc->err = -2; return; }
            wc->lba++;
            wc->off = 0;
        }
    }
}

/* 冲刷写游标剩余不满一扇区的数据(补零) */
static void wcursor_flush(ramfs_wcursor_t *wc) {
    if (wc->err) return;
    if (wc->off > 0) {
        for (uint32_t i = wc->off; i < RAMFS_SECTOR; i++) wc->buf[i] = 0;
        if (ata_write_sectors(wc->lba, 1, wc->buf) != 0) { wc->err = -2; return; }
        wc->lba++;
        wc->off = 0;
    }
}

/* 递归写出一个节点及其子树 (path 为该节点绝对路径) */
static void snap_write_node(ramfs_wcursor_t *wc, ramfs_node_t *n,
                            char *path, uint32_t pathlen, uint32_t *count) {
    if (!n || wc->err) return;
    uint32_t is_dir = (n->inode.mode & FS_DIR) ? 1u : 2u;
    /* 根目录本身不写记录(隐式存在), 只写其子节点 */
    if (!(pathlen == 1 && path[0] == '/')) {
        uint8_t hdr[16];
        uint32_t datalen = (is_dir == 2u) ? (uint32_t)n->inode.size : 0u;
        snap_put_u32(hdr + 0,  is_dir);
        snap_put_u32(hdr + 4,  (uint32_t)(n->inode.mode & 0777));
        snap_put_u32(hdr + 8,  pathlen);
        snap_put_u32(hdr + 12, datalen);
        wcursor_push(wc, hdr, 16);
        wcursor_push(wc, path, pathlen);
        if (datalen > 0 && n->data) wcursor_push(wc, n->data, datalen);
        (*count)++;
    }
    /* 递归子节点 */
    for (ramfs_node_t *c = n->child; c && !wc->err; c = c->sibling) {
        uint32_t nl = rstrlen(c->dentry.name);
        /* 组装子路径: path + '/' + name (根目录不重复 '/') */
        char child_path[512];
        uint32_t cl = 0;
        if (pathlen == 1 && path[0] == '/') {
            child_path[cl++] = '/';
        } else {
            for (uint32_t i = 0; i < pathlen && cl < 510; i++) child_path[cl++] = path[i];
            child_path[cl++] = '/';
        }
        for (uint32_t i = 0; i < nl && cl < 511; i++) child_path[cl++] = c->dentry.name[i];
        child_path[cl] = 0;
        snap_write_node(wc, c, child_path, cl, count);
    }
}

int ramfs_snapshot_save(void) {
    if (!g_ramfs_ready || !g_root) { ramfs_log("[RAMFS] save: not ready\n"); return -1; }
    if (!ata_present())            { ramfs_log("[RAMFS] save: no disk\n");   return -1; }

    ramfs_wcursor_t wc;
    wc.lba = 1; wc.off = 0; wc.total = 0; wc.err = 0;
    for (int i = 0; i < (int)RAMFS_SECTOR; i++) wc.buf[i] = 0;

    uint32_t count = 0;
    char rootpath[2]; rootpath[0] = '/'; rootpath[1] = 0;
    snap_write_node(&wc, g_root, rootpath, 1, &count);

    /* 终止记录 type=0 */
    if (!wc.err) {
        uint8_t term[16];
        for (int i = 0; i < 16; i++) term[i] = 0;
        wcursor_push(&wc, term, 16);
    }
    uint32_t stream_bytes = wc.total;
    wcursor_flush(&wc);
    if (wc.err) { ramfs_log("[RAMFS] save: write error\n"); return -2; }

    /* 写 superblock (扇区0) */
    uint8_t sb[RAMFS_SECTOR];
    for (int i = 0; i < (int)RAMFS_SECTOR; i++) sb[i] = 0;
    ramfs_snap_super_t *S = (ramfs_snap_super_t *)sb;
    S->magic        = RAMFS_SNAP_MAGIC;
    S->version      = RAMFS_SNAP_VERSION;
    S->node_count   = count;
    S->total_bytes  = stream_bytes;
    S->sector_count = wc.lba - 1u;
    if (ata_write_sectors(0, 1, sb) != 0) { ramfs_log("[RAMFS] save: sb write err\n"); return -2; }
    ata_flush();
    ramfs_log("[RAMFS] snapshot saved\n");
    return 0;
}

/* ---------- 反序列化: 读游标 ---------- */
typedef struct {
    uint32_t lba;              /* 下一个要读的扇区 */
    uint32_t off;              /* buf 内已消费偏移 */
    uint32_t valid;            /* buf 内有效字节(总为 512) */
    uint32_t remain;           /* 记录流剩余未读字节 */
    int      err;
    uint8_t  buf[RAMFS_SECTOR];
} ramfs_rcursor_t;

static void rcursor_fill(ramfs_rcursor_t *rc) {
    if (rc->err) return;
    if (rc->lba >= 1u + RAMFS_SNAP_MAXSEC) { rc->err = -1; return; }
    if (ata_read_sectors(rc->lba, 1, rc->buf) != 0) { rc->err = -2; return; }
    rc->lba++;
    rc->off = 0;
    rc->valid = RAMFS_SECTOR;
}

/* 从读游标拉取 len 字节到 dst; 不足时自动读下一扇区 */
static void rcursor_pull(ramfs_rcursor_t *rc, void *dst, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    while (len > 0 && !rc->err) {
        if (rc->off >= rc->valid) { rcursor_fill(rc); if (rc->err) return; }
        uint32_t avail = rc->valid - rc->off;
        uint32_t n = (len < avail) ? len : avail;
        if (rc->remain < n) { rc->err = -3; return; }   /* 超出记录流长度 */
        if (d) rmemcpy(d, rc->buf + rc->off, n);
        rc->off    += n;
        rc->remain -= n;
        if (d) d   += n;
        len        -= n;
    }
}

/* 丢弃(skip) len 字节 */
static void rcursor_skip(ramfs_rcursor_t *rc, uint32_t len) {
    rcursor_pull(rc, 0, len);
}

int ramfs_snapshot_load(void) {
    if (!g_ramfs_ready || !g_root) { ramfs_log("[RAMFS] load: not ready\n"); return -1; }
    if (!ata_present())            { ramfs_log("[RAMFS] load: no disk\n");   return -1; }

    /* 读 superblock */
    uint8_t sb[RAMFS_SECTOR];
    if (ata_read_sectors(0, 1, sb) != 0) { ramfs_log("[RAMFS] load: sb read err\n"); return -2; }
    ramfs_snap_super_t *S = (ramfs_snap_super_t *)sb;
    if (S->magic != RAMFS_SNAP_MAGIC) { ramfs_log("[RAMFS] load: no snapshot (bad magic)\n"); return 1; }
    if (S->version != RAMFS_SNAP_VERSION) { ramfs_log("[RAMFS] load: version mismatch\n"); return -3; }

    ramfs_rcursor_t rc;
    rc.lba = 1; rc.off = 0; rc.valid = 0; rc.remain = S->total_bytes; rc.err = 0;

    uint32_t restored = 0;
    for (;;) {
        uint8_t hdr[16];
        if (rc.remain < 16) break;
        rcursor_pull(&rc, hdr, 16);
        if (rc.err) break;
        uint32_t type    = snap_get_u32(hdr + 0);
        uint32_t perm    = snap_get_u32(hdr + 4);
        uint32_t pathlen = snap_get_u32(hdr + 8);
        uint32_t datalen = snap_get_u32(hdr + 12);
        if (type == 0) break;                 /* 终止记录 */
        if (pathlen == 0 || pathlen >= 512) { rc.err = -4; break; }

        char path[512];
        rcursor_pull(&rc, path, pathlen);
        if (rc.err) break;
        path[pathlen] = 0;

        if (type == 1u) {
            /* 目录: ensure_dirs 会建中间段, leaf 为最后一段 */
            char leaf[MAX_NAME];
            ramfs_node_t *parent = ensure_dirs(path, leaf, sizeof(leaf));
            if (parent && leaf[0]) {
                ramfs_node_t *d = dir_lookup(parent, leaf, rstrlen(leaf));
                if (!d) node_create(parent, leaf, FS_DIR | (perm & 0777));
            }
            restored++;
        } else if (type == 2u) {
            char leaf[MAX_NAME];
            ramfs_node_t *parent = ensure_dirs(path, leaf, sizeof(leaf));
            if (parent && leaf[0]) {
                ramfs_node_t *n = dir_lookup(parent, leaf, rstrlen(leaf));
                if (!n) n = node_create(parent, leaf, FS_FILE | (perm & 0777));
                if (n && datalen > 0) {
                    if (file_ensure_cap(n, (unsigned long)datalen) == 0) {
                        rcursor_pull(&rc, n->data, datalen);
                        n->inode.size = (unsigned long)datalen;
                    } else {
                        rcursor_skip(&rc, datalen);
                    }
                } else if (datalen > 0) {
                    rcursor_skip(&rc, datalen);
                }
            } else {
                rcursor_skip(&rc, datalen);
            }
            restored++;
        } else {
            /* 未知类型: 跳过数据 */
            rcursor_skip(&rc, datalen);
        }
        if (rc.err) break;
    }

    if (rc.err) { ramfs_log("[RAMFS] load: stream error\n"); return -5; }
    ramfs_log("[RAMFS] snapshot loaded\n");
    return 0;
}
