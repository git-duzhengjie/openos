#ifndef OPENOS_ARCH_X86_64_OPK_INSTALL_H
#define OPENOS_ARCH_X86_64_OPK_INSTALL_H

/*
 * .opk installer core (M5.4c).
 *
 * Design: pure parse/verify logic driven by an injected filesystem
 * backend (opk_fs_ops_t). The kernel wires the backend to the writable
 * ramfs (vfs_mkdir / vfs_open / vfs_write / vfs_close); the host unit
 * test wires it to a plain-FS simulation. No direct VFS dependency here,
 * so the installer is testable on the host with -Werror.
 *
 * Install flow for an in-memory .opk image:
 *   1. validate header (magic/version/sizes) + whole-image CRC32
 *   2. mkdir <root> and <root>/<pkgname>
 *   3. for each TOC entry: verify per-entry CRC32, mkdir parent dirs,
 *      create + write file at <root>/<pkgname>/<entry.name>
 */

#include <stdint.h>
#include "opk64.h"

/* -------- error codes -------- */
#define OPK_OK              0
#define OPK_ERR_ARG        -1   /* null / bad argument */
#define OPK_ERR_TRUNC      -2   /* image too small for header/TOC/payload */
#define OPK_ERR_MAGIC      -3   /* bad magic */
#define OPK_ERR_VERSION    -4   /* unsupported version */
#define OPK_ERR_LAYOUT     -5   /* inconsistent offsets / entry_cnt */
#define OPK_ERR_HDR_CRC    -6   /* header/whole-image CRC mismatch */
#define OPK_ERR_ENTRY_CRC  -7   /* per-entry payload CRC mismatch */
#define OPK_ERR_NAME       -8   /* illegal entry name (abs / .. / overflow) */
#define OPK_ERR_FS_MKDIR   -9   /* backend mkdir failed */
#define OPK_ERR_FS_WRITE   -10  /* backend create/write failed */

/*
 * Filesystem backend. All paths are absolute, NUL-terminated. mkdir
 * should treat "already exists" as success (return 0). write_file
 * creates (truncating) the file and writes the full buffer.
 */
typedef struct opk_fs_ops {
    int (*mkdir)(void *ctx, const char *path, uint32_t mode);
    int (*write_file)(void *ctx, const char *path,
                      const void *data, uint32_t size, uint32_t mode);
} opk_fs_ops_t;

/* installation report (optional; pass NULL to ignore) */
typedef struct opk_install_result {
    char     pkgname[OPK_PKGNAME_MAX];
    uint32_t files_installed;
    uint64_t bytes_written;
} opk_install_result_t;

/* CRC32 (IEEE 802.3, poly 0xEDB88320) — must match host packager/zlib. */
uint32_t opk_crc32(const void *data, uint32_t len);

/*
 * Install an in-memory .opk image. root is the install prefix
 * (e.g. "/pkg"); files land under <root>/<pkgname>/<entry.name>.
 * Returns OPK_OK or a negative OPK_ERR_*.
 */
int opk_install(const uint8_t *image, uint32_t image_size,
                const char *root,
                const opk_fs_ops_t *ops, void *ctx,
                opk_install_result_t *result);

#endif /* OPENOS_ARCH_X86_64_OPK_INSTALL_H */
