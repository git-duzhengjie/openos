/* ============================================================
 * openos - Minimal DNS client
 * Supports one in-flight A-record query over UDP/IPv4
 * ============================================================ */
#include "dns.h"
#include "dhcp.h"
#include "string.h"
#include "vga.h"

#define DNS_CLIENT_PORT 5300
#define DNS_MAX_PACKET 512
#define DNS_MAX_NAME   128
#define DNS_TYPE_A     1
#define DNS_CLASS_IN   1
#define DNS_FLAG_QUERY 0x0100
#define DNS_FLAG_RCODE_MASK 0x000F

typedef struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

typedef struct dns_client {
    dns_state_t state;
    uint16_t next_id;
    uint16_t active_id;
    uint32_t server_ip;
    uint32_t last_result;
    char last_name[DNS_MAX_NAME];
    uint32_t queries_tx;
    uint32_t replies_rx;
} dns_client_t;

static dns_client_t dns;

static uint16_t dns_htons(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}

static uint32_t dns_htonl(uint32_t x) {
    return ((x & 0x000000ffU) << 24) |
           ((x & 0x0000ff00U) << 8) |
           ((x & 0x00ff0000U) >> 8) |
           ((x & 0xff000000U) >> 24);
}

static uint16_t dns_ntohs(uint16_t x) {
    return dns_htons(x);
}

static uint32_t dns_ntohl(uint32_t x) {
    return dns_htonl(x);
}

static void dns_print_ip(uint32_t ip) {
    char text[16];
    net_format_ipv4(ip, text);
    vga_write(text);
}

static int dns_name_len(const char *name) {
    int len = 0;
    if (!name || !name[0]) return -1;
    while (name[len]) {
        if (len >= DNS_MAX_NAME - 1) return -1;
        len++;
    }
    return len;
}

static int dns_encode_name(const char *name, uint8_t *out, uint16_t *pos, uint16_t max) {
    uint16_t label_start = 0;
    uint16_t label_len = 0;
    uint16_t i = 0;
    int total_len = dns_name_len(name);
    if (total_len <= 0) return -1;
    while (1) {
        char c = name[i];
        if (c == '.' || c == '\0') {
            if (label_len == 0 || label_len > 63) return -1;
            if ((uint32_t)(*pos) + 1U + label_len >= max) return -1;
            out[(*pos)++] = (uint8_t)label_len;
            memcpy(out + *pos, name + label_start, label_len);
            *pos = (uint16_t)(*pos + label_len);
            if (c == '\0') break;
            label_start = (uint16_t)(i + 1U);
            label_len = 0;
        } else {
            label_len++;
        }
        i++;
    }
    if (*pos >= max) return -1;
    out[(*pos)++] = 0;
    return 0;
}

static int dns_skip_name(const uint8_t *packet, uint16_t len, uint16_t *pos) {
    uint16_t p = *pos;
    uint16_t guard = 0;
    while (p < len && guard++ < len) {
        uint8_t c = packet[p++];
        if (c == 0) {
            *pos = p;
            return 0;
        }
        if ((c & 0xC0U) == 0xC0U) {
            if (p >= len) return -1;
            p++;
            *pos = p;
            return 0;
        }
        if ((c & 0xC0U) != 0) return -1;
        if ((uint16_t)(p + c) > len) return -1;
        p = (uint16_t)(p + c);
    }
    return -1;
}

