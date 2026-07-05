#ifndef NETSTACK64_H
#define NETSTACK64_H

#include "types.h"

/* ===================================================================
 * M1.3 迷你网络栈：以太网 + ARP + IPv4 + ICMP
 * 基于 virtio_net 收发原始以太网帧，向上提供 ARP/IPv4/ICMP 能力。
 * 字节序：网络字节序为大端（BE），主机 x86_64 为小端（LE）。
 * =================================================================== */

/* ---- EtherType（网络字节序在帧里，这里给主机序常量） ---- */
#define ETH_TYPE_IPV4 0x0800u
#define ETH_TYPE_ARP  0x0806u

#define ETH_ADDR_LEN  6u
#define ETH_HDR_LEN   14u
#define ETH_FRAME_MAX 1518u

/* ---- ARP ---- */
#define ARP_HTYPE_ETH   1u
#define ARP_PTYPE_IPV4  0x0800u
#define ARP_OP_REQUEST  1u
#define ARP_OP_REPLY    2u

/* ---- IP 协议号 ---- */
#define IP_PROTO_ICMP 1u
#define IP_PROTO_TCP  6u
#define IP_PROTO_UDP  17u

/* ---- ICMP 类型 ---- */
#define ICMP_TYPE_ECHO_REPLY   0u
#define ICMP_TYPE_ECHO_REQUEST 8u

/* 以太网帧头 */
typedef struct eth_hdr {
    uint8_t  dst[ETH_ADDR_LEN];
    uint8_t  src[ETH_ADDR_LEN];
    uint16_t type;   /* 网络字节序 */
} __attribute__((packed)) eth_hdr_t;

/* ARP 报文（IPv4 over Ethernet） */
typedef struct arp_pkt {
    uint16_t htype;      /* 硬件类型 网络序 */
    uint16_t ptype;      /* 协议类型 网络序 */
    uint8_t  hlen;       /* 硬件地址长度=6 */
    uint8_t  plen;       /* 协议地址长度=4 */
    uint16_t oper;       /* 操作码 网络序 */
    uint8_t  sha[ETH_ADDR_LEN];  /* 发送方 MAC */
    uint32_t spa;                /* 发送方 IP 网络序 */
    uint8_t  tha[ETH_ADDR_LEN];  /* 目标 MAC */
    uint32_t tpa;                /* 目标 IP 网络序 */
} __attribute__((packed)) arp_pkt_t;

/* IPv4 头（无选项，20 字节） */
typedef struct ipv4_hdr {
    uint8_t  ver_ihl;    /* 高4位版本 低4位IHL */
    uint8_t  tos;
    uint16_t total_len;  /* 网络序 */
    uint16_t id;         /* 网络序 */
    uint16_t flags_frag; /* 网络序 */
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;   /* 网络序 */
    uint32_t src;        /* 网络序 */
    uint32_t dst;        /* 网络序 */
} __attribute__((packed)) ipv4_hdr_t;

/* ICMP 头（echo） */
typedef struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;   /* 网络序 */
    uint16_t id;         /* 网络序 */
    uint16_t seq;        /* 网络序 */
} __attribute__((packed)) icmp_hdr_t;

/* ---- 字节序辅助 ---- */
static inline uint16_t ns_htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint16_t ns_ntohs(uint16_t v) { return ns_htons(v); }
static inline uint32_t ns_htonl(uint32_t v) {
    return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
           ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
}
static inline uint32_t ns_ntohl(uint32_t v) { return ns_htonl(v); }

/* 组装点分十进制 IPv4（主机序 a.b.c.d -> 网络序 uint32） */
#define NS_IP4(a, b, c, d) \
    ns_htonl((((uint32_t)(a) & 0xffu) << 24) | (((uint32_t)(b) & 0xffu) << 16) | \
             (((uint32_t)(c) & 0xffu) << 8) | ((uint32_t)(d) & 0xffu))

/* ===================================================================
 * 对外 API
 * =================================================================== */

/* 初始化网络栈：绑定 virtio_net 的 MAC，设置静态 IP/网关。
 * QEMU user 网络默认：本机 10.0.2.15，网关 10.0.2.2，掩码 255.255.255.0。 */
void netstack_init(void);

/* 是否已就绪（网卡存在且已配置） */
int netstack_ready(void);

/* 主循环轮询：从网卡收帧并分发处理（在内核 poll loop 里周期调用） */
void netstack_poll(void);

/* 处理一个收到的以太网帧（内部分发 ARP / IPv4） */
void netstack_input(const uint8_t *frame, uint32_t len);

/* 发送 IPv4 报文：payload 为上层协议数据（ICMP/UDP/TCP）。
 * 内部自动补 IP 头 + ARP 解析目的 MAC + 以太网封装。成功返回 0。 */
int netstack_send_ipv4(uint32_t dst_ip_net, uint8_t proto,
                       const uint8_t *payload, uint16_t payload_len);

/* 主动发送 ARP 请求解析某 IP 的 MAC */
int netstack_arp_request(uint32_t target_ip_net);

/* 主动 ping：向 dst 发一个 ICMP echo request。成功返回 0。 */
int netstack_ping(uint32_t dst_ip_net);

/* 查询本机配置 */
uint32_t netstack_local_ip(void);
uint32_t netstack_gateway(void);

/* 串口打印网络栈状态与统计 */
void netstack_dump(void);

/* Internet 校验和（用于 IP/ICMP），data 为字节流，len 字节 */
uint16_t netstack_checksum(const void *data, uint32_t len);

#endif /* NETSTACK64_H */
