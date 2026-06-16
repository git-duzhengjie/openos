#include "net.h"
#include "string.h"
#include "vga.h"
#include "devmgr.h"
#include "socket.h"

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP  0x0806

#define ARP_HW_ETHERNET 1
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17
#define NET_IPV4_BROADCAST 0xffffffffU

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define ARP_CACHE_SIZE 8
#define UDP_BIND_SIZE 8
#define TCP_LISTEN_SIZE 8
#define TCP_CONN_SIZE 16
#define TCP_RECV_BUFFER_SIZE 2048
#define TCP_RETX_BUFFER_SIZE 536
#define TCP_RETX_TIMEOUT_MS 500u
#define TCP_RETX_MAX_RETRIES 5u
#define TCP_MSS TCP_RETX_BUFFER_SIZE
#define TCP_INITIAL_CWND TCP_MSS
#define TCP_INITIAL_SSTHRESH (TCP_MSS * 4u)
#define TCP_DEFAULT_WINDOW TCP_RECV_BUFFER_SIZE
#define TCP_TIME_WAIT_MS 2000u

#define TCP_STATE_CLOSED      NET_TCP_STATE_CLOSED
#define TCP_STATE_LISTEN      NET_TCP_STATE_LISTEN
#define TCP_STATE_SYN_SENT    NET_TCP_STATE_SYN_SENT
#define TCP_STATE_SYN_RCVD    NET_TCP_STATE_SYN_RECEIVED
#define TCP_STATE_ESTABLISHED NET_TCP_STATE_ESTABLISHED
#define TCP_STATE_FIN_WAIT_1  NET_TCP_STATE_FIN_WAIT_1
#define TCP_STATE_FIN_WAIT_2  NET_TCP_STATE_FIN_WAIT_2
#define TCP_STATE_CLOSE_WAIT  NET_TCP_STATE_CLOSE_WAIT
#define TCP_STATE_CLOSING     NET_TCP_STATE_CLOSING
#define TCP_STATE_LAST_ACK    NET_TCP_STATE_LAST_ACK
#define TCP_STATE_TIME_WAIT   NET_TCP_STATE_TIME_WAIT

struct eth_header {
    uint8_t dst[NET_ETH_ADDR_LEN];
    uint8_t src[NET_ETH_ADDR_LEN];
    uint16_t type;
} __attribute__((packed));

struct arp_packet {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[NET_ETH_ADDR_LEN];
    uint32_t spa;
    uint8_t tha[NET_ETH_ADDR_LEN];
    uint32_t tpa;
} __attribute__((packed));

struct ipv4_header {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed));

struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t seq;
} __attribute__((packed));

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed));

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

struct arp_entry {
    int used;
    uint32_t ip;
    uint8_t mac[NET_ETH_ADDR_LEN];
};

struct udp_binding {
    int used;
    uint16_t port;
    udp_recv_func_t cb;
};

struct tcp_listener {
    int used;
    uint16_t port;
    tcp_recv_func_t cb;
};

struct tcp_connection {
    int used;
    int id;
    uint8_t state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint16_t snd_wnd;
    uint16_t rcv_wnd;
    uint8_t rx[TCP_RECV_BUFFER_SIZE];
    uint16_t rx_len;
    int unacked_used;
    uint32_t unacked_seq;
    uint32_t unacked_ack;
    uint16_t unacked_len;
    uint8_t unacked_flags;
    uint32_t unacked_sent_ms;
    uint8_t unacked_retries;
    uint32_t cwnd;
    uint32_t ssthresh;
    uint32_t cwnd_acked;
    uint32_t state_since_ms;
    uint8_t unacked_data[TCP_RETX_BUFFER_SIZE];
};

static net_device_t *default_dev;
static uint32_t icmp_echo_requests;
static uint32_t icmp_echo_replies;
static net_firewall_rule_t firewall_rules[NET_FIREWALL_RULES];
static struct arp_entry arp_cache[ARP_CACHE_SIZE];
static struct udp_binding udp_bindings[UDP_BIND_SIZE];
static struct tcp_listener tcp_listeners[TCP_LISTEN_SIZE];
static struct tcp_connection tcp_connections[TCP_CONN_SIZE];
static int tcp_next_id = 1;
static uint16_t ipv4_ident = 1;
static uint32_t tcp_clock_ms;

static uint16_t bswap16(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}

static uint32_t bswap32(uint32_t x) {
    return ((x & 0x000000ffU) << 24) |
           ((x & 0x0000ff00U) << 8) |
           ((x & 0x00ff0000U) >> 8) |
           ((x & 0xff000000U) >> 24);
}

static uint16_t htons(uint16_t x) { return bswap16(x); }
static uint16_t ntohs(uint16_t x) { return bswap16(x); }
static uint32_t htonl(uint32_t x) { return bswap32(x); }
static uint32_t ntohl(uint32_t x) { return bswap32(x); }

static void print_dec(uint32_t value) {
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    if (value == 0) {
        vga_putc('0');
        return;
    }
    while (value > 0 && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }
    vga_write(&buf[i]);
}

static int same_mac(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, NET_ETH_ADDR_LEN) == 0;
}

static void copy_mac(uint8_t *dst, const uint8_t *src) {
    memcpy(dst, src, NET_ETH_ADDR_LEN);
}

static int arp_lookup(uint32_t ip, uint8_t *mac) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].used && arp_cache[i].ip == ip) {
            copy_mac(mac, arp_cache[i].mac);
            return 0;
        }
    }
    return -1;
}

static void arp_insert(uint32_t ip, const uint8_t *mac) {
    int i;
    int slot = -1;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].used && arp_cache[i].ip == ip) {
            slot = i;
            break;
        }
        if (!arp_cache[i].used && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) slot = 0;
    arp_cache[slot].used = 1;
    arp_cache[slot].ip = ip;
    copy_mac(arp_cache[slot].mac, mac);
}

