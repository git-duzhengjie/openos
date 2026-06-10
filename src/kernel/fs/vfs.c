/* ============================================================
 * openos - VFS 实现 (Phase 3)
 * ============================================================ */

#include "vfs.h"
#include "serial.h"
#include "pmm.h"
#include "string.h"
#include "ramfs.h"
#include "tmpfs.h"
#include "chardev.h"
#include "blockdev.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ---- 全局状态 ---- */
static dentry_t *root_dentry;
static inode_t  *root_inode;
static mount_t  *mount_list;

#define MAX_INODES 256
static inode_t  inode_pool[MAX_INODES];
static uint32_t next_ino = 1;

#define MAX_DENTRY 256
static dentry_t dentry_pool[MAX_DENTRY];

#define MAX_MOUNTS 16
static mount_t mount_pool[MAX_MOUNTS];

/* ============================================================
 * 小工具函数
 * ============================================================ */

static int vfs_is_dir(inode_t *ip) {
    return ip && ((ip->mode & FS_DIR) == FS_DIR);
}

static int vfs_is_file(inode_t *ip) {
    return ip && ((ip->mode & FS_FILE) == FS_FILE);
}

// static int vfs_is_chardev(inode_t *ip) { return ip && ((ip->mode & FS_CHAR_DEVICE) == FS_CHAR_DEVICE); }
// static int vfs_is_blockdev(inode_t *ip) { return ip && ((ip->mode & FS_BLOCK_DEVICE) == FS_BLOCK_DEVICE); }

// static int path_copy_name(char *dst, const char *src, uint32_t len) { if (!dst||!src||len==0||len>=MAX_NAME) return -1; for(uint32_t i=0;i<len;i++)dst[i]=src[i]; dst[len]=0; return 0; }

static int is_open_write(int flags) {
    int acc = flags & 3;
    return acc == O_WRONLY || acc == O_RDWR;
}

static int is_open_read(int flags) {
    int acc = flags & 3;
    return acc == O_RDONLY || acc == O_RDWR;
}

/* ============================================================
 * inode / dentry / mount 分配
 * ============================================================ */

static inode_t *alloc_inode(void) {
    for (int i = 0; i < MAX_INODES; i++) {
        if (inode_pool[i].ref_count == 0) {
            inode_t *ip = &inode_pool[i];
            memset(ip, 0, sizeof(inode_t));
            ip->ino = next_ino++;
            ip->ref_count = 1;
            ip->nlinks = 1;
            return ip;
        }
    }
    return NULL;
}

static void free_inode(inode_t *ip) {
    if (!ip) return;
    if (ip->ref_count > 0) ip->ref_count--;
    if (ip->ref_count == 0) {
        memset(ip, 0, sizeof(inode_t));
    }
}

static dentry_t *alloc_dentry(void) {
    for (int i = 0; i < MAX_DENTRY; i++) {
        if (dentry_pool[i].inode == NULL && dentry_pool[i].name[0] == 0) {
            dentry_t *d = &dentry_pool[i];
            memset(d, 0, sizeof(dentry_t));
            return d;
        }
    }
    return NULL;
}

static void free_dentry(dentry_t *d) {
    if (!d) return;
    if (d->inode) free_inode(d->inode);
    memset(d, 0, sizeof(dentry_t));
}

static mount_t *alloc_mount(void) {
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mount_pool[i].fs == NULL) {
            memset(&mount_pool[i], 0, sizeof(mount_t));
            return &mount_pool[i];
        }
    }
    return NULL;
}

static void free_mount(mount_t *m) {
    if (!m) return;
    memset(m, 0, sizeof(mount_t));
}

static void add_child(dentry_t *parent, dentry_t *child) {
    if (!parent || !child) return;
    child->parent = parent;
    child->sibling = parent->child;
    parent->child = child;
}

static void detach_child(dentry_t *child) {
    if (!child || !child->parent) return;
    dentry_t *parent = child->parent;
    dentry_t **pp = &parent->child;
    while (*pp) {
        if (*pp == child) {
            *pp = child->sibling;
            child->parent = NULL;
            child->sibling = NULL;
            return;
        }
        pp = &(*pp)->sibling;
    }
}

static dentry_t *find_child(dentry_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    dentry_t *c = parent->child;
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->sibling;
    }
    return NULL;
}

static dentry_t *resolve_mount(dentry_t *d) {
    while (d && d->mount) d = d->mount;
    return d;
}

static int has_children(dentry_t *d) {
    d = resolve_mount(d);
    return d && d->child != NULL;
}

