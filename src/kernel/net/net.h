#ifndef OPENOS_NET_H
#define OPENOS_NET_H

#include "types.h"

#define NET_ETH_ADDR_LEN 6
#define NET_IPV4_ADDR_LEN 4
#define NET_ETH_MTU 1500
#define NET_FRAME_MAX_SIZE 1518

#define NET_IP4(a, b, c, d) \
    ((((uint32_t)(a) & 0xffU) << 24) | (((uint32_t)(b) & 0xffU) << 16) | \
     (((uint32_t)(c) & 0xffU) << 8) | ((uint32_t)(d) & 0xffU))

typedef struct net_device net_device_t;

typedef int (*net_tx_func_t)(net_device_t *dev, const uint8_t *frame, uint16_t len);

struct net_device {
    char name[16];
    uint8_t mac[NET_ETH_ADDR_LEN];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    net_tx_func_t transmit;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
    void *driver_data;
};

typedef void (*udp_recv_func_t)(uint32_t src_ip, uint16_t src_port,
                                uint16_t dst_port, const uint8_t *data,
                                uint16_t len);

typedef void (*tcp_recv_func_t)(uint32_t src_ip, uint16_t src_port,
                                uint16_t dst_port, const uint8_t *data,
                                uint16_t len);

void net_init(void);
int net_register_device(net_device_t *dev);
net_device_t *net_get_default_device(void);
void net_set_default_ipv4(uint32_t ip, uint32_t netmask, uint32_t gateway);
void net_input(net_device_t *dev, const uint8_t *frame, uint16_t len);
int net_send_ipv4(uint32_t dst_ip, uint8_t protocol, const uint8_t *payload, uint16_t payload_len);
int net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const uint8_t *data, uint16_t len);
int net_send_udp_broadcast(uint16_t src_port, uint16_t dst_port,
                           const uint8_t *data, uint16_t len);
int net_udp_bind(uint16_t port, udp_recv_func_t cb);
int net_tcp_listen(uint16_t port, tcp_recv_func_t cb);
int net_tcp_send_syn(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port);
int net_ping_self(void);
void net_print_info(void);
void net_format_ipv4(uint32_t ip, char *out);
int net_parse_ipv4(const char *text, uint32_t *out);

#endif /* OPENOS_NET_H */