static uint16_t checksum16(const void *data, uint16_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint16_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
    return (uint16_t)(~sum);
}

static int eth_send(net_device_t *dev, const uint8_t *dst, uint16_t type,
                    const uint8_t *payload, uint16_t payload_len) {
    uint8_t frame[NET_FRAME_MAX_SIZE];
    struct eth_header *eth;
    uint16_t frame_len;

    if (!dev || !dev->transmit || payload_len > NET_ETH_MTU) return -1;

    eth = (struct eth_header *)frame;
    copy_mac(eth->dst, dst);
    copy_mac(eth->src, dev->mac);
    eth->type = htons(type);
    memcpy(frame + sizeof(struct eth_header), payload, payload_len);
    frame_len = (uint16_t)(sizeof(struct eth_header) + payload_len);

    if (dev->transmit(dev, frame, frame_len) == 0) {
        dev->tx_packets++;
        return 0;
    }
    dev->tx_dropped++;
    return -1;
}

static int arp_send_request(uint32_t target_ip) {
    uint8_t broadcast[NET_ETH_ADDR_LEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
    struct arp_packet arp;
    if (!default_dev) return -1;
    memset(&arp, 0, sizeof(arp));
    arp.htype = htons(ARP_HW_ETHERNET);
    arp.ptype = htons(ETH_TYPE_IPV4);
    arp.hlen = NET_ETH_ADDR_LEN;
    arp.plen = NET_IPV4_ADDR_LEN;
    arp.oper = htons(ARP_OP_REQUEST);
    copy_mac(arp.sha, default_dev->mac);
    arp.spa = htonl(default_dev->ip);
    arp.tpa = htonl(target_ip);
    return eth_send(default_dev, broadcast, ETH_TYPE_ARP, (const uint8_t *)&arp, sizeof(arp));
}

static int arp_send_reply(net_device_t *dev, const struct arp_packet *req) {
    struct arp_packet arp;
    if (!dev) return -1;
    memset(&arp, 0, sizeof(arp));
    arp.htype = htons(ARP_HW_ETHERNET);
    arp.ptype = htons(ETH_TYPE_IPV4);
    arp.hlen = NET_ETH_ADDR_LEN;
    arp.plen = NET_IPV4_ADDR_LEN;
    arp.oper = htons(ARP_OP_REPLY);
    copy_mac(arp.sha, dev->mac);
    arp.spa = htonl(dev->ip);
    copy_mac(arp.tha, req->sha);
    arp.tpa = req->spa;
    return eth_send(dev, req->sha, ETH_TYPE_ARP, (const uint8_t *)&arp, sizeof(arp));
}

static void handle_arp(net_device_t *dev, const uint8_t *data, uint16_t len) {
    const struct arp_packet *arp;
    uint16_t op;
    uint32_t spa;
    uint32_t tpa;
    if (len < sizeof(struct arp_packet)) return;
    arp = (const struct arp_packet *)data;
    if (ntohs(arp->htype) != ARP_HW_ETHERNET || ntohs(arp->ptype) != ETH_TYPE_IPV4) return;
    spa = ntohl(arp->spa);
    tpa = ntohl(arp->tpa);
    arp_insert(spa, arp->sha);
    op = ntohs(arp->oper);
    if (op == ARP_OP_REQUEST && dev && tpa == dev->ip) {
        arp_send_reply(dev, arp);
    }
}

int net_send_ipv4(uint32_t dst_ip, uint8_t protocol, const uint8_t *payload, uint16_t payload_len) {
    uint8_t packet[NET_ETH_MTU];
    struct ipv4_header *ip;
    uint8_t dst_mac[NET_ETH_ADDR_LEN];
    uint16_t total_len;
    uint32_t next_hop;

    if (!default_dev || payload_len + sizeof(struct ipv4_header) > NET_ETH_MTU) return -1;
    next_hop = dst_ip;
    if (dst_ip != NET_IPV4_BROADCAST && default_dev->gateway != 0 &&
        ((dst_ip ^ default_dev->ip) & default_dev->netmask) != 0) {
        next_hop = default_dev->gateway;
    }
    if (dst_ip == NET_IPV4_BROADCAST) {
        memset(dst_mac, 0xff, sizeof(dst_mac));
    } else if (arp_lookup(next_hop, dst_mac) != 0) {
        if (next_hop == default_dev->ip) {
            copy_mac(dst_mac, default_dev->mac);
            arp_insert(next_hop, dst_mac);
        } else {
            arp_send_request(next_hop);
            return -1;
        }
    }

    memset(packet, 0, sizeof(struct ipv4_header));
    ip = (struct ipv4_header *)packet;
    total_len = (uint16_t)(sizeof(struct ipv4_header) + payload_len);
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(total_len);
    ip->id = htons(ipv4_ident++);
    ip->frag_off = htons(0x4000);
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->src = htonl(default_dev->ip);
    ip->dst = htonl(dst_ip);
    memcpy(packet + sizeof(struct ipv4_header), payload, payload_len);
    ip->checksum = 0;
    ip->checksum = htons(checksum16(ip, sizeof(struct ipv4_header)));

    return eth_send(default_dev, dst_mac, ETH_TYPE_IPV4, packet, total_len);
}

static int icmp_send_reply(uint32_t dst_ip, const uint8_t *request, uint16_t len) {
    uint8_t payload[NET_ETH_MTU];
    struct icmp_header *icmp;
    if (len > sizeof(payload)) return -1;
    memcpy(payload, request, len);
    icmp = (struct icmp_header *)payload;
    icmp->type = ICMP_ECHO_REPLY;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->checksum = htons(checksum16(payload, len));
    return net_send_ipv4(dst_ip, IP_PROTO_ICMP, payload, len);
}

static void handle_icmp(uint32_t src_ip, const uint8_t *payload, uint16_t len) {
    const struct icmp_header *icmp;
    if (len < sizeof(struct icmp_header)) return;
    icmp = (const struct icmp_header *)payload;
    if (icmp->type == ICMP_ECHO_REQUEST) {
        icmp_echo_requests++;
        icmp_send_reply(src_ip, payload, len);
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        icmp_echo_replies++;
    }
}

int net_udp_bind(uint16_t port, udp_recv_func_t cb) {
    int i;
    for (i = 0; i < UDP_BIND_SIZE; i++) {
        if (!udp_bindings[i].used || udp_bindings[i].port == port) {
            udp_bindings[i].used = 1;
            udp_bindings[i].port = port;
            udp_bindings[i].cb = cb;
            return 0;
        }
    }
    return -1;
}

int net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const uint8_t *data, uint16_t len) {
    uint8_t payload[NET_ETH_MTU];
    struct udp_header *udp;
    uint16_t udp_len;
    if (len + sizeof(struct udp_header) > sizeof(payload)) return -1;
    udp = (struct udp_header *)payload;
    udp_len = (uint16_t)(sizeof(struct udp_header) + len);
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->len = htons(udp_len);
    udp->checksum = 0;
    memcpy(payload + sizeof(struct udp_header), data, len);
    return net_send_ipv4(dst_ip, IP_PROTO_UDP, payload, udp_len);
}

