/*
 * opkg-build : host-side .opk packager for openOS (M5.4b)
 *
 * Usage:
 *   opkg-build -o <out.opk> -n <pkgname> [-e] FILE[:instname] ...
 *
 *   -o   output .opk path (required)
 *   -n   package name stored in header (required)
 *   -e   mark the *next* file as executable (OPK_MODE_EXEC)
 *   FILE          host source file to package
 *   FILE:instname override install name inside the package
 *
 * Produces a .opk blob: [header][toc][payload], all little-endian,
 * with CRC32 over header-tail and per-entry CRC32.
 *
 * This tool is host-only (compiled with the system cc), so it uses the
 * hosted libc freely. The on-disk layout is defined by opk64.h which is
 * shared with the kernel-side installer (M5.4c).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* keep this file self-contained: mirror opk64.h layout here and
 * static_assert the sizes so host and kernel never drift. */
#define OPK_MAGIC        0x4B50304F36344C55ULL
#define OPK_VERSION      1u
#define OPK_NAME_MAX     64u
#define OPK_PKGNAME_MAX  48u
#define OPK_MAX_ENTRIES  128u
#define OPK_MODE_FILE    0x8000u
#define OPK_MODE_EXEC    0x0049u

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t entry_cnt;
    uint64_t toc_off;
    uint64_t data_off;
    uint64_t total_size;
    uint32_t crc32;
    char     pkgname[OPK_PKGNAME_MAX];
    uint32_t reserved;
} opk_header_t;

typedef struct {
    char     name[OPK_NAME_MAX];
    uint64_t data_rel;
    uint64_t size;
    uint32_t mode;
    uint32_t crc32;
} opk_entry_t;

/* -------- CRC32 (IEEE 802.3, poly 0xEDB88320, standard zlib variant) -------- */
static uint32_t crc32_table[256];
static int      crc32_ready = 0;

static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}

static uint32_t crc32_calc(const void *buf, size_t len)
{
    if (!crc32_ready) crc32_init();
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        c = crc32_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* -------- helpers -------- */
struct src_file {
    char     inst[OPK_NAME_MAX]; /* install name inside package */
    uint8_t *data;
    size_t   size;
    uint32_t mode;
};

static uint8_t *read_whole(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "opkg-build: cannot open '%s'\n", path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)n ? (size_t)n : 1);
    if (!buf) { fclose(f); return NULL; }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "opkg-build: short read on '%s'\n", path);
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

/* derive default install name = basename of host path */
static const char *basename_of(const char *p)
{
    const char *b = p;
    for (const char *c = p; *c; ++c)
        if (*c == '/' || *c == '\\') b = c + 1;
    return b;
}

int main(int argc, char **argv)
{
    const char *out_path = NULL;
    const char *pkgname  = NULL;
    struct src_file files[OPK_MAX_ENTRIES];
    int nfiles = 0;
    int next_exec = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            pkgname = argv[++i];
        } else if (strcmp(argv[i], "-e") == 0) {
            next_exec = 1;
        } else {
            if (nfiles >= (int)OPK_MAX_ENTRIES) {
                fprintf(stderr, "opkg-build: too many files (max %u)\n", OPK_MAX_ENTRIES);
                return 2;
            }
            /* split FILE[:instname] */
            char *arg = argv[i];
            char *colon = strrchr(arg, ':');
            const char *src, *inst;
            char srcbuf[1024];
            if (colon && colon != arg && colon[1] != '\0') {
                size_t sl = (size_t)(colon - arg);
                if (sl >= sizeof(srcbuf)) sl = sizeof(srcbuf) - 1;
                memcpy(srcbuf, arg, sl); srcbuf[sl] = '\0';
                src = srcbuf; inst = colon + 1;
            } else {
                src = arg; inst = basename_of(arg);
            }
            size_t len = 0;
            uint8_t *data = read_whole(src, &len);
            if (!data) return 3;
            struct src_file *sf = &files[nfiles++];
            memset(sf->inst, 0, sizeof(sf->inst));
            strncpy(sf->inst, inst, sizeof(sf->inst) - 1);
            sf->data = data;
            sf->size = len;
            sf->mode = OPK_MODE_FILE | (next_exec ? OPK_MODE_EXEC : 0u);
            next_exec = 0;
        }
    }

    if (!out_path || !pkgname || nfiles == 0) {
        fprintf(stderr,
            "usage: opkg-build -o <out.opk> -n <pkgname> [-e] FILE[:instname] ...\n");
        return 1;
    }

    /* compute layout */
    uint64_t toc_off  = sizeof(opk_header_t);
    uint64_t data_off = toc_off + (uint64_t)nfiles * sizeof(opk_entry_t);

    /* build TOC */
    opk_entry_t *toc = (opk_entry_t *)calloc((size_t)nfiles, sizeof(opk_entry_t));
    if (!toc) { fprintf(stderr, "opkg-build: OOM\n"); return 4; }
    uint64_t blob = 0;
    for (int i = 0; i < nfiles; ++i) {
        memcpy(toc[i].name, files[i].inst, OPK_NAME_MAX - 1);
        toc[i].name[OPK_NAME_MAX - 1] = '\0';
        toc[i].data_rel = blob;
        toc[i].size     = files[i].size;
        toc[i].mode     = files[i].mode;
        toc[i].crc32    = crc32_calc(files[i].data, files[i].size);
        blob += files[i].size;
    }
    uint64_t total = data_off + blob;

    /* assemble full image in memory */
    uint8_t *img = (uint8_t *)calloc(1, (size_t)total);
    if (!img) { fprintf(stderr, "opkg-build: OOM image\n"); return 4; }
    opk_header_t *h = (opk_header_t *)img;
    h->magic      = OPK_MAGIC;
    h->version    = OPK_VERSION;
    h->entry_cnt  = (uint32_t)nfiles;
    h->toc_off    = toc_off;
    h->data_off   = data_off;
    h->total_size = total;
    h->reserved   = 0;
    memset(h->pkgname, 0, sizeof(h->pkgname));
    strncpy(h->pkgname, pkgname, sizeof(h->pkgname) - 1);
    memcpy(img + toc_off, toc, (size_t)nfiles * sizeof(opk_entry_t));
    for (int i = 0; i < nfiles; ++i)
        memcpy(img + data_off + toc[i].data_rel, files[i].data, files[i].size);

    /* header crc32 covers everything after the crc32 field */
    size_t crc_start = (size_t)((uint8_t *)&h->crc32 - img) + sizeof(h->crc32);
    h->crc32 = crc32_calc(img + crc_start, (size_t)total - crc_start);

    /* write out */
    FILE *of = fopen(out_path, "wb");
    if (!of) { fprintf(stderr, "opkg-build: cannot create '%s'\n", out_path); return 5; }
    if (fwrite(img, 1, (size_t)total, of) != (size_t)total) {
        fprintf(stderr, "opkg-build: write failed\n"); fclose(of); return 5;
    }
    fclose(of);

    printf("opkg-build: wrote '%s' pkg='%s' entries=%d total=%llu bytes\n",
           out_path, pkgname, nfiles, (unsigned long long)total);
    for (int i = 0; i < nfiles; ++i)
        printf("  [%d] %-20s size=%-8llu mode=0x%04x crc=0x%08x\n",
               i, toc[i].name, (unsigned long long)toc[i].size,
               toc[i].mode, toc[i].crc32);

    for (int i = 0; i < nfiles; ++i) free(files[i].data);
    free(toc); free(img);
    return 0;
}
