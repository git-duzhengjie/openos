#include "openos64.h"

/*
 * nslookup — resolve a hostname to an IPv4 address via SYS_DNSLOOKUP (316).
 *
 * Usage: nslookup <hostname>
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
    /* kernel returns host byte order: MSB is the first octet */
    for (int i = 3; i >= 0; --i) {
        app_u32(buf, pos, (uint8_t)((ip_h >> (i * 8)) & 0xff));
        if (i != 0) buf[(*pos)++] = '.';
    }
}

int openos64_main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2) {
        put_str("用法: nslookup <域名>\n");
        return 1;
    }

    const char *host = argv[1];
    uint32_t ip_be = 0;

    if (openos64_dnslookup(host, &ip_be) != 0) {
        put_str("nslookup: 解析失败: ");
        put_str(host);
        put_str("\n");
        return 1;
    }

    char line[96];
    int p = 0;
    const char *h = host;
    while (*h) line[p++] = *h++;
    const char *arrow = "  ->  ";
    const char *s = arrow; while (*s) line[p++] = *s++;
    app_ip(line, &p, ip_be);
    line[p++] = '\n';
    openos64_write(1, line, (openos64_size_t)p);

    return 0;
}
