/*
 * test_opk : host-side unit test for the .opk package format (M5.4b).
 *
 * Builds a package in-memory via the opkg-build tool, then independently
 * re-parses the produced blob and verifies every structural invariant:
 *   - header magic/version/offsets/sizes
 *   - TOC entry names/sizes/modes
 *   - per-entry CRC32 of payload
 *   - whole-image header CRC32
 *   - payload byte-exactness
 *
 * Self-contained: mirrors opk64.h layout and its own CRC32, so it does
 * not depend on the tool's internals -- a true cross-check.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPK_MAGIC        0x4B50304F36344C55ULL
#define OPK_VERSION      1u
#define OPK_NAME_MAX     64u
#define OPK_PKGNAME_MAX  48u
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

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; printf("  PASS  %s\n", msg); } \
    else      { g_fail++; printf("  FAIL  %s\n", msg); } \
} while (0)

static uint8_t *read_whole(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    uint8_t *b = malloc((size_t)n);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)n;
    return b;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/tmp/opktest/demo.opk";
    printf("[test_opk] parsing '%s'\n", path);

    size_t len = 0;
    uint8_t *img = read_whole(path, &len);
    if (!img) { fprintf(stderr, "[test_opk] cannot read %s\n", path); return 2; }

    /* --- header --- */
    CHECK(len >= sizeof(opk_header_t), "file large enough for header");
    opk_header_t *h = (opk_header_t *)img;
    CHECK(h->magic == OPK_MAGIC, "header magic matches");
    CHECK(h->version == OPK_VERSION, "header version == 1");
    CHECK(h->total_size == (uint64_t)len, "header total_size == file size");
    CHECK(h->toc_off == sizeof(opk_header_t), "toc_off == sizeof(header)");
    CHECK(h->data_off == h->toc_off + (uint64_t)h->entry_cnt * sizeof(opk_entry_t),
          "data_off == toc_off + entries*sizeof(entry)");
    CHECK(strcmp(h->pkgname, "demopkg") == 0, "pkgname == 'demopkg'");
    CHECK(h->reserved == 0, "reserved == 0");

    /* --- whole-image header crc32 --- */
    size_t crc_start = (size_t)((uint8_t *)&h->crc32 - img) + sizeof(h->crc32);
    uint32_t want = crc32_calc(img + crc_start, len - crc_start);
    CHECK(h->crc32 == want, "header crc32 verifies whole image");

    /* --- TOC --- */
    CHECK(h->entry_cnt == 2, "entry_cnt == 2");
    opk_entry_t *toc = (opk_entry_t *)(img + h->toc_off);

    /* entry 0: a.txt, plain file */
    const char *exp0 = "hello from opk file one";
    CHECK(strcmp(toc[0].name, "a.txt") == 0, "entry0 name == 'a.txt'");
    CHECK(toc[0].size == strlen(exp0), "entry0 size matches");
    CHECK(toc[0].mode == OPK_MODE_FILE, "entry0 mode == FILE (no exec)");
    CHECK(toc[0].data_rel == 0, "entry0 data_rel == 0");
    {
        uint8_t *d = img + h->data_off + toc[0].data_rel;
        CHECK(memcmp(d, exp0, toc[0].size) == 0, "entry0 payload byte-exact");
        CHECK(crc32_calc(d, toc[0].size) == toc[0].crc32, "entry0 crc32 verifies");
    }

    /* entry 1: bin/prog, executable */
    const char *exp1 = "second payload content here!!";
    CHECK(strcmp(toc[1].name, "bin/prog") == 0, "entry1 name == 'bin/prog'");
    CHECK(toc[1].size == strlen(exp1), "entry1 size matches");
    CHECK((toc[1].mode & OPK_MODE_EXEC) == OPK_MODE_EXEC, "entry1 mode has EXEC bit");
    CHECK(toc[1].data_rel == toc[0].size, "entry1 data_rel == entry0 size");
    {
        uint8_t *d = img + h->data_off + toc[1].data_rel;
        CHECK(memcmp(d, exp1, toc[1].size) == 0, "entry1 payload byte-exact");
        CHECK(crc32_calc(d, toc[1].size) == toc[1].crc32, "entry1 crc32 verifies");
    }

    /* --- corruption detection: flip a payload byte, header crc must break --- */
    {
        uint8_t save = img[h->data_off];
        img[h->data_off] ^= 0xFF;
        uint32_t bad = crc32_calc(img + crc_start, len - crc_start);
        CHECK(bad != h->crc32, "corrupted payload breaks header crc32");
        img[h->data_off] = save; /* restore */
    }

    free(img);
    printf("\n[test_opk] %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) printf("[test_opk] ALL PASS\n");
    return g_fail ? 1 : 0;
}
