#include "openos.h"

int main(int argc, char **argv)
{
    openos_netinfo_t info;
    (void)argc;
    (void)argv;

    if (netinfo(&info) < 0) {
        printf("netstat: network stack unavailable\n");
        return 1;
    }

    printf("Kernel Interface table\n");
    printf("Iface      RX-OK RX-DRP TX-OK TX-DRP\n");
    printf("%-10s %5u %6u %5u %6u\n",
           info.name, info.rx_packets, info.rx_dropped,
           info.tx_packets, info.tx_dropped);
    printf("\nProtocol statistics\n");
    printf("ARP entries:      %u\n", info.arp_entries);
    printf("UDP bindings:     %u\n", info.udp_bindings);
    printf("TCP listeners:    %u\n", info.tcp_listeners);
    printf("TCP connections:  %u\n", info.tcp_connections);
    printf("ICMP echo req:    %u\n", info.icmp_echo_requests);
    printf("ICMP echo reply:  %u\n", info.icmp_echo_replies);
    return 0;
}