/* ============================================================
 * 设备文件 ops
 * ============================================================ */

static int vfs_chardev_open(file_t *f) {
    if (!f || !f->inode) return -1;
    return chardev_open((chardev_t *)f->inode->fs_data);
}

static int vfs_chardev_close(file_t *f) {
    if (!f || !f->inode) return -1;
    return chardev_close((chardev_t *)f->inode->fs_data);
}

static int vfs_chardev_read(file_t *f, void *buf, uint32_t count) {
    if (!f || !f->inode) return -1;
    return chardev_read((chardev_t *)f->inode->fs_data, buf, count);
}

static int vfs_chardev_write(file_t *f, const void *buf, uint32_t count) {
    if (!f || !f->inode) return -1;
    return chardev_write((chardev_t *)f->inode->fs_data, buf, count);
}

static file_ops_t vfs_chardev_ops = {
    .open = vfs_chardev_open,
    .close = vfs_chardev_close,
    .read = vfs_chardev_read,
    .write = vfs_chardev_write,
    .seek = NULL,
    .truncate = NULL,
    .readdir = NULL,
};

static int vfs_blockdev_open(file_t *f) {
    if (!f || !f->inode) return -1;
    return blockdev_open((blockdev_t *)f->inode->fs_data);
}

static int vfs_blockdev_close(file_t *f) {
    if (!f || !f->inode) return -1;
    return blockdev_close((blockdev_t *)f->inode->fs_data);
}

static int vfs_blockdev_read(file_t *f, void *buf, uint32_t count) {
    if (!f || !f->inode || !buf) return -1;
    blockdev_t *dev = (blockdev_t *)f->inode->fs_data;
    if (!dev || dev->sector_size == 0) return -1;
    if ((f->offset % dev->sector_size) != 0 || (count % dev->sector_size) != 0) return -1;
    int r = blockdev_read_blocks(dev, f->offset / dev->sector_size,
                                 count / dev->sector_size, buf);
    if (r < 0) return r;
    f->offset += count;
    return (int)count;
}

static int vfs_blockdev_write(file_t *f, const void *buf, uint32_t count) {
    if (!f || !f->inode || !buf) return -1;
    blockdev_t *dev = (blockdev_t *)f->inode->fs_data;
    if (!dev || dev->sector_size == 0) return -1;
    if ((f->offset % dev->sector_size) != 0 || (count % dev->sector_size) != 0) return -1;
    int r = blockdev_write_blocks(dev, f->offset / dev->sector_size,
                                  count / dev->sector_size, buf);
    if (r < 0) return r;
    f->offset += count;
    return (int)count;
}

static int vfs_blockdev_seek(file_t *f, int offset, int whence) {
    if (!f || !f->inode) return -1;
    uint32_t size = blockdev_size_bytes((blockdev_t *)f->inode->fs_data);
    uint32_t new_off;
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
        if (offset < 0 && (uint32_t)(-offset) > size) return -1;
        new_off = size + offset;
        break;
    default:
        return -1;
    }
    if (new_off > size) return -1;
    f->offset = new_off;
    return (int)new_off;
}

static file_ops_t vfs_blockdev_ops = {
    .open = vfs_blockdev_open,
    .close = vfs_blockdev_close,
    .read = vfs_blockdev_read,
    .write = vfs_blockdev_write,
    .seek = vfs_blockdev_seek,
    .truncate = NULL,
    .readdir = NULL,
};

/* ============================================================
 * 路径规范化与解析
 * ============================================================ */

/* ============================================================
 * 进程 cwd 辅助（简化版，暂时 fallback 到静态变量）
 * ============================================================ */
#include "process.h"

static file_t *fallback_fds[16];
static int fallback_fds_inited = 0;
static char fallback_cwd[MAX_PATH] = "/";

static char *vfs_get_cwd(void) {
    return fallback_cwd;
}

static file_t **get_current_fd_table(void) {
    if (!fallback_fds_inited) {
        memset(fallback_fds, 0, sizeof(fallback_fds));
        fallback_fds_inited = 1;
    }
    return fallback_fds;
}

