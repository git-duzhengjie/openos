/* ============================================================
 * openos x86_64 RAMFS —— 内存树形可读写文件系统
 *
 * 取代 gui64_stubs.c 中基于 initrd 全路径前缀匹配的只读桩。
 * 提供真正的目录树 (parent/child/sibling)，文件数据动态分配，
 * 支持 open/read/write/seek/close/truncate/mkdir/rmdir/unlink/
 * readdir/stat/rename。对上导出 gui.c 依赖的 vfs_* 接口。
 * ============================================================ */

#include "ramfs64.h"
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
    ramfs_file_t *f = fd_get(fd);
    if (!f) return -1;
    f->used = 0;
    f->node = 0;
    f->pos  = 0;
    return 0;
}

int vfs_read(int fd, void *buf, uint32_t count) {
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
}
