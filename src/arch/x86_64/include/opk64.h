#ifndef OPENOS_ARCH_X86_64_OPK64_H
#define OPENOS_ARCH_X86_64_OPK64_H

/*
 * .opk (openOS package) minimal package format.
 *
 * Layout on disk (all multi-byte fields little-endian):
 *
 *   +-------------------------+  offset 0
 *   |  opk_header_t           |  fixed-size header
 *   +-------------------------+
 *   |  opk_entry_t[entry_cnt] |  table of content (TOC)
 *   +-------------------------+
 *   |  payload blob           |  concatenated file data
 *   +-------------------------+
 *
 * Each opk_entry_t describes one packaged file: its path (relative,
 * install target under /pkg/<name>/...), byte size, mode bits and the
 * offset of its data within the payload blob (relative to data_off in
 * the header).
 */

#include <stdint.h>

#define OPK_MAGIC        0x4B50304F36344C55ULL /* LE bytes spell "UL46O0PK" */
#define OPK_VERSION      1u
#define OPK_NAME_MAX     64u
#define OPK_PKGNAME_MAX  48u
#define OPK_MAX_ENTRIES  128u

/* mode bits (subset, aligned with ramfs64 node modes) */
#define OPK_MODE_FILE    0x8000u
#define OPK_MODE_DIR     0x4000u
#define OPK_MODE_EXEC    0x0049u /* r-x for demo: owner/group/other x */

typedef struct opk_header {
    uint64_t magic;        /* OPK_MAGIC */
    uint32_t version;      /* OPK_VERSION */
    uint32_t entry_cnt;    /* number of opk_entry_t following header */
    uint64_t toc_off;      /* byte offset of TOC (== sizeof header) */
    uint64_t data_off;     /* byte offset of payload blob */
    uint64_t total_size;   /* total .opk file size in bytes */
    uint32_t crc32;        /* CRC32 of everything after this field */
    char     pkgname[OPK_PKGNAME_MAX]; /* package name, NUL-terminated */
    uint32_t reserved;     /* pad / future use, must be 0 */
} opk_header_t;

typedef struct opk_entry {
    char     name[OPK_NAME_MAX]; /* relative path, NUL-terminated */
    uint64_t data_rel;           /* byte offset within payload blob */
    uint64_t size;               /* file byte size */
    uint32_t mode;               /* OPK_MODE_* bits */
    uint32_t crc32;              /* CRC32 of this file's bytes */
} opk_entry_t;

#endif /* OPENOS_ARCH_X86_64_OPK64_H */
