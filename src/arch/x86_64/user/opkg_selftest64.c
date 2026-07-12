/*
 * opkg_selftest64.c — M5.4d end-to-end self-test for the package manager.
 *
 * Runs the full install -> list -> info -> remove closed loop inside one
 * ring3 process, exercising every new syscall the opkg CLI relies on:
 *   - SYS_OPK_INSTALL (479)  install an in-memory .opk into /pkg
 *   - SYS_READDIR     (239)  enumerate directory entries (list / info)
 *   - SYS_STAT               probe /pkg/<pkg> presence + type
 *   - SYS_UNLINK / SYS_RMDIR recursive removal
 *
 * This is the headless counterpart to the interactive `opkg` CLI: the CLI
 * parses argv and prints; this test drives the same primitives and asserts.
 *
 * Launch: selected as the initial ring3 image when the kernel is built with
 * -DM5_OPKG_DIAG (see kernel64.c). Prints "[opkg_selftest] ALL PASS".
 */
#include <stddef.h>
#include <stdint.h>

#include "openos64.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "opk64.h"

#define SYS_OPK_INSTALL 479ULL

/* file-type bits mirror kernel VFS (src/kernel/core/fs/vfs.h). */
#define OPKG_FS_DIR  0x2000u

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *name, int cond)
{
    if (cond) { g_pass++; printf("  PASS  %s\n", name); }
    else      { g_fail++; printf("  FAIL  %s\n", name); }
}

/* CRC32 (poly 0xEDB88320) — matches the host packager and kernel core. */
static uint32_t st_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

typedef struct { const char *name; const char *data; uint32_t mode; } spec_t;

/* build a valid .opk into out; returns total size (0 on overflow) */
static uint32_t build_opk(uint8_t *out, uint32_t cap, const char *pkgname,
                          const spec_t *files, uint32_t nfiles)
{
    uint64_t data_off = sizeof(opk_header_t) + (uint64_t)nfiles * sizeof(opk_entry_t);
    if (data_off > cap) return 0;

    opk_header_t *h = (opk_header_t *)out;
    opk_entry_t *toc = (opk_entry_t *)(out + sizeof(opk_header_t));
    uint8_t *payload = out + data_off;

    memset(out, 0, (openos64_size_t)data_off);

    uint64_t rel = 0;
    for (uint32_t i = 0; i < nfiles; i++) {
        uint32_t len = (uint32_t)strlen(files[i].data);
        if (data_off + rel + len > cap) return 0;
        memset(toc[i].name, 0, OPK_NAME_MAX);
        strcpy(toc[i].name, files[i].name);
        toc[i].data_rel = rel;
        toc[i].size = len;
        toc[i].mode = files[i].mode;
        memcpy(payload + rel, files[i].data, len);
        toc[i].crc32 = st_crc32(payload + rel, len);
        rel += len;
    }
    uint32_t total = (uint32_t)(data_off + rel);

    h->magic = OPK_MAGIC;
    h->version = OPK_VERSION;
    h->entry_cnt = nfiles;
    h->toc_off = sizeof(opk_header_t);
    h->data_off = data_off;
    h->total_size = total;
    memset(h->pkgname, 0, OPK_PKGNAME_MAX);
    strcpy(h->pkgname, pkgname);
    h->reserved = 0;

    uint32_t crc_off = (uint32_t)((uint8_t *)&h->crc32 - out) + (uint32_t)sizeof(h->crc32);
    h->crc32 = st_crc32(out + crc_off, total - crc_off);
    return total;
}

/* count non-dot directory entries under `path` */
static int count_entries(const char *path)
{
    openos64_dirent_t de;
    int n = 0;
    for (int i = 0; ; i++) {
        int r = openos64_readdir(path, i, &de);
        if (r <= 0) break;
        if (de.name[0] == '.' &&
            (de.name[1] == '\0' || (de.name[1] == '.' && de.name[2] == '\0')))
            continue;
        n++;
    }
    return n;
}

/* is `name` present among the entries of `path`? */
static int dir_has(const char *path, const char *name)
{
    openos64_dirent_t de;
    for (int i = 0; ; i++) {
        int r = openos64_readdir(path, i, &de);
        if (r <= 0) break;
        if (strcmp(de.name, name) == 0) return 1;
    }
    return 0;
}