int net_send_udp_broadcast(uint16_t src_port, uint16_t dst_port,
                           const uint8_t *data, uint16_t len) {
    return net_send_udp(NET_IPV4_BROADCAST, src_port, dst_port, data, len);
}

static int firewall_protocol_valid(uint32_t protocol) {
    return protocol == NET_FW_PROTO_ANY || protocol == NET_FW_PROTO_ICMP ||
           protocol == NET_FW_PROTO_TCP || protocol == NET_FW_PROTO_UDP;
}

static int firewall_action_valid(uint32_t action) {
    return action == NET_FW_ACTION_ALLOW || action == NET_FW_ACTION_DENY;
}

static int firewall_allows(uint8_t protocol, uint16_t dst_port) {
    uint32_t i;
    for (i = 0; i < NET_FIREWALL_RULES; i++) {
        net_firewall_rule_t *r = &firewall_rules[i];
        if (!r->used) continue;
        if (r->protocol != NET_FW_PROTO_ANY && r->protocol != protocol) continue;
        if (r->port != 0 && r->port != dst_port) continue;
        r->hits++;
        return r->action != NET_FW_ACTION_DENY;
    }
    return 1;
}

int net_firewall_get(uint32_t index, net_firewall_rule_t *rule) {
    if (!rule || index >= NET_FIREWALL_RULES) return -1;
    *rule = firewall_rules[index];
    return 0;
}

int net_firewall_add(const net_firewall_rule_t *rule) {
    uint32_t i;
    if (!rule || !firewall_protocol_valid(rule->protocol) ||
        !firewall_action_valid(rule->action) || rule->port > 65535U) {
        return -1;
    }
    for (i = 0; i < NET_FIREWALL_RULES; i++) {
        if (!firewall_rules[i].used) {
            firewall_rules[i].used = 1;
            firewall_rules[i].action = rule->action;
            firewall_rules[i].protocol = rule->protocol;
            firewall_rules[i].port = rule->port;
            firewall_rules[i].hits = 0;
            return (int)i;
        }
    }
    return -1;
}

int net_firewall_delete(uint32_t index) {
    if (index >= NET_FIREWALL_RULES) return -1;
    memset(&firewall_rules[index], 0, sizeof(firewall_rules[index]));
    return 0;
}

void net_firewall_clear(void) {
    memset(firewall_rules, 0, sizeof(firewall_rules));
}

static void handle_udp(uint32_t src_ip, const uint8_t *payload, uint16_t len) {
    const struct udp_header *udp;
    uint16_t dst_port;
    uint16_t udp_len;
    uint16_t data_len;
    int i;
    if (len < sizeof(struct udp_header)) return;
    udp = (const struct udp_header *)payload;
    udp_len = ntohs(udp->len);
    if (udp_len < sizeof(struct udp_header) || udp_len > len) return;
    dst_port = ntohs(udp->dst_port);
    data_len = (uint16_t)(udp_len - sizeof(struct udp_header));
    if (!firewall_allows(IP_PROTO_UDP, dst_port)) return;
    if (socket_deliver_udp(src_ip, ntohs(udp->src_port), dst_port,
                           payload + sizeof(struct udp_header), data_len) == 0) {
        return;
    }
    for (i = 0; i < UDP_BIND_SIZE; i++) {
        if (udp_bindings[i].used && udp_bindings[i].port == dst_port && udp_bindings[i].cb) {
            udp_bindings[i].cb(src_ip, ntohs(udp->src_port), dst_port,
                               payload + sizeof(struct udp_header), data_len);
            return;
        }
    }
}

int net_tcp_listen(uint16_t port, tcp_recv_func_t cb) {
    int i;
    for (i = 0; i < TCP_LISTEN_SIZE; i++) {
        if (!tcp_listeners[i].used || tcp_listeners[i].port == port) {
            tcp_listeners[i].used = 1;
            tcp_listeners[i].port = port;
            tcp_listeners[i].cb = cb;
            return 0;
        }
    }
    return -1;
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const uint8_t *tcp, uint16_t tcp_len) {
    uint8_t pseudo[NET_ETH_MTU];
    uint16_t plen;
    if ((uint32_t)tcp_len + 12U > (uint32_t)sizeof(pseudo)) return 0;
    *(uint32_t *)(pseudo + 0) = htonl(src_ip);
    *(uint32_t *)(pseudo + 4) = htonl(dst_ip);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_TCP;
    *(uint16_t *)(pseudo + 10) = htons(tcp_len);
    memcpy(pseudo + 12, tcp, tcp_len);
    plen = (uint16_t)(tcp_len + 12);
    return checksum16(pseudo, plen);
}

