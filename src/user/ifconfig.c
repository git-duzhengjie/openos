#include "openos.h"

static void print_ip(unsigned int ip)
{
    printf("%u.%u.%u.%u",
           (ip >> 24) & 0xffU,
           (ip >> 16) & 0xffU,
           (ip >> 8) & 0xffU,
           ip & 0xffU);
}

static int parse_dec_octet(const char *s, int *idx, unsigned int *out)
{
    unsigned int value = 0;
    int digits = 0;

    while (s[*idx] >= '0' && s[*idx] <= '9') {
        value = value * 10u + (unsigned int)(s[*idx] - '0');
        if (value > 255u) return -1;
        (*idx)++;
        digits++;
    }
    if (digits == 0) return -1;
    *out = value;
    return 0;
}

static int parse_ip(const char *s, unsigned int *out)
{
    unsigned int a, b, c, d;
    int idx = 0;

    if (!s || !out) return -1;
    if (parse_dec_octet(s, &idx, &a) < 0 || s[idx++] != '.') return -1;
    if (parse_dec_octet(s, &idx, &b) < 0 || s[idx++] != '.') return -1;
    if (parse_dec_octet(s, &idx, &c) < 0 || s[idx++] != '.') return -1;
    if (parse_dec_octet(s, &idx, &d) < 0 || s[idx] != '\0') return -1;
    *out = (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

static void usage(void)
{
    printf("usage:\n");
    printf("  ifconfig\n");
    printf("  ifconfig <ip> [netmask <mask>] [gateway <gw>]\n");
}

int main(int argc, char **argv)
{
    openos_netinfo_t info;
    unsigned int ip;
    unsigned int mask;
    unsigned int gw;
    int i;

    if (netinfo(&info) < 0) {
        printf("ifconfig: no network device\n");
        return 1;
    }

    if (argc > 1) {
        ip = info.ip;
        mask = info.netmask;
        gw = info.gateway;

        if (parse_ip(argv[1], &ip) < 0) {
            usage();
            return 1;
        }

        i = 2;
        while (i < argc) {
            if (openos_strcmp(argv[i], "netmask") == 0 && i + 1 < argc) {
                if (parse_ip(argv[i + 1], &mask) < 0) {
                    usage();
                    return 1;
                }
                i += 2;
            } else if (openos_strcmp(argv[i], "gateway") == 0 && i + 1 < argc) {
                if (parse_ip(argv[i + 1], &gw) < 0) {
                    usage();
                    return 1;
                }
                i += 2;
            } else {
                usage();
                return 1;
            }
        }

        if (netconfig(ip, mask, gw) < 0) {
            printf("ifconfig: failed to configure network\n");
            return 1;
        }
        if (netinfo(&info) < 0) {
            printf("ifconfig: no network device\n");
            return 1;
        }
    }

    printf("%s  Link encap:Ethernet  HWaddr %02x:%02x:%02x:%02x:%02x:%02x\n",
           info.name,
           info.mac[0], info.mac[1], info.mac[2],
           info.mac[3], info.mac[4], info.mac[5]);
    printf("      inet addr:");
    print_ip(info.ip);
    printf("  Mask:");
    print_ip(info.netmask);
    printf("  Gateway:");
    print_ip(info.gateway);
    printf("\n");
    printf("      RX packets:%u dropped:%u\n", info.rx_packets, info.rx_dropped);
    printf("      TX packets:%u dropped:%u\n", info.tx_packets, info.tx_dropped);
    return 0;
}