/* recursive remove of /pkg/<pkg> (same algorithm as the CLI) */
static int rm_recursive(const char *path)
{
    openos64_dirent_t de;
    for (;;) {
        int r = openos64_readdir(path, 0, &de);
        if (r <= 0) break;
        char child[256];
        {
            size_t k = 0;
            for (const char *p = path; *p && k + 1 < sizeof(child); p++) child[k++] = *p;
            if (k && child[k - 1] != '/' && k + 1 < sizeof(child)) child[k++] = '/';
            for (const char *p = de.name; *p && k + 1 < sizeof(child); p++) child[k++] = *p;
            child[k] = '\0';
        }
        if (de.mode & OPKG_FS_DIR) {
            if (rm_recursive(child) != 0) return 1;
            if (openos64_rmdir(child) != 0) return 1;
        } else {
            if (openos64_unlink(child) != 0) return 1;
        }
    }
    return 0;
}

int openos64_main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    printf("\n[opkg_selftest] M5.4d package-manager end-to-end test\n");

    static uint8_t img[8192];
    const spec_t files[] = {
        { "bin/tool",     "OPK-TOOL-BINARY",  OPK_MODE_EXEC },
        { "etc/tool.cfg", "mode=demo\nver=1\n", OPK_MODE_FILE },
        { "README",       "installed by opk",  OPK_MODE_FILE },
    };
    uint32_t sz = build_opk(img, sizeof(img), "toolpkg", files, 3);
    check("build .opk image in user memory", sz > 0);

    /* ---- install ---- */
    long r = openos64_opk_install(img, sz, "/pkg");
    check("opk_install returns OPK_OK", r == 0);

    /* ---- stat: /pkg/toolpkg exists and is a directory ---- */
    openos64_stat_t stt;
    int sr = openos64_stat("/pkg/toolpkg", &stt);
    check("stat /pkg/toolpkg succeeds", sr == 0);
    check("/pkg/toolpkg is a directory", (stt.mode & OPKG_FS_DIR) != 0);

    /* ---- list: /pkg contains toolpkg ---- */
    check("readdir(/pkg) finds 'toolpkg'", dir_has("/pkg", "toolpkg"));

    /* ---- info: /pkg/toolpkg has README + the two subdirs ---- */
    check("readdir(/pkg/toolpkg) finds 'README'", dir_has("/pkg/toolpkg", "README"));
    check("readdir(/pkg/toolpkg) finds 'bin'",    dir_has("/pkg/toolpkg", "bin"));
    check("readdir(/pkg/toolpkg) finds 'etc'",    dir_has("/pkg/toolpkg", "etc"));
    check("/pkg/toolpkg has 3 top-level entries", count_entries("/pkg/toolpkg") == 3);

    /* ---- verify a leaf file is readable ---- */
    {
        int fd = openos64_open("/pkg/toolpkg/README", OPENOS64_O_RDONLY, 0);
        check("open /pkg/toolpkg/README", fd >= 0);
        if (fd >= 0) {
            char buf[64];
            openos64_ssize_t n = openos64_read(fd, buf, sizeof(buf) - 1);
            openos64_close(fd);
            if (n > 0) buf[n] = '\0'; else buf[0] = '\0';
            check("README content matches", strcmp(buf, "installed by opk") == 0);
        }
    }

    /* ---- remove: recursively delete /pkg/toolpkg ---- */
    int rc = rm_recursive("/pkg/toolpkg");
    check("rm_recursive(/pkg/toolpkg) succeeds", rc == 0);
    rc = openos64_rmdir("/pkg/toolpkg");
    check("rmdir /pkg/toolpkg succeeds", rc == 0);

    /* ---- confirm it is gone ---- */
    check("/pkg/toolpkg no longer present", !dir_has("/pkg", "toolpkg"));
    int sr2 = openos64_stat("/pkg/toolpkg", &stt);
    check("stat /pkg/toolpkg now fails", sr2 != 0);

    printf("[opkg_selftest] %d passed, %d failed\n", g_pass, g_fail);
    printf(g_fail == 0 ? "[opkg_selftest] ALL PASS\n" : "[opkg_selftest] FAILURES\n");

    openos64_exit(g_fail == 0 ? 0 : 1);
    return 0;
}