static char *vfs_resolve_to_absolute(const char *path, char *buf, uint32_t buf_size) {
    if (!path || !buf || buf_size < 2) return NULL;
    if (path[0] == '/') {
        strncpy(buf, path, buf_size - 1);
        buf[buf_size - 1] = 0;
        return buf;
    }
    const char *cwd = vfs_get_cwd();
    uint32_t cwd_len = (uint32_t)strlen(cwd);
    uint32_t path_len = (uint32_t)strlen(path);
    if (cwd_len + 1 + path_len + 1 > buf_size) return NULL;
    strncpy(buf, cwd, buf_size - 1);
    if (cwd_len > 0 && buf[cwd_len - 1] != '/') {
        buf[cwd_len] = '/';
        cwd_len++;
    }
    strncpy(buf + cwd_len, path, buf_size - 1 - cwd_len);
    buf[buf_size - 1] = 0;
    return buf;
}

int vfs_normalize_path(const char *path, char *out, uint32_t out_size) {
    if (!path || !out || out_size < 2) return -1;

    char abs_buf[MAX_PATH];
    const char *abs = vfs_resolve_to_absolute(path, abs_buf, sizeof(abs_buf));
    if (!abs) return -1;

    char parts[32][MAX_NAME];
    uint32_t depth = 0;
    uint32_t i = 0;

    while (abs[i]) {
        while (abs[i] == '/') i++;
        if (!abs[i]) break;

        char name[MAX_NAME];
        uint32_t n = 0;
        while (abs[i] && abs[i] != '/') {
            if (n + 1 >= MAX_NAME) return -1;
            name[n++] = abs[i++];
        }
        name[n] = 0;

        if (strcmp(name, ".") == 0) {
            continue;
        }
        if (strcmp(name, "..") == 0) {
            if (depth > 0) depth--;
            continue;
        }
        if (depth >= 32) return -1;
        strcpy(parts[depth++], name);
    }

    uint32_t pos = 0;
    out[pos++] = '/';
    if (depth == 0) {
        out[pos] = 0;
        return 0;
    }

    for (uint32_t p = 0; p < depth; p++) {
        uint32_t n = (uint32_t)strlen(parts[p]);
        if (pos + n + 1 >= out_size) return -1;
        for (uint32_t j = 0; j < n; j++) out[pos++] = parts[p][j];
        if (p + 1 < depth) out[pos++] = '/';
    }
    out[pos] = 0;
    return 0;
}

static int split_parent_path(const char *path, char *parent_out, char *name_out) {
    char norm[MAX_PATH];
    if (vfs_normalize_path(path, norm, sizeof(norm)) < 0) return -1;
    if (strcmp(norm, "/") == 0) return -1;

    int len = (int)strlen(norm);
    int slash = len - 1;
    while (slash > 0 && norm[slash] != '/') slash--;

    const char *name = &norm[slash + 1];
    if (!name[0] || strlen(name) >= MAX_NAME) return -1;
    strcpy(name_out, name);

    if (slash == 0) {
        strcpy(parent_out, "/");
    } else {
        if ((uint32_t)slash >= MAX_PATH) return -1;
        for (int i = 0; i < slash; i++) parent_out[i] = norm[i];
        parent_out[slash] = 0;
    }
    return 0;
}

static dentry_t *vfs_path_lookup_internal(const char *path, int follow_final_mount) {
    if (!root_dentry || !path) return NULL;

    char norm[MAX_PATH];
    if (vfs_normalize_path(path, norm, sizeof(norm)) < 0) return NULL;
    if (strcmp(norm, "/") == 0) {
        return follow_final_mount ? resolve_mount(root_dentry) : root_dentry;
    }

    dentry_t *cur = root_dentry;
    uint32_t i = 1;
    while (norm[i]) {
        char name[MAX_NAME];
        uint32_t n = 0;
        while (norm[i] && norm[i] != '/') {
            if (n + 1 >= MAX_NAME) return NULL;
            name[n++] = norm[i++];
        }
        name[n] = 0;
        while (norm[i] == '/') i++;

        cur = resolve_mount(cur);
        if (!vfs_is_dir(cur ? cur->inode : NULL)) return NULL;
        cur = find_child(cur, name);
        if (!cur) return NULL;

        if (norm[i]) {
            cur = resolve_mount(cur);
        } else if (follow_final_mount) {
            cur = resolve_mount(cur);
        }
    }
    return cur;
}

dentry_t *vfs_path_lookup(const char *path) {
    return vfs_path_lookup_internal(path, 1);
}

/* ============================================================
 * VFS 初始化与内部创建辅助
 * ============================================================ */

void vfs_init_fds_for_process(void *proc) {
    process_t *p = (process_t *)proc;
    if (!p) return;
    for (int i = 0; i < 16; i++) p->fds[i] = NULL;
    strncpy(p->cwd, "/", sizeof(p->cwd) - 1);
    p->cwd[sizeof(p->cwd) - 1] = 0;
}

