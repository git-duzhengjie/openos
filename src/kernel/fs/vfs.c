/* ============================================================
 * openos - VFS 实现 (Phase 3)
 * ============================================================ */

#include "vfs.h"
#include "serial.h"
#include "pmm.h"
#include "string.h"
#include "ramfs.h"
#include "chardev.h"
#include "blockdev.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ---- 全局状态 ---- */
static dentry_t *root_dentry;       /* 根目录项 */
static inode_t  *root_inode;        /* 根 inode */
static mount_t  *mount_list;        /* 挂载点链表 */

/* 进程 fd 表 - Phase 3 简化：全局共享 */
static file_t   *fd_table[MAX_FDS_TOTAL];

/* inode 分配 */
#define MAX_INODES 256
static inode_t  inode_pool[MAX_INODES];
static uint32_t next_ino = 1;

/* dentry 缓存 */
#define MAX_DENTRY 256
static dentry_t dentry_pool[MAX_DENTRY];

/* ---- inode 分配 ---- */
static inode_t *alloc_inode(void) {
    for (int i = 0; i < MAX_INODES; i++) {
        if (inode_pool[i].ref_count == 0) {
            inode_t *ip = &inode_pool[i];
            ip->ino = next_ino++;
            ip->ref_count = 1;
            ip->nlinks = 1;
            ip->size = 0;
            ip->fs_data = NULL;
            ip->ops = NULL;
            return ip;
        }
    }
    return NULL;
}

static void free_inode(inode_t *ip) {
    if (!ip) return;
    ip->ref_count--;
    if (ip->ref_count <= 0) {
        ip->ref_count = 0;
        ip->ino = 0;
        ip->size = 0;
    }
}

/* ---- dentry 分配 ---- */
static dentry_t *alloc_dentry(void) {
    for (int i = 0; i < MAX_DENTRY; i++) {
        if (dentry_pool[i].inode == NULL && dentry_pool[i].name[0] == '\0') {
            dentry_t *d = &dentry_pool[i];
            d->inode = NULL;
            d->parent = NULL;
            d->child = NULL;
            d->sibling = NULL;
            d->mount = NULL;
            for (int j = 0; j < MAX_NAME; j++) d->name[j] = 0;
            return d;
        }
    }
    return NULL;
}

static void free_dentry(dentry_t *d) {
    if (!d) return;
    if (d->inode) free_inode(d->inode);
    d->inode = NULL;
    d->name[0] = '\0';
    d->parent = NULL;
    d->child = NULL;
    d->sibling = NULL;
}

/* ---- 路径解析 ---- */
/* 分割路径为组件，返回组件数 */
static int split_path(const char *path, char parts[][MAX_NAME], int max_parts) {
    int count = 0;
    int i = 0;
    
    /* 跳过前导 / */
    while (path[i] == '/') i++;
    
    while (path[i] && count < max_parts) {
        int j = 0;
        while (path[i] && path[i] != '/' && j < MAX_NAME - 1) {
            parts[count][j++] = path[i++];
        }
        parts[count][j] = '\0';
        if (j > 0) count++;
        while (path[i] == '/') i++;
    }
    return count;
}

dentry_t *vfs_path_lookup(const char *path) {
    if (!path) return NULL;
    serial_write("[VFS] lookup: \"");
    serial_write(path);
    serial_write("\"\n");
    /* handle "." and "./path" */
    if (path[0] == 0x2E && (path[1] == 0x2F || path[1] == 0x00)) {  /* "." or "./" */
        serial_write("[VFS] dot -> root\n");
        return root_dentry;
    }
    if (path[0] == 0x2F && path[1] == 0x2E && path[2] == 0x00) {  /* "/." */
        serial_write("[VFS] slashdot -> root\n");
        return root_dentry;
    }
    if (path[0] != 0x2F) return NULL;
    if (path[1] == '\0') return root_dentry;  /* "/" */
    
    char parts[16][MAX_NAME];
    int n = split_path(path, parts, 16);
    if (n == 0) return root_dentry;
    
    dentry_t *cur = root_dentry;
    
    for (int i = 0; i < n; i++) {
        /* 跳过 "." 部分 */
        if (parts[i][0] == 0x2E && parts[i][1] == 0x00) continue;
        
        /* 检查挂载点 */
        if (cur->mount) cur = cur->mount;
        
        /* 在子项中查找 */
        dentry_t *child = cur->child;
        while (child) {
            int match = 1;
            for (int j = 0; j < MAX_NAME; j++) {
                if (child->name[j] != parts[i][j]) { match = 0; break; }
                if (child->name[j] == '\0') break;
            }
            if (match) { cur = child; break; }
            child = child->sibling;
        }
        if (!child) return NULL;  /* 未找到 */
    }
    
    if (cur->mount) return cur->mount;
    return cur;
}