static int tcp_send_segment_window(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                                   uint32_t seq, uint32_t ack, uint8_t flags,
                                   uint16_t window, const uint8_t *data, uint16_t data_len) {
    uint8_t payload[NET_ETH_MTU];
    struct tcp_header *tcp;
    uint16_t tcp_len;
    if (sizeof(struct tcp_header) + data_len > sizeof(payload)) return -1;
    tcp = (struct tcp_header *)payload;
    tcp_len = (uint16_t)(sizeof(struct tcp_header) + data_len);
    memset(tcp, 0, sizeof(struct tcp_header));
    tcp->src_port = htons(src_port);
    tcp->dst_port = htons(dst_port);
    tcp->seq = htonl(seq);
    tcp->ack = htonl(ack);
    tcp->data_offset = (uint8_t)(5 << 4);
    tcp->flags = flags;
    tcp->window = htons(window);
    if (data_len) memcpy(payload + sizeof(struct tcp_header), data, data_len);
    tcp->checksum = 0;
    tcp->checksum = htons(tcp_checksum(default_dev ? default_dev->ip : 0, dst_ip, payload, tcp_len));
    return net_send_ipv4(dst_ip, IP_PROTO_TCP, payload, tcp_len);
}

static int tcp_send_segment(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                            uint32_t seq, uint32_t ack, uint8_t flags,
                            const uint8_t *data, uint16_t data_len) {
    return tcp_send_segment_window(dst_ip, src_port, dst_port, seq, ack, flags,
                                   TCP_DEFAULT_WINDOW, data, data_len);
}

static struct tcp_connection *tcp_find_conn(int id) {
    int i;
    for (i = 0; i < TCP_CONN_SIZE; i++) {
        if (tcp_connections[i].used && tcp_connections[i].id == id) return &tcp_connections[i];
    }
    return 0;
}

static struct tcp_connection *tcp_match_conn(uint32_t local_ip, uint16_t local_port,
                                             uint32_t remote_ip, uint16_t remote_port) {
    int i;
    for (i = 0; i < TCP_CONN_SIZE; i++) {
        struct tcp_connection *c = &tcp_connections[i];
        if (c->used && c->local_port == local_port && c->remote_port == remote_port &&
            c->remote_ip == remote_ip && (c->local_ip == 0 || c->local_ip == local_ip)) {
            return c;
        }
    }
    return 0;
}

static struct tcp_connection *tcp_alloc_conn(void) {
    int i;
    for (i = 0; i < TCP_CONN_SIZE; i++) {
        if (!tcp_connections[i].used) {
            memset(&tcp_connections[i], 0, sizeof(tcp_connections[i]));
            tcp_connections[i].used = 1;
            tcp_connections[i].id = tcp_next_id++;
            if (tcp_next_id <= 0) tcp_next_id = 1;
            tcp_connections[i].cwnd = TCP_INITIAL_CWND;
            tcp_connections[i].ssthresh = TCP_INITIAL_SSTHRESH;
            tcp_connections[i].cwnd_acked = 0;
            tcp_connections[i].snd_wnd = TCP_DEFAULT_WINDOW;
            tcp_connections[i].rcv_wnd = TCP_DEFAULT_WINDOW;
            return &tcp_connections[i];
        }
    }
    return 0;
}

static uint32_t tcp_state_consume(uint8_t flags, uint16_t data_len) {
    uint32_t consume = data_len;
    if (flags & TCP_FLAG_SYN) consume++;
    if (flags & TCP_FLAG_FIN) consume++;
    return consume;
}

static int tcp_seq_acked(uint32_t ack, uint32_t seq, uint32_t consume) {
    uint32_t end = seq + consume;
    return ((int32_t)(ack - end)) >= 0;
}

static void tcp_clear_unacked(struct tcp_connection *c) {
    if (!c) return;
    c->unacked_used = 0;
    c->unacked_len = 0;
    c->unacked_retries = 0;
}

static void tcp_set_state(struct tcp_connection *c, uint8_t state) {
    if (!c || c->state == state) return;
    c->state = state;
    c->state_since_ms = tcp_clock_ms;
}

static void tcp_release_conn(struct tcp_connection *c) {
    if (!c) return;
    tcp_clear_unacked(c);
    c->state = TCP_STATE_CLOSED;
    c->used = 0;
}

static uint32_t tcp_max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static void tcp_congestion_on_ack(struct tcp_connection *c, uint32_t acked) {
    if (!c || !acked) return;
    if (c->cwnd < TCP_MSS) c->cwnd = TCP_INITIAL_CWND;
    if (c->ssthresh < TCP_MSS) c->ssthresh = TCP_INITIAL_SSTHRESH;
    if (c->cwnd < c->ssthresh) {
        c->cwnd += TCP_MSS;
    } else {
        c->cwnd_acked += acked;
        if (c->cwnd_acked >= c->cwnd) {
            c->cwnd += TCP_MSS;
            c->cwnd_acked = 0;
        }
    }
}

static void tcp_congestion_on_timeout(struct tcp_connection *c) {
    if (!c) return;
    if (c->cwnd < TCP_MSS) c->cwnd = TCP_INITIAL_CWND;
    c->ssthresh = tcp_max_u32(c->cwnd / 2u, TCP_MSS * 2u);
    c->cwnd = TCP_INITIAL_CWND;
    c->cwnd_acked = 0;
}

static uint16_t tcp_receive_window(const struct tcp_connection *c) {
    uint32_t room;
    if (!c || c->rx_len >= TCP_RECV_BUFFER_SIZE) return 0;
    room = (uint32_t)TCP_RECV_BUFFER_SIZE - c->rx_len;
    return room > 0xffffU ? 0xffffU : (uint16_t)room;
}

