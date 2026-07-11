/*
 * .opk installer core (M5.4c) — pure logic, backend-injected.
 * See opk_install.h for the design rationale.
 */

#include "opk_install.h"

#define OPK_KERNEL_DBG 0  /* install-path diagnostics; set to 1 to trace */
#if OPK_KERNEL_DBG
extern void early_console64_write(const char *s);
extern void early_console64_write_hex64(unsigned long long v);
#define OPK_DBG(s) early_console64_write(s)
#else
#define OPK_DBG(s) ((void)0)
#endif

/* ---- small local helpers (freestanding-safe, no libc dependency) ---- */

static uint32_t opk_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* append src to dst at *pos (bounded by cap incl. NUL); returns 0 ok, -1 overflow */
static int opk_append(char *dst, uint32_t cap, uint32_t *pos, const char *src) {
    uint32_t i = 0;
    while (src[i]) {
        if (*pos + 1 >= cap) return -1;
        dst[(*pos)++] = src[i++];
    }
    dst[*pos] = '\0';
    return 0;
}

/* read LE u32/u64 from an opk struct that is already in memory as LE.
 * On x86_64 (LE host+target) a direct field read is fine; we keep it
 * explicit-free by trusting the platform is little-endian. */

/* ---- CRC32 (poly 0xEDB88320), matches zlib / host packager ---- */

