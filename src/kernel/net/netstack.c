/* ============================================================
 * netstack.c — OpenOS 真实网络协议栈 (M1.3)
 * 链路层(Ethernet) + ARP + IPv4 + ICMP + UDP + TCP(精简)
 * 承载于 virtio-net 网卡驱动之上
 * ------------------------------------------------------------
 * 实现 net.h 声明的全部 API。字节序：网络序=大端。
 * 本文件为分批构建，第1批：头部/全局/工具/以太网/ARP
 * ============================================================ */
#include "net.h"
#include "net_config.h"
#include "../include/virtio_net.h"

/* ---- 外部依赖 ---- */
extern void early_serial64_write(const char *s);
extern void *arch_x86_64_kmalloc(uint64_t size);
extern void arch_x86_64_kfree(void *ptr);

/* ============================================================
 * 基础工具：内存 / 字节序 / 打印
 * ============================================================ */
static void *ns_memset(void *d, int c, uint64_t n) {
    uint8_t *p = (uint8_t *)d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}
static void *ns_memcpy(void *d, const void *s, uint64_t n) {
    uint8_t *dp = (uint8_t *)d; const uint8_t *sp = (const uint8_t *)s;
    while (n--) *dp++ = *sp++;
    return d;
}
static int ns_memcmp(const void *a, const void *b, uint64_t n) __attribute__((unused));
static int ns_memcmp(const void *a, const void *b, uint64_t n) {
    const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
    while (n--) { if (*pa != *pb) return (int)*pa - (int)*pb; pa++; pb++; }
    return 0;
}

/* 16/32 位主机<->网络序转换（x86 小端） */
static inline uint16_t hton16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint16_t ntoh16(uint16_t v) { return hton16(v); }
static inline uint32_t hton32(uint32_t v) {
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) |
           ((v & 0xFF0000u) >> 8) | ((v >> 24) & 0xFFu);
}
static inline uint32_t ntoh32(uint32_t v) { return hton32(v); }

/* 从缓冲区读取大端 16/32 位 */
static inline uint16_t rd_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline void wr_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void wr_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* 串口十进制/十六进制打印辅助 */
static void ns_puts(const char *s) { early_serial64_write(s); }
static void ns_put_dec(uint32_t v) {
    char buf[12]; int i = 0;
    if (v == 0) { early_serial64_write("0"); return; }
    while (v && i < 11) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    char out[12]; int j = 0;
    while (i) out[j++] = buf[--i];
    out[j] = '\0';
    early_serial64_write(out);
}
static void ns_put_hex2(uint8_t v) {
    const char *hx = "0123456789abcdef";
    char b[3]; b[0] = hx[(v >> 4) & 0xF]; b[1] = hx[v & 0xF]; b[2] = '\0';
    early_serial64_write(b);
}
static void ns_put_ip(uint32_t ip) {
    /* ip 以主机序存储（高字节=第一段） */
    ns_put_dec((ip >> 24) & 0xFF); ns_puts(".");
    ns_put_dec((ip >> 16) & 0xFF); ns_puts(".");
    ns_put_dec((ip >> 8) & 0xFF); ns_puts(".");
    ns_put_dec(ip & 0xFF);
}
static void ns_put_mac(const uint8_t *m) {
    for (int i = 0; i < 6; i++) { if (i) ns_puts(":"); ns_put_hex2(m[i]); }
}

/* ============================================================
 * 协议常量
 * ============================================================ */
#define ETH_HDR_LEN        14
#define ETH_TYPE_IPV4      0x0800
#define ETH_TYPE_ARP       0x0806
#define ETH_MTU            1500
#define ETH_FRAME_MAX      1514

#define ARP_HTYPE_ETH      1
#define ARP_PTYPE_IPV4     0x0800
#define ARP_OP_REQUEST     1
#define ARP_OP_REPLY       2

#define IP_PROTO_ICMP      1
#define IP_PROTO_TCP       6
#define IP_PROTO_UDP       17

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

/* ============================================================
 * 全局网络状态
 * ============================================================ */
#define NET_MAX_DEVICES   4
#define ARP_CACHE_SIZE    16
#define UDP_BIND_MAX      8
#define TCP_CONN_MAX      8

static net_device_t *g_devices[NET_MAX_DEVICES];
static uint32_t g_device_count = 0;
static net_device_t *g_default_dev = 0;
static uint32_t g_net_ticks = 0;

/* 诊断统计 */
static net_diag_stats_t g_stats;

/* ARP 缓存条目 */
typedef struct {
    uint32_t ip;                    /* 主机序 */
    uint8_t  mac[6];
    uint8_t  valid;
    uint32_t last_tick;
} arp_entry_t;
static arp_entry_t g_arp_cache[ARP_CACHE_SIZE];

