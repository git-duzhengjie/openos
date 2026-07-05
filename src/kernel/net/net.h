#ifndef OPENOS_NET_H
#define OPENOS_NET_H

#include "types.h"

#define NET_ETH_ADDR_LEN 6
#define NET_IPV4_ADDR_LEN 4
#define NET_ETH_MTU 1500
#define NET_FRAME_MAX_SIZE 1518
#define NET_DEVICE_NAME_MAX 32
#define NET_DRIVER_NAME_MAX 16

#define NET_IP4(a, b, c, d) \
    ((((uint32_t)(a) & 0xffU) << 24) | (((uint32_t)(b) & 0xffU) << 16) | \
     (((uint32_t)(c) & 0xffU) << 8) | ((uint32_t)(d) & 0xffU))

typedef struct net_device net_device_t;

typedef struct net_diag_stats {
    char name[16];
    uint8_t mac[NET_ETH_ADDR_LEN];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
    uint32_t arp_entries;
    uint32_t udp_bindings;
    uint32_t tcp_listeners;
    uint32_t tcp_connections;
    uint32_t icmp_echo_requests;
    uint32_t icmp_echo_replies;
    uint32_t last_ipv4_src;
    uint32_t last_ipv4_dst;
    uint32_t last_ipv4_protocol;
    uint32_t last_icmp_src;
    uint32_t last_icmp_type;
    uint32_t last_icmp_code;
    uint32_t ipv4_drop_short;
    uint32_t ipv4_drop_version;
    uint32_t ipv4_drop_ihl;
    uint32_t ipv4_drop_len;
    uint32_t ipv4_drop_checksum;
    uint32_t ipv4_drop_dst;
    uint32_t last_ipv4_tx_src;
    uint32_t last_ipv4_tx_dst;
    uint32_t last_ipv4_tx_next_hop;
    uint32_t last_ipv4_tx_protocol;
    uint32_t last_ipv4_tx_len;
    int32_t last_ipv4_tx_result;
    uint32_t last_ping_dst;
    uint32_t last_ping_id;
    uint32_t last_ping_seq;
    int32_t last_ping_send_result;
} net_diag_stats_t;

#define NET_FIREWALL_RULES 16u
#define NET_FW_OP_GET    0u
#define NET_FW_OP_ADD    1u
#define NET_FW_OP_DELETE 2u
#define NET_FW_OP_CLEAR  3u
#define NET_FW_ACTION_ALLOW 0u
#define NET_FW_ACTION_DENY  1u
#define NET_FW_PROTO_ANY  0u
#define NET_FW_PROTO_ICMP 1u
#define NET_FW_PROTO_TCP  6u
#define NET_FW_PROTO_UDP  17u

typedef struct net_firewall_rule {
    uint32_t used;
    uint32_t action;
    uint32_t protocol;
    uint32_t port;
    uint32_t hits;
} net_firewall_rule_t;

typedef int (*net_tx_func_t)(net_device_t *dev, const uint8_t *frame, uint16_t len);

typedef enum net_config_mode {
    NET_CONFIG_MODE_NONE = 0,
    NET_CONFIG_MODE_STATIC = 1,
    NET_CONFIG_MODE_DHCP = 2
} net_config_mode_t;

struct net_device {
    char name[16];
    uint8_t mac[NET_ETH_ADDR_LEN];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    net_config_mode_t config_mode;
    uint32_t link_up;
    uint32_t admin_up;
    net_tx_func_t transmit;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
    void *driver_data;
};

typedef struct net_device_info {
    char name[NET_DEVICE_NAME_MAX];
    char driver[NET_DRIVER_NAME_MAX];
    uint8_t mac[NET_ETH_ADDR_LEN];
    uint32_t mtu;
    uint32_t flags;
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t config_mode;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_errors;
    uint32_t tx_errors;
} net_device_info_t;

#define NET_DEVICE_FLAG_PRESENT 0x00000001u
#define NET_DEVICE_FLAG_UP      0x00000002u
#define NET_DEVICE_FLAG_LINK_UP 0x00000004u
#define NET_DEVICE_FLAG_DHCP    0x00000008u
#define NET_DEVICE_FLAG_DEFAULT 0x00000010u
#define NET_DEVICE_FLAG_STATIC  0x00000020u
#define NET_DEVICE_FLAG_WIRED   0x00000040u
#define NET_DEVICE_FLAG_WIRELESS 0x00000080u