/* ---- 字符设备节点操作 ---- */
static int vfs_chardev_open(file_t *f) {
    if (!f || !f->inode || !f->inode->fs_data) return -1;
    return chardev_open((chardev_t *)f->inode->fs_data);
}

static int vfs_chardev_close(file_t *f) {
    if (!f || !f->inode || !f->inode->fs_data) return -1;
    return chardev_close((chardev_t *)f->inode->fs_data);
}

static int vfs_chardev_read(file_t *f, void *buf, uint32_t count) {
    if (!f || !f->inode || !f->inode->fs_data) return -1;
    return chardev_read((chardev_t *)f->inode->fs_data, buf, count);
}

static int vfs_chardev_write(file_t *f, const void *buf, uint32_t count) {
    if (!f || !f->inode || !f->inode->fs_data) return -1;
    return chardev_write((chardev_t *)f->inode->fs_data, buf, count);
}

static file_ops_t vfs_chardev_ops = {
    vfs_chardev_open,
    vfs_chardev_close,
    vfs_chardev_read,
    vfs_chardev_write,
    0,
    0
};

/* ---- 块设备节点操作 ----
 * VFS 对块设备暴露为可 seek 的字节流，内部按 sector 做整块读写。
 * 非 sector 对齐的 read/write 使用一个 512B 临时 bounce buffer。
 */
static int vfs_blockdev_open(file_t *f) {
    if (!f || !f->inode || !f->inode->fs_data) return -1;
    f->offset = 0;
    return blockdev_open((blockdev_t *)f->inode->fs_data);
}

static int vfs_blockdev_close(file_t *f) {
    if (!f || !f->inode || !f->inode->fs_data) return -1;
    return blockdev_close((blockdev_t *)f->inode->fs_data);
}

static int vfs_blockdev_read(file_t *f, void *buf, uint32_t count) {
    blockdev_t *dev;
    uint8_t *out;
    uint8_t sector_buf[BLOCKDEV_SECTOR_SIZE_DEFAULT];
    uint32_t total_size;
    uint32_t done = 0;

    if (!f || !f->inode || !f->inode->fs_data || !buf) return -1;
    dev = (blockdev_t *)f->inode->fs_data;
    if (dev->sector_size != BLOCKDEV_SECTOR_SIZE_DEFAULT) return -1;
    total_size = blockdev_size_bytes(dev);
    if ((uint32_t)f->offset >= total_size) return 0;
    if (count > total_size - (uint32_t)f->offset) count = total_size - (uint32_t)f->offset;

    out = (uint8_t *)buf;
    while (done < count) {
        uint32_t pos = (uint32_t)f->offset;
        uint32_t lba = pos / dev->sector_size;
        uint32_t off = pos % dev->sector_size;
        uint32_t chunk = dev->sector_size - off;
        if (chunk > count - done) chunk = count - done;

        if (off == 0 && chunk == dev->sector_size) {
            if (blockdev_read_blocks(dev, lba, 1, out + done) != 1) return done ? (int)done : -1;
        } else {
            if (blockdev_read_blocks(dev, lba, 1, sector_buf) != 1) return done ? (int)done : -1;
            memcpy(out + done, sector_buf + off, chunk);
        }
        f->offset += chunk;
        done += chunk;
    }
    return (int)done;
}