uint32_t opk_crc32(const void *data, uint32_t len) {
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

/* ---- entry name safety: reject absolute / "" / ".." components ---- */
static int opk_name_safe(const char *name) {
    if (!name || name[0] == '\0') return 0;
    if (name[0] == '/') return 0;              /* no absolute paths */
    /* scan components between '/' */
    uint32_t i = 0;
    uint32_t comp_start = 0;
    for (;;) {
        char c = name[i];
        if (c == '/' || c == '\0') {
            uint32_t len = i - comp_start;
            const char *comp = name + comp_start;
            if (len == 0) {
                /* empty component: leading handled above; reject "//" and trailing "/" */
                if (c == '\0') break; /* allow nothing here, loop ends */
                return 0;
            }
            if (len == 2 && comp[0] == '.' && comp[1] == '.') return 0; /* ".." */
            if (len == 1 && comp[0] == '.') return 0;                   /* "."  */
            comp_start = i + 1;
        }
        if (c == '\0') break;
        i++;
    }
    return 1;
}

/* mkdir every parent directory of <base>/<rel> under root path already built */
static int opk_mkdir_parents(char *full,
                             const opk_fs_ops_t *ops, void *ctx) {
    /* full is a complete file path; create each intermediate dir */
    for (uint32_t i = 1; full[i]; i++) {
        if (full[i] == '/') {
            full[i] = '\0';
            int r = ops->mkdir(ctx, full, OPK_MODE_DIR);
            full[i] = '/';
            if (r != 0) return OPK_ERR_FS_MKDIR;
        }
    }
    return OPK_OK;
}

int opk_install(const uint8_t *image, uint32_t image_size,
                const char *root,
                const opk_fs_ops_t *ops, void *ctx,
                opk_install_result_t *result) {
    if (!image || !root || !ops || !ops->mkdir || !ops->write_file)
        return OPK_ERR_ARG;
    if (image_size < sizeof(opk_header_t))
        return OPK_ERR_TRUNC;

    const opk_header_t *h = (const opk_header_t *)image;

    if (h->magic != OPK_MAGIC)      return OPK_ERR_MAGIC;
    if (h->version != OPK_VERSION)  return OPK_ERR_VERSION;

    /* layout sanity */
    if (h->toc_off != sizeof(opk_header_t))       return OPK_ERR_LAYOUT;
    if (h->entry_cnt > OPK_MAX_ENTRIES)           return OPK_ERR_LAYOUT;
    if (h->total_size != image_size)              return OPK_ERR_LAYOUT;

    uint64_t toc_bytes = (uint64_t)h->entry_cnt * sizeof(opk_entry_t);
    if (h->data_off != h->toc_off + toc_bytes)    return OPK_ERR_LAYOUT;
    if (h->data_off > image_size)                 return OPK_ERR_TRUNC;

    /* whole-image CRC: everything after the crc32 field in the header */
    uint32_t crc_field_off = (uint32_t)((const uint8_t *)&h->crc32 - image)
                             + (uint32_t)sizeof(h->crc32);
    if (crc_field_off > image_size)               return OPK_ERR_TRUNC;
    uint32_t got = opk_crc32(image + crc_field_off, image_size - crc_field_off);
    if (got != h->crc32)                          return OPK_ERR_HDR_CRC;

    const opk_entry_t *toc = (const opk_entry_t *)(image + h->toc_off);
    const uint8_t *payload = image + h->data_off;
    uint64_t payload_max = image_size - h->data_off;

    /* validate every entry before touching the filesystem */
    for (uint32_t i = 0; i < h->entry_cnt; i++) {
        const opk_entry_t *e = &toc[i];
        /* name must be NUL-terminated within its field */
        int term = 0;
        for (uint32_t k = 0; k < OPK_NAME_MAX; k++) {
            if (e->name[k] == '\0') { term = 1; break; }
        }
        if (!term)                        return OPK_ERR_NAME;
        if (!opk_name_safe(e->name))      return OPK_ERR_NAME;
        if (e->data_rel > payload_max)    return OPK_ERR_TRUNC;
        if (e->size > payload_max - e->data_rel) return OPK_ERR_TRUNC;
        uint32_t ecrc = opk_crc32(payload + e->data_rel, (uint32_t)e->size);
        if (ecrc != e->crc32)             return OPK_ERR_ENTRY_CRC;
    }

    /* pkgname must be NUL-terminated */
    int pkg_term = 0;
    for (uint32_t k = 0; k < OPK_PKGNAME_MAX; k++) {
        if (h->pkgname[k] == '\0') { pkg_term = 1; break; }
    }
    if (!pkg_term)                        return OPK_ERR_LAYOUT;

    /* create <root> then <root>/<pkgname> */
    if (ops->mkdir(ctx, root, OPK_MODE_DIR) != 0)
        return OPK_ERR_FS_MKDIR;

    char base[256];
    uint32_t bp = 0;
    if (opk_append(base, sizeof(base), &bp, root) != 0) return OPK_ERR_NAME;
    if (bp == 0 || base[bp - 1] != '/') {
        if (opk_append(base, sizeof(base), &bp, "/") != 0) return OPK_ERR_NAME;
    }
    if (opk_append(base, sizeof(base), &bp, h->pkgname) != 0) return OPK_ERR_NAME;
    if (ops->mkdir(ctx, base, OPK_MODE_DIR) != 0)
        return OPK_ERR_FS_MKDIR;
    if (opk_append(base, sizeof(base), &bp, "/") != 0) return OPK_ERR_NAME;

    uint64_t total_bytes = 0;

    /* install each entry */
    for (uint32_t i = 0; i < h->entry_cnt; i++) {
        const opk_entry_t *e = &toc[i];

        char full[512];
        uint32_t fp = 0;
        if (opk_append(full, sizeof(full), &fp, base) != 0) return OPK_ERR_NAME;
        if (opk_append(full, sizeof(full), &fp, e->name) != 0) return OPK_ERR_NAME;
        OPK_DBG("[opk] dbg base="); OPK_DBG(base);
        OPK_DBG(" name="); OPK_DBG(e->name);
        OPK_DBG(" full="); OPK_DBG(full); OPK_DBG("\n");

        /* ensure parent directories under base exist */
        int r = opk_mkdir_parents(full, ops, ctx);
        if (r != OPK_OK) return r;

        if (ops->write_file(ctx, full, payload + e->data_rel,
                            (uint32_t)e->size, e->mode) != 0)
            return OPK_ERR_FS_WRITE;

        total_bytes += e->size;
    }

    if (result) {
        uint32_t k = 0;
        for (; k < OPK_PKGNAME_MAX - 1 && h->pkgname[k]; k++)
            result->pkgname[k] = h->pkgname[k];
        result->pkgname[k] = '\0';
        result->files_installed = h->entry_cnt;
        result->bytes_written = total_bytes;
    }

    (void)opk_strlen; /* reserved helper */
    return OPK_OK;
}
