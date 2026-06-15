/* ============================================================
 * openos - 虚拟文件系统 (VFS) - Phase 3
 * ============================================================ */

#ifndef KERNEL_FS_VFS_H
#define KERNEL_FS_VFS_H

#include <stdint.h>
#include "fd.h"

/* 文件类型 */
#define FS_FILE          0x1000
#define FS_DIR           0x2000
#define FS_CHAR_DEVICE   0x4000
#define FS_BLOCK_DEVICE  0x6000
#define FS_DEVICE        FS_CHAR_DEVICE  /* 兼容旧代码：默认设备节点为字符设备 */
#define FS_PIPE          0x8000

/* 打开标志 */
#define O_RDONLY     0
#define O_WRONLY     1
#define O_RDWR       2
#define O_CREAT      0x100
#define O_TRUNC      0x200

/* 文件权限 */
#define S_IRUSR      0400
#define S_IWUSR      0200

/* whence for seek */
#define SEEK_SET     0
#define SEEK_CUR     1
#define SEEK_END     2

/* 最大路径长度 */
#define MAX_PATH     256

/* 最大文件名 */
#define MAX_NAME     32

/* ---- inode 操作表 ---- */
struct inode_ops;
struct file_ops;
struct inode;
struct file;
struct dentry;

typedef struct inode *(*lookup_fn_t)(struct inode *dir, const char *name);
typedef int (*create_fn_t)(struct inode *dir, const char *name, uint32_t mode);
typedef int (*mkdir_fn_t)(struct inode *dir, const char *name, uint32_t mode);
typedef int (*unlink_fn_t)(struct inode *dir, const char *name);
typedef int (*rmdir_fn_t)(struct inode *dir, const char *name);
typedef int (*link_fn_t)(struct inode *dir, const char *name, struct inode *target);
typedef int (*symlink_fn_t)(struct inode *dir, const char *name, const char *target);
typedef int (*readlink_fn_t)(struct inode *inode, char *buf, uint32_t size);
typedef int (*rename_fn_t)(struct inode *old_dir, const char *old_name,
                           struct inode *new_dir, const char *new_name);
typedef int (*chmod_fn_t)(struct inode *inode, uint32_t mode);
typedef int (*chown_fn_t)(struct inode *inode, uint32_t uid, uint32_t gid);

typedef struct inode_ops {
    lookup_fn_t  lookup;
    create_fn_t  create;
    mkdir_fn_t   mkdir;
    unlink_fn_t  unlink;
    rmdir_fn_t   rmdir;
    link_fn_t    link;
    symlink_fn_t symlink;
    readlink_fn_t readlink;
    rename_fn_t  rename;
    chmod_fn_t   chmod;
    chown_fn_t   chown;
} inode_ops_t;

/* ---- 文件操作表 ---- */
typedef int (*open_fn_t)(struct file *f);
typedef int (*close_fn_t)(struct file *f);
typedef int (*read_fn_t)(struct file *f, void *buf, uint32_t count);
typedef int (*write_fn_t)(struct file *f, const void *buf, uint32_t count);
typedef int (*seek_fn_t)(struct file *f, int offset, int whence);
typedef int (*truncate_fn_t)(struct inode *inode, uint32_t size);
typedef struct dentry *(*readdir_fn_t)(struct file *f);

typedef struct file_ops {
    open_fn_t     open;
    close_fn_t    close;
    read_fn_t     read;
    write_fn_t    write;
    seek_fn_t     seek;
    truncate_fn_t truncate;
    readdir_fn_t  readdir;
} file_ops_t;

/* ---- inode ---- */
typedef struct inode {
    uint32_t ino;           /* inode 号 */
    uint32_t mode;          /* 文件类型 + 权限 */
    uint32_t size;          /* 文件大小 */
    uint32_t nlinks;        /* 硬链接数 */
    uint32_t ref_count;     /* 引用计数 */
    uint32_t fs_type;       /* 所属文件系统类型 */
    void     *fs_data;      /* 文件系统私有数据 */
    inode_ops_t *iops;      /* inode 操作 */
    file_ops_t *ops;        /* 文件操作 */
} inode_t;