static const uint8_t BROADCAST_MAC[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t ZERO_MAC[6]      = { 0,0,0,0,0,0 };

/* ============================================================
 * 校验和（IP/ICMP/UDP/TCP 通用 16 位反码和）
 * ============================================================ */
static uint16_t net_checksum(const uint8_t *data, uint32_t len, uint32_t initial) {
    uint32_t sum = initial;
    while (len > 1) { sum += (uint32_t)((data[0] << 8) | data[1]); data += 2; len -= 2; }
    if (len) sum += (uint32_t)(data[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

/* ============================================================
 * 以太网层：帧发送
 * ============================================================ */
/* 组装并发送一个以太网帧。payload 为上层数据(IP/ARP)，返回0成功 */
static int eth_send(net_device_t *dev, const uint8_t *dst_mac,
                    uint16_t ethertype, const uint8_t *payload, uint16_t payload_len) {
    if (!dev || !dev->transmit) return -1;
    if (payload_len > ETH_MTU) return -1;
    uint8_t frame[ETH_FRAME_MAX];
    ns_memcpy(frame, dst_mac, 6);
    ns_memcpy(frame + 6, dev->mac, 6);
    wr_be16(frame + 12, ethertype);
    ns_memcpy(frame + ETH_HDR_LEN, payload, payload_len);
    uint16_t total = (uint16_t)(ETH_HDR_LEN + payload_len);
    /* 以太网最小帧 60 字节(不含FCS)，不足补零 */
    if (total < 60) { ns_memset(frame + total, 0, 60u - total); total = 60; }
    int r = dev->transmit(dev, frame, total);
    if (r == 0) { dev->tx_packets++; g_stats.tx_packets++; }
    else { dev->tx_dropped++; g_stats.tx_dropped++; }
    return r;
}

/* ============================================================
 * ARP 缓存
 * ============================================================ */
static void arp_cache_put(uint32_t ip, const uint8_t *mac) {
    int free_slot = -1, oldest = 0;
    uint32_t oldest_tick = 0xFFFFFFFFu;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            ns_memcpy(g_arp_cache[i].mac, mac, 6);
            g_arp_cache[i].last_tick = g_net_ticks;
            return;
        }
        if (!g_arp_cache[i].valid && free_slot < 0) free_slot = i;
        if (g_arp_cache[i].last_tick < oldest_tick) { oldest_tick = g_arp_cache[i].last_tick; oldest = i; }
    }
    int slot = (free_slot >= 0) ? free_slot : oldest;
    g_arp_cache[slot].ip = ip;
    ns_memcpy(g_arp_cache[slot].mac, mac, 6);
    g_arp_cache[slot].valid = 1;
    g_arp_cache[slot].last_tick = g_net_ticks;
}
static int arp_cache_lookup(uint32_t ip, uint8_t *mac_out) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            ns_memcpy(mac_out, g_arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}
static uint32_t arp_cache_count(void) {
    uint32_t n = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) if (g_arp_cache[i].valid) n++;
    return n;
}

/* ============================================================
 * ARP：发送请求 / 应答，处理收到的 ARP 报文
 * ARP 报文格式(以太上28字节): htype(2) ptype(2) hlen(1) plen(1)
 *   op(2) sha(6) spa(4) tha(6) tpa(4)
 * ============================================================ */
static void arp_build(uint8_t *buf, uint16_t op, const uint8_t *sha, uint32_t spa,
                      const uint8_t *tha, uint32_t tpa) {
    wr_be16(buf + 0, ARP_HTYPE_ETH);
    wr_be16(buf + 2, ARP_PTYPE_IPV4);
    buf[4] = 6; buf[5] = 4;
    wr_be16(buf + 6, op);
    ns_memcpy(buf + 8, sha, 6);
    wr_be32(buf + 14, spa);
    ns_memcpy(buf + 18, tha, 6);
    wr_be32(buf + 24, tpa);
}

/* 发送 ARP 请求：谁拥有 target_ip? 广播 */
static void arp_send_request(net_device_t *dev, uint32_t target_ip) {
    uint8_t arp[28];
    arp_build(arp, ARP_OP_REQUEST, dev->mac, dev->ip, ZERO_MAC, target_ip);
    eth_send(dev, BROADCAST_MAC, ETH_TYPE_ARP, arp, 28);
}

/* 处理收到的 ARP 报文（含请求应答） */
static void arp_input(net_device_t *dev, const uint8_t *arp, uint16_t len) {
    if (len < 28) return;
    uint16_t htype = rd_be16(arp + 0);
    uint16_t ptype = rd_be16(arp + 2);
    if (htype != ARP_HTYPE_ETH || ptype != ARP_PTYPE_IPV4) return;
    if (arp[4] != 6 || arp[5] != 4) return;
    uint16_t op = rd_be16(arp + 6);
    const uint8_t *sha = arp + 8;
    uint32_t spa = rd_be32(arp + 14);
    uint32_t tpa = rd_be32(arp + 24);
    /* 学习发送方地址映射 */
    if (spa != 0) arp_cache_put(spa, sha);
    if (op == ARP_OP_REQUEST && tpa == dev->ip && dev->ip != 0) {
        /* 有人问我的 IP，回应 */
        uint8_t reply[28];
        arp_build(reply, ARP_OP_REPLY, dev->mac, dev->ip, sha, spa);
        eth_send(dev, sha, ETH_TYPE_ARP, reply, 28);
        ns_puts("[net] ARP reply to "); ns_put_ip(spa); ns_puts("\n");
    }
}

/* 解析下一跳 MAC：命中缓存返回0；否则发ARP请求返回-1(调用方稍后重试) */
static int arp_resolve(net_device_t *dev, uint32_t next_hop_ip, uint8_t *mac_out) {
    if (arp_cache_lookup(next_hop_ip, mac_out) == 0) return 0;
    arp_send_request(dev, next_hop_ip);
    return -1;
}

/* ============================================================
 * IPv4 层
 * IP 头(20字节): ver/ihl(1) tos(1) total_len(2) id(2) frag(2)
 *   ttl(1) proto(1) checksum(2) src(4) dst(4)
 * ============================================================ */
static uint16_t g_ip_id = 1;

/* 前向声明上层处理 */
static void icmp_input(net_device_t *dev, uint32_t src_ip, const uint8_t *data, uint16_t len);
static void udp_input(net_device_t *dev, uint32_t src_ip, const uint8_t *data, uint16_t len);
static void tcp_input(net_device_t *dev, uint32_t src_ip, const uint8_t *data, uint16_t len);

/* 判断目标是否同子网，选择下一跳 */
static uint32_t ip_next_hop(net_device_t *dev, uint32_t dst_ip) {
    if ((dst_ip & dev->netmask) == (dev->ip & dev->netmask)) return dst_ip;
    return dev->gateway ? dev->gateway : dst_ip;
}

/* 发送一个 IPv4 包。payload 为传输层数据，返回0成功/-1(ARP未就绪)/-2(错误) */
static int ipv4_send(net_device_t *dev, uint32_t dst_ip, uint8_t proto,
                     const uint8_t *payload, uint16_t payload_len) {
    if (!dev) return -2;
    if (payload_len > ETH_MTU - 20) return -2;
    uint32_t next_hop = ip_next_hop(dev, dst_ip);
    uint8_t dmac[6];
    /* 受限广播(255.255.255.255) 或 子网定向广播 直接用广播MAC，跳过ARP */
    if (dst_ip == 0xFFFFFFFFu || next_hop == 0xFFFFFFFFu ||
        (dev->netmask && (dst_ip | dev->netmask) == 0xFFFFFFFFu &&
         (dst_ip & dev->netmask) == (dev->ip & dev->netmask))) {
        for (int _bi = 0; _bi < 6; _bi++) dmac[_bi] = 0xFF;
    } else if (arp_resolve(dev, next_hop, dmac) != 0) {
        return -1; /* ARP未就绪 */
    }

    uint8_t pkt[ETH_MTU];
    uint16_t total_len = (uint16_t)(20 + payload_len);
    pkt[0] = 0x45;              /* ver=4 ihl=5 */
    pkt[1] = 0;                 /* tos */
    wr_be16(pkt + 2, total_len);
    wr_be16(pkt + 4, g_ip_id++);
    wr_be16(pkt + 6, 0x4000);   /* DF */
    pkt[8] = 64;                /* ttl */
    pkt[9] = proto;
    wr_be16(pkt + 10, 0);       /* checksum 先置0 */
    wr_be32(pkt + 12, dev->ip);
    wr_be32(pkt + 16, dst_ip);
    uint16_t csum = net_checksum(pkt, 20, 0);
    wr_be16(pkt + 10, csum);
    ns_memcpy(pkt + 20, payload, payload_len);
    return eth_send(dev, dmac, ETH_TYPE_IPV4, pkt, total_len);
}

/* 处理收到的 IPv4 包 */
static void ipv4_input(net_device_t *dev, const uint8_t *pkt, uint16_t len) {
    if (len < 20) return;
    uint8_t ver = pkt[0] >> 4;
    uint8_t ihl = (pkt[0] & 0x0F) * 4;
    if (ver != 4 || ihl < 20 || len < ihl) return;
    /* 校验和验证 */
    if (net_checksum(pkt, ihl, 0) != 0) { g_stats.ipv4_drop_checksum++; return; }
    uint16_t total_len = rd_be16(pkt + 2);
    if (total_len > len || total_len < ihl) return;
    uint8_t proto = pkt[9];
    uint32_t src_ip = rd_be32(pkt + 12);
    uint32_t dst_ip = rd_be32(pkt + 16);
    /* 只收目标是自己或广播的包 */
    uint32_t bcast = (dev->ip & dev->netmask) | (~dev->netmask);
    if (dev->ip != 0 && dst_ip != dev->ip && dst_ip != 0xFFFFFFFFu && dst_ip != bcast) return;
    const uint8_t *l4 = pkt + ihl;
    uint16_t l4_len = (uint16_t)(total_len - ihl);
    switch (proto) {
        case IP_PROTO_ICMP: icmp_input(dev, src_ip, l4, l4_len); break;
        case IP_PROTO_UDP:  udp_input(dev, src_ip, l4, l4_len); break;
        case IP_PROTO_TCP:  tcp_input(dev, src_ip, l4, l4_len); break;
        default: break;
    }
}

/* ============================================================
 * ICMP（echo 请求/应答）
 * ICMP 头(8字节): type(1) code(1) checksum(2) id(2) seq(2)
 * ============================================================ */
static uint16_t g_icmp_id = 0x1234;
static uint16_t g_icmp_seq = 0;
static volatile int g_ping_got_reply = 0;
static uint32_t g_ping_reply_from = 0;

static void icmp_input(net_device_t *dev, uint32_t src_ip, const uint8_t *data, uint16_t len) {
    if (len < 8) return;
    if (net_checksum(data, len, 0) != 0) return;
    uint8_t type = data[0];
    if (type == ICMP_TYPE_ECHO_REQUEST) {
        /* 构造 echo reply：type=0，其余原样返回 */
        uint8_t reply[ETH_MTU];
        if (len > sizeof(reply)) return;
        ns_memcpy(reply, data, len);
        reply[0] = ICMP_TYPE_ECHO_REPLY;
        reply[1] = 0;
        wr_be16(reply + 2, 0);
        uint16_t csum = net_checksum(reply, len, 0);
        wr_be16(reply + 2, csum);
        ipv4_send(dev, src_ip, IP_PROTO_ICMP, reply, len);
        ns_puts("[net] ICMP echo reply -> "); ns_put_ip(src_ip); ns_puts("\n");
    } else if (type == ICMP_TYPE_ECHO_REPLY) {
        uint16_t id = rd_be16(data + 4);
        if (id == g_icmp_id) {
            g_ping_got_reply = 1;
            g_ping_reply_from = src_ip;
        }
    }
}

/* ============================================================
 * UDP
 * UDP 头(8字节): src_port(2) dst_port(2) length(2) checksum(2)
 * ============================================================ */
typedef struct {
    uint16_t port;              /* 本地端口(主机序) */
    uint8_t  used;
    udp_recv_func_t cb;
} udp_bind_t;
static udp_bind_t g_udp_binds[UDP_BIND_MAX];

static void udp_input(net_device_t *dev, uint32_t src_ip, const uint8_t *data, uint16_t len) {
    (void)dev;
    if (len < 8) return;
    uint16_t src_port = rd_be16(data + 0);
    uint16_t dst_port = rd_be16(data + 2);
    uint16_t ulen = rd_be16(data + 4);
    if (ulen < 8 || ulen > len) return;
    const uint8_t *payload = data + 8;
    uint16_t plen = (uint16_t)(ulen - 8);
    for (int i = 0; i < UDP_BIND_MAX; i++) {
        if (g_udp_binds[i].used && g_udp_binds[i].port == dst_port && g_udp_binds[i].cb) {
            g_udp_binds[i].cb(src_ip, src_port, dst_port, payload, plen);
            return;
        }
    }
}

/* 发送 UDP 数据报。使用伪头计算校验和 */
static int udp_send_impl(net_device_t *dev, uint32_t dst_ip, uint16_t src_port,
                         uint16_t dst_port, const uint8_t *data, uint16_t data_len) {
    if (!dev) return -2;
    if (data_len > ETH_MTU - 20 - 8) return -2;
    uint8_t seg[ETH_MTU - 20];
    uint16_t ulen = (uint16_t)(8 + data_len);
    wr_be16(seg + 0, src_port);
    wr_be16(seg + 2, dst_port);
    wr_be16(seg + 4, ulen);
    wr_be16(seg + 6, 0);            /* checksum 先置0 */
    ns_memcpy(seg + 8, data, data_len);
    /* 伪头: src_ip(4) dst_ip(4) zero(1) proto(1) udp_len(2) */
    uint32_t pseudo = 0;
    pseudo += (dev->ip >> 16) & 0xFFFF; pseudo += dev->ip & 0xFFFF;
    pseudo += (dst_ip >> 16) & 0xFFFF; pseudo += dst_ip & 0xFFFF;
    pseudo += IP_PROTO_UDP;
    pseudo += ulen;
    uint16_t csum = net_checksum(seg, ulen, pseudo);
    if (csum == 0) csum = 0xFFFF;   /* UDP 校验和0表示未用，取反 */
    wr_be16(seg + 6, csum);
    return ipv4_send(dev, dst_ip, IP_PROTO_UDP, seg, ulen);
}

/* ============================================================
 * TCP：完整状态机（M1.4）
 *   - 三次握手（主动 connect / 被动 listen+accept）
 *   - 数据收发（累积 ACK + 简单接收环形缓冲）
 *   - 四次挥手（FIN/ACK）
 *   - 超时重传（单一 SYN/数据重传定时器，基于 net_tick 驱动）
 * ============================================================ */
#define TCP_RX_BUF_SZ    2048   /* 每连接接收缓冲 */
#define TCP_TX_BUF_SZ    2048   /* 每连接未确认发送缓冲(用于重传) */
#define TCP_MSS          1460   /* 最大段大小(以太网MTU-20-20) */
#define TCP_RTO_TICKS    50     /* 重传超时(net_tick 单位, ~按调用频率) */
#define TCP_MAX_RETRIES  5      /* 最大重传次数, 超过则放弃 */
#define TCP_DEFAULT_WIN  TCP_RX_BUF_SZ

typedef struct {
    uint8_t  used;
    uint8_t  state;             /* NET_TCP_STATE_* */
    uint8_t  is_server_child;   /* 由 listen 派生的已连接子连接 */
    uint8_t  listen_idx;        /* 派生自哪个 listen 槽(用于回调继承) */
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    /* 发送方序号空间 */
    uint32_t snd_una;           /* 最早未确认序号 */
    uint32_t snd_nxt;           /* 下一个要发送的序号 */
    uint32_t iss;               /* 初始发送序号 */
    uint16_t snd_wnd;           /* 对端通告窗口 */
    /* 接收方序号空间 */
    uint32_t rcv_nxt;           /* 期望收到的下一个序号 */
    uint32_t irs;               /* 初始接收序号 */
    uint16_t rcv_wnd;           /* 本地通告窗口 */
    /* 接收环形缓冲 */
    uint8_t  rx_buf[TCP_RX_BUF_SZ];
    uint32_t rx_head;           /* 读指针 */
    uint32_t rx_tail;           /* 写指针 */
    uint32_t rx_len;            /* 缓冲内有效字节 */
    /* 未确认发送缓冲(重传用) */
    uint8_t  tx_buf[TCP_TX_BUF_SZ];
    uint32_t tx_len;            /* 未确认字节数 */
    uint32_t tx_seq;            /* tx_buf[0] 对应的序号 */
    uint8_t  tx_flags;          /* 未确认段携带的标志(SYN/FIN) */
    /* 重传定时器 */
    int32_t  rto_timer;         /* >0 倒计时; <=0 停用 */
    uint8_t  retries;
    tcp_recv_func_t cb;
    void *ctx;
} tcp_conn_t;
static tcp_conn_t g_tcp_conns[TCP_CONN_MAX];

/* 单调递增的 ISS 生成器(简易) */
static uint32_t g_tcp_iss_gen = 0x1000;
static uint32_t tcp_gen_iss(void) { g_tcp_iss_gen += 0x4000; return g_tcp_iss_gen; }

/* TCP 标志位 */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

/* 前置声明 */
static tcp_conn_t *tcp_find_conn(uint32_t rip, uint16_t rport, uint16_t lport);
static tcp_conn_t *tcp_alloc_conn(void);

/* 发送一个 TCP 段（payload 可为空）。使用伪首部计算校验和 */
static int tcp_send_segment(tcp_conn_t *c, uint8_t flags,
                            const uint8_t *payload, uint16_t plen) {
    net_device_t *dev = g_devices[0];
    static uint8_t seg[20 + TCP_MSS];
    if (plen > TCP_MSS) plen = TCP_MSS;
    wr_be16(seg + 0, c->local_port);
    wr_be16(seg + 2, c->remote_port);
    wr_be32(seg + 4, c->snd_nxt);
    wr_be32(seg + 8, (flags & TCP_ACK) ? c->rcv_nxt : 0);
    seg[12] = (5 << 4);
    seg[13] = flags;
    wr_be16(seg + 14, c->rcv_wnd);
    wr_be16(seg + 16, 0);
    wr_be16(seg + 18, 0);
    if (payload && plen) ns_memcpy(seg + 20, payload, plen);
    uint16_t seglen = (uint16_t)(20 + plen);
    /* 伪首部部分和 */
    uint32_t pseudo = 0;
    pseudo += (dev->ip >> 16) & 0xFFFF; pseudo += dev->ip & 0xFFFF;
    pseudo += (c->remote_ip >> 16) & 0xFFFF; pseudo += c->remote_ip & 0xFFFF;
    pseudo += IP_PROTO_TCP;
    pseudo += seglen;
    uint16_t csum = net_checksum(seg, seglen, pseudo);
    wr_be16(seg + 16, csum);
    return ipv4_send(dev, c->remote_ip, IP_PROTO_TCP, seg, seglen);
}

/* 把收到的数据压入接收环形缓冲 */
static void tcp_rx_push(tcp_conn_t *c, const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len && c->rx_len < TCP_RX_BUF_SZ; i++) {
        c->rx_buf[c->rx_tail] = data[i];
        c->rx_tail = (c->rx_tail + 1) % TCP_RX_BUF_SZ;
        c->rx_len++;
    }
    /* 更新通告窗口 */
    c->rcv_wnd = (uint16_t)(TCP_RX_BUF_SZ - c->rx_len);
}

