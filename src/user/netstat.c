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
    printf("Last IPv4:        src=%u.%u.%u.%u dst=%u.%u.%u.%u proto=%u\n",
           (info.last_ipv4_src >> 24) & 255, (info.last_ipv4_src >> 16) & 255,
           (info.last_ipv4_src >> 8) & 255, info.last_ipv4_src & 255,
           (info.last_ipv4_dst >> 24) & 255, (info.last_ipv4_dst >> 16) & 255,
           (info.last_ipv4_dst >> 8) & 255, info.last_ipv4_dst & 255,
           info.last_ipv4_protocol);
    printf("Last ICMP:        src=%u.%u.%u.%u type=%u code=%u\n",
           (info.last_icmp_src >> 24) & 255, (info.last_icmp_src >> 16) & 255,
           (info.last_icmp_src >> 8) & 255, info.last_icmp_src & 255,
           info.last_icmp_type, info.last_icmp_code);
    printf("IPv4 drops:       short=%u version=%u ihl=%u len=%u checksum=%u dst=%u\n",
           info.ipv4_drop_short, info.ipv4_drop_version, info.ipv4_drop_ihl,
           info.ipv4_drop_len, info.ipv4_drop_checksum, info.ipv4_drop_dst);
    printf("Last IPv4 TX:     src=%u.%u.%u.%u dst=%u.%u.%u.%u next=%u.%u.%u.%u proto=%u len=%u result=%d\n",
           (info.last_ipv4_tx_src >> 24) & 255, (info.last_ipv4_tx_src >> 16) & 255,
           (info.last_ipv4_tx_src >> 8) & 255, info.last_ipv4_tx_src & 255,
           (info.last_ipv4_tx_dst >> 24) & 255, (info.last_ipv4_tx_dst >> 16) & 255,
           (info.last_ipv4_tx_dst >> 8) & 255, info.last_ipv4_tx_dst & 255,
           (info.last_ipv4_tx_next_hop >> 24) & 255, (info.last_ipv4_tx_next_hop >> 16) & 255,
           (info.last_ipv4_tx_next_hop >> 8) & 255, info.last_ipv4_tx_next_hop & 255,
           info.last_ipv4_tx_protocol, info.last_ipv4_tx_len, info.last_ipv4_tx_result);
    printf("Last ping TX:     dst=%u.%u.%u.%u id=%u seq=%u result=%d\n",
           (info.last_ping_dst >> 24) & 255, (info.last_ping_dst >> 16) & 255,
           (info.last_ping_dst >> 8) & 255, info.last_ping_dst & 255,
           info.last_ping_id, info.last_ping_seq, info.last_ping_send_result);
    return 0;
}