#define NET_WIFI_MAX_RESULTS 8
#define NET_WIFI_SSID_MAX    32

typedef struct net_wifi_network_info {
    char ssid[NET_WIFI_SSID_MAX];
    uint32_t signal_percent;
    uint8_t secured;
    uint8_t connected;
} net_wifi_network_info_t;

#define NETDEV_CTL_SET_DOWN    0u
#define NETDEV_CTL_SET_UP      1u
#define NETDEV_CTL_DHCP_START   2u
#define NETDEV_CTL_DHCP_RENEW   3u
#define NETDEV_CTL_DHCP_RELEASE 4u
#define NETDEV_CTL_REFRESH      5u

typedef void (*udp_recv_func_t)(uint32_t src_ip, uint16_t src_port,
                                uint16_t dst_port, const uint8_t *data,
                                uint16_t len);

typedef void (*tcp_recv_func_t)(uint32_t src_ip, uint16_t src_port,
                                uint16_t dst_port, const uint8_t *data,
                                uint16_t len);

#define NET_TCP_STATE_CLOSED       0u
#define NET_TCP_STATE_LISTEN       1u
#define NET_TCP_STATE_SYN_SENT     2u
#define NET_TCP_STATE_SYN_RECEIVED 3u
#define NET_TCP_STATE_ESTABLISHED  4u
#define NET_TCP_STATE_FIN_WAIT_1   5u
#define NET_TCP_STATE_FIN_WAIT_2   6u
#define NET_TCP_STATE_CLOSE_WAIT   7u
#define NET_TCP_STATE_CLOSING      8u
#define NET_TCP_STATE_LAST_ACK     9u
#define NET_TCP_STATE_TIME_WAIT    10u

void net_init(void);
int net_register_device(net_device_t *dev);
net_device_t *net_get_default_device(void);
uint32_t net_device_count(void);
net_device_t *net_get_device_by_index(uint32_t index);
net_device_t *net_find_device(const char *name);
int net_get_device_info(uint32_t index, net_device_info_t *out);
int net_get_device_info_by_name(const char *name, net_device_info_t *out);
uint32_t net_scan_wifi(net_wifi_network_info_t *out_list, uint32_t max_results);
int net_set_device_admin_up(const char *name, int up);
int net_refresh_device_status(const char *name);
void net_set_default_ipv4(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns);
void net_set_default_ipv4_dhcp(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns);
void net_input(net_device_t *dev, const uint8_t *frame, uint16_t len);
void net_poll(void);
int net_send_ipv4(uint32_t dst_ip, uint8_t protocol, const uint8_t *payload, uint16_t payload_len);
int net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const uint8_t *data, uint16_t len);
int net_send_udp_broadcast(uint16_t src_port, uint16_t dst_port,
                           const uint8_t *data, uint16_t len);
int net_udp_bind(uint16_t port, udp_recv_func_t cb);
int net_tcp_listen(uint16_t port, tcp_recv_func_t cb);
int net_tcp_open(uint32_t local_ip, uint16_t local_port,
                 uint32_t remote_ip, uint16_t remote_port, int active);
int net_tcp_send(int conn_id, const uint8_t *data, uint16_t len);
int net_tcp_available(int conn_id);
int net_tcp_recv(int conn_id, uint8_t *data, uint16_t len);
int net_tcp_close(int conn_id);
int net_tcp_state(int conn_id);
int net_tcp_get_endpoint(int conn_id, uint32_t *local_ip, uint16_t *local_port,
                         uint32_t *remote_ip, uint16_t *remote_port);
void net_tick(uint32_t elapsed_ms);
int net_tcp_send_syn(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port);
int net_ping_self(void);
int net_ping_ipv4(uint32_t dst_ip);
int net_get_diag_stats(net_diag_stats_t *stats);
int net_config_ipv4(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns);
int net_dhcp_start(void);
int net_dhcp_state(void);
int net_firewall_get(uint32_t index, net_firewall_rule_t *rule);
int net_firewall_add(const net_firewall_rule_t *rule);
int net_firewall_delete(uint32_t index);
void net_firewall_clear(void);
void net_print_info(void);
void net_format_ipv4(uint32_t ip, char *out);
int net_parse_ipv4(const char *text, uint32_t *out);

#endif /* OPENOS_NET_H */