static int vfs_blockdev_write(file_t *f, const void *buf, uint32_t count) {
    blockdev_t *dev;
    const uint8_t *in;
    uint8_t sector_buf[BLOCKDEV_SECTOR_SIZE_DEFAULT];
    uint32_t total_size;
    uint32_t done = 0;

    if (!f || !f->inode || !f->inode->fs_data || !buf) return -1;
    dev = (blockdev_t *)f->inode->fs_data;
    if (dev->sector_size != BLOCKDEV_SECTOR_SIZE_DEFAULT) return -1;
    total_size = blockdev_size_bytes(dev);
    if ((uint32_t)f->offset >= total_size) return 0;
    if (count > total_size - (uint32_t)f->offset) count = total_size - (uint32_t)f->offset;

    in = (const uint8_t *)buf;
    while (done < count) {
        uint32_t pos = (uint32_t)f->offset;
        uint32_t lba = pos / dev->sector_size;
        uint32_t off = pos % dev->sector_size;
        uint32_t chunk = dev->sector_size - off;
        if (chunk > count - done) chunk = count - done;

        if (off == 0 && chunk == dev->sector_size) {
            if (blockdev_write_blocks(dev, lba, 1, in + done) != 1) return done ? (int)done : -1;
        } else {
            if (blockdev_read_blocks(dev, lba, 1, sector_buf) != 1) return done ? (int)done : -1;
            memcpy(sector_buf + off, in + done, chunk);
            if (blockdev_write_blocks(dev, lba, 1, sector_buf) != 1) return done ? (int)done : -1;
        }
        f->offset += chunk;
        done += chunk;
    }
    return (int)done;
}

static int vfs_blockdev_seek(file_t *f, int offset, int whence) {
    blockdev_t *dev;
    int base;
    int newpos;

    if (!f || !f->inode || !f->inode->fs_data) return -1;
    dev = (blockdev_t *)f->inode->fs_data;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = f->offset;
    else if (whence == SEEK_END) base = (int)blockdev_size_bytes(dev);
    else return -1;

    newpos = base + offset;
    if (newpos < 0) return -1;
    if ((uint32_t)newpos > blockdev_size_bytes(dev)) return -1;
    f->offset = newpos;
    return newpos;
}

static file_ops_t vfs_blockdev_ops = {
    vfs_blockdev_open,
    vfs_blockdev_close,
    vfs_blockdev_read,
    vfs_blockdev_write,
    vfs_blockdev_seek,
    0
};

/* ---- 在目录下创建目录项 ---- */
static dentry_t *create_dentry_under(dentry_t *parent, const char *name, uint32_t mode) {
    serial_write("[CREATE] enter, parent=0x");
    serial_write_hex((uint32_t)parent);
    serial_write("\n");
    if (!parent) return NULL;
    
    dentry_t *d = alloc_dentry();
    if (!d) {
        serial_write("[CREATE] alloc_dentry failed\n");
        return NULL;
    }
    serial_write("[CREATE] d=0x");
    serial_write_hex((uint32_t)d);
    serial_write("\n");
    
    inode_t *ip = alloc_inode();
    if (!ip) {
        serial_write("[CREATE] alloc_inode failed\n");
        free_dentry(d);
        return NULL;
    }
    serial_write("[CREATE] ip=0x");
    serial_write_hex((uint32_t)ip);
    serial_write("\n");
    
    ip->mode = mode;
    d->inode = ip;
    for (int i = 0; i < MAX_NAME && name[i]; i++)
        d->name[i] = name[i];
    d->parent = parent;
    
    /* 调用 ramfs ops，默认文件系统；设备节点使用专用 ops */
    if ((mode & 0xF000) == FS_CHAR_DEVICE || (mode & 0xF000) == FS_DEVICE) {
        ip->fs_type = FS_CHAR_DEVICE;
        ip->ops = &vfs_chardev_ops;
    } else if ((mode & 0xF000) == FS_BLOCK_DEVICE) {
        ip->fs_type = FS_BLOCK_DEVICE;
        ip->ops = &vfs_blockdev_ops;
    } else {
        ramfs_setup_inode(ip, mode);
    }
    
    /* 链接到父目录的链表 */
    serial_write("[CREATE] before link: parent->child=0x");
    serial_write_hex((uint32_t)parent->child);
    serial_write("\n");
    if (!parent->child) {
        parent->child = d;
        serial_write("[CREATE] parent->child was NULL, set to d\n");
    } else {
        dentry_t *sib = parent->child;
        while (sib->sibling) sib = sib->sibling;
        sib->sibling = d;
        serial_write("[CREATE] appended to sibling list\n");
    }
    serial_write("[CREATE] after link: parent->child=0x");
    serial_write_hex((uint32_t)parent->child);
    serial_write(" child->sibling=0x");
    serial_write_hex(parent->child ? (uint32_t)parent->child->sibling : 0);
    serial_write(" d->sibling=0x");
    serial_write_hex((uint32_t)d->sibling);
    serial_write("\n");
    
    return d;
}