void vfs_init_fds(void) {
    for (int i = 0; i < 16; i++) fallback_fds[i] = NULL;
    strncpy(fallback_cwd, "/", sizeof(fallback_cwd)-1);
    fallback_cwd[sizeof(fallback_cwd)-1] = 0;
}

void vfs_init(void) {
    memset(inode_pool, 0, sizeof(inode_pool));
    memset(dentry_pool, 0, sizeof(dentry_pool));
    memset(mount_pool, 0, sizeof(mount_pool));
    mount_list = NULL;
    next_ino = 1;
    vfs_init_fds();

    root_inode = alloc_inode();
    root_inode->mode = FS_DIR | S_IRUSR | S_IWUSR;
    root_inode->ops = NULL;

    root_dentry = alloc_dentry();
    strcpy(root_dentry->name, "/");
    root_dentry->inode = root_inode;
    root_dentry->parent = root_dentry;

    ramfs_setup_inode(root_inode, root_inode->mode);

    serial_write("[VFS] initialized\n");
}

dentry_t *vfs_create_node_under(dentry_t *parent, const char *name,
                                uint32_t mode, file_ops_t *ops,
                                void *fs_data, uint32_t size) {
    parent = resolve_mount(parent);
    if (!parent || !vfs_is_dir(parent->inode) || !name || !name[0]) return NULL;
    if (strlen(name) >= MAX_NAME) return NULL;
    if (find_child(parent, name)) return NULL;

    inode_t *ip = alloc_inode();
    if (!ip) return NULL;
    ip->mode = mode;
    ip->size = size;
    ip->fs_data = fs_data;
    ip->ops = ops;

    if (!ops && (mode & (FS_FILE | FS_DIR))) {
        if (parent->inode && parent->inode->fs_type == TMPFS_MAGIC) {
            tmpfs_setup_inode(ip, mode);
        } else {
            ramfs_setup_inode(ip, mode);
        }
    }

    dentry_t *d = alloc_dentry();
    if (!d) {
        free_inode(ip);
        return NULL;
    }
    strcpy(d->name, name);
    d->inode = ip;
    add_child(parent, d);
    return d;
}

static dentry_t *create_regular_file(dentry_t *parent, const char *name, uint32_t mode) {
    return vfs_create_node_under(parent, name, FS_FILE | mode, NULL, NULL, 0);
}

/* ============================================================
 * fd 管理
 * ============================================================ */

int vfs_alloc_fd(void) {
    file_t **fds = get_current_fd_table();
    for (int i = 0; i < 16; i++) {
        if (!fds[i]) return i;
    }
    return -1;
}

file_t *vfs_get_file(int fd) {
    if (fd < 0 || fd >= 16) return NULL;
    file_t **fds = get_current_fd_table();
    return fds[fd];
}

int vfs_put_file(int fd, file_t *file) {
    if (fd < 0 || fd >= 16) return -1;
    file_t **fds = get_current_fd_table();
    fds[fd] = file;
    return 0;
}

/* ============================================================
 * 文件操作
 * ============================================================ */

static int vfs_try_truncate_inode(inode_t *inode, uint32_t size);

int vfs_open(const char *path, int flags, int mode) {
    if (!path) return -1;

    dentry_t *d = vfs_path_lookup(path);
    if (!d) {
        if (!(flags & O_CREAT)) return -1;
        char parent_path[MAX_PATH];
        char name[MAX_NAME];
        if (split_parent_path(path, parent_path, name) < 0) return -1;
        dentry_t *parent = vfs_path_lookup(parent_path);
        if (!parent || !vfs_is_dir(parent->inode)) return -1;
        d = create_regular_file(parent, name, (uint32_t)mode);
        if (!d) return -1;
    }

    d = resolve_mount(d);
    if (!d || !d->inode) return -1;
    if (vfs_is_dir(d->inode) && is_open_write(flags)) return -1;

    if (is_open_read(flags) && !(d->inode->mode & S_IRUSR)) return -1;
    if (is_open_write(flags) && !(d->inode->mode & S_IWUSR)) return -1;

    if ((flags & O_TRUNC) && is_open_write(flags)) {
        if (vfs_try_truncate_inode(d->inode, 0) < 0) return -1;
    }

    int fd = vfs_alloc_fd();
    if (fd < 0) return -1;

    file_t *f = (file_t *)pmm_alloc_page();
    if (!f) return -1;
    memset(f, 0, sizeof(file_t));
    f->inode = d->inode;
    f->dentry = d;
    f->flags = (uint32_t)flags;
    f->offset = 0;
    f->ref_count = 1;
    f->ops = d->inode->ops;

    if (f->ops && f->ops->open) {
        if (f->ops->open(f) < 0) {
            pmm_free_page(f);
            return -1;
        }
    }

    vfs_put_file(fd, f);
    return fd;
}

