/*
 * account_db64.c — M6.11.3: account database parser
 *
 * Scans the initrd-resident /etc/passwd and /etc/shadow images and copies
 * the requested account's fields into caller buffers. No dynamic memory,
 * no policy: authentication decisions are made in login64.c.
 */
#include "../include/account_db64.h"
#include "../include/initrd64.h"

#include <stdint.h>
#include <stddef.h>

/* ---- local string helpers (kernel has no libc here) ---- */

static size_t adb_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int adb_streq(const char *a, const char *b)
{
    size_t i = 0;
    for (; a[i] && b[i]; i++) {
        if (a[i] != b[i]) return 0;
    }
    return a[i] == b[i];
}

/* Copy [src, src+len) into dst (cap includes NUL). Always NUL-terminates. */
static void adb_copy_field(char *dst, size_t cap, const char *src, size_t len)
{
    size_t i = 0;
    if (cap == 0) return;
    for (; i < len && i + 1 < cap; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* Parse an unsigned decimal from [p, end). Stops at first non-digit. */
static uint32_t adb_parse_u32(const char *p, const char *end)
{
    uint32_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
    }
    return v;
}

/*
 * Iterate lines of a text blob. On entry *cursor points at the start of the
 * next line (or blob end). Returns the line [*line_start, *line_end) and
 * advances *cursor past the newline. Returns 0 when no more lines.
 */
static int adb_next_line(const char *blob, size_t blob_len, size_t *cursor,
                         const char **line_start, const char **line_end)
{
    if (*cursor >= blob_len) return 0;
    size_t s = *cursor;
    size_t e = s;
    while (e < blob_len && blob[e] != '\n') e++;
    *line_start = blob + s;
    *line_end   = blob + e;
    *cursor     = (e < blob_len) ? e + 1 : e; /* skip '\n' if present */
    return 1;
}

/*
 * Split a colon-separated line into up to max_fields spans. Returns the
 * number of fields found. field_start[i]/field_end[i] bound field i.
 */
static int adb_split_colons(const char *ls, const char *le,
                            const char **field_start, const char **field_end,
                            int max_fields)
{
    int n = 0;
    const char *p = ls;
    const char *fs = ls;
    while (p <= le && n < max_fields) {
        if (p == le || *p == ':') {
            field_start[n] = fs;
            field_end[n]   = p;
            n++;
            fs = p + 1;
            if (p == le) break;
        }
        p++;
    }
    return n;
}

int arch_x86_64_passwd_lookup(const char *name, x86_64_passwd_entry_t *out)
{
    if (!name || !out) return -1;

    const x86_64_initrd_file_t *f = arch_x86_64_initrd_find("/etc/passwd");
    if (!f || !f->data) return -2;

    const char *blob = (const char *)f->data;
    size_t blob_len  = (size_t)f->size;
    size_t want_len  = adb_strlen(name);

    size_t cursor = 0;
    const char *ls, *le;
    while (adb_next_line(blob, blob_len, &cursor, &ls, &le)) {
        if (ls == le) continue; /* blank line */

        /* passwd: name:passwd:uid:gid:gecos:home:shell (7 fields) */
        const char *fstart[7];
        const char *fend[7];
        int nf = adb_split_colons(ls, le, fstart, fend, 7);
        if (nf < 7) continue;

        size_t nlen = (size_t)(fend[0] - fstart[0]);
        if (nlen != want_len) continue;

        int match = 1;
        for (size_t i = 0; i < nlen; i++) {
            if (fstart[0][i] != name[i]) { match = 0; break; }
        }
        if (!match) continue;

        adb_copy_field(out->name, sizeof(out->name), fstart[0], nlen);
        out->uid = adb_parse_u32(fstart[2], fend[2]);
        out->gid = adb_parse_u32(fstart[3], fend[3]);
        adb_copy_field(out->home, sizeof(out->home),
                       fstart[5], (size_t)(fend[5] - fstart[5]));
        adb_copy_field(out->shell, sizeof(out->shell),
                       fstart[6], (size_t)(fend[6] - fstart[6]));
        return 0;
    }
    (void)adb_streq;
    return -3; /* not found */
}

int arch_x86_64_shadow_lookup(const char *name, x86_64_shadow_entry_t *out)
{
    if (!name || !out) return -1;

    const x86_64_initrd_file_t *f = arch_x86_64_initrd_find("/etc/shadow");
    if (!f || !f->data) return -2;

    const char *blob = (const char *)f->data;
    size_t blob_len  = (size_t)f->size;
    size_t want_len  = adb_strlen(name);

    size_t cursor = 0;
    const char *ls, *le;
    while (adb_next_line(blob, blob_len, &cursor, &ls, &le)) {
        if (ls == le) continue;

        /* shadow: name:hash:lastchg:min:max:warn:inactive:expire:reserved */
        const char *fstart[9];
        const char *fend[9];
        int nf = adb_split_colons(ls, le, fstart, fend, 9);
        if (nf < 2) continue;

        size_t nlen = (size_t)(fend[0] - fstart[0]);
        if (nlen != want_len) continue;

        int match = 1;
        for (size_t i = 0; i < nlen; i++) {
            if (fstart[0][i] != name[i]) { match = 0; break; }
        }
        if (!match) continue;

        adb_copy_field(out->name, sizeof(out->name), fstart[0], nlen);
        adb_copy_field(out->hash, sizeof(out->hash),
                       fstart[1], (size_t)(fend[1] - fstart[1]));
        return 0;
    }
    return -3;
}