/* ---- VFS 初始化 ---- */
void vfs_init(void) {
    /* 清空所有池 */
    for (int i = 0; i < MAX_INODES; i++) {
        inode_pool[i].ref_count = 0;
        inode_pool[i].ino = 0;
    }
    for (int i = 0; i < MAX_DENTRY; i++) {
        dentry_pool[i].inode = NULL;
        dentry_pool[i].name[0] = '\0';
    }
    for (int i = 0; i < MAX_FDS_TOTAL; i++) {
        fd_table[i] = NULL;
    }
    mount_list = NULL;
    
    /* 创建根目录 */
    root_inode = alloc_inode();
    root_inode->mode = FS_DIR | S_IRUSR | S_IWUSR;
    root_inode->size = 0;
    
    root_dentry = alloc_dentry();
    root_dentry->inode = root_inode;
    root_dentry->name[0] = '/';
    root_dentry->name[1] = '\0';
    root_dentry->parent = root_dentry;  /* 根的 parent 是自己 */
    
    /* 根目录默认使用 ramfs ops */
    ramfs_setup_inode(root_inode, root_inode->mode);
    
    serial_write("[OK] VFS initialized\n");
}

/* ---- fd 管理 ---- */
int vfs_alloc_fd(void) {
    for (int i = 0; i < MAX_FDS_TOTAL; i++) {
        if (fd_table[i] == NULL) return i;
    }
    return -1;
}

file_t *vfs_get_file(int fd) {
    if (fd < 0 || fd >= MAX_FDS_TOTAL) return NULL;
    return fd_table[fd];
}

void vfs_init_fds(void) {
    /* Phase 3: 全局 fd 表已在 vfs_init 清空 */
}

/* ---- 文件操作 ---- */
static dentry_t *create_dentry_under(dentry_t *parent, const char *name, uint32_t mode);
int vfs_open(const char *path, int flags, int mode) {
    dentry_t *d = vfs_path_lookup(path);
    
    if (!d) {
        /* 文件不存在，O_CREAT 则创建 */
        if (!(flags & O_CREAT)) return -1;
        
        /* 找到父目录 */
        /* 提取父路径和文件名 */
        char parent_path[MAX_PATH];
        char fname[MAX_NAME];
        int i;
        
        /* 找最后一个 / */
        int last_slash = -1;
        for (i = 0; path[i]; i++) {
            if (path[i] == '/') last_slash = i;
        }
        
        if (last_slash <= 0) {
            parent_path[0] = '/';
            parent_path[1] = '\0';
        } else {
            for (i = 0; i < last_slash && i < MAX_PATH - 1; i++)
                parent_path[i] = path[i];
            parent_path[i] = '\0';
        }
        
        int fi = 0;
        for (i = last_slash + 1; path[i] && fi < MAX_NAME - 1; i++)
            fname[fi++] = path[i];
        fname[fi] = '\0';
        
        dentry_t *parent = vfs_path_lookup(parent_path);
        if (!parent) return -1;
        
        d = create_dentry_under(parent, fname, FS_FILE | (mode & 0xFFFF));
        if (!d) return -1;
        
        /* 如果有文件系统 ops，调用 create */
        if (d->inode && d->inode->ops && d->inode->ops->open) {
            /* 将在 file 分配后调用 */
        }
    }
    
    if (!d || !d->inode) return -1;
    
    /* 分配 file 和 fd */
    file_t *f = (file_t *)pmm_alloc_page();
    if (!f) return -1;
    for (int i = 0; i < (int)sizeof(file_t); i++) ((char *)f)[i] = 0;
    
    f->inode = d->inode;
    f->dentry = d;
    f->flags = flags & 0xFF;
    f->offset = 0;
    f->ref_count = 1;
    f->ops = d->inode->ops;
    
    d->inode->ref_count++;
    
    int fd = vfs_alloc_fd();
    if (fd < 0) {
        pmm_free_page(f);
        return -1;
    }
    fd_table[fd] = f;
    
    /* 调用文件系统 open */
    if (f->ops && f->ops->open) {
        if (f->ops->open(f) < 0) {
            fd_table[fd] = NULL;
            if (f->inode) free_inode(f->inode);
            pmm_free_page(f);
            return -1;
        }
    }
    
    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS_TOTAL) return -1;
    file_t *f = fd_table[fd];
    if (!f) return -1;
    
    if (f->ops && f->ops->close) {
        f->ops->close(f);
    }
    
    if (f->inode) free_inode(f->inode);
    pmm_free_page(f);
    fd_table[fd] = NULL;
    return 0;
}

