/* ============================================================
 * openos - Minimal DHCPv4 client
 * Implements Discover -> Offer -> Request -> Ack over UDP/IPv4
 * ============================================================ */
#include "dhcp.h"
#include "string.h"
#include "vga.h"

#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY   2
#define DHCP_HTYPE_ETH   1
#define DHCP_HLEN_ETH    6
#define DHCP_MAGIC       0x63825363U

#define DHCP_OPTION_PAD             0
#define DHCP_OPTION_SUBNET_MASK     1
#define DHCP_OPTION_ROUTER          3
#define DHCP_OPTION_REQUESTED_IP    50
#define DHCP_OPTION_LEASE_TIME      51
#define DHCP_OPTION_MESSAGE_TYPE    53
#define DHCP_OPTION_SERVER_ID       54
#define DHCP_OPTION_PARAM_LIST      55
#define DHCP_OPTION_END             255

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5
#define DHCPNAK      6

#define DHCP_FLAGS_BROADCAST 0x8000U
#define DHCP_OPTIONS_MAX 312

typedef struct dhcp_packet {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[DHCP_OPTIONS_MAX];
} __attribute__((packed)) dhcp_packet_t;

typedef struct dhcp_client {
    dhcp_state_t state;
    uint32_t xid;
    uint32_t offered_ip;
    uint32_t server_ip;
    uint32_t subnet_mask;
    uint32_t router;
    uint32_t lease_time;
    uint32_t packets_rx;
    uint32_t packets_tx;
} dhcp_client_t;

static dhcp_client_t dhcp;

static uint16_t dhcp_htons(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}

static uint32_t dhcp_htonl(uint32_t x) {
    return ((x & 0x000000ffU) << 24) |
           ((x & 0x0000ff00U) << 8) |
           ((x & 0x00ff0000U) >> 8) |
           ((x & 0xff000000U) >> 24);
}

static uint32_t dhcp_ntohl(uint32_t x) {
    return dhcp_htonl(x);
}

static void dhcp_print_ip(uint32_t ip) {
    char text[16];
    net_format_ipv4(ip, text);
    vga_write(text);
}

static void dhcp_put_option(uint8_t *opts, uint16_t *pos, uint8_t code,
                            const uint8_t *data, uint8_t len) {
    if ((uint32_t)(*pos) + 2U + len >= DHCP_OPTIONS_MAX) return;
    opts[(*pos)++] = code;
    opts[(*pos)++] = len;
    if (len) {
        memcpy(opts + *pos, data, len);
        *pos = (uint16_t)(*pos + len);
    }
}

static void dhcp_put_u32_option(uint8_t *opts, uint16_t *pos, uint8_t code, uint32_t value) {
    uint32_t net_value = dhcp_htonl(value);
    dhcp_put_option(opts, pos, code, (const uint8_t *)&net_value, 4);
}

static void dhcp_make_base_packet(dhcp_packet_t *pkt, uint8_t msg_type) {
    net_device_t *dev = net_get_default_device();
    uint16_t pos = 0;
    uint8_t param_list[3] = { DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_ROUTER, DHCP_OPTION_LEASE_TIME };

    memset(pkt, 0, sizeof(*pkt));
    pkt->op = DHCP_BOOTREQUEST;
    pkt->htype = DHCP_HTYPE_ETH;
    pkt->hlen = DHCP_HLEN_ETH;
    pkt->xid = dhcp_htonl(dhcp.xid);
    pkt->flags = dhcp_htons(DHCP_FLAGS_BROADCAST);
    pkt->magic = dhcp_htonl(DHCP_MAGIC);
    if (dev) memcpy(pkt->chaddr, dev->mac, NET_ETH_ADDR_LEN);

    dhcp_put_option(pkt->options, &pos, DHCP_OPTION_MESSAGE_TYPE, &msg_type, 1);
    dhcp_put_option(pkt->options, &pos, DHCP_OPTION_PARAM_LIST, param_list, sizeof(param_list));
    if (pos < DHCP_OPTIONS_MAX) pkt->options[pos++] = DHCP_OPTION_END;
}

static int dhcp_send_discover(void) {
    dhcp_packet_t pkt;
    dhcp_make_base_packet(&pkt, DHCPDISCOVER);
    if (net_send_udp_broadcast(DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                               (const uint8_t *)&pkt, sizeof(pkt)) == 0) {
        dhcp.packets_tx++;
        dhcp.state = DHCP_STATE_SELECTING;
        return 0;
    }
    dhcp.state = DHCP_STATE_FAILED;
    return -1;
}

static int dhcp_send_request(void) {
    dhcp_packet_t pkt;
    uint16_t pos = 0;
    uint8_t msg_type = DHCPREQUEST;
    uint8_t param_list[3] = { DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_ROUTER, DHCP_OPTION_LEASE_TIME };

    dhcp_make_base_packet(&pkt, DHCPREQUEST);
    memset(pkt.options, 0, sizeof(pkt.options));
    dhcp_put_option(pkt.options, &pos, DHCP_OPTION_MESSAGE_TYPE, &msg_type, 1);
    dhcp_put_u32_option(pkt.options, &pos, DHCP_OPTION_REQUESTED_IP, dhcp.offered_ip);
    dhcp_put_u32_option(pkt.options, &pos, DHCP_OPTION_SERVER_ID, dhcp.server_ip);
    dhcp_put_option(pkt.options, &pos, DHCP_OPTION_PARAM_LIST, param_list, sizeof(param_list));
    if (pos < DHCP_OPTIONS_MAX) pkt.options[pos++] = DHCP_OPTION_END;

    if (net_send_udp_broadcast(DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                               (const uint8_t *)&pkt, sizeof(pkt)) == 0) {
        dhcp.packets_tx++;
        dhcp.state = DHCP_STATE_REQUESTING;
        return 0;
    }
    dhcp.state = DHCP_STATE_FAILED;
    return -1;
}

