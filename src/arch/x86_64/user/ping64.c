#include "openos64.h"

/*
 * ping — send ICMP echo requests to a host.
 *
 * Usage: ping <ip-or-hostname> [count]
 *
 * Resolves the target (dotted-quad parsed locally, hostname via SYS_DNSLOOKUP),
 * then issues SYS_PING (293) `count` times (default 4).
 */

static void put_str(const char *s) {
    openos64_write(1, s, openos64_strlen(s));
}

static void app_u32(char *buf, int *pos, uint32_t v) {
    char tmp[12];
    int n = 0;
    if (v == 0) { buf[(*pos)++] = '0'; return; }
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) buf[(*pos)++] = tmp[--n];
}

static void app_ip(char *buf, int *pos, uint32_t ip_h) {
    /* kernel uses host byte order: MSB is the first octet */
    for (int i = 3; i >= 0; --i) {
        app_u32(buf, pos, (uint8_t)((ip_h >> (i * 8)) & 0xff));
        if (i != 0) buf[(*pos)++] = '.';
    }
}

/* parse "a.b.c.d" -> host byte order (a is MSB). return 1 on success. */
static int parse_ipv4(const char *s, uint32_t *out) {
    uint32_t parts[4];
    int pi = 0;
    uint32_t cur = 0;
    int digits = 0;
    for (const char *p = s; ; ++p) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + (uint32_t)(c - '0');
            if (cur > 255) return 0;
            digits++;
        } else if (c == '.' || c == '\0') {
            if (digits == 0 || pi >= 4) return 0;
            parts[pi++] = cur;
            cur = 0; digits = 0;
            if (c == '\0') break;
        } else {
            return 0; /* non-numeric char -> treat as hostname */
        }
    }
    if (pi != 4) return 0;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | (parts[3]);
    return 1;
}

/* very small atoi for the optional count arg */
static int small_atoi(const char *s, int fallback) {
    int v = 0, any = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); any = 1; ++s; }
    return any ? v : fallback;
}

int openos64_main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2) {
        put_str("用法: ping <ip或域名> [次数]\n");
        return 1;
    }

    const char *target = argv[1];
    int count = (argc >= 3) ? small_atoi(argv[2], 4) : 4;
    if (count <= 0) count = 4;
    if (count > 32) count = 32;

    uint32_t ip_be = 0;
    if (!parse_ipv4(target, &ip_be)) {
        /* not a dotted quad -> resolve via DNS */
        if (openos64_dnslookup(target, &ip_be) != 0) {
            put_str("ping: 无法解析主机 ");
            put_str(target);
            put_str("\n");
            return 1;
        }
    }

    {
        char line[80];
        int p = 0;
        const char *pre = "PING ";
        while (*pre) line[p++] = *pre++;
        app_ip(line, &p, ip_be);
        const char *mid = " : ";
        while (*mid) line[p++] = *mid++;
        app_u32(line, &p, (uint32_t)count);
        const char *end = " 次\n";
        while (*end) line[p++] = *end++;
        openos64_write(1, line, (openos64_size_t)p);
    }

    int ok = 0, lost = 0;
    for (int i = 0; i < count; ++i) {
        int rc = openos64_ping(ip_be, 1000);
        char line[80];
        int p = 0;
        const char *seq = "  seq=";
        while (*seq) line[p++] = *seq++;
        app_u32(line, &p, (uint32_t)(i + 1));
        if (rc == 0) {
            const char *oks = "  reply\n";
            const char *s = oks; while (*s) line[p++] = *s++;
            ok++;
        } else {
            const char *ts = "  timeout\n";
            const char *s = ts; while (*s) line[p++] = *s++;
            lost++;
        }
        openos64_write(1, line, (openos64_size_t)p);
    }

    {
        char line[80];
        int p = 0;
        const char *pre = "统计: ";
        while (*pre) line[p++] = *pre++;
        app_u32(line, &p, (uint32_t)ok);
        const char *m1 = " 成功 / ";
        const char *s = m1; while (*s) line[p++] = *s++;
        app_u32(line, &p, (uint32_t)lost);
        const char *m2 = " 丢失\n";
        s = m2; while (*s) line[p++] = *s++;
        openos64_write(1, line, (openos64_size_t)p);
    }

    return (ok > 0) ? 0 : 1;
}