int vfs_read(int fd, void *buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_FDS_TOTAL) {
        serial_write("[VFS_READ] Invalid fd\n");
        return -1;
    }
    file_t *f = fd_table[fd];
    if (!f) {
        serial_write("[VFS_READ] No file for fd\n");
        return -1;
    }
    if (!f->ops || !f->ops->read) {
        serial_write("[VFS_READ] No ops/read: ops=0x");
        serial_write_hex((uint32_t)f->ops);
        if (f->ops) {
            serial_write(" ops->read=0x");
            serial_write_hex((uint32_t)f->ops->read);
        }
        serial_write("\n");
        return -1;
    }
    return f->ops->read(f, buf, count);
}

int vfs_write(int fd, const void *buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_FDS_TOTAL) return -1;
    file_t *f = fd_table[fd];
    if (!f || !f->ops || !f->ops->write) return -1;
    return f->ops->write(f, buf, count);
}

int vfs_seek(int fd, int offset, int whence) {
    file_t *f;
    uint32_t new_off;

    if (fd < 0 || fd >= MAX_FDS_TOTAL) return -1;
    f = fd_table[fd];
    if (!f) return -1;

    if (f->ops && f->ops->seek) {
        return f->ops->seek(f, offset, whence);
    }

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
        if (!f->inode) return -1;
        if (offset < 0 && (uint32_t)(-offset) > f->inode->size) return -1;
        new_off = f->inode->size + offset;
        break;
    default:
        return -1;
    }

    f->offset = new_off;
    return (int)new_off;
}

