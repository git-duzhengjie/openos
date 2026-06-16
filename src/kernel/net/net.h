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
void net_set_default_ipv4(uint32_t ip, uint32_t netmask, uint32_t gateway);
void net_input(net_device_t *dev, const uint8_t *frame, uint16_t len);
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
int net_tcp_recv(int conn_id, uint8_t *data, uint16_t len);
int net_tcp_close(int conn_id);
int net_tcp_state(int conn_id);
void net_tick(uint32_t elapsed_ms);
int net_tcp_send_syn(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port);
int net_ping_self(void);
void net_print_info(void);
void net_format_ipv4(uint32_t ip, char *out);
int net_parse_ipv4(const char *text, uint32_t *out);

#endif /* OPENOS_NET_H */
