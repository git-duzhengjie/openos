#include "openos.h"

static void print_ip(unsigned int ip)
{
    printf("%u.%u.%u.%u",
           (ip >> 24) & 0xffU,
           (ip >> 16) & 0xffU,
           (ip >> 8) & 0xffU,
           ip & 0xffU);
}

int main(int argc, char **argv)
{
    openos_netinfo_t info;
    (void)argc;
    (void)argv;

    if (netinfo(&info) < 0) {
        printf("ifconfig: no network device\n");
        return 1;
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