static void dns_copy_name(char *dst, const char *src) {
    uint16_t i = 0;
    while (src && src[i] && i + 1U < DNS_MAX_NAME) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int dns_parse_answer(const uint8_t *packet, uint16_t len) {
    const dns_header_t *hdr;
    uint16_t qd;
    uint16_t an;
    uint16_t i;
    uint16_t pos;

    if (len < sizeof(dns_header_t)) return -1;
    hdr = (const dns_header_t *)packet;
    if (dns_ntohs(hdr->id) != dns.active_id) return -1;
    if ((dns_ntohs(hdr->flags) & DNS_FLAG_RCODE_MASK) != 0) return -1;

    qd = dns_ntohs(hdr->qdcount);
    an = dns_ntohs(hdr->ancount);
    pos = sizeof(dns_header_t);

    for (i = 0; i < qd; i++) {
        if (dns_skip_name(packet, len, &pos) < 0) return -1;
        if ((uint16_t)(pos + 4U) > len) return -1;
        pos = (uint16_t)(pos + 4U);
    }

    for (i = 0; i < an; i++) {
        uint16_t type;
        uint16_t klass;
        uint16_t rdlen;
        if (dns_skip_name(packet, len, &pos) < 0) return -1;
        if ((uint16_t)(pos + 10U) > len) return -1;
        type = dns_ntohs(*(const uint16_t *)(packet + pos));
        klass = dns_ntohs(*(const uint16_t *)(packet + pos + 2U));
        rdlen = dns_ntohs(*(const uint16_t *)(packet + pos + 8U));
        pos = (uint16_t)(pos + 10U);
        if ((uint16_t)(pos + rdlen) > len) return -1;
        if (type == DNS_TYPE_A && klass == DNS_CLASS_IN && rdlen == 4U) {
            dns.last_result = dns_ntohl(*(const uint32_t *)(packet + pos));
            dns.state = DNS_STATE_RESOLVED;
            return 0;
        }
        pos = (uint16_t)(pos + rdlen);
    }
    return -1;
}

static void dns_handle_udp(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                           const uint8_t *data, uint16_t len) {
    (void)dst_port;
    if (src_port != DNS_PORT || len > DNS_MAX_PACKET) return;
    if (dns.server_ip != 0 && src_ip != dns.server_ip) return;
    dns.replies_rx++;
    if (dns_parse_answer(data, len) == 0) {
        vga_write("dns: ");
        vga_write(dns.last_name);
        vga_write(" -> ");
        dns_print_ip(dns.last_result);
        vga_write("\n");
    } else {
        dns.state = DNS_STATE_FAILED;
    }
}

void dns_init(void) {
    memset(&dns, 0, sizeof(dns));
    dns.state = DNS_STATE_IDLE;
    dns.next_id = 0x4F53;
    dns.server_ip = DNS_DEFAULT_SERVER;
    net_udp_bind(DNS_CLIENT_PORT, dns_handle_udp);
}

void dns_set_server(uint32_t server_ip) {
    if (server_ip != 0) dns.server_ip = server_ip;
}

uint32_t dns_get_server(void) {
    uint32_t dhcp_dns = dhcp_get_dns_server();
    if (dhcp_dns != 0) return dhcp_dns;
    return dns.server_ip;
}

uint32_t dns_get_last_result(void) {
    return dns.last_result;
}

dns_state_t dns_get_state(void) {
    return dns.state;
}

int dns_query_a(const char *name) {
    uint8_t packet[DNS_MAX_PACKET];
    dns_header_t *hdr;
    uint16_t pos;
    uint16_t qtype;
    uint16_t qclass;
    uint32_t server;

    if (dns_name_len(name) <= 0) return -1;
    memset(packet, 0, sizeof(packet));
    dns.active_id = ++dns.next_id;
    dns.last_result = 0;
    dns_copy_name(dns.last_name, name);
    dns.state = DNS_STATE_QUERYING;

    hdr = (dns_header_t *)packet;
    hdr->id = dns_htons(dns.active_id);
    hdr->flags = dns_htons(DNS_FLAG_QUERY);
    hdr->qdcount = dns_htons(1);
    pos = sizeof(dns_header_t);
    if (dns_encode_name(name, packet, &pos, sizeof(packet)) < 0) {
        dns.state = DNS_STATE_FAILED;
        return -1;
    }
    if ((uint16_t)(pos + 4U) > sizeof(packet)) {
        dns.state = DNS_STATE_FAILED;
        return -1;
    }
    qtype = dns_htons(DNS_TYPE_A);
    qclass = dns_htons(DNS_CLASS_IN);
    memcpy(packet + pos, &qtype, 2);
    pos = (uint16_t)(pos + 2U);
    memcpy(packet + pos, &qclass, 2);
    pos = (uint16_t)(pos + 2U);

    server = dns_get_server();
    dns.server_ip = server;
    if (server == 0 || net_send_udp(server, DNS_CLIENT_PORT, DNS_PORT, packet, pos) < 0) {
        dns.state = DNS_STATE_FAILED;
        return -1;
    }
    dns.queries_tx++;
    return 0;
}

void dns_print_info(void) {
    vga_write("dns state: ");
    if (dns.state == DNS_STATE_IDLE) vga_write("idle");
    else if (dns.state == DNS_STATE_QUERYING) vga_write("querying");
    else if (dns.state == DNS_STATE_RESOLVED) vga_write("resolved");
    else vga_write("failed");
    vga_write("\nserver: ");
    dns_print_ip(dns_get_server());
    vga_write("\nlast: ");
    vga_write(dns.last_name[0] ? dns.last_name : "-");
    vga_write(" -> ");
    dns_print_ip(dns.last_result);
    vga_write("\n");
}
