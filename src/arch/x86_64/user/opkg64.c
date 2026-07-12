/*
 * opkg64.c — M5.4d: user-space package manager CLI.
 *
 * A ring3 program that drives the M5.4c .opk installer and the writable
 * ramfs to provide install / remove / list / info sub-commands.
 *
 *   opkg install <file.opk>   read an .opk image and hand it to SYS_OPK_INSTALL
 *   opkg remove  <pkg>        recursively delete /pkg/<pkg>/
 *   opkg list                 enumerate installed packages under /pkg/
 *   opkg info    <pkg>        show the file manifest of one installed package
 *
 * Built freestanding (-ffreestanding -nostdlib) and linked against the M5.3
 * libc subset (string/stdio/stdlib) plus the openos64 syscall runtime.
 */
#include "openos64.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "libc/stdlib.h"

/* file-type bits mirror kernel VFS (src/kernel/core/fs/vfs.h). */
#define OPKG_FS_FILE 0x1000u
#define OPKG_FS_DIR  0x2000u

#define OPKG_PKG_ROOT "/pkg"
#define OPKG_PATH_MAX 256

/* ---- small path helper (no snprintf dependency on width) ---------------- */
static void path_join(char *dst, size_t cap, const char *a, const char *b) {
    size_t n = 0;
    for (const char *p = a; *p && n + 1 < cap; p++) dst[n++] = *p;
    if (n && dst[n - 1] != '/' && n + 1 < cap) dst[n++] = '/';
    for (const char *p = b; *p && n + 1 < cap; p++) dst[n++] = *p;
    dst[n] = '\0';
}

/* ---- opkg list ---------------------------------------------------------- */
static int cmd_list(void) {
    openos64_dirent_t de;
    int count = 0;
    for (int i = 0; ; i++) {
        int r = openos64_readdir(OPKG_PKG_ROOT, i, &de);
        if (r <= 0) break;                 /* 0 = end, <0 = error */
        if (de.name[0] == '.' &&
            (de.name[1] == '\0' || (de.name[1] == '.' && de.name[2] == '\0')))
            continue;                      /* skip . and .. */
        if (de.mode & OPKG_FS_DIR) {
            printf("%s\n", de.name);
            count++;
        }
    }
    if (count == 0) printf("(no packages installed)\n");
    else            printf("-- %d package(s) installed --\n", count);
    return 0;
}

/* ---- opkg info <pkg> ---------------------------------------------------- */
static int cmd_info(const char *pkg) {
    char dir[OPKG_PATH_MAX];
    path_join(dir, sizeof(dir), OPKG_PKG_ROOT, pkg);

    openos64_stat_t st;
    if (openos64_stat(dir, &st) != 0 || !(st.mode & OPKG_FS_DIR)) {
        printf("opkg: package '%s' is not installed\n", pkg);
        return 1;
    }

    printf("Package: %s\n", pkg);
    printf("Path:    %s\n", dir);
    printf("Files:\n");

    openos64_dirent_t de;
    int files = 0;
    unsigned long total = 0;
    for (int i = 0; ; i++) {
        int r = openos64_readdir(dir, i, &de);
        if (r <= 0) break;
        if (de.name[0] == '.' &&
            (de.name[1] == '\0' || (de.name[1] == '.' && de.name[2] == '\0')))
            continue;
        const char *tag = (de.mode & OPKG_FS_DIR) ? "d" : "-";
        printf("  %s %8u  %s\n", tag, de.size, de.name);
        if (!(de.mode & OPKG_FS_DIR)) { files++; total += de.size; }
    }
    printf("-- %d file(s), %lu byte(s) --\n", files, total);
    return 0;
}