int vfs_mkdir(const char *path, int mode) {
    serial_write("[MKDIR] enter, path=\"");
    serial_write(path ? path : "(null)");
    serial_write("\"\n");
    dentry_t *d = vfs_path_lookup(path);
    if (d) {
        serial_write("[MKDIR] already exists\n");
        return -1;  /* already exists */
    }
    
    /* find parent directory */
    char parent_path[MAX_PATH];
    char dirname[MAX_NAME];
    int last_slash = -1, i;
    
    for (i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;
    
    if (last_slash <= 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (i = 0; i < last_slash && i < MAX_PATH - 1; i++)
            parent_path[i] = path[i];
        parent_path[i] = '\0';
    }
    
    int fi = 0;
    for (i = last_slash + 1; path[i] && fi < MAX_NAME - 1; i++)
        dirname[fi++] = path[i];
    dirname[fi] = '\0';
    
    serial_write("[MKDIR] parent_path=\"");
    serial_write(parent_path);
    serial_write("\" dirname=\"");
    serial_write(dirname);
    serial_write("\"\n");
    
    dentry_t *parent = vfs_path_lookup(parent_path);
    if (!parent) {
        serial_write("[MKDIR] parent not found\n");
        return -1;
    }
    
    serial_write("[MKDIR] calling create_dentry_under\n");
    dentry_t *nd = create_dentry_under(parent, dirname, FS_DIR | (mode & 0xFFFF));
    serial_write("[MKDIR] result: nd=0x");
    serial_write_hex((uint32_t)nd);
    serial_write("\n");
    return nd ? 0 : -1;
}



int vfs_mknod(const char *path, int mode, const char *dev_name) {
    dentry_t *d;
    dentry_t *parent;
    chardev_t *cdev = NULL;
    blockdev_t *bdev = NULL;
    uint32_t dev_type;
    char parent_path[MAX_PATH];
    char node_name[MAX_NAME];
    int last_slash = -1;
    int i;
    int ni;

    if (!path || !dev_name) return -1;

    dev_type = (uint32_t)mode & 0xF000;
    if (dev_type == FS_DEVICE) dev_type = FS_CHAR_DEVICE;

    if (dev_type == FS_CHAR_DEVICE) {
        cdev = chardev_find(dev_name);
        if (!cdev) return -1;
    } else if (dev_type == FS_BLOCK_DEVICE) {
        bdev = blockdev_find(dev_name);
        if (!bdev) return -1;
    } else {
        return -1;
    }

    d = vfs_path_lookup(path);
    if (d) return -1;

    for (i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash <= 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (i = 0; i < last_slash && i < MAX_PATH - 1; i++) {
            parent_path[i] = path[i];
        }
        parent_path[i] = '\0';
    }

    ni = 0;
    for (i = last_slash + 1; path[i] && ni < MAX_NAME - 1; i++) {
        node_name[ni++] = path[i];
    }
    node_name[ni] = '\0';

    if (node_name[0] == '\0') return -1;

    parent = vfs_path_lookup(parent_path);
    if (!parent || !parent->inode || (parent->inode->mode & 0xF000) != FS_DIR) return -1;

    d = create_dentry_under(parent, node_name, ((uint32_t)mode & ~0xF000U) | dev_type);
    if (!d || !d->inode) return -1;

    if (dev_type == FS_CHAR_DEVICE) {
        d->inode->fs_data = cdev;
        d->inode->size = 0;
    } else {
        d->inode->fs_data = bdev;
        d->inode->size = blockdev_size_bytes(bdev);
    }
    return 0;
}

int vfs_rmdir(const char *path) {
    dentry_t *d = vfs_path_lookup(path);
    if (!d || !d->inode || (d->inode->mode & 0xF000) != FS_DIR) return -1;
    if (d->child) return -1;  /* 非空目录 */
    
    /* 从父目录的子链表中移除 */
    dentry_t *parent = d->parent;
    if (parent) {
        if (parent->child == d) {
            parent->child = d->sibling;
        } else {
            dentry_t *sib = parent->child;
            while (sib && sib->sibling != d) sib = sib->sibling;
            if (sib) sib->sibling = d->sibling;
        }
    }
    
    free_dentry(d);
    return 0;
}

int vfs_unlink(const char *path) {
    dentry_t *d = vfs_path_lookup(path);
    if (!d || !d->inode) return -1;
    if ((d->inode->mode & 0xF000) == FS_DIR) return -1;
    
    dentry_t *parent = d->parent;
    if (parent) {
        if (parent->child == d) {
            parent->child = d->sibling;
        } else {
            dentry_t *sib = parent->child;
            while (sib && sib->sibling != d) sib = sib->sibling;
            if (sib) sib->sibling = d->sibling;
        }
    }
    
    free_dentry(d);
    return 0;
}

dentry_t *vfs_readdir(const char *path, int index) {
    dentry_t *d = vfs_path_lookup(path);
    if (!d || !d->inode || (d->inode->mode & 0xF000) != FS_DIR) return NULL;
    
    dentry_t *child = d->child;
    for (int i = 0; i < index && child; i++)
        child = child->sibling;
    return child;
}

int vfs_stat(const char *path, inode_t *st) {
    dentry_t *d = vfs_path_lookup(path);
    if (!d || !d->inode) return -1;
    if (st) {
        st->ino = d->inode->ino;
        st->mode = d->inode->mode;
        st->size = d->inode->size;
        st->nlinks = d->inode->nlinks;
    }
    return 0;
}

int vfs_mount(const char *path, fs_type_t *fs) {
    dentry_t *d = vfs_path_lookup(path);
    if (!d) return -1;
    
    mount_t *m = (mount_t *)pmm_alloc_page();
    if (!m) return -1;
    m->fs = fs;
    m->mountpoint = d;
    m->root = NULL;  /* FS 自己设置 */
    m->next = mount_list;
    mount_list = m;
    
    /* 标记挂载点 */
    d->mount = m->root ? (dentry_t *)1 : NULL;  /* 简化 */
    
    serial_write("[VFS] Mounted ");
    serial_write((char *)path);
    serial_write("\n");
    return 0;
}

int vfs_umount(const char *path) {
    dentry_t *d = vfs_path_lookup(path);
    if (!d) return -1;
    
    mount_t **pp = &mount_list;
    while (*pp) {
        if ((*pp)->mountpoint == d) {
            *pp = (*pp)->next;
            d->mount = NULL;
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}