static void tcp_refresh_receive_window(struct tcp_connection *c) {
    if (!c) return;
    c->rcv_wnd = tcp_receive_window(c);
}

static uint32_t tcp_effective_send_window(const struct tcp_connection *c) {
    uint32_t cwnd;
    uint32_t swnd;
    if (!c) return 0;
    cwnd = c->cwnd ? c->cwnd : TCP_INITIAL_CWND;
    swnd = c->snd_wnd;
    return cwnd < swnd ? cwnd : swnd;
}

static uint32_t tcp_bytes_in_flight(const struct tcp_connection *c) {
    if (!c || !c->unacked_used) return 0;
    return tcp_state_consume(c->unacked_flags, c->unacked_len);
}

static int tcp_window_can_send(struct tcp_connection *c, uint32_t consume) {
    uint32_t in_flight;
    uint32_t effective;
    if (!c || !consume) return 1;
    if (c->cwnd < TCP_MSS) c->cwnd = TCP_INITIAL_CWND;
    in_flight = tcp_bytes_in_flight(c);
    effective = tcp_effective_send_window(c);
    return in_flight + consume <= effective;
}

static void tcp_record_unacked(struct tcp_connection *c, uint32_t seq, uint32_t ack,
                               uint8_t flags, const uint8_t *data, uint16_t len) {
    if (!c) return;
    if (tcp_state_consume(flags, len) == 0) return;
    if (len > TCP_RETX_BUFFER_SIZE) return;
    c->unacked_used = 1;
    c->unacked_seq = seq;
    c->unacked_ack = ack;
    c->unacked_flags = flags;
    c->unacked_len = len;
    c->unacked_sent_ms = tcp_clock_ms;
    c->unacked_retries = 0;
    if (len && data) memcpy(c->unacked_data, data, len);
}

static int tcp_ack_unacked(struct tcp_connection *c, uint32_t ack) {
    uint32_t consume;
    if (!c || !c->unacked_used) return 0;
    consume = tcp_state_consume(c->unacked_flags, c->unacked_len);
    if (tcp_seq_acked(ack, c->unacked_seq, consume)) {
        tcp_congestion_on_ack(c, consume);
        tcp_clear_unacked(c);
        return 1;
    }
    return 0;
}

static void tcp_buffer_data(struct tcp_connection *c, const uint8_t *data, uint16_t len) {
    uint16_t room;
    if (!c || !data || !len) return;
    room = (uint16_t)(TCP_RECV_BUFFER_SIZE - c->rx_len);
    if (len > room) len = room;
    if (!len) return;
    memcpy(c->rx + c->rx_len, data, len);
    c->rx_len = (uint16_t)(c->rx_len + len);
    tcp_refresh_receive_window(c);
}

static int tcp_send_for_conn(struct tcp_connection *c, uint8_t flags,
                             const uint8_t *data, uint16_t len) {
    int ret;
    uint32_t seq;
    uint32_t consume;
    if (!c) return -1;
    consume = tcp_state_consume(flags, len);
    if (consume && c->unacked_used) return -1;
    if (!tcp_window_can_send(c, consume)) return -1;
    if (len > TCP_RETX_BUFFER_SIZE) return -1;
    seq = c->snd_nxt;
    tcp_refresh_receive_window(c);
    ret = tcp_send_segment_window(c->remote_ip, c->local_port, c->remote_port,
                                  seq, c->rcv_nxt, flags, c->rcv_wnd, data, len);
    if (ret == 0) {
        c->snd_nxt += consume;
        tcp_record_unacked(c, seq, c->rcv_nxt, flags, data, len);
    }
    return ret;
}

int net_tcp_open(uint32_t local_ip, uint16_t local_port,
                 uint32_t remote_ip, uint16_t remote_port, int active) {
    struct tcp_connection *c = tcp_alloc_conn();
    if (!c || !local_port) return -1;
    c->local_ip = local_ip ? local_ip : (default_dev ? default_dev->ip : 0);
    c->local_port = local_port;
    c->remote_ip = remote_ip;
    c->remote_port = remote_port;
    c->snd_nxt = 1;
    c->rcv_nxt = 0;
    tcp_set_state(c, active ? TCP_STATE_SYN_SENT : TCP_STATE_LISTEN);
    if (active) {
        if (!remote_ip || !remote_port || tcp_send_for_conn(c, TCP_FLAG_SYN, 0, 0) != 0) {
            c->used = 0;
            return -1;
        }
    }
    return c->id;
}

int net_tcp_send(int conn_id, const uint8_t *data, uint16_t len) {
    struct tcp_connection *c = tcp_find_conn(conn_id);
    if (!c || c->state != TCP_STATE_ESTABLISHED || (!data && len)) return -1;
    return tcp_send_for_conn(c, TCP_FLAG_ACK | (len ? TCP_FLAG_PSH : 0), data, len);
}

int net_tcp_recv(int conn_id, uint8_t *data, uint16_t len) {
    struct tcp_connection *c = tcp_find_conn(conn_id);
    uint16_t n;
    if (!c || !data || !len) return -1;
    n = c->rx_len;
    if (n > len) n = len;
    if (!n) return 0;
    memcpy(data, c->rx, n);
    if (n < c->rx_len) memmove(c->rx, c->rx + n, (uint16_t)(c->rx_len - n));
    c->rx_len = (uint16_t)(c->rx_len - n);
    tcp_refresh_receive_window(c);
    if (c->state == TCP_STATE_ESTABLISHED && !c->unacked_used) {
        tcp_send_segment_window(c->remote_ip, c->local_port, c->remote_port,
                                c->snd_nxt, c->rcv_nxt, TCP_FLAG_ACK,
                                c->rcv_wnd, 0, 0);
    }
    return n;
}