/* ---- opkg remove <pkg> -------------------------------------------------- */
/* Recursively delete /pkg/<pkg>/. Depth-limited: pkg dirs are shallow. */
static int rm_recursive(const char *path) {
    openos64_dirent_t de;
    /* Always re-scan index 0: unlink shifts the directory, so consuming
     * from the front keeps the walk stable. */
    for (;;) {
        int r = openos64_readdir(path, 0, &de);
        if (r <= 0) break;
        if (de.name[0] == '.' &&
            (de.name[1] == '\0' || (de.name[1] == '.' && de.name[2] == '\0'))) {
            /* '.'/'..' should not be enumerated by ramfs; guard anyway to
             * avoid an infinite loop if they ever are. */
            int r2 = openos64_readdir(path, 1, &de);
            if (r2 <= 0) break;
        }
        char child[OPKG_PATH_MAX];
        path_join(child, sizeof(child), path, de.name);
        if (de.mode & OPKG_FS_DIR) {
            if (rm_recursive(child) != 0) return 1;
            if (openos64_rmdir(child) != 0) {
                printf("opkg: failed to rmdir %s\n", child);
                return 1;
            }
        } else {
            if (openos64_unlink(child) != 0) {
                printf("opkg: failed to unlink %s\n", child);
                return 1;
            }
        }
    }
    return 0;
}

static int cmd_remove(const char *pkg) {
    char dir[OPKG_PATH_MAX];
    path_join(dir, sizeof(dir), OPKG_PKG_ROOT, pkg);

    openos64_stat_t st;
    if (openos64_stat(dir, &st) != 0 || !(st.mode & OPKG_FS_DIR)) {
        printf("opkg: package '%s' is not installed\n", pkg);
        return 1;
    }
    if (rm_recursive(dir) != 0) return 1;
    if (openos64_rmdir(dir) != 0) {
        printf("opkg: failed to rmdir %s\n", dir);
        return 1;
    }
    printf("Removed package '%s'\n", pkg);
    return 0;
}

/* ---- opkg install <file.opk> ------------------------------------------- */
#define OPKG_IMG_MAX (256 * 1024)   /* max .opk image we load into memory */

static int cmd_install(const char *file) {
    int fd = openos64_open(file, OPENOS64_O_RDONLY, 0);
    if (fd < 0) {
        printf("opkg: cannot open '%s'\n", file);
        return 1;
    }

    unsigned char *buf = (unsigned char *)malloc(OPKG_IMG_MAX);
    if (!buf) {
        printf("opkg: out of memory\n");
        openos64_close(fd);
        return 1;
    }

    unsigned long total = 0;
    for (;;) {
        long n = openos64_read(fd, buf + total, OPKG_IMG_MAX - total);
        if (n < 0) {
            printf("opkg: read error on '%s'\n", file);
            free(buf);
            openos64_close(fd);
            return 1;
        }
        if (n == 0) break;
        total += (unsigned long)n;
        if (total >= OPKG_IMG_MAX) {
            printf("opkg: package too large (> %d bytes)\n", OPKG_IMG_MAX);
            free(buf);
            openos64_close(fd);
            return 1;
        }
    }
    openos64_close(fd);

    long rc = openos64_opk_install(buf, total, OPKG_PKG_ROOT);
    free(buf);
    if (rc != 0) {
        printf("opkg: install failed (code %ld)\n", rc);
        return 1;
    }
    printf("Installed '%s' (%lu bytes)\n", file, total);
    return 0;
}

/* ---- usage -------------------------------------------------------------- */
static int usage(void) {
    printf("openos package manager (opkg)\n");
    printf("usage:\n");
    printf("  opkg install <file.opk>   install a package image\n");
    printf("  opkg remove  <pkg>        remove an installed package\n");
    printf("  opkg list                 list installed packages\n");
    printf("  opkg info    <pkg>        show a package's file manifest\n");
    return 2;
}

int openos64_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc < 2) return usage();

    const char *cmd = argv[1];
    if (strcmp(cmd, "list") == 0) {
        return cmd_list();
    } else if (strcmp(cmd, "info") == 0) {
        if (argc < 3) return usage();
        return cmd_info(argv[2]);
    } else if (strcmp(cmd, "remove") == 0) {
        if (argc < 3) return usage();
        return cmd_remove(argv[2]);
    } else if (strcmp(cmd, "install") == 0) {
        if (argc < 3) return usage();
        return cmd_install(argv[2]);
    }
    printf("opkg: unknown command '%s'\n", cmd);
    return usage();
}
