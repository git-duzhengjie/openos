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
    printf("  ifconfig <dev> up|down\n");
    printf("  ifconfig <ip> [netmask <mask>] [gateway <gw>]\n");
}

static const char *config_mode_name(unsigned int mode)
{
    if (mode == NET_CONFIG_MODE_DHCP) return "dhcp";
    if (mode == NET_CONFIG_MODE_STATIC) return "static";
    return "none";
}

static void print_flags(unsigned int flags)
{
    printf("      flags:");
    if (flags & NET_DEVICE_FLAG_UP) printf(" UP");
    else printf(" DOWN");
    if (flags & NET_DEVICE_FLAG_LINK_UP) printf(" LINK_UP");
    else printf(" NO_LINK");
    if (flags & NET_DEVICE_FLAG_DEFAULT) printf(" DEFAULT");
    if (flags & NET_DEVICE_FLAG_DHCP) printf(" DHCP");
    if (flags & NET_DEVICE_FLAG_STATIC) printf(" STATIC");
    printf("\n");
}

static int handle_dev_control(const char *name, const char *op)
{
    unsigned int ctl;

    if (openos_strcmp(op, "up") == 0) {
        ctl = NETDEV_CTL_SET_UP;
    } else if (openos_strcmp(op, "down") == 0) {
        ctl = NETDEV_CTL_SET_DOWN;
    } else {
        usage();
        return 1;
    }

    if (netdevctl(name, ctl) < 0) {
        printf("ifconfig: failed to set %s %s\n", name, op);
        return 1;
    }
    return 0;
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

    if (argc == 3 &&
        (openos_strcmp(argv[2], "up") == 0 || openos_strcmp(argv[2], "down") == 0)) {
        if (handle_dev_control(argv[1], argv[2]) != 0)
            return 1;
        if (netinfo(&info) < 0) {
            printf("ifconfig: no network device\n");
            return 1;
        }
    } else if (argc > 1) {
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
    print_flags(info.flags);
    printf("      mode:%s  inet addr:", config_mode_name(info.config_mode));
    print_ip(info.ip);
    printf("  Mask:");
    print_ip(info.netmask);
    printf("  Gateway:");
    print_ip(info.gateway);
    printf("  DNS:");
    print_ip(info.dns);
    printf("\n");
    printf("      RX packets:%u dropped:%u\n", info.rx_packets, info.rx_dropped);
    printf("      TX packets:%u dropped:%u\n", info.tx_packets, info.tx_dropped);
    return 0;
}