int net_tcp_close(int conn_id) {
    struct tcp_connection *c = tcp_find_conn(conn_id);
    if (!c) return -1;
    if (c->state == TCP_STATE_ESTABLISHED) {
        if (tcp_send_for_conn(c, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0) != 0) return -1;
        tcp_set_state(c, TCP_STATE_FIN_WAIT_1);
        return 0;
    }
    if (c->state == TCP_STATE_CLOSE_WAIT) {
        if (tcp_send_for_conn(c, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0) != 0) return -1;
        tcp_set_state(c, TCP_STATE_LAST_ACK);
        return 0;
    }
    tcp_release_conn(c);
    return 0;
}

int net_tcp_state(int conn_id) {
    struct tcp_connection *c = tcp_find_conn(conn_id);
    return c ? c->state : -1;
}

int net_tcp_get_endpoint(int conn_id, uint32_t *local_ip, uint16_t *local_port,
                         uint32_t *remote_ip, uint16_t *remote_port) {
    struct tcp_connection *c = tcp_find_conn(conn_id);
    if (!c) return -1;
    if (local_ip) *local_ip = c->local_ip;
    if (local_port) *local_port = c->local_port;
    if (remote_ip) *remote_ip = c->remote_ip;
    if (remote_port) *remote_port = c->remote_port;
    return 0;
}

int net_tcp_send_syn(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port) {
    return tcp_send_segment(dst_ip, src_port, dst_port, 1, 0, TCP_FLAG_SYN, 0, 0);
}

void net_tick(uint32_t elapsed_ms) {
    int i;
    tcp_clock_ms += elapsed_ms;
    for (i = 0; i < TCP_CONN_SIZE; i++) {
        struct tcp_connection *c = &tcp_connections[i];
        if (!c->used) continue;
        if (c->state == TCP_STATE_TIME_WAIT &&
            (uint32_t)(tcp_clock_ms - c->state_since_ms) >= TCP_TIME_WAIT_MS) {
            tcp_release_conn(c);
            continue;
        }
        if (!c->unacked_used) continue;
        if ((uint32_t)(tcp_clock_ms - c->unacked_sent_ms) < TCP_RETX_TIMEOUT_MS) continue;
        if (c->unacked_retries >= TCP_RETX_MAX_RETRIES) {
            tcp_release_conn(c);
            continue;
        }
        tcp_congestion_on_timeout(c);
        tcp_refresh_receive_window(c);
        if (tcp_send_segment_window(c->remote_ip, c->local_port, c->remote_port,
                                    c->unacked_seq, c->unacked_ack, c->unacked_flags,
                                    c->rcv_wnd,
                                    c->unacked_len ? c->unacked_data : 0, c->unacked_len) == 0) {
            c->unacked_sent_ms = tcp_clock_ms;
            c->unacked_retries++;
        }
    }
}

static void tcp_send_rst(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint32_t ack) {
    tcp_send_segment(dst_ip, src_port, dst_port, 0, ack, TCP_FLAG_RST | TCP_FLAG_ACK, 0, 0);
}

static void handle_tcp(uint32_t src_ip, const uint8_t *payload, uint16_t len) {
    const struct tcp_header *tcp;
    struct tcp_connection *c;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t header_len;
    uint16_t data_len;
    uint32_t consume;
    int i;
    if (len < sizeof(struct tcp_header)) return;
    tcp = (const struct tcp_header *)payload;
    header_len = (uint8_t)((tcp->data_offset >> 4) * 4);
    if (header_len < sizeof(struct tcp_header) || header_len > len) return;
    src_port = ntohs(tcp->src_port);
    dst_port = ntohs(tcp->dst_port);
    if (!firewall_allows(IP_PROTO_TCP, dst_port)) return;
    seq = ntohl(tcp->seq);
    ack = ntohl(tcp->ack);
    data_len = (uint16_t)(len - header_len);
    consume = tcp_state_consume(tcp->flags, data_len);

    c = tcp_match_conn(default_dev ? default_dev->ip : 0, dst_port, src_ip, src_port);
    if (c && (tcp->flags & TCP_FLAG_RST)) {
        tcp_release_conn(c);
        return;
    }

    if (!c && (tcp->flags & TCP_FLAG_SYN)) {
        for (i = 0; i < TCP_CONN_SIZE; i++) {
            if (tcp_connections[i].used && tcp_connections[i].state == TCP_STATE_LISTEN &&
                tcp_connections[i].local_port == dst_port) {
                c = &tcp_connections[i];
                c->local_ip = default_dev ? default_dev->ip : c->local_ip;
                c->remote_ip = src_ip;
                c->remote_port = src_port;
                c->snd_nxt = 100;
                c->rcv_nxt = seq + 1;
                if (tcp_send_for_conn(c, TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0) == 0) {
                    tcp_set_state(c, TCP_STATE_SYN_RCVD);
                }
                return;
            }
        }
        for (i = 0; i < TCP_LISTEN_SIZE; i++) {
            if (tcp_listeners[i].used && tcp_listeners[i].port == dst_port) {
                tcp_send_segment(src_ip, dst_port, src_port, 100, seq + 1,
                                 TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0);
                return;
            }
        }
    }

    if (!c) {
        for (i = 0; i < TCP_LISTEN_SIZE; i++) {
            if (tcp_listeners[i].used && tcp_listeners[i].port == dst_port) {
                if (data_len && tcp_listeners[i].cb) {
                    tcp_listeners[i].cb(src_ip, src_port, dst_port,
                                        payload + header_len, data_len);
                    tcp_send_segment(src_ip, dst_port, src_port, 101, seq + data_len,
                                     TCP_FLAG_ACK, 0, 0);
                }
                return;
            }
        }
        if (!(tcp->flags & TCP_FLAG_RST)) tcp_send_rst(src_ip, dst_port, src_port, seq + consume);
        return;
    }

    c->snd_wnd = ntohs(tcp->window);
    if (tcp->flags & TCP_FLAG_ACK) tcp_ack_unacked(c, ack);
    if (consume) c->rcv_nxt = seq + consume;

    switch (c->state) {
    case TCP_STATE_SYN_SENT:
        if ((tcp->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) && ack == c->snd_nxt) {
            c->rcv_nxt = seq + 1;
            tcp_send_for_conn(c, TCP_FLAG_ACK, 0, 0);
            tcp_set_state(c, TCP_STATE_ESTABLISHED);
        }
        break;
    case TCP_STATE_SYN_RCVD:
        if ((tcp->flags & TCP_FLAG_ACK) && ack == c->snd_nxt) tcp_set_state(c, TCP_STATE_ESTABLISHED);
        break;
    case TCP_STATE_ESTABLISHED:
        if (data_len) {
            tcp_buffer_data(c, payload + header_len, data_len);
            tcp_send_for_conn(c, TCP_FLAG_ACK, 0, 0);
        }
        if (tcp->flags & TCP_FLAG_FIN) {
            tcp_send_for_conn(c, TCP_FLAG_ACK, 0, 0);
            tcp_set_state(c, TCP_STATE_CLOSE_WAIT);
        }
        break;
    case TCP_STATE_CLOSE_WAIT:
        if (data_len || (tcp->flags & TCP_FLAG_FIN)) {
            tcp_send_for_conn(c, TCP_FLAG_ACK, 0, 0);
        }
        break;
    case TCP_STATE_FIN_WAIT_1:
        if ((tcp->flags & TCP_FLAG_ACK) && ack == c->snd_nxt) tcp_set_state(c, TCP_STATE_FIN_WAIT_2);
        if (tcp->flags & TCP_FLAG_FIN) {
            uint8_t next_state = (c->state == TCP_STATE_FIN_WAIT_2) ? TCP_STATE_TIME_WAIT : TCP_STATE_CLOSING;
            tcp_send_for_conn(c, TCP_FLAG_ACK, 0, 0);
            tcp_set_state(c, next_state);
        }
        break;
    case TCP_STATE_FIN_WAIT_2:
        if (tcp->flags & TCP_FLAG_FIN) {
            tcp_send_for_conn(c, TCP_FLAG_ACK, 0, 0);
            tcp_set_state(c, TCP_STATE_TIME_WAIT);
        }
        break;
    case TCP_STATE_CLOSING:
        if ((tcp->flags & TCP_FLAG_ACK) && ack == c->snd_nxt) tcp_set_state(c, TCP_STATE_TIME_WAIT);
        break;
    case TCP_STATE_LAST_ACK:
        if ((tcp->flags & TCP_FLAG_ACK) && ack == c->snd_nxt) {
            tcp_release_conn(c);
        }
        break;
    case TCP_STATE_TIME_WAIT:
        if (tcp->flags & TCP_FLAG_FIN) tcp_send_for_conn(c, TCP_FLAG_ACK, 0, 0);
        break;
    default:
        break;
    }
}