static void tcp_input(net_device_t *dev, uint32_t src_ip, const uint8_t *data, uint16_t len) {
    (void)dev;
    if (len < 20) return;
    uint16_t sport = rd_be16(data + 0);
    uint16_t dport = rd_be16(data + 2);
    uint32_t seq   = rd_be32(data + 4);
    uint32_t ack   = rd_be32(data + 8);
    uint8_t  doff  = (uint8_t)((data[12] >> 4) * 4);
    uint8_t  flags = data[13];
    uint16_t wnd   = rd_be16(data + 14);
    if (doff < 20 || doff > len) return;
    const uint8_t *payload = data + doff;
    uint16_t plen = (uint16_t)(len - doff);

    tcp_conn_t *c = tcp_find_conn(src_ip, sport, dport);

    /* 无匹配连接：检查是否有 LISTEN 槽在监听该端口 */
    if (!c) {
        tcp_conn_t *lst = NULL;
        for (int i = 0; i < TCP_CONN_MAX; i++) {
            if (g_tcp_conns[i].used && g_tcp_conns[i].state == NET_TCP_STATE_LISTEN
                && g_tcp_conns[i].local_port == dport) { lst = &g_tcp_conns[i]; break; }
        }
        if (lst && (flags & TCP_SYN) && !(flags & TCP_ACK)) {
            /* 被动打开：派生子连接，发送 SYN+ACK */
            tcp_conn_t *nc = tcp_alloc_conn();
            if (!nc) return;
            nc->state = NET_TCP_STATE_SYN_RECEIVED;
            nc->is_server_child = 1;
            nc->remote_ip = src_ip; nc->remote_port = sport; nc->local_port = dport;
            nc->irs = seq; nc->rcv_nxt = seq + 1;
            nc->iss = tcp_gen_iss(); nc->snd_una = nc->iss; nc->snd_nxt = nc->iss;
            nc->snd_wnd = wnd; nc->rcv_wnd = TCP_DEFAULT_WIN;
            nc->cb = lst->cb; nc->ctx = lst->ctx;
            tcp_send_segment(nc, TCP_SYN | TCP_ACK, NULL, 0);
            nc->snd_nxt = nc->iss + 1;
            nc->rto_timer = TCP_RTO_TICKS; nc->retries = 0;
            nc->tx_flags = TCP_SYN;
        }
        return;
    }

    /* RST：直接关闭 */
    if (flags & TCP_RST) { c->state = NET_TCP_STATE_CLOSED; c->used = 0; return; }

    switch (c->state) {
    case NET_TCP_STATE_SYN_SENT:
        /* 主动打开等待 SYN+ACK */
        if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
            if (ack != c->iss + 1) { tcp_send_segment(c, TCP_RST, NULL, 0); return; }
            c->irs = seq; c->rcv_nxt = seq + 1;
            c->snd_una = ack; c->snd_wnd = wnd;
            c->state = NET_TCP_STATE_ESTABLISHED;
            c->rto_timer = 0; c->retries = 0; c->tx_flags = 0;
            tcp_send_segment(c, TCP_ACK, NULL, 0);
        }
        break;
    case NET_TCP_STATE_SYN_RECEIVED:
        /* 被动打开等待握手最后 ACK */
        if (flags & TCP_ACK) {
            if (ack == c->iss + 1) {
                c->snd_una = ack; c->snd_wnd = wnd;
                c->state = NET_TCP_STATE_ESTABLISHED;
                c->rto_timer = 0; c->retries = 0; c->tx_flags = 0;
            }
        }
        break;
    case NET_TCP_STATE_ESTABLISHED:
        /* 确认我方数据 */
        if (flags & TCP_ACK) { if (ack > c->snd_una) c->snd_una = ack; c->snd_wnd = wnd;
            if (c->snd_una >= c->tx_seq + c->tx_len) { c->tx_len = 0; c->rto_timer = 0; } }
        /* 收到按序数据 */
        if (plen > 0 && seq == c->rcv_nxt) {
            tcp_rx_push(c, payload, plen);
            c->rcv_nxt += plen;
            tcp_send_segment(c, TCP_ACK, NULL, 0);
            if (c->cb) c->cb(src_ip, sport, dport, payload, plen);
        } else if (plen > 0) {
            /* 乱序/重复：重发 ACK */
            tcp_send_segment(c, TCP_ACK, NULL, 0);
        }
        /* 对端请求关闭 */
        if (flags & TCP_FIN) {
            c->rcv_nxt += 1;
            tcp_send_segment(c, TCP_ACK, NULL, 0);
            c->state = NET_TCP_STATE_CLOSE_WAIT;
        }
        break;
    case NET_TCP_STATE_FIN_WAIT_1:
        if (flags & TCP_ACK) { if (ack > c->snd_una) c->snd_una = ack;
            if (c->snd_una >= c->snd_nxt) c->state = NET_TCP_STATE_FIN_WAIT_2; }
        if (flags & TCP_FIN) {
            c->rcv_nxt += 1; tcp_send_segment(c, TCP_ACK, NULL, 0);
            c->state = (c->state == NET_TCP_STATE_FIN_WAIT_2)
                       ? NET_TCP_STATE_TIME_WAIT : NET_TCP_STATE_CLOSING;
        }
        break;
    case NET_TCP_STATE_FIN_WAIT_2:
        if (flags & TCP_FIN) {
            c->rcv_nxt += 1; tcp_send_segment(c, TCP_ACK, NULL, 0);
            c->state = NET_TCP_STATE_TIME_WAIT; c->rto_timer = TCP_RTO_TICKS;
        }
        break;
    case NET_TCP_STATE_CLOSING:
        if (flags & TCP_ACK) { c->state = NET_TCP_STATE_TIME_WAIT; c->rto_timer = TCP_RTO_TICKS; }
        break;
    case NET_TCP_STATE_LAST_ACK:
        if (flags & TCP_ACK) { c->state = NET_TCP_STATE_CLOSED; c->used = 0; }
        break;
    default: break;
    }
}

