/* ============================================================
 * openos - 虚拟文件系统 (VFS) - Phase 3
 * ============================================================ */

#ifndef KERNEL_FS_VFS_H
#define KERNEL_FS_VFS_H

#include <stdint.h>

/* 文件类型 */
#define FS_FILE      1
#define FS_DIR       2
#define FS_DEVICE    3
#define FS_PIPE      4

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

/* 最大文件描述符 */
#define MAX_FD       16
#define MAX_FDS_TOTAL 256

/* 最大路径长度 */
#define MAX_PATH     256

/* 最大文件名 */
#define MAX_NAME     32

/* ---- 文件操作表 ---- */
struct file_ops;
struct inode;
struct file;
struct dentry;

typedef int (*open_fn_t)(struct file *f);
typedef int (*close_fn_t)(struct file *f);
typedef int (*read_fn_t)(struct file *f, void *buf, uint32_t count);
typedef int (*write_fn_t)(struct file *f, const void *buf, uint32_t count);
typedef int (*seek_fn_t)(struct file *f, int offset, int whence);
typedef struct dentry *(*readdir_fn_t)(struct file *f);

typedef struct file_ops {
    open_fn_t   open;
    close_fn_t  close;
    read_fn_t   read;
    write_fn_t  write;
    seek_fn_t   seek;
    readdir_fn_t readdir;
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
    file_ops_t *ops;        /* 文件操作 */
} inode_t;

/* ---- 目录项 ---- */
typedef struct dentry {
    char name[MAX_NAME];
    struct inode *inode;
    struct dentry *parent;
    struct dentry *child;      /* 第一个子项 */
    struct dentry *sibling;    /* 兄弟链表 */
    struct dentry *mount;      /* 挂载点 (非NULL=挂载了其他FS) */
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

/* 目录操作 */
int    vfs_mkdir(const char *path, int mode);
int    vfs_rmdir(const char *path);
int    vfs_unlink(const char *path);
dentry_t *vfs_readdir(const char *path, int index);

/* 挂载 */
int    vfs_mount(const char *path, fs_type_t *fs);
int    vfs_umount(const char *path);

/* 路径解析 */
dentry_t *vfs_path_lookup(const char *path);

/* 进程文件描述符管理 */
void   vfs_init_fds(void);                        /* 初始化当前进程的 fd 表 */
file_t *vfs_get_file(int fd);                     /* 通过 fd 获取 file */
int    vfs_alloc_fd(void);                        /* 分配一个空闲 fd */

#endif /* KERNEL_FS_VFS_H */