static int dhcp_parse_options(const dhcp_packet_t *pkt, uint16_t len, uint8_t *msg_type) {
    uint16_t opts_len;
    uint16_t i = 0;
    if (len <= (uint16_t)((const uint8_t *)pkt->options - (const uint8_t *)pkt)) return -1;
    opts_len = (uint16_t)(len - (uint16_t)((const uint8_t *)pkt->options - (const uint8_t *)pkt));
    *msg_type = 0;
    while (i < opts_len) {
        uint8_t code = pkt->options[i++];
        uint8_t opt_len;
        if (code == DHCP_OPTION_END) break;
        if (code == DHCP_OPTION_PAD) continue;
        if (i >= opts_len) break;
        opt_len = pkt->options[i++];
        if ((uint16_t)(i + opt_len) > opts_len) break;
        if (code == DHCP_OPTION_MESSAGE_TYPE && opt_len == 1) {
            *msg_type = pkt->options[i];
        } else if (code == DHCP_OPTION_SERVER_ID && opt_len == 4) {
            dhcp.server_ip = dhcp_ntohl(*(const uint32_t *)(pkt->options + i));
        } else if (code == DHCP_OPTION_SUBNET_MASK && opt_len == 4) {
            dhcp.subnet_mask = dhcp_ntohl(*(const uint32_t *)(pkt->options + i));
        } else if (code == DHCP_OPTION_ROUTER && opt_len >= 4) {
            dhcp.router = dhcp_ntohl(*(const uint32_t *)(pkt->options + i));
        } else if (code == DHCP_OPTION_LEASE_TIME && opt_len == 4) {
            dhcp.lease_time = dhcp_ntohl(*(const uint32_t *)(pkt->options + i));
        }
        i = (uint16_t)(i + opt_len);
    }
    return *msg_type ? 0 : -1;
}

static void dhcp_handle_packet(uint32_t src_ip, uint16_t src_port,
                               uint16_t dst_port, const uint8_t *data,
                               uint16_t len) {
    const dhcp_packet_t *pkt;
    uint8_t msg_type;
    uint32_t xid;
    net_device_t *dev;

    (void)dst_port;
    if (src_port != DHCP_SERVER_PORT || len < 240) return;
    pkt = (const dhcp_packet_t *)data;
    xid = dhcp_ntohl(pkt->xid);
    if (pkt->op != DHCP_BOOTREPLY || xid != dhcp.xid) return;
    if (dhcp_ntohl(pkt->magic) != DHCP_MAGIC) return;
    if (dhcp_parse_options(pkt, len, &msg_type) < 0) return;

    dhcp.packets_rx++;
    if (dhcp.server_ip == 0) dhcp.server_ip = src_ip;

    if (msg_type == DHCPOFFER && dhcp.state == DHCP_STATE_SELECTING) {
        dhcp.offered_ip = dhcp_ntohl(pkt->yiaddr);
        if (dhcp.offered_ip == 0) return;
        dhcp_send_request();
    } else if (msg_type == DHCPACK && dhcp.state == DHCP_STATE_REQUESTING) {
        if (dhcp.offered_ip == 0) dhcp.offered_ip = dhcp_ntohl(pkt->yiaddr);
        if (dhcp.subnet_mask == 0) dhcp.subnet_mask = NET_IP4(255, 255, 255, 0);
        dev = net_get_default_device();
        if (dev) {
            net_set_default_ipv4(dhcp.offered_ip, dhcp.subnet_mask, dhcp.router);
            dhcp.state = DHCP_STATE_BOUND;
            vga_write("dhcp: bound ");
            dhcp_print_ip(dhcp.offered_ip);
            vga_write("\n");
        }
    } else if (msg_type == DHCPNAK) {
        dhcp.state = DHCP_STATE_FAILED;
    }
}

void dhcp_init(void) {
    memset(&dhcp, 0, sizeof(dhcp));
    dhcp.state = DHCP_STATE_INIT;
    dhcp.xid = 0x4F504448U;
    net_udp_bind(DHCP_CLIENT_PORT, dhcp_handle_packet);
}

int dhcp_start(void) {
    net_device_t *dev = net_get_default_device();
    if (!dev) {
        dhcp.state = DHCP_STATE_FAILED;
        return -1;
    }
    dhcp.xid ^= ((uint32_t)dev->mac[3] << 16) | ((uint32_t)dev->mac[4] << 8) | dev->mac[5];
    dhcp.offered_ip = 0;
    dhcp.server_ip = 0;
    dhcp.subnet_mask = 0;
    dhcp.router = 0;
    dhcp.lease_time = 0;
    dev->ip = 0;
    dev->netmask = 0;
    dev->gateway = 0;
    return dhcp_send_discover();
}

dhcp_state_t dhcp_get_state(void) {
    return dhcp.state;
}

void dhcp_print_info(void) {
    vga_write("dhcp state: ");
    if (dhcp.state == DHCP_STATE_INIT) vga_write("init");
    else if (dhcp.state == DHCP_STATE_SELECTING) vga_write("selecting");
    else if (dhcp.state == DHCP_STATE_REQUESTING) vga_write("requesting");
    else if (dhcp.state == DHCP_STATE_BOUND) vga_write("bound");
    else vga_write("failed");
    vga_write("\noffered: ");
    dhcp_print_ip(dhcp.offered_ip);
    vga_write("\nserver: ");
    dhcp_print_ip(dhcp.server_ip);
    vga_write("\nrouter: ");
    dhcp_print_ip(dhcp.router);
    vga_write("\n");
}