static void handle_ipv4(net_device_t *dev, const uint8_t *data, uint16_t len) {
    const struct ipv4_header *ip;
    uint8_t ihl;
    uint16_t total_len;
    uint32_t src;
    uint32_t dst;
    const uint8_t *payload;
    uint16_t payload_len;
    if (len < sizeof(struct ipv4_header)) return;
    ip = (const struct ipv4_header *)data;
    if ((ip->ver_ihl >> 4) != 4) return;
    ihl = (uint8_t)((ip->ver_ihl & 0x0f) * 4);
    if (ihl < sizeof(struct ipv4_header) || ihl > len) return;
    total_len = ntohs(ip->total_len);
    if (total_len < ihl || total_len > len) return;
    if (checksum16(ip, ihl) != 0) return;
    src = ntohl(ip->src);
    dst = ntohl(ip->dst);
    if (dev && dst != dev->ip && dst != 0xffffffffU) return;
    payload = data + ihl;
    payload_len = (uint16_t)(total_len - ihl);

    if (ip->protocol == IP_PROTO_ICMP && firewall_allows(IP_PROTO_ICMP, 0)) handle_icmp(src, payload, payload_len);
    else if (ip->protocol == IP_PROTO_UDP) handle_udp(src, payload, payload_len);
    else if (ip->protocol == IP_PROTO_TCP) handle_tcp(src, payload, payload_len);
}

