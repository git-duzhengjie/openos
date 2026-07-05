#include "openos64.h"

/*
 * ifconfig — show the primary network interface state.
 *
 * Talks to the live virtio-net TCP/IP stack via SYS_NETINFO (292).
 * Displays MAC / IPv4 config / DHCP-static mode / packet + error counters.
 */

/* --- tiny local formatting helpers (no libc in ring3) --- */

static void put_str(const char *s) {
    openos64_write(1, s, openos64_strlen(s));
}

/* append decimal of v into buf at *pos */
static void app_u32(char *buf, int *pos, uint32_t v) {
    char tmp[12];
    int n = 0;
    if (v == 0) { buf[(*pos)++] = '0'; return; }
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) buf[(*pos)++] = tmp[--n];
}

/* append "a.b.c.d" for a host-byte-order IPv4 into buf at *pos.
 * kernel stores ip in host order (e.g. 10.0.2.15 == 0x0A00020F),
 * so the most-significant byte is the first octet. */
static void app_ip(char *buf, int *pos, uint32_t ip_h) {
    uint8_t b0 = (uint8_t)((ip_h >> 24) & 0xff);
    uint8_t b1 = (uint8_t)((ip_h >> 16) & 0xff);
    uint8_t b2 = (uint8_t)((ip_h >> 8) & 0xff);
    uint8_t b3 = (uint8_t)(ip_h & 0xff);
    app_u32(buf, pos, b0); buf[(*pos)++] = '.';
    app_u32(buf, pos, b1); buf[(*pos)++] = '.';
    app_u32(buf, pos, b2); buf[(*pos)++] = '.';
    app_u32(buf, pos, b3);
}

/* append two-digit lowercase hex */
static void app_hex2(char *buf, int *pos, uint8_t v) {
    const char *hx = "0123456789abcdef";
    buf[(*pos)++] = hx[(v >> 4) & 0xf];
    buf[(*pos)++] = hx[v & 0xf];
}

static void print_ip_line(const char *label, uint32_t ip_be) {
    char line[64];
    int p = 0;
    while (*label) line[p++] = *label++;
    app_ip(line, &p, ip_be);
    line[p++] = '\n';
    openos64_write(1, line, (openos64_size_t)p);
}

static void print_u32_line(const char *label, uint32_t v) {
    char line[64];
    int p = 0;
    while (*label) line[p++] = *label++;
    app_u32(line, &p, v);
    line[p++] = '\n';
    openos64_write(1, line, (openos64_size_t)p);
}

int openos64_main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    openos64_netinfo_t ni;
    if (openos64_netinfo(&ni) != 0) {
        put_str("ifconfig: 无法读取网络状态 (SYS_NETINFO 失败)\n");
        return 1;
    }

    /* interface name + link flags */
    char head[80];
    int p = 0;
    const char *nm = ni.name[0] ? ni.name : "net0";
    while (*nm) head[p++] = *nm++;
    const char *up = (ni.flags & OPENOS64_NETFLAG_UP) ? "  <UP>\n" : "  <DOWN>\n";
    const char *q = up;
    while (*q) head[p++] = *q++;
    openos64_write(1, head, (openos64_size_t)p);

    /* MAC address */
    {
        char line[48];
        int lp = 0;
        const char *lbl = "  ether  ";
        while (*lbl) line[lp++] = *lbl++;
        for (int i = 0; i < 6; ++i) {
            app_hex2(line, &lp, ni.mac[i]);
            if (i != 5) line[lp++] = ':';
        }
        line[lp++] = '\n';
        openos64_write(1, line, (openos64_size_t)lp);
    }

    print_ip_line("  inet   ", ni.ip);
    print_ip_line("  mask   ", ni.netmask);
    print_ip_line("  gw     ", ni.gateway);
    print_ip_line("  dns    ", ni.dns);

    put_str(ni.config_mode == OPENOS64_NETCFG_STATIC
                ? "  mode   static\n"
                : "  mode   dhcp\n");

    print_u32_line("  rx_pkts  ", ni.rx_packets);
    print_u32_line("  tx_pkts  ", ni.tx_packets);
    print_u32_line("  rx_drop  ", ni.rx_dropped);
    print_u32_line("  tx_drop  ", ni.tx_dropped);
    print_u32_line("  arp      ", ni.arp_entries);
    print_u32_line("  icmp_rx  ", ni.icmp_echo_replies);
    print_u32_line("  tcp_conn ", ni.tcp_connections);

    return 0;
}
