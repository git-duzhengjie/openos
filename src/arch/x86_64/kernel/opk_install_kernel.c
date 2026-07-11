/*
 * Kernel-side .opk installer bridge (M5.4c).
 *
 * Wires the pure opk_install() core to the writable ramfs VFS
 * (vfs_mkdir / vfs_open / vfs_write / vfs_close). This is the only file
 * that depends on the kernel VFS; the installer logic itself stays in
 * opk_install.c and remains host-testable.
 */

#include "opk_install.h"

/* early serial console for install diagnostics */
extern void early_console64_write(const char *s);
extern void early_console64_write_hex64(uint64_t value);

/* writable VFS entry points (implemented in gui64/ramfs64.c) */
extern int vfs_mkdir(const char *path, int mode);
extern int vfs_open(const char *path, int flags, int mode);
extern int vfs_write(int fd, const void *buf, unsigned int count);
extern int vfs_close(int fd);
/* vfs_stat fills a full inode_t (see kernel/core/fs/vfs.h). We only read the
 * mode field to detect a directory, but the backing buffer MUST be large
 * enough to hold the entire inode_t or vfs_stat overflows our stack frame
 * and smashes the caller's path buffer (root-caused a truncation bug where
 * write_file received "/pkg" instead of the full entry path). inode_t is
 * 8*u32 + 3*ptr + 3*vfs_time_t ~= 100 bytes; reserve a generous margin. */
struct opk_kstat { uint32_t ino; uint32_t mode; uint32_t _rest[48]; };
extern int vfs_stat(const char *path, void *st);
#define OPK_FS_DIR 0x2000u  /* FS_DIR type bit (see vfs.h) */

/* open flags mirror kernel/core/fs/vfs.h */
#define OPK_O_WRONLY 1
#define OPK_O_CREAT  0x100
#define OPK_O_TRUNC  0x200

static int kfs_mkdir(void *ctx, const char *path, uint32_t mode) {
    (void)ctx;
    int r = vfs_mkdir(path, (int)mode);
    if (r == 0) return 0;
    /* mkdir failed: tolerate "already exists as a directory" — the
     * ramfs vfs_mkdir returns <0 when the path already exists, so we
     * re-check via stat and treat an existing directory as success. */
    struct opk_kstat st;
    if (vfs_stat(path, &st) == 0 && (st.mode & OPK_FS_DIR))
        return 0;
    return -1;
}

static int kfs_write_file(void *ctx, const char *path,
                          const void *data, uint32_t size, uint32_t mode) {
    (void)ctx;
    int fd = vfs_open(path, OPK_O_WRONLY | OPK_O_CREAT | OPK_O_TRUNC, (int)mode);
    if (fd < 0) {
        early_console64_write("[opk] wf open<0 path=");
        early_console64_write(path);
        early_console64_write("\n");
        return -1;
    }
    if (size == 0) { vfs_close(fd); return 0; }

    uint32_t off = 0;
    while (off < size) {
        int w = vfs_write(fd, (const uint8_t *)data + off, size - off);
        if (w <= 0) {
            early_console64_write("[opk] wf vfs_write<=0 fd=0x");
            early_console64_write_hex64((unsigned long long)(unsigned)fd);
            early_console64_write(" path=");
            early_console64_write(path);
            early_console64_write("\n");
            vfs_close(fd);
            return -1;
        }
        off += (uint32_t)w;
    }
    vfs_close(fd);
    return 0;
}

static const opk_fs_ops_t g_kernel_opk_ops = {
    kfs_mkdir,
    kfs_write_file,
};

/*
 * Install an in-memory .opk image into the writable ramfs under <root>
 * (typically "/pkg"). Thin wrapper the syscall layer / boot code calls.
 */
int opk_install_to_ramfs(const uint8_t *image, uint32_t image_size,
                         const char *root, opk_install_result_t *result) {
    return opk_install(image, image_size, root,
                       &g_kernel_opk_ops, (void *)0, result);
}
