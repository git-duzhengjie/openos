/*
 * Host unit test for the .opk installer core (M5.4c).
 * Wires opk_fs_ops_t to an in-memory simulated filesystem and checks:
 *   - clean install of a multi-file / nested package
 *   - CRC32 matches an independent reference implementation
 *   - header CRC / entry CRC corruption is detected
 *   - malformed layout (magic/version/offsets/truncation) is rejected
 *   - path attacks (absolute, "..", ".") are rejected
 *
 * Build (host): cc -std=c11 -Wall -Wextra -Werror test_opk_install.c \
 *               ../kernel/opk_install.c -I../include -o /tmp/t && /tmp/t
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "opk_install.h"

/* ---------- independent CRC32 reference (table-driven) ---------- */
static uint32_t ref_crc32(const void *data, uint32_t len) {
    static uint32_t tbl[256];
    static int init = 0;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        init = 1;
    }
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++)
        c = tbl[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

/* ---------- in-memory simulated filesystem backend ---------- */
#define SIM_MAX 64
typedef struct {
    char path[512];
    int  is_dir;
    uint8_t data[4096];
    uint32_t size;
    uint32_t mode;
} sim_node_t;

typedef struct {
    sim_node_t nodes[SIM_MAX];
    int count;
} sim_fs_t;

static sim_node_t *sim_find(sim_fs_t *fs, const char *path) {
    for (int i = 0; i < fs->count; i++)
        if (strcmp(fs->nodes[i].path, path) == 0) return &fs->nodes[i];
    return NULL;
}

static int sim_mkdir(void *ctx, const char *path, uint32_t mode) {
    sim_fs_t *fs = ctx;
    sim_node_t *n = sim_find(fs, path);
    if (n) return n->is_dir ? 0 : -1;   /* exists as dir => ok */
    if (fs->count >= SIM_MAX) return -1;
    n = &fs->nodes[fs->count++];
    snprintf(n->path, sizeof(n->path), "%s", path);
    n->is_dir = 1; n->size = 0; n->mode = mode;
    return 0;
}

static int sim_write_file(void *ctx, const char *path,
                          const void *data, uint32_t size, uint32_t mode) {
    sim_fs_t *fs = ctx;
    if (size > sizeof(((sim_node_t *)0)->data)) return -1;
    sim_node_t *n = sim_find(fs, path);
    if (!n) {
        if (fs->count >= SIM_MAX) return -1;
        n = &fs->nodes[fs->count++];
        snprintf(n->path, sizeof(n->path), "%s", path);
    }
    n->is_dir = 0; n->size = size; n->mode = mode;
    memcpy(n->data, data, size);
    return 0;
}

/* ---------- .opk image builder (mirrors the host packager) ---------- */
typedef struct { const char *name; const char *data; uint32_t mode; } file_spec_t;

/* builds a valid .opk into out (>= 64KB), returns total size */
static uint32_t build_opk(uint8_t *out, const char *pkgname,
                          const file_spec_t *files, uint32_t nfiles) {
    opk_header_t *h = (opk_header_t *)out;
    opk_entry_t *toc = (opk_entry_t *)(out + sizeof(opk_header_t));
    uint64_t data_off = sizeof(opk_header_t) + (uint64_t)nfiles * sizeof(opk_entry_t);
    uint8_t *payload = out + data_off;

    memset(out, 0, data_off);
    uint64_t rel = 0;
    for (uint32_t i = 0; i < nfiles; i++) {
        uint32_t len = (uint32_t)strlen(files[i].data);
        snprintf(toc[i].name, OPK_NAME_MAX, "%s", files[i].name);
        toc[i].data_rel = rel;
        toc[i].size = len;
        toc[i].mode = files[i].mode;
        memcpy(payload + rel, files[i].data, len);
        toc[i].crc32 = ref_crc32(payload + rel, len);
        rel += len;
    }
    uint32_t total = (uint32_t)(data_off + rel);

    h->magic = OPK_MAGIC;
    h->version = OPK_VERSION;
    h->entry_cnt = nfiles;
    h->toc_off = sizeof(opk_header_t);
    h->data_off = data_off;
    h->total_size = total;
    snprintf(h->pkgname, OPK_PKGNAME_MAX, "%s", pkgname);
    h->reserved = 0;

    uint32_t crc_off = (uint32_t)((uint8_t *)&h->crc32 - out) + (uint32_t)sizeof(h->crc32);
    h->crc32 = ref_crc32(out + crc_off, total - crc_off);
    return total;
}

/* ---------- test harness ---------- */
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s\n", msg); } \
} while (0)

int main(void) {
    static uint8_t img[65536];
    static const opk_fs_ops_t ops = { sim_mkdir, sim_write_file };

    printf("== opk_install unit tests ==\n");

    /* ---- 0. CRC32 agreement between core and reference ---- */
    {
        const char *s = "The quick brown fox";
        CHECK(opk_crc32(s, (uint32_t)strlen(s)) == ref_crc32(s, (uint32_t)strlen(s)),
              "crc32 core == reference");
        CHECK(opk_crc32("", 0) == ref_crc32("", 0), "crc32 empty");
    }

    /* ---- 1. clean install of a nested multi-file package ---- */
    {
        file_spec_t files[] = {
            { "bin/hello",   "HELLO-BINARY-BYTES", OPK_MODE_EXEC },
            { "etc/conf.ini", "key=value\n",        OPK_MODE_FILE },
            { "readme.txt",   "openos package",     OPK_MODE_FILE },
        };
        uint32_t n = build_opk(img, "demo", files, 3);
        sim_fs_t fs = {0};
        opk_install_result_t res = {0};
        int r = opk_install(img, n, "/pkg", &ops, &fs, &res);
        CHECK(r == OPK_OK, "clean install returns OPK_OK");
        CHECK(strcmp(res.pkgname, "demo") == 0, "result pkgname");
        CHECK(res.files_installed == 3, "result files_installed==3");

        sim_node_t *hb = sim_find(&fs, "/pkg/demo/bin/hello");
        CHECK(hb && !hb->is_dir, "bin/hello installed as file");
        CHECK(hb && hb->size == strlen("HELLO-BINARY-BYTES"), "bin/hello size");
        CHECK(hb && memcmp(hb->data, "HELLO-BINARY-BYTES", hb->size) == 0,
              "bin/hello content");
        CHECK(hb && hb->mode == OPK_MODE_EXEC, "bin/hello exec mode");
        CHECK(sim_find(&fs, "/pkg") != NULL, "root /pkg created");
        CHECK(sim_find(&fs, "/pkg/demo") != NULL, "pkg dir created");
        CHECK(sim_find(&fs, "/pkg/demo/bin") != NULL, "nested parent dir created");
        CHECK(sim_find(&fs, "/pkg/demo/etc") != NULL, "etc parent dir created");
        CHECK(sim_find(&fs, "/pkg/demo/etc/conf.ini") != NULL, "etc/conf.ini installed");
    }

    /* ---- 2. header CRC corruption detected ---- */
    {
        file_spec_t f[] = { { "a", "data", OPK_MODE_FILE } };
        uint32_t n = build_opk(img, "p", f, 1);
        img[n - 1] ^= 0xFF;   /* flip a payload byte */
        sim_fs_t fs = {0};
        int r = opk_install(img, n, "/pkg", &ops, &fs, NULL);
        CHECK(r == OPK_ERR_HDR_CRC, "flipped payload -> header CRC mismatch");
    }

    /* ---- 3. entry CRC corruption detected (header CRC kept valid) ---- */
    {
        file_spec_t f[] = { { "a", "AAAA", OPK_MODE_FILE } };
        uint32_t n = build_opk(img, "p", f, 1);
        opk_entry_t *e = (opk_entry_t *)(img + sizeof(opk_header_t));
        e->crc32 ^= 0x1;  /* corrupt only the entry crc */
        /* recompute header CRC so we exercise the per-entry path */
        opk_header_t *h = (opk_header_t *)img;
        uint32_t crc_off = (uint32_t)((uint8_t *)&h->crc32 - img) + (uint32_t)sizeof(h->crc32);
        h->crc32 = ref_crc32(img + crc_off, n - crc_off);
        sim_fs_t fs = {0};
        int r = opk_install(img, n, "/pkg", &ops, &fs, NULL);
        CHECK(r == OPK_ERR_ENTRY_CRC, "corrupt entry crc -> OPK_ERR_ENTRY_CRC");
    }

    /* ---- 4. bad magic / version ---- */
    {
        file_spec_t f[] = { { "a", "x", OPK_MODE_FILE } };
        uint32_t n = build_opk(img, "p", f, 1);
        opk_header_t *h = (opk_header_t *)img;
        uint64_t save = h->magic; h->magic = 0;
        sim_fs_t fs = {0};
        CHECK(opk_install(img, n, "/pkg", &ops, &fs, NULL) == OPK_ERR_MAGIC,
              "bad magic rejected");
        h->magic = save; h->version = 999;
        /* header CRC now invalid; but magic ok, version check is before CRC */
        CHECK(opk_install(img, n, "/pkg", &ops, &fs, NULL) == OPK_ERR_VERSION,
              "bad version rejected");
    }

    /* ---- 5. truncated image ---- */
    {
        file_spec_t f[] = { { "a", "data", OPK_MODE_FILE } };
        uint32_t n = build_opk(img, "p", f, 1);
        sim_fs_t fs = {0};
        CHECK(opk_install(img, sizeof(opk_header_t) - 1, "/pkg", &ops, &fs, NULL) == OPK_ERR_TRUNC,
              "image smaller than header rejected");
        /* total_size mismatch -> layout error */
        CHECK(opk_install(img, n - 1, "/pkg", &ops, &fs, NULL) == OPK_ERR_LAYOUT,
              "size != total_size rejected");
    }

    /* ---- 6. path attacks ---- */
    {
        file_spec_t abs[]  = { { "/etc/passwd", "x", OPK_MODE_FILE } };
        file_spec_t dots[] = { { "../escape",   "x", OPK_MODE_FILE } };
        file_spec_t dot[]  = { { "./here",      "x", OPK_MODE_FILE } };
        sim_fs_t fs = {0};
        uint32_t n;
        n = build_opk(img, "p", abs, 1);
        CHECK(opk_install(img, n, "/pkg", &ops, &fs, NULL) == OPK_ERR_NAME,
              "absolute entry path rejected");
        n = build_opk(img, "p", dots, 1);
        CHECK(opk_install(img, n, "/pkg", &ops, &fs, NULL) == OPK_ERR_NAME,
              "'..' entry path rejected");
        n = build_opk(img, "p", dot, 1);
        CHECK(opk_install(img, n, "/pkg", &ops, &fs, NULL) == OPK_ERR_NAME,
              "'.' entry path rejected");
    }

    /* ---- 7. NULL argument guards ---- */
    {
        sim_fs_t fs = {0};
        CHECK(opk_install(NULL, 100, "/pkg", &ops, &fs, NULL) == OPK_ERR_ARG,
              "NULL image rejected");
        CHECK(opk_install(img, 100, NULL, &ops, &fs, NULL) == OPK_ERR_ARG,
              "NULL root rejected");
        CHECK(opk_install(img, 100, "/pkg", NULL, &fs, NULL) == OPK_ERR_ARG,
              "NULL ops rejected");
    }

    printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    if (g_fail == 0) printf("ALL PASS\n");
    return g_fail ? 1 : 0;
}