int vfs_close(int fd) {
    file_t *f = vfs_get_file(fd);
    if (!f) return -1;
    if (f->ops && f->ops->close) f->ops->close(f);
    vfs_put_file(fd, NULL);
    pmm_free_page(f);
    return 0;
}

int vfs_read(int fd, void *buf, uint32_t count) {
    file_t *f = vfs_get_file(fd);
    if (!f || !buf) return -1;
    if (!is_open_read((int)f->flags)) return -1;
    if (vfs_is_dir(f->inode)) return -1;
    if (!f->ops || !f->ops->read) return -1;
    return f->ops->read(f, buf, count);
}

int vfs_write(int fd, const void *buf, uint32_t count) {
    file_t *f = vfs_get_file(fd);
    if (!f || !buf) return -1;
    if (!is_open_write((int)f->flags)) return -1;
    if (vfs_is_dir(f->inode)) return -1;
    if (!f->ops || !f->ops->write) return -1;
    return f->ops->write(f, buf, count);
}

int vfs_seek(int fd, int offset, int whence) {
    file_t *f = vfs_get_file(fd);
    if (!f) return -1;
    if (!f->ops || !f->ops->seek) return -1;
    return f->ops->seek(f, offset, whence);
}

int vfs_stat(const char *path, inode_t *st) {
    if (!st) return -1;
    dentry_t *d = vfs_path_lookup(path);
    if (!d || !d->inode) return -1;
    memcpy(st, d->inode, sizeof(inode_t));
    return 0;
}

int vfs_truncate(const char *path, uint32_t size) {
    dentry_t *d = vfs_path_lookup(path);
    if (!d || !d->inode) return -1;
    if (!vfs_is_file(d->inode)) return -1;
    if (!(d->inode->mode & S_IWUSR)) return -1;
    if (!d->inode->ops || !d->inode->ops->truncate) return -1;
    return d->inode->ops->truncate(d->inode, size);
}

static int vfs_try_truncate_inode(inode_t *inode, uint32_t size) {
    if (!inode || !vfs_is_file(inode)) return -1;
    if (!(inode->mode & S_IWUSR)) return -1;
    if (!inode->ops || !inode->ops->truncate) return -1;
    return inode->ops->truncate(inode, size);
}

/* ============================================================
 * 目录/节点操作
 * ============================================================ */

int vfs_mkdir(const char *path, int mode) {
    char parent_path[MAX_PATH];
    char name[MAX_NAME];
    if (split_parent_path(path, parent_path, name) < 0) return -1;
    if (vfs_path_lookup(path)) return -1;
    dentry_t *parent = vfs_path_lookup(parent_path);
    if (!parent || !vfs_is_dir(parent->inode)) return -1;
    return vfs_create_node_under(parent, name, FS_DIR | (uint32_t)mode, NULL, NULL, 0) ? 0 : -1;
}

int vfs_mknod(const char *path, int mode, const char *dev_name) {
    if (!dev_name) return -1;
    char parent_path[MAX_PATH];
    char name[MAX_NAME];
    if (split_parent_path(path, parent_path, name) < 0) return -1;
    if (vfs_path_lookup(path)) return -1;
    dentry_t *parent = vfs_path_lookup(parent_path);
    if (!parent || !vfs_is_dir(parent->inode)) return -1;

    if ((mode & FS_BLOCK_DEVICE) == FS_BLOCK_DEVICE) {
        blockdev_t *bd = blockdev_find(dev_name);
        if (!bd) return -1;
        return vfs_create_node_under(parent, name, FS_BLOCK_DEVICE | S_IRUSR | S_IWUSR,
                                     &vfs_blockdev_ops, bd, blockdev_size_bytes(bd)) ? 0 : -1;
    }

    chardev_t *cd = chardev_find(dev_name);
    if (!cd) return -1;
    return vfs_create_node_under(parent, name, FS_CHAR_DEVICE | S_IRUSR | S_IWUSR,
                                 &vfs_chardev_ops, cd, 0) ? 0 : -1;
}