/* ---- 目录项 ---- */
typedef struct dentry {
    char name[MAX_NAME];
    struct inode *inode;
    struct dentry *parent;
    struct dentry *child;      /* 第一个子项 */
    struct dentry *sibling;    /* 兄弟链表 */
    struct dentry *mount;      /* 挂载覆盖后的根目录项 */
} dentry_t;

/* ---- 打开的文件 ---- */
typedef struct file {
    inode_t   *inode;
    dentry_t  *dentry;
    uint32_t   flags;         /* 打开标志 */
    uint32_t   offset;        /* 当前偏移 */
    uint32_t   ref_count;
    file_ops_t *ops;
} file_t;

/* ---- 文件系统类型 ---- */
typedef struct fs_type {
    char name[16];
    uint32_t magic;
    inode_t *(*lookup)(struct fs_type *fs, const char *path);
    inode_t *(*create)(struct fs_type *fs, const char *name, uint32_t mode);
    int (*mkdir)(struct fs_type *fs, const char *name);
    int (*unlink)(struct fs_type *fs, const char *name);
    void *data;               /* 文件系统私有数据 */
} fs_type_t;

/* ---- 挂载点 ---- */
typedef struct mount {
    fs_type_t *fs;
    dentry_t  *mountpoint;    /* 挂载到的目录项 */
    inode_t   *root;          /* 文件系统根 inode */
    struct mount *next;
} mount_t;

/* ============================================================
 * VFS 全局接口
 * ============================================================ */

/* 初始化 */
void vfs_init(void);

/* 文件操作 */
int    vfs_open(const char *path, int flags, int mode);
int    vfs_close(int fd);
int    vfs_read(int fd, void *buf, uint32_t count);
int    vfs_write(int fd, const void *buf, uint32_t count);
int    vfs_seek(int fd, int offset, int whence);
int    vfs_stat(const char *path, inode_t *st);
int    vfs_truncate(const char *path, uint32_t size);

/* 目录/节点操作 */
int    vfs_mkdir(const char *path, int mode);
int    vfs_mknod(const char *path, int mode, const char *dev_name);
int    vfs_rmdir(const char *path);
int    vfs_unlink(const char *path);
int    vfs_rename(const char *oldpath, const char *newpath);
int    vfs_link(const char *oldpath, const char *newpath);
int    vfs_symlink(const char *target, const char *linkpath);
int    vfs_readlink(const char *path, char *buf, uint32_t size);
int    vfs_chmod(const char *path, uint32_t mode);
int    vfs_chown(const char *path, uint32_t uid, uint32_t gid);
dentry_t *vfs_readdir(const char *path, int index);

/* 挂载 */
int    vfs_mount(const char *path, fs_type_t *fs);
int    vfs_umount(const char *path);

/* 路径解析 */
dentry_t *vfs_path_lookup(const char *path);
int    vfs_normalize_path(const char *path, char *out, uint32_t out_size);

/* 文件系统驱动内部辅助：在指定目录项下创建节点 */
dentry_t *vfs_create_node_under(dentry_t *parent, const char *name,
                                uint32_t mode, file_ops_t *ops,
                                void *fs_data, uint32_t size);

/* 进程文件描述符管理 */
void   vfs_init_fds_for_process(void *proc);       /* 初始化给定进程的 fd 表 */
void   vfs_init_fds(void);                        /* 初始化当前进程的 fd 表 */
file_t *vfs_get_file(int fd);                     /* 通过 fd 获取 file */
int    vfs_alloc_fd(void);                        /* 分配一个空闲 fd */
int    vfs_put_file(int fd, file_t *file);        /* 把 file 放入 fd 位置 */
void   vfs_close_fds_for_process(void *proc);      /* 关闭指定进程的 fd 表 */

/* 进程 cwd 管理 */
int    vfs_chdir(const char *path);               /* 切换当前工作目录 */
int    vfs_getcwd(char *buf, uint32_t size);      /* 获取当前工作目录 */

/* fd duplication */
int    vfs_dup(int oldfd);
int    vfs_dup2(int oldfd, int newfd);

#endif /* KERNEL_FS_VFS_H */