/* ============================================================
 * 以太网帧总入口：从驱动收到的原始帧分发到 ARP/IPv4
 * ============================================================ */
static void eth_input(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    if (len < ETH_HDR_LEN) return;
    dev->rx_packets++;
    g_stats.rx_packets++;
    uint16_t ethertype = rd_be16(frame + 12);
    const uint8_t *payload = frame + ETH_HDR_LEN;
    uint16_t plen = (uint16_t)(len - ETH_HDR_LEN);
    switch (ethertype) {
        case ETH_TYPE_ARP:  arp_input(dev, payload, plen); break;
        case ETH_TYPE_IPV4: ipv4_input(dev, payload, plen); break;
        default: break;
    }
}

/* ============================================================
 * 驱动轮询：从 virtio-net 拉取收到的包并处理
 * ============================================================ */
#define NET_RX_BUF_MAX  2048
static uint8_t g_rx_buf[NET_RX_BUF_MAX];

void net_poll(void) {
    if (!g_default_dev) return;
    /* virtio-net 只有一个设备时，循环拉取直到空 */
    for (int guard = 0; guard < 64; guard++) {
        int n = virtio_net_poll_recv(g_rx_buf, NET_RX_BUF_MAX);
        if (n <= 0) break;
        eth_input(g_default_dev, g_rx_buf, (uint16_t)n);
    }
}

void net_tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    g_net_ticks++;
    net_poll();
    /* TCP 重传 / TIME_WAIT 回收扫描 */
    for (int i = 0; i < TCP_CONN_MAX; i++) {
        tcp_conn_t *c = &g_tcp_conns[i];
        if (!c->used || c->rto_timer <= 0) continue;
        c->rto_timer--;
        if (c->rto_timer > 0) continue;
        /* 定时器到期 */
        if (c->state == NET_TCP_STATE_TIME_WAIT) {
            c->used = 0; c->state = NET_TCP_STATE_CLOSED; continue;
        }
        if (c->retries >= TCP_MAX_RETRIES) {
            /* 放弃连接 */
            tcp_send_segment(c, TCP_RST, NULL, 0);
            c->used = 0; c->state = NET_TCP_STATE_CLOSED; continue;
        }
        c->retries++;
        /* 根据状态重传 */
        if (c->state == NET_TCP_STATE_SYN_SENT) {
            uint32_t save = c->snd_nxt; c->snd_nxt = c->iss;
            tcp_send_segment(c, TCP_SYN, NULL, 0); c->snd_nxt = save;
        } else if (c->state == NET_TCP_STATE_SYN_RECEIVED) {
            uint32_t save = c->snd_nxt; c->snd_nxt = c->iss;
            tcp_send_segment(c, TCP_SYN | TCP_ACK, NULL, 0); c->snd_nxt = save;
        } else if (c->tx_flags & TCP_FIN) {
            uint32_t save = c->snd_nxt; c->snd_nxt--;
            tcp_send_segment(c, TCP_FIN | TCP_ACK, NULL, 0); c->snd_nxt = save;
        } else if (c->tx_len > 0) {
            uint32_t save = c->snd_nxt; c->snd_nxt = c->tx_seq;
            tcp_send_segment(c, TCP_PSH | TCP_ACK, c->tx_buf, (uint16_t)c->tx_len);
            c->snd_nxt = save;
        } else { c->rto_timer = 0; continue; }
        c->rto_timer = TCP_RTO_TICKS;
    }
}

/* ============================================================
 * virtio-net 设备适配：transmit 回调 + net_init
 * ============================================================ */
static net_device_t g_virtio_netdev;

/* net_device 的 transmit 回调：直接下发给 virtio-net */
static int virtio_netdev_tx(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    (void)dev;
    return virtio_net_send(frame, (uint32_t)len);
}