int vfs_unlink(const char *path) {
    dentry_t *d = vfs_path_lookup(path);
    if (!d || d == root_dentry || !d->parent || !d->inode) return -1;
    if (vfs_is_dir(d->inode)) return -1;
    if (d->mount) return -1;
    detach_child(d);
    free_dentry(d);
    return 0;
}

int vfs_rmdir(const char *path) {
    dentry_t *d = vfs_path_lookup(path);
    if (!d || d == root_dentry || !d->parent || !d->inode) return -1;
    if (!vfs_is_dir(d->inode)) return -1;
    if (d->mount) return -1;
    if (has_children(d)) return -1;
    detach_child(d);
    free_dentry(d);
    return 0;
}

/* ============================================================
 * 新增 VFS 接口占位
 * ============================================================ */
int vfs_chdir(const char *path) {
    char norm[MAX_PATH];
    if (vfs_normalize_path(path, norm, sizeof(norm)) < 0) return -1;
    dentry_t *d = vfs_path_lookup(norm);
    if (!d || !vfs_is_dir(d->inode)) return -1;
    strncpy(fallback_cwd, norm, sizeof(fallback_cwd) - 1);
    fallback_cwd[sizeof(fallback_cwd) - 1] = 0;
    return 0;
}

int vfs_getcwd(char *buf, uint32_t size) {
    if (!buf || size < 2) return -1;
    const char *cwd = vfs_get_cwd();
    uint32_t len = (uint32_t)strlen(cwd);
    if (len + 1 > size) return -1;
    strncpy(buf, cwd, size);
    return 0;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    (void)oldpath; (void)newpath;
    return -1; /* TODO */
}

int vfs_link(const char *oldpath, const char *newpath) {
    (void)oldpath; (void)newpath;
    return -1; /* TODO */
}

int vfs_symlink(const char *target, const char *linkpath) {
    (void)target; (void)linkpath;
    return -1; /* TODO */
}

int vfs_readlink(const char *path, char *buf, uint32_t size) {
    (void)path; (void)buf; (void)size;
    return -1; /* TODO */
}

int vfs_chmod(const char *path, uint32_t mode) {
    (void)path; (void)mode;
    return -1; /* TODO */
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    (void)path; (void)uid; (void)gid;
    return -1; /* TODO */
}

dentry_t *vfs_readdir(const char *path, int index) {
    if (index < 0) return NULL;
    dentry_t *d = vfs_path_lookup(path);
    d = resolve_mount(d);
    if (!d || !vfs_is_dir(d->inode)) return NULL;

    dentry_t *c = d->child;
    int i = 0;
    while (c) {
        if (i == index) return resolve_mount(c);
        c = c->sibling;
        i++;
    }
    return NULL;
}

/* ============================================================
 * 挂载
 * ============================================================ */

int vfs_mount(const char *path, fs_type_t *fs) {
    if (!fs) return -1;
    dentry_t *mp = vfs_path_lookup_internal(path, 0);
    if (!mp || !vfs_is_dir(mp->inode)) return -1;
    if (mp->mount) return -1;

    inode_t *root = NULL;
    if (fs->lookup) root = fs->lookup(fs, "/");
    if (!root) {
        root = alloc_inode();
        if (!root) return -1;
        root->mode = FS_DIR | S_IRUSR | S_IWUSR;
        root->fs_type = fs->magic;
        root->fs_data = fs->data;
        if (fs->magic == TMPFS_MAGIC) {
            tmpfs_setup_inode(root, root->mode);
        } else {
            ramfs_setup_inode(root, root->mode);
        }
    }

    dentry_t *md = alloc_dentry();
    if (!md) return -1;
    strcpy(md->name, mp->name);
    md->inode = root;
    md->parent = mp->parent ? mp->parent : mp;

    mp->mount = md;

    mount_t *m = alloc_mount();
    if (!m) {
        mp->mount = NULL;
        free_dentry(md);
        return -1;
    }
    m->fs = fs;
    m->mountpoint = mp;
    m->root = root;
    m->next = mount_list;
    mount_list = m;
    return 0;
}

int vfs_umount(const char *path) {
    dentry_t *mp = vfs_path_lookup_internal(path, 0);
    if (!mp) return -1;

    mount_t **pp = &mount_list;
    while (*pp) {
        mount_t *m = *pp;
        if (m->mountpoint == mp) {
            if (!mp->mount) return -1;
            if (mp->mount->child) return -1;
            dentry_t *mounted = mp->mount;
            mp->mount = NULL;
            *pp = m->next;
            free_dentry(mounted);
            free_mount(m);
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}