void net_input(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    const struct eth_header *eth;
    uint16_t type;
    uint8_t broadcast[NET_ETH_ADDR_LEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
    if (!dev || !frame || len < sizeof(struct eth_header)) return;
    eth = (const struct eth_header *)frame;
    if (!same_mac(eth->dst, dev->mac) && !same_mac(eth->dst, broadcast)) return;
    type = ntohs(eth->type);
    dev->rx_packets++;
    if (type == ETH_TYPE_ARP) {
        handle_arp(dev, frame + sizeof(struct eth_header), (uint16_t)(len - sizeof(struct eth_header)));
    } else if (type == ETH_TYPE_IPV4) {
        handle_ipv4(dev, frame + sizeof(struct eth_header), (uint16_t)(len - sizeof(struct eth_header)));
    }
}

int net_register_device(net_device_t *dev) {
    if (!dev) return -1;
    if (!default_dev) default_dev = dev;
    devmgr_register(dev->name, "net", DEVMGR_TYPE_NET, 0, 0, 0, dev);
    return 0;
}

net_device_t *net_get_default_device(void) {
    return default_dev;
}

void net_set_default_ipv4(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    if (!default_dev) return;
    default_dev->ip = ip;
    default_dev->netmask = netmask;
    default_dev->gateway = gateway;
}

int net_config_ipv4(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    if (!default_dev) return -1;
    if (netmask == 0 || netmask == 0xffffffffU) return -1;
    if (ip == 0 || ip == 0xffffffffU) return -1;
    default_dev->ip = ip;
    default_dev->netmask = netmask;
    default_dev->gateway = gateway;
    return 0;
}

void net_format_ipv4(uint32_t ip, char *out) {
    uint8_t p[4];
    int pos = 0;
    int i;
    p[0] = (uint8_t)((ip >> 24) & 0xff);
    p[1] = (uint8_t)((ip >> 16) & 0xff);
    p[2] = (uint8_t)((ip >> 8) & 0xff);
    p[3] = (uint8_t)(ip & 0xff);
    for (i = 0; i < 4; i++) {
        char tmp[4];
        int n = 0;
        int v = p[i];
        if (v >= 100) tmp[n++] = (char)('0' + v / 100);
        if (v >= 10) tmp[n++] = (char)('0' + (v / 10) % 10);
        tmp[n++] = (char)('0' + v % 10);
        memcpy(out + pos, tmp, n);
        pos += n;
        if (i != 3) out[pos++] = '.';
    }
    out[pos] = '\0';
}

int net_parse_ipv4(const char *text, uint32_t *out) {
    uint32_t parts[4];
    uint32_t part = 0;
    uint32_t count = 0;
    uint32_t i = 0;
    if (!text || !out) return -1;
    while (1) {
        char c = text[i++];
        if (c >= '0' && c <= '9') {
            part = part * 10U + (uint32_t)(c - '0');
            if (part > 255U) return -1;
        } else if (c == '.' || c == '\0') {
            if (count >= 4U) return -1;
            parts[count++] = part;
            part = 0;
            if (c == '\0') break;
        } else {
            return -1;
        }
    }
    if (count != 4U) return -1;
    *out = NET_IP4(parts[0], parts[1], parts[2], parts[3]);
    return 0;
}

void net_print_info(void) {
    char ip[16];
    if (!default_dev) {
        vga_write("network: no device\n");
        return;
    }
    net_format_ipv4(default_dev->ip, ip);
    vga_write("network device: ");
    vga_write(default_dev->name);
    vga_write("\n");
    vga_write("ipv4: ");
    vga_write(ip);
    vga_write("\nrx packets: ");
    print_dec(default_dev->rx_packets);
    vga_write("\ntx packets: ");
    print_dec(default_dev->tx_packets);
    vga_write("\n");
}

static int loopback_transmit(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    net_input(dev, frame, len);
    return 0;
}

int net_ping_ipv4(uint32_t dst_ip) {
    uint8_t payload[sizeof(struct icmp_header) + 4];
    struct icmp_header *icmp = (struct icmp_header *)payload;
    if (!default_dev) return -1;
    memset(payload, 0, sizeof(payload));
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->ident = htons(1);
    icmp->seq = htons((uint16_t)(icmp_echo_requests + icmp_echo_replies + 1U));
    payload[sizeof(struct icmp_header) + 0] = 'p';
    payload[sizeof(struct icmp_header) + 1] = 'i';
    payload[sizeof(struct icmp_header) + 2] = 'n';
    payload[sizeof(struct icmp_header) + 3] = 'g';
    icmp->checksum = htons(checksum16(payload, sizeof(payload)));
    return net_send_ipv4(dst_ip, IP_PROTO_ICMP, payload, sizeof(payload));
}

int net_ping_self(void) {
    if (!default_dev) return -1;
    return net_ping_ipv4(default_dev->ip);
}

int net_get_diag_stats(net_diag_stats_t *stats) {
    int i;
    if (!stats || !default_dev) return -1;
    memset(stats, 0, sizeof(*stats));
    strncpy(stats->name, default_dev->name, sizeof(stats->name) - 1);
    stats->name[sizeof(stats->name) - 1] = '\0';
    copy_mac(stats->mac, default_dev->mac);
    stats->ip = default_dev->ip;
    stats->netmask = default_dev->netmask;
    stats->gateway = default_dev->gateway;
    stats->rx_packets = default_dev->rx_packets;
    stats->tx_packets = default_dev->tx_packets;
    stats->rx_dropped = default_dev->rx_dropped;
    stats->tx_dropped = default_dev->tx_dropped;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].used) stats->arp_entries++;
    }
    for (i = 0; i < UDP_BIND_SIZE; i++) {
        if (udp_bindings[i].used) stats->udp_bindings++;
    }
    for (i = 0; i < TCP_LISTEN_SIZE; i++) {
        if (tcp_listeners[i].used) stats->tcp_listeners++;
    }
    for (i = 0; i < TCP_CONN_SIZE; i++) {
        if (tcp_connections[i].state != NET_TCP_STATE_CLOSED) stats->tcp_connections++;
    }
    stats->icmp_echo_requests = icmp_echo_requests;
    stats->icmp_echo_replies = icmp_echo_replies;
    return 0;
}

void net_init(void) {
    static net_device_t loopdev;
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(udp_bindings, 0, sizeof(udp_bindings));
    memset(tcp_listeners, 0, sizeof(tcp_listeners));
    memset(tcp_connections, 0, sizeof(tcp_connections));
    tcp_next_id = 1;
    icmp_echo_requests = 0;
    icmp_echo_replies = 0;
    memset(&loopdev, 0, sizeof(loopdev));
    strcpy(loopdev.name, "loopnet0");
    loopdev.mac[0] = 0x02;
    loopdev.mac[1] = 0x00;
    loopdev.mac[2] = 0x00;
    loopdev.mac[3] = 0x00;
    loopdev.mac[4] = 0x00;
    loopdev.mac[5] = 0x01;
    loopdev.ip = NET_IP4(10, 0, 2, 15);
    loopdev.netmask = NET_IP4(255, 255, 255, 0);
    loopdev.gateway = NET_IP4(10, 0, 2, 2);
    loopdev.transmit = loopback_transmit;
    default_dev = 0;
    net_register_device(&loopdev);
    arp_insert(loopdev.ip, loopdev.mac);
    vga_write("network: tcp/ip stack initialized\n");
}
