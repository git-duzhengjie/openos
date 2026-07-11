/*
 * M5.4c opk_demo64.c — end-to-end test for the .opk package installer,
 * exercised from a real ring3 process.
 *
 * Flow:
 *   1. Build a small .opk image entirely in user memory (same on-disk
 *      layout the host packager emits: header + TOC + payload, LE, with
 *      whole-image CRC32 and per-entry CRC32).
 *   2. Invoke SYS_OPK_INSTALL (479) so the kernel verifies the image and
 *      unpacks it into the writable ramfs under "/pkg".
 *   3. Read the installed files back through the VFS syscalls and check
 *      their contents byte-for-byte.
 *
 * This proves the pack -> install -> run pipeline end to end: the exact
 * same opk_install.c core validated on the host now runs in ring0 driven
 * by a ring3 request.
 *
 * Launch chain tail: /bin/fs_demo execve's into /bin/opk_demo.
 */

#include <stddef.h>
#include <stdint.h>

#include "openos64.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "opk64.h"

#define SYS_OPK_INSTALL 479ULL

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *name, int cond)
{
    if (cond) { g_pass++; printf("  PASS  %s\n", name); }
    else      { g_fail++; printf("  FAIL  %s\n", name); }
}

/* CRC32 (poly 0xEDB88320) — matches the host packager and kernel core. */
static uint32_t demo_crc32(const void *data, uint32_t len)
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

/* one file to pack: logical name + literal content */
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
        toc[i].crc32 = demo_crc32(payload + rel, len);
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
    h->crc32 = demo_crc32(out + crc_off, total - crc_off);
    return total;
}

/* read an installed file back and compare to expected content */
static void verify_file(const char *path, const char *expect)
{
    int fd = openos64_open(path, OPENOS64_O_RDONLY, 0);
    if (fd < 0) { check(path, 0); return; }
    char buf[256];
    openos64_ssize_t n = openos64_read(fd, buf, sizeof(buf) - 1);
    openos64_close(fd);
    if (n < 0) { check(path, 0); return; }
    buf[n] = '\0';
    check(path, (uint32_t)n == (uint32_t)strlen(expect) && strcmp(buf, expect) == 0);
}

int openos64_main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    printf("\n[opk_demo] M5.4c .opk install end-to-end test\n");

    static uint8_t img[8192];
    const spec_t files[] = {
        { "bin/tool",     "OPK-TOOL-BINARY",  OPK_MODE_EXEC },
        { "etc/tool.cfg", "mode=demo\nver=1\n", OPK_MODE_FILE },
        { "README",       "installed by opk",  OPK_MODE_FILE },
    };

    uint32_t sz = build_opk(img, sizeof(img), "toolpkg", files, 3);
    check("build .opk image in user memory", sz > 0);

    /* ---- 1. install a valid package ---- */
    long r = openos64_syscall3(SYS_OPK_INSTALL,
                               (uint64_t)(uintptr_t)img,
                               (uint64_t)sz,
                               (uint64_t)(uintptr_t)"/pkg");
    check("SYS_OPK_INSTALL returns OPK_OK", r == 0);

    /* ---- 2. read every installed file back and compare ---- */
    verify_file("/pkg/toolpkg/bin/tool",     "OPK-TOOL-BINARY");
    verify_file("/pkg/toolpkg/etc/tool.cfg", "mode=demo\nver=1\n");
    verify_file("/pkg/toolpkg/README",       "installed by opk");

    /* ---- 3. corrupt the whole-image CRC -> install must be rejected ---- */
    {
        img[sz - 1] ^= 0xFF;
        long rr = openos64_syscall3(SYS_OPK_INSTALL,
                                    (uint64_t)(uintptr_t)img,
                                    (uint64_t)sz,
                                    (uint64_t)(uintptr_t)"/pkg");
        check("corrupted image rejected (CRC)", rr != 0);
        img[sz - 1] ^= 0xFF; /* restore */
    }

    printf("[opk_demo] %d passed, %d failed\n", g_pass, g_fail);
    printf(g_fail == 0 ? "[opk_demo] ALL PASS\n" : "[opk_demo] FAILURES\n");

    openos64_exit(g_fail == 0 ? 0 : 1);
    return 0;
}
