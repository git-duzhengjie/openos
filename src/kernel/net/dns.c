/* ============================================================
 * openos - Minimal DNS client
 * Supports one in-flight A-record query over UDP/IPv4
 * ============================================================ */
#include "dns.h"
#include "dhcp.h"
#include "process.h"
#include "string.h"
#include "vga.h"

#define DNS_CLIENT_PORT 5300
#define DNS_MAX_PACKET 512
#define DNS_MAX_NAME   128
#define DNS_CACHE_SIZE 8
#define DNS_CACHE_SUCCESS_TTL_MS 300000U
#define DNS_CACHE_FAILURE_TTL_MS 30000U
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

typedef struct dns_cache_entry {
    int valid;
    int negative;
    uint32_t ip;
    uint32_t expires_ms;
    char name[DNS_MAX_NAME];
} dns_cache_entry_t;

typedef struct dns_client {
    dns_state_t state;
    uint16_t next_id;
    uint16_t active_id;
    uint32_t server_ip;
    uint32_t last_result;
    char last_name[DNS_MAX_NAME];
    uint32_t queries_tx;
    uint32_t replies_rx;
    dns_cache_entry_t cache[DNS_CACHE_SIZE];
    uint32_t cache_cursor;
    uint32_t cache_hits;
    uint32_t cache_negative_hits;
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

static void dns_print_dec(uint32_t value) {
    char text[11];
    int pos = 10;
    text[pos] = '\0';
    if (value == 0) {
        vga_write("0");
        return;
    }
    while (value && pos > 0) {
        text[--pos] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    vga_write(&text[pos]);
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

static int dns_parse_ipv4_literal(const char *name, uint32_t *out_ip) {
    uint32_t parts[4];
    uint32_t value = 0;
    uint32_t part = 0;
    int saw_digit = 0;
    const char *p;

    if (!name || !out_ip) return -1;
    p = name;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10U + (uint32_t)(*p - '0');
            if (value > 255U) return -1;
            saw_digit = 1;
        } else if (*p == '.') {
            if (!saw_digit || part >= 3U) return -1;
            parts[part++] = value;
            value = 0;
            saw_digit = 0;
        } else {
            return -1;
        }
        p++;
    }
    if (!saw_digit || part != 3U) return -1;
    parts[part] = value;
    *out_ip = NET_IP4(parts[0], parts[1], parts[2], parts[3]);
    return 0;
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

static int dns_name_equals(const char *a, const char *b) {
    uint16_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && i < DNS_MAX_NAME) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static dns_cache_entry_t *dns_find_cache(const char *name, uint32_t now_ms) {
    uint32_t i;
    for (i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache_entry_t *entry = &dns.cache[i];
        if (!entry->valid) continue;
        if ((int32_t)(entry->expires_ms - now_ms) <= 0) {
            entry->valid = 0;
            continue;
        }
        if (dns_name_equals(entry->name, name)) return entry;
    }
    return 0;
}

static void dns_store_cache(const char *name, uint32_t ip, int negative) {
    uint32_t now_ms = sched_time_ms();
    uint32_t i;
    dns_cache_entry_t *entry = 0;

    for (i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns.cache[i].valid && dns_name_equals(dns.cache[i].name, name)) {
            entry = &dns.cache[i];
            break;
        }
    }
    if (!entry) {
        entry = &dns.cache[dns.cache_cursor % DNS_CACHE_SIZE];
        dns.cache_cursor++;
    }
    memset(entry, 0, sizeof(*entry));
    entry->valid = 1;
    entry->negative = negative ? 1 : 0;
    entry->ip = negative ? 0 : ip;
    entry->expires_ms = now_ms + (negative ? DNS_CACHE_FAILURE_TTL_MS : DNS_CACHE_SUCCESS_TTL_MS);
    dns_copy_name(entry->name, name);
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
            dns_store_cache(dns.last_name, dns.last_result, 0);
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
        vga_write("dns: response parse failed from ");
        dns_print_ip(src_ip);
        vga_write("\n");
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
    net_device_t *dev = net_get_default_device();
    uint32_t dhcp_dns;

    if (dev && dev->dns != 0) return dev->dns;

    dhcp_dns = dhcp_get_dns_server();
    if (dhcp_dns != 0) return dhcp_dns;
    return dns.server_ip;
}

uint32_t dns_get_last_result(void) {
    return dns.last_result;
}

dns_state_t dns_get_state(void) {
    return dns.state;
}

uint32_t dns_get_cache_hits(void) {
    return dns.cache_hits;
}

uint32_t dns_get_cache_negative_hits(void) {
    return dns.cache_negative_hits;
}

int dns_query_a(const char *name) {
    uint8_t packet[DNS_MAX_PACKET];
    dns_header_t *hdr;
    uint16_t pos;
    uint16_t qtype;
    uint16_t qclass;
    uint32_t server;

    if (dns_name_len(name) <= 0) return -1;
    dns_copy_name(dns.last_name, name);
    {
        dns_cache_entry_t *entry = dns_find_cache(name, sched_time_ms());
        if (entry) {
            if (entry->negative) {
                dns.last_result = 0;
                dns.state = DNS_STATE_FAILED;
                dns.cache_negative_hits++;
                return -1;
            }
            dns.last_result = entry->ip;
            dns.state = DNS_STATE_RESOLVED;
            dns.cache_hits++;
            return 0;
        }
    }
    if (dns_parse_ipv4_literal(name, &dns.last_result) == 0) {
        dns_store_cache(name, dns.last_result, 0);
        dns.state = DNS_STATE_RESOLVED;
        return 0;
    }
    memset(packet, 0, sizeof(packet));
    dns.active_id = ++dns.next_id;
    dns.last_result = 0;
    dns.state = DNS_STATE_QUERYING;

    hdr = (dns_header_t *)packet;
    hdr->id = dns_htons(dns.active_id);
    hdr->flags = dns_htons(DNS_FLAG_QUERY);
    hdr->qdcount = dns_htons(1);
    pos = sizeof(dns_header_t);
    if (dns_encode_name(name, packet, &pos, sizeof(packet)) < 0) {
        dns_store_cache(name, 0, 1);
        dns.state = DNS_STATE_FAILED;
        return -1;
    }
    if ((uint16_t)(pos + 4U) > sizeof(packet)) {
        dns_store_cache(name, 0, 1);
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
        vga_write("dns: send query failed server=");
        dns_print_ip(server);
        vga_write("\n");
        dns_store_cache(name, 0, 1);
        dns.state = DNS_STATE_FAILED;
        return -1;
    }
    dns.queries_tx++;
    return 0;
}

void dns_mark_failed(void) {
    if (dns.last_name[0])
        dns_store_cache(dns.last_name, 0, 1);
    dns.last_result = 0;
    dns.state = DNS_STATE_FAILED;
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
    vga_write("\ncache hits: ");
    dns_print_dec(dns.cache_hits);
    vga_write(" negative hits: ");
    dns_print_dec(dns.cache_negative_hits);
    vga_write("\n");
}