void net_init(void) {
    ns_memset(g_devices, 0, sizeof(g_devices));
    ns_memset(g_arp_cache, 0, sizeof(g_arp_cache));
    ns_memset(g_udp_binds, 0, sizeof(g_udp_binds));
    ns_memset(g_tcp_conns, 0, sizeof(g_tcp_conns));
    ns_memset(&g_stats, 0, sizeof(g_stats));
    g_device_count = 0;
    g_default_dev = 0;

    /* 初始化 virtio-net 网卡（如尚未初始化） */
    if (virtio_net_device_count() == 0) {
        virtio_net_init();
    }
    if (virtio_net_device_count() == 0) {
        ns_puts("[net] 未发现 virtio-net 网卡，网络栈未启用\n");
        return;
    }

    /* 构造 net_device */
    ns_memset(&g_virtio_netdev, 0, sizeof(g_virtio_netdev));
    const char *nm = "eth0";
    for (int i = 0; i < 4; i++) g_virtio_netdev.name[i] = nm[i];
    g_virtio_netdev.name[4] = '\0';
    virtio_net_get_mac(g_virtio_netdev.mac);
    g_virtio_netdev.transmit = virtio_netdev_tx;
    g_virtio_netdev.link_up = 1;
    g_virtio_netdev.admin_up = 1;
    g_virtio_netdev.config_mode = NET_CONFIG_MODE_STATIC;

    /* 静态 IP 配置（QEMU user 网络默认网段 10.0.2.0/24，网关 10.0.2.2） */
    g_virtio_netdev.ip      = (10u<<24)|(0u<<16)|(2u<<8)|15u;   /* 10.0.2.15 */
    g_virtio_netdev.netmask = 0xFFFFFF00u;                        /* 255.255.255.0 */
    g_virtio_netdev.gateway = (10u<<24)|(0u<<16)|(2u<<8)|2u;    /* 10.0.2.2 */
    g_virtio_netdev.dns     = (10u<<24)|(0u<<16)|(2u<<8)|3u;    /* 10.0.2.3 */

    net_register_device(&g_virtio_netdev);

    ns_puts("[net] 网络栈已启用 eth0 MAC=");
    ns_put_mac(g_virtio_netdev.mac);
    ns_puts(" IP="); ns_put_ip(g_virtio_netdev.ip);
    ns_puts(" GW="); ns_put_ip(g_virtio_netdev.gateway);
    ns_puts("\n");
}

/* ============================================================
 * 设备管理 API
 * ============================================================ */
int net_register_device(net_device_t *dev) {
    if (!dev) return -1;
    if (g_device_count >= NET_MAX_DEVICES) return -1;
    g_devices[g_device_count++] = dev;
    if (!g_default_dev || dev->admin_up) g_default_dev = dev;
    return 0;
}

net_device_t *net_get_default_device(void) {
    return g_default_dev;
}

net_device_t *net_find_device(const char *name) {
    if (!name) return 0;
    for (uint32_t i = 0; i < g_device_count; i++) {
        net_device_t *d = g_devices[i];
        int eq = 1;
        for (int j = 0; j < 16; j++) {
            if (d->name[j] != name[j]) { eq = 0; break; }
            if (name[j] == '\0') break;
        }
        if (eq) return d;
    }
    return 0;
}

net_device_t *net_get_device_by_index(uint32_t index) {
    if (index >= g_device_count) return 0;
    return g_devices[index];
}

uint32_t net_device_count(void) {
    return g_device_count;
}

/* ============================================================
 * 数据收发 API
 * ============================================================ */
/* 从驱动注入一个原始以太网帧（供中断/轮询路径调用） */
void net_input(net_device_t *dev, const uint8_t *frame, uint16_t len) {
    if (!dev) dev = g_default_dev;
    if (!dev) return;
    eth_input(dev, frame, len);
}

/* 发送一个 IPv4 包（供上层直接调用） */
int net_send_ipv4(uint32_t dst_ip, uint8_t proto, const uint8_t *data, uint16_t len) {
    if (!g_default_dev) return -1;
    return ipv4_send(g_default_dev, dst_ip, proto, data, len);
}

/* 发送 UDP 数据报 */
int net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const uint8_t *data, uint16_t len) {
    if (!g_default_dev) return -1;
    return udp_send_impl(g_default_dev, dst_ip, src_port, dst_port, data, len);
}

/* 广播 UDP */
int net_send_udp_broadcast(uint16_t src_port, uint16_t dst_port,
                           const uint8_t *data, uint16_t len) {
    if (!g_default_dev) return -1;
    return udp_send_impl(g_default_dev, 0xFFFFFFFFu, src_port, dst_port, data, len);
}

/* 绑定 UDP 端口 */
int net_udp_bind(uint16_t port, udp_recv_func_t cb) {
    if (!cb) return -1;
    for (int i = 0; i < UDP_BIND_MAX; i++) {
        if (g_udp_binds[i].used && g_udp_binds[i].port == port) {
            g_udp_binds[i].cb = cb;
            return 0;
        }
    }
    for (int i = 0; i < UDP_BIND_MAX; i++) {
        if (!g_udp_binds[i].used) {
            g_udp_binds[i].used = 1;
            g_udp_binds[i].port = port;
            g_udp_binds[i].cb = cb;
            return 0;
        }
    }
    return -1;
}

/* ============================================================
 * ping + IP 配置 + 格式化 API
 * ============================================================ */
/* 发送 ICMP echo 请求并轮询等待应答。返回0=成功 -1=超时 */
int net_ping_ipv4(uint32_t dst_ip) {
    if (!g_default_dev) return -1;
    /* Ensure interrupts are enabled while we busy-poll the NIC: when this
     * runs inside a syscall/interrupt gate IF may be cleared, which would
     * starve virtio RX and make every echo reply time out. */
    __asm__ __volatile__("sti");
    g_ping_got_reply = 0;
    g_ping_reply_from = 0;
    g_icmp_seq++;
    uint8_t icmp[64];
    icmp[0] = ICMP_TYPE_ECHO_REQUEST;
    icmp[1] = 0;
    wr_be16(icmp + 2, 0);
    wr_be16(icmp + 4, g_icmp_id);
    wr_be16(icmp + 6, g_icmp_seq);
    /* 32 字节载荷 */
    for (int i = 0; i < 32; i++) icmp[8 + i] = (uint8_t)('a' + (i % 26));
    uint16_t icmp_len = 8 + 32;
    uint16_t csum = net_checksum(icmp, icmp_len, 0);
    wr_be16(icmp + 2, csum);

    /* 可能需要先 ARP，重试几次 */
    int sent = -1;
    for (int attempt = 0; attempt < 4 && sent != 0; attempt++) {
        sent = ipv4_send(g_default_dev, dst_ip, IP_PROTO_ICMP, icmp, icmp_len);
        if (sent != 0) {
            /* ARP 未就绪，轮询收包等 ARP 应答 */
            for (int w = 0; w < 100000; w++) net_poll();
        }
    }
    if (sent != 0) return -1;
    /* 轮询等待 echo reply */
    for (int w = 0; w < 2000000; w++) {
        net_poll();
        if (g_ping_got_reply) return 0;
    }
    return -1;
}

/* 向自身 IP 发 ping（回环测试） */
int net_ping_self(void) {
    if (!g_default_dev) return -1;
    return net_ping_ipv4(g_default_dev->ip);
}

/* 配置默认设备 IPv4 */
int net_config_ipv4(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns) {
    if (!g_default_dev) return -1;
    g_default_dev->ip = ip;
    g_default_dev->netmask = netmask;
    g_default_dev->gateway = gateway;
    g_default_dev->dns = dns;
    g_default_dev->config_mode = NET_CONFIG_MODE_STATIC;
    return 0;
}

void net_set_default_ipv4(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns) {
    net_config_ipv4(ip, netmask, gateway, dns);
}

void net_set_default_ipv4_dhcp(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns) {
    if (!g_default_dev) return;
    g_default_dev->ip = ip;
    g_default_dev->netmask = netmask;
    g_default_dev->gateway = gateway;
    g_default_dev->dns = dns;
    g_default_dev->config_mode = NET_CONFIG_MODE_DHCP;
}

/* 将主机序 IP 格式化为 "a.b.c.d"（out 至少 16 字节） */
void net_format_ipv4(uint32_t ip, char *out) {
    if (!out) return;
    int pos = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        uint32_t seg = (ip >> shift) & 0xFF;
        if (seg >= 100) { out[pos++] = (char)('0' + seg / 100); seg %= 100; out[pos++] = (char)('0' + seg / 10); out[pos++] = (char)('0' + seg % 10); }
        else if (seg >= 10) { out[pos++] = (char)('0' + seg / 10); out[pos++] = (char)('0' + seg % 10); }
        else out[pos++] = (char)('0' + seg);
        if (shift > 0) out[pos++] = '.';
    }
    out[pos] = '\0';
}

/* 解析 "a.b.c.d" 为主机序 IP。返回0成功 */
int net_parse_ipv4(const char *text, uint32_t *out) {
    if (!text || !out) return -1;
    uint32_t ip = 0; int seg = 0, digits = 0, val = 0;
    for (const char *p = text; ; p++) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 255) return -1;
            digits++;
        } else if (*p == '.' || *p == '\0') {
            if (digits == 0) return -1;
            ip = (ip << 8) | (uint32_t)val;
            seg++;
            val = 0; digits = 0;
            if (*p == '\0') break;
            if (seg >= 4) return -1;
        } else return -1;
    }
    if (seg != 4) return -1;
    *out = ip;
    return 0;
}

/* ============================================================
 * 设备信息与诊断 API
 * ============================================================ */
static void fill_device_info(net_device_t *d, net_device_info_t *out) {
    ns_memset(out, 0, sizeof(*out));
    for (int i = 0; i < NET_DEVICE_NAME_MAX && i < 16; i++) out->name[i] = d->name[i];
    const char *drv = "virtio-net";
    for (int i = 0; i < NET_DRIVER_NAME_MAX - 1 && drv[i]; i++) out->driver[i] = drv[i];
    ns_memcpy(out->mac, d->mac, 6);
    out->mtu = ETH_MTU;
    out->flags = 0;
    if (d->link_up) out->flags |= NET_DEVICE_FLAG_UP;
    out->ip = d->ip;
    out->netmask = d->netmask;
    out->gateway = d->gateway;
    out->dns = d->dns;
    out->config_mode = d->config_mode;
    out->rx_packets = d->rx_packets;
    out->tx_packets = d->tx_packets;
    out->rx_dropped = d->rx_dropped;
    out->tx_dropped = d->tx_dropped;
}

int net_get_device_info(uint32_t index, net_device_info_t *out) {
    if (!out || index >= g_device_count) return -1;
    fill_device_info(g_devices[index], out);
    return 0;
}

int net_get_device_info_by_name(const char *name, net_device_info_t *out) {
    if (!out) return -1;
    net_device_t *d = net_find_device(name);
    if (!d) return -1;
    fill_device_info(d, out);
    return 0;
}

int net_get_diag_stats(net_diag_stats_t *stats) {
    if (!stats) return -1;
    ns_memcpy(stats, &g_stats, sizeof(*stats));
    if (g_default_dev) {
        for (int i = 0; i < 16; i++) stats->name[i] = g_default_dev->name[i];
        ns_memcpy(stats->mac, g_default_dev->mac, 6);
    }
    stats->arp_entries = arp_cache_count();
    return 0;
}

int net_set_device_admin_up(const char *name, int up) {
    net_device_t *d = net_find_device(name);
    if (!d) return -1;
    d->admin_up = up ? 1 : 0;
    d->link_up = up ? 1 : 0;   /* virtio 虚拟链路跟随 admin */
    return 0;
}

int net_refresh_device_status(const char *name) {
    net_device_t *d = net_find_device(name);
    if (!d) return -1;
    /* virtio-net 虚拟链路始终 up */
    d->link_up = d->admin_up ? 1 : 0;
    return 0;
}

uint32_t net_scan_wifi(net_wifi_network_info_t *out_list, uint32_t max_results) {
    (void)out_list; (void)max_results;
    return 0;  /* 无无线网卡，返回0 */
}

/* ============================================================
 * TCP API（M1.4 完整状态机）
 * ============================================================ */
static tcp_conn_t *tcp_find_conn(uint32_t rip, uint16_t rport, uint16_t lport) {
    for (int i = 0; i < TCP_CONN_MAX; i++) {
        tcp_conn_t *c = &g_tcp_conns[i];
        if (c->used && c->state != NET_TCP_STATE_LISTEN
            && c->remote_ip == rip && c->remote_port == rport && c->local_port == lport)
            return c;
    }
    return NULL;
}
static tcp_conn_t *tcp_alloc_conn(void) {
    for (int i = 0; i < TCP_CONN_MAX; i++)
        if (!g_tcp_conns[i].used) {
            ns_memset(&g_tcp_conns[i], 0, sizeof(tcp_conn_t));
            g_tcp_conns[i].used = 1;
            g_tcp_conns[i].rcv_wnd = TCP_DEFAULT_WIN;
            return &g_tcp_conns[i];
        }
    return NULL;
}
int net_tcp_listen(uint16_t port, tcp_recv_func_t cb) {
    for (int i = 0; i < TCP_CONN_MAX; i++) {
        if (!g_tcp_conns[i].used) {
            ns_memset(&g_tcp_conns[i], 0, sizeof(tcp_conn_t));
            g_tcp_conns[i].used = 1;
            g_tcp_conns[i].state = NET_TCP_STATE_LISTEN;
            g_tcp_conns[i].local_port = port;
            g_tcp_conns[i].rcv_wnd = TCP_DEFAULT_WIN;
            g_tcp_conns[i].cb = cb;
            return i;
        }
    }
    return -1;
}
int net_tcp_open(uint32_t local_ip, uint16_t local_port,
                 uint32_t remote_ip, uint16_t remote_port, int active) {
    (void)local_ip;
    tcp_conn_t *c = tcp_alloc_conn();
    if (!c) return -1;
    c->local_port = local_port ? local_port : (uint16_t)(40000 + (g_tcp_iss_gen & 0x3FFF));
    c->remote_ip = remote_ip;
    c->remote_port = remote_port;
    if (active) {
        /* 主动打开：发 SYN */
        c->iss = tcp_gen_iss();
        c->snd_una = c->iss;
        c->snd_nxt = c->iss;
        c->state = NET_TCP_STATE_SYN_SENT;
        tcp_send_segment(c, TCP_SYN, NULL, 0);
        c->snd_nxt = c->iss + 1;
        c->rto_timer = TCP_RTO_TICKS;
        c->retries = 0;
        c->tx_flags = TCP_SYN;
    } else {
        c->state = NET_TCP_STATE_CLOSED;
    }
    return (int)(c - g_tcp_conns);
}
int net_tcp_send(int conn_id, const uint8_t *data, uint16_t len) {
    if (conn_id < 0 || conn_id >= TCP_CONN_MAX) return -1;
    tcp_conn_t *c = &g_tcp_conns[conn_id];
    if (!c->used || c->state != NET_TCP_STATE_ESTABLISHED) return -1;
    if (len > TCP_MSS) len = TCP_MSS;
    /* 发送 PSH+ACK 数据段 */
    tcp_send_segment(c, TCP_PSH | TCP_ACK, data, len);
    /* 存入未确认缓冲用于重传 */
    if (len <= TCP_TX_BUF_SZ) {
        ns_memcpy(c->tx_buf, data, len);
        c->tx_len = len;
        c->tx_seq = c->snd_nxt;
        c->tx_flags = 0;
        c->rto_timer = TCP_RTO_TICKS;
        c->retries = 0;
    }
    c->snd_nxt += len;
    return (int)len;
}
int net_tcp_available(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_CONN_MAX || !g_tcp_conns[conn_id].used) return 0;
    return (int)g_tcp_conns[conn_id].rx_len;
}
int net_tcp_recv(int conn_id, uint8_t *data, uint16_t len) {
    if (conn_id < 0 || conn_id >= TCP_CONN_MAX) return -1;
    tcp_conn_t *c = &g_tcp_conns[conn_id];
    if (!c->used) return -1;
    uint16_t n = 0;
    while (n < len && c->rx_len > 0) {
        data[n++] = c->rx_buf[c->rx_head];
        c->rx_head = (c->rx_head + 1) % TCP_RX_BUF_SZ;
        c->rx_len--;
    }
    c->rcv_wnd = (uint16_t)(TCP_RX_BUF_SZ - c->rx_len);
    return (int)n;
}
int net_tcp_close(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_CONN_MAX) return -1;
    tcp_conn_t *c = &g_tcp_conns[conn_id];
    if (!c->used) return -1;
    if (c->state == NET_TCP_STATE_ESTABLISHED) {
        /* 主动关闭：发 FIN */
        tcp_send_segment(c, TCP_FIN | TCP_ACK, NULL, 0);
        c->snd_nxt += 1;
        c->state = NET_TCP_STATE_FIN_WAIT_1;
        c->rto_timer = TCP_RTO_TICKS; c->retries = 0; c->tx_flags = TCP_FIN;
    } else if (c->state == NET_TCP_STATE_CLOSE_WAIT) {
        /* 被动关闭：回 FIN 进入 LAST_ACK */
        tcp_send_segment(c, TCP_FIN | TCP_ACK, NULL, 0);
        c->snd_nxt += 1;
        c->state = NET_TCP_STATE_LAST_ACK;
        c->rto_timer = TCP_RTO_TICKS; c->retries = 0; c->tx_flags = TCP_FIN;
    } else {
        c->used = 0;
        c->state = NET_TCP_STATE_CLOSED;
    }
    return 0;
}
int net_tcp_state(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_CONN_MAX || !g_tcp_conns[conn_id].used)
        return NET_TCP_STATE_CLOSED;
    return g_tcp_conns[conn_id].state;
}
int net_tcp_get_endpoint(int conn_id, uint32_t *local_ip, uint16_t *local_port,
                         uint32_t *remote_ip, uint16_t *remote_port) {
    if (conn_id < 0 || conn_id >= TCP_CONN_MAX || !g_tcp_conns[conn_id].used) return -1;
    tcp_conn_t *c = &g_tcp_conns[conn_id];
    if (local_ip) *local_ip = g_default_dev ? g_default_dev->ip : 0;
    if (local_port) *local_port = c->local_port;
    if (remote_ip) *remote_ip = c->remote_ip;
    if (remote_port) *remote_port = c->remote_port;
    return 0;
}
int net_tcp_send_syn(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port) {
    return net_tcp_open(0, src_port, dst_ip, dst_port, 1);
}

/* ============================================================
 * 防火墙 API
 * ============================================================ */
#define FIREWALL_MAX  16
static net_firewall_rule_t g_firewall[FIREWALL_MAX];

int net_firewall_get(uint32_t index, net_firewall_rule_t *rule) {
    if (!rule || index >= FIREWALL_MAX || !g_firewall[index].used) return -1;
    ns_memcpy(rule, &g_firewall[index], sizeof(*rule));
    return 0;
}
int net_firewall_add(const net_firewall_rule_t *rule) {
    if (!rule) return -1;
    for (int i = 0; i < FIREWALL_MAX; i++) {
        if (!g_firewall[i].used) {
            ns_memcpy(&g_firewall[i], rule, sizeof(*rule));
            g_firewall[i].used = 1;
            g_firewall[i].hits = 0;
            return i;
        }
    }
    return -1;
}
int net_firewall_delete(uint32_t index) {
    if (index >= FIREWALL_MAX || !g_firewall[index].used) return -1;
    ns_memset(&g_firewall[index], 0, sizeof(g_firewall[index]));
    return 0;
}
void net_firewall_clear(void) {
    ns_memset(g_firewall, 0, sizeof(g_firewall));
}

/* ============================================================
 * 信息打印
 * ============================================================ */
void net_print_info(void) {
    ns_puts("===== 网络栈信息 =====\n");
    ns_puts("设备数: "); ns_put_dec(g_device_count); ns_puts("\n");
    for (uint32_t i = 0; i < g_device_count; i++) {
        net_device_t *d = g_devices[i];
        ns_puts("  "); ns_puts(d->name);
        ns_puts(" MAC="); ns_put_mac(d->mac);
        ns_puts(" IP="); ns_put_ip(d->ip);
        ns_puts(" GW="); ns_put_ip(d->gateway);
        ns_puts(d->link_up ? " [UP]\n" : " [DOWN]\n");
        ns_puts("    RX="); ns_put_dec(d->rx_packets);
        ns_puts(" TX="); ns_put_dec(d->tx_packets); ns_puts("\n");
    }
    ns_puts("ARP 缓存: "); ns_put_dec(arp_cache_count()); ns_puts(" 条\n");
    ns_puts("====================\n");
}

/* ============================================================
 * DHCP 客户端（M1.5.1）
 *   DISCOVER -> OFFER -> REQUEST -> ACK 四步拿 IP
 *   复用 UDP 广播(bootpc 68 -> bootps 67)
 * ============================================================ */
#define DHCP_OP_REQUEST   1
#define DHCP_OP_REPLY     2
#define DHCP_HTYPE_ETH    1
#define DHCP_MAGIC        0x63825363u
#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_ACK      5
#define DHCP_MSG_NAK      6
#define DHCP_OPT_SUBNET   1
#define DHCP_OPT_ROUTER   3
#define DHCP_OPT_DNS      6
#define DHCP_OPT_REQ_IP   50
#define DHCP_OPT_MSGTYPE  53
#define DHCP_OPT_SERVER   54
#define DHCP_OPT_PARAMS   55
#define DHCP_OPT_END      255

static uint8_t  g_dhcp_state = 0;   /* 0=idle 1=discover发出 2=request发出 3=bound */
static uint32_t g_dhcp_xid = 0x4f504f53;  /* 'OPOS' */
static uint32_t g_dhcp_offered_ip = 0;
static uint32_t g_dhcp_server_ip = 0;
static uint32_t g_dhcp_router = 0;
static uint32_t g_dhcp_subnet = 0;
static uint32_t g_dhcp_dns = 0;

/* 构造并发送一个 DHCP 报文(DISCOVER 或 REQUEST) */
static void dhcp_send(uint8_t msg_type) {
    net_device_t *dev = g_devices[0];
    if (!dev) return;
    static uint8_t buf[300];
    ns_memset(buf, 0, sizeof(buf));
    buf[0] = DHCP_OP_REQUEST;
    buf[1] = DHCP_HTYPE_ETH;
    buf[2] = 6;      /* hlen */
    buf[3] = 0;      /* hops */
    wr_be32(buf + 4, g_dhcp_xid);
    wr_be16(buf + 8, 0);      /* secs */
    wr_be16(buf + 10, 0x8000);/* flags: broadcast */
    /* ciaddr/yiaddr/siaddr/giaddr = 0 (offset 12..27) */
    ns_memcpy(buf + 28, dev->mac, 6);  /* chaddr */
    /* magic cookie @ offset 236 */
    wr_be32(buf + 236, DHCP_MAGIC);
    uint32_t o = 240;
    /* option 53: message type */
    buf[o++] = DHCP_OPT_MSGTYPE; buf[o++] = 1; buf[o++] = msg_type;
    if (msg_type == DHCP_MSG_REQUEST) {
        /* option 50: requested IP */
        buf[o++] = DHCP_OPT_REQ_IP; buf[o++] = 4;
        wr_be32(buf + o, g_dhcp_offered_ip); o += 4;
        /* option 54: server id */
        buf[o++] = DHCP_OPT_SERVER; buf[o++] = 4;
        wr_be32(buf + o, g_dhcp_server_ip); o += 4;
    }
    /* option 55: parameter request list */
    buf[o++] = DHCP_OPT_PARAMS; buf[o++] = 3;
    buf[o++] = DHCP_OPT_SUBNET; buf[o++] = DHCP_OPT_ROUTER; buf[o++] = DHCP_OPT_DNS;
    buf[o++] = DHCP_OPT_END;
    net_send_udp_broadcast(68, 67, buf, (uint16_t)o);
}

/* 解析 DHCP 选项，返回报文类型 */
static uint8_t dhcp_parse(const uint8_t *data, uint16_t len) {
    if (len < 240) return 0;
    if (rd_be32(data + 236) != DHCP_MAGIC) return 0;
    uint8_t msg_type = 0;
    g_dhcp_offered_ip = rd_be32(data + 16);  /* yiaddr */
    uint32_t o = 240;
    while (o < len) {
        uint8_t opt = data[o++];
        if (opt == DHCP_OPT_END) break;
        if (opt == 0) continue;
        if (o >= len) break;
        uint8_t l = data[o++];
        if (o + l > len) break;
        switch (opt) {
        case DHCP_OPT_MSGTYPE: msg_type = data[o]; break;
        case DHCP_OPT_SERVER:  g_dhcp_server_ip = rd_be32(data + o); break;
        case DHCP_OPT_SUBNET:  g_dhcp_subnet = rd_be32(data + o); break;
        case DHCP_OPT_ROUTER:  g_dhcp_router = rd_be32(data + o); break;
        case DHCP_OPT_DNS:     g_dhcp_dns = rd_be32(data + o); break;
        default: break;
        }
        o += l;
    }
    return msg_type;
}

/* UDP 68 端口回调：处理 OFFER / ACK */
static void dhcp_udp_cb(uint32_t src_ip, uint16_t src_port,
                        uint16_t dst_port, const uint8_t *data, uint16_t len) {
    (void)src_ip; (void)src_port; (void)dst_port;
    uint8_t mt = dhcp_parse(data, len);
    if (mt == DHCP_MSG_OFFER && g_dhcp_state == 1) {
        ns_puts("[dhcp] 收到 OFFER, IP="); ns_put_ip(g_dhcp_offered_ip);
        ns_puts(" server="); ns_put_ip(g_dhcp_server_ip); ns_puts("\n");
        g_dhcp_state = 2;
        dhcp_send(DHCP_MSG_REQUEST);
        ns_puts("[dhcp] 已发 REQUEST\n");
    } else if (mt == DHCP_MSG_ACK && g_dhcp_state == 2) {
        g_dhcp_state = 3;
        /* 应用配置：使用 DHCP 专用配置函数，保留 config_mode=DHCP
         * （net_config_ipv4 会强制设为 STATIC，导致 ifconfig 误报） */
        net_set_default_ipv4_dhcp(g_dhcp_offered_ip,
                        g_dhcp_subnet ? g_dhcp_subnet : 0xFFFFFF00u,
                        g_dhcp_router, g_dhcp_dns);
        ns_puts("[dhcp] BOUND! IP="); ns_put_ip(g_dhcp_offered_ip);
        ns_puts(" GW="); ns_put_ip(g_dhcp_router);
        ns_puts(" DNS="); ns_put_ip(g_dhcp_dns); ns_puts("\n");
    } else if (mt == DHCP_MSG_NAK) {
        ns_puts("[dhcp] 收到 NAK, 重新 DISCOVER\n");
        g_dhcp_state = 0;
    }
}

/* 启动 DHCP：绑定 68 端口并发 DISCOVER */
int net_dhcp_start(void) {
    g_dhcp_state = 0;
    g_dhcp_offered_ip = g_dhcp_server_ip = g_dhcp_router = g_dhcp_subnet = g_dhcp_dns = 0;
    if (net_udp_bind(68, dhcp_udp_cb) < 0) {
        ns_puts("[dhcp] 绑定 68 端口失败\n");
        return -1;
    }
    g_dhcp_state = 1;
    dhcp_send(DHCP_MSG_DISCOVER);
    ns_puts("[dhcp] 已发 DISCOVER, 等待 OFFER...\n");
    return 0;
}

/* 返回 DHCP 状态：3=已获取地址 */
int net_dhcp_state(void) { return (int)g_dhcp_state; }

/* ============================================================
 * DNS 客户端（M1.5.2）
 *   A 记录查询：构造 QNAME -> 单播 UDP 53 -> 解析响应
 *   支持指针压缩(0xC0)跳转、CNAME 跳过、简单缓存
 * ============================================================ */
#define DNS_PORT          53
#define DNS_LOCAL_PORT    50053
#define DNS_TYPE_A        1
#define DNS_TYPE_CNAME    5
#define DNS_CLASS_IN      1
#define DNS_CACHE_MAX     16
#define DNS_NAME_MAX      128

typedef struct {
    char     name[DNS_NAME_MAX];
    uint32_t ip;
    uint8_t  valid;
} dns_cache_entry_t;

static dns_cache_entry_t g_dns_cache[DNS_CACHE_MAX];
static uint16_t g_dns_xid       = 0x1234;
static uint16_t g_dns_query_xid  = 0;
static volatile uint32_t g_dns_result_ip = 0;
static volatile uint8_t  g_dns_got_reply = 0;
static uint8_t g_dns_bound = 0;

/* 比较两个 C 字符串是否相等 */
static int dns_streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

/* 查缓存 */
static uint32_t dns_cache_lookup(const char *name) {
    for (int i = 0; i < DNS_CACHE_MAX; i++)
        if (g_dns_cache[i].valid && dns_streq(g_dns_cache[i].name, name))
            return g_dns_cache[i].ip;
    return 0;
}

/* 写缓存(简单轮转覆盖) */
static void dns_cache_store(const char *name, uint32_t ip) {
    static int slot = 0;
    /* 已存在则更新 */
    for (int i = 0; i < DNS_CACHE_MAX; i++)
        if (g_dns_cache[i].valid && dns_streq(g_dns_cache[i].name, name)) {
            g_dns_cache[i].ip = ip;
            return;
        }
    dns_cache_entry_t *e = &g_dns_cache[slot];
    slot = (slot + 1) % DNS_CACHE_MAX;
    int j = 0;
    while (name[j] && j < DNS_NAME_MAX - 1) { e->name[j] = name[j]; j++; }
    e->name[j] = '\0';
    e->ip = ip;
    e->valid = 1;
}

/* 将 "www.a.com" 编码为 DNS QNAME 格式: 3www1a3com0，返回写入字节数 */
static int dns_encode_qname(const char *host, uint8_t *out) {
    int op = 0;
    const char *p = host;
    while (*p) {
        const char *label = p;
        int len = 0;
        while (*p && *p != '.') { p++; len++; }
        if (len == 0 || len > 63) return -1;
        out[op++] = (uint8_t)len;
        for (int i = 0; i < len; i++) out[op++] = (uint8_t)label[i];
        if (*p == '.') p++;
    }
    out[op++] = 0;  /* 根标签 */
    return op;
}

/* 跳过一个可能压缩的名字，返回跳过后的偏移 */
static uint16_t dns_skip_name(const uint8_t *msg, uint16_t len, uint16_t off) {
    while (off < len) {
        uint8_t l = msg[off];
        if (l == 0) { off++; break; }
        if ((l & 0xC0) == 0xC0) { off += 2; break; }  /* 压缩指针占2字节 */
        off += 1 + l;
    }
    return off;
}

/* 解析 DNS 响应，提取第一个 A 记录 IP */
static uint32_t dns_parse_response(const uint8_t *msg, uint16_t len) {
    if (len < 12) return 0;
    uint16_t xid    = rd_be16(msg + 0);
    uint16_t flags  = rd_be16(msg + 2);
    uint16_t qdcnt  = rd_be16(msg + 4);
    uint16_t ancnt  = rd_be16(msg + 6);
    if (xid != g_dns_query_xid) return 0;
    if (!(flags & 0x8000)) return 0;       /* 非响应 */
    if ((flags & 0x000F) != 0) return 0;   /* RCODE 非0 */
    uint16_t off = 12;
    /* 跳过问题区 */
    for (uint16_t q = 0; q < qdcnt; q++) {
        off = dns_skip_name(msg, len, off);
        off += 4; /* QTYPE + QCLASS */
    }
    /* 遍历回答区 */
    for (uint16_t a = 0; a < ancnt && off + 10 <= len; a++) {
        off = dns_skip_name(msg, len, off);
        if (off + 10 > len) break;
        uint16_t rtype  = rd_be16(msg + off);
        uint16_t rdlen  = rd_be16(msg + off + 8);
        off += 10;
        if (off + rdlen > len) break;
        if (rtype == DNS_TYPE_A && rdlen == 4) {
            return rd_be32(msg + off);
        }
        off += rdlen;  /* CNAME 等跳过 */
    }
    return 0;
}

/* UDP 本地端口回调：处理 DNS 响应 */
static void dns_udp_cb(uint32_t src_ip, uint16_t src_port,
                       uint16_t dst_port, const uint8_t *data, uint16_t len) {
    (void)src_ip; (void)src_port; (void)dst_port;
    if (g_dns_got_reply) return;
    uint32_t ip = dns_parse_response(data, len);
    if (ip != 0) {
        g_dns_result_ip = ip;
        g_dns_got_reply = 1;
    }
}

/* 阻塞式 DNS 解析：返回0成功，*out_ip 主机序 */
int net_dns_resolve(const char *hostname, uint32_t *out_ip) {
    if (!hostname || !out_ip || !g_default_dev) return -1;
    /* keep interrupts on while polling the NIC (see net_ping_ipv4) */
    __asm__ __volatile__("sti");
    /* 若本身就是点分十进制，直接解析 */
    uint32_t direct;
    if (net_parse_ipv4(hostname, &direct) == 0) { *out_ip = direct; return 0; }
    /* 查缓存 */
    uint32_t cached = dns_cache_lookup(hostname);
    if (cached != 0) { *out_ip = cached; return 0; }

    uint32_t dns_server = g_default_dev->dns;
    if (dns_server == 0) { ns_puts("[dns] 无 DNS 服务器\n"); return -1; }

    /* 绑定本地端口(仅首次) */
    if (!g_dns_bound) {
        if (net_udp_bind(DNS_LOCAL_PORT, dns_udp_cb) < 0) {
            ns_puts("[dns] 绑定端口失败\n");
            return -1;
        }
        g_dns_bound = 1;
    }

    /* 构造查询报文 */
    static uint8_t buf[512];
    ns_memset(buf, 0, sizeof(buf));
    g_dns_query_xid = g_dns_xid++;
    wr_be16(buf + 0, g_dns_query_xid);
    wr_be16(buf + 2, 0x0100);   /* 标准查询，期望递归 */
    wr_be16(buf + 4, 1);        /* QDCOUNT=1 */
    /* ANCOUNT/NSCOUNT/ARCOUNT = 0 */
    int qn = dns_encode_qname(hostname, buf + 12);
    if (qn < 0) { ns_puts("[dns] 主机名非法\n"); return -1; }
    uint16_t o = (uint16_t)(12 + qn);
    wr_be16(buf + o, DNS_TYPE_A);  o += 2;
    wr_be16(buf + o, DNS_CLASS_IN); o += 2;

    g_dns_got_reply = 0;
    g_dns_result_ip = 0;
    ns_puts("[dns] 查询 "); ns_puts(hostname);
    ns_puts(" @ "); ns_put_ip(dns_server); ns_puts("\n");

    /* 发送(带重试等 ARP) */
    int sent = -1;
    for (int attempt = 0; attempt < 4 && sent != 0; attempt++) {
        sent = net_send_udp(dns_server, DNS_LOCAL_PORT, DNS_PORT, buf, o);
        if (sent != 0)
            for (int w = 0; w < 100000; w++) net_poll();
    }
    if (sent != 0) { ns_puts("[dns] 发送失败\n"); return -1; }

    /* 轮询等待响应 */
    for (int w = 0; w < 3000000; w++) {
        net_poll();
        if (g_dns_got_reply) {
            *out_ip = g_dns_result_ip;
            dns_cache_store(hostname, g_dns_result_ip);
            ns_puts("[dns] 解析成功 "); ns_puts(hostname);
            ns_puts(" -> "); ns_put_ip(g_dns_result_ip); ns_puts("\n");
            return 0;
        }
    }
    ns_puts("[dns] 查询超时\n");
    return -1;
}
