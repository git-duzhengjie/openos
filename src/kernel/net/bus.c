#include "bus.h"
#include "account.h"
#include "net.h"
#include "string.h"
#include "vga.h"
#include "serial.h"

#define BUS_PROTO "OBUS/1"
#define BUS_PACKET_MAX 512
#define BUS_RELIABLE_PACKET_MAX 512
#define BUS_RELIABLE_PEER_MAX DISCOVERY_MAX_PEERS


#define BUS_TOPIC_WILDCARD "*"

typedef struct bus_subscriber {
    int used;
    char topic[BUS_TOPIC_MAX];
    bus_handler_t handler;
    void *context;
} bus_subscriber_t;

typedef struct bus_shell_tap {
    int used;
    char topic[BUS_TOPIC_MAX];
    uint32_t count;
} bus_shell_tap_t;

typedef enum bus_reliable_state {
    BUS_REL_EMPTY = 0,
    BUS_REL_PENDING = 1,
    BUS_REL_ACKED = 2,
    BUS_REL_FAILED = 3
} bus_reliable_state_t;

typedef struct bus_reliable_peer {
    int used;
    int acked;
    char device_id[DISCOVERY_DEVICE_ID_MAX];
} bus_reliable_peer_t;

typedef struct bus_reliable_entry {
    bus_reliable_state_t state;
    uint16_t port;
    char msg_id[BUS_MSG_ID_MAX];
    char packet[BUS_RELIABLE_PACKET_MAX];
    uint16_t len;
    uint32_t first_sent;
    uint32_t last_sent;
    uint32_t attempts;
    uint32_t peer_count;
    bus_reliable_peer_t peers[BUS_RELIABLE_PEER_MAX];
} bus_reliable_entry_t;

typedef struct bus_reliable_seen_entry {
    int used;
    uint16_t port;
    char msg_id[BUS_MSG_ID_MAX];
    char from[DISCOVERY_DEVICE_ID_MAX];
    uint32_t seen_at;
} bus_reliable_seen_entry_t;


static bus_subscriber_t subscribers[BUS_MAX_SUBSCRIBERS];
static bus_shell_tap_t shell_tap;
static uint32_t bus_msg_counter;
static uint32_t bus_local_published;
static uint32_t bus_remote_published;
static uint32_t bus_received;
static uint32_t bus_delivered;
static uint32_t bus_duplicate_dropped;
static uint32_t bus_rejected;
static uint32_t bus_reliable_time;
static uint32_t bus_rel_sent_total;
static uint32_t bus_rel_acked_total;
static uint32_t bus_rel_peer_acked_total;
static uint32_t bus_rel_directed_total;
static uint32_t bus_rel_failed_total;
static uint32_t bus_rel_duplicate_total;
static bus_reliable_entry_t reliable_queue[BUS_RELIABLE_MAX];
static bus_reliable_seen_entry_t reliable_seen[BUS_SEEN_MAX];
static int bus_ready;

static void print_dec(uint32_t value) {
    char buf[11];
    int i = 0;
    if (value == 0) { vga_putc('0'); return; }
    while (value > 0 && i < 10) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) vga_putc(buf[--i]);
}

static void safe_copy(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void append_str(char *buf, uint32_t size, uint32_t *pos, const char *s) {
    if (!buf || !pos || !s || size == 0) return;
    while (*s && *pos + 1 < size) {
        buf[*pos] = *s;
        (*pos)++;
        s++;
    }
    buf[*pos] = '\0';
}

static void append_u32(char *buf, uint32_t size, uint32_t *pos, uint32_t value) {
    char tmp[11];
    int i = 0;
    if (value == 0) {
        append_str(buf, size, pos, "0");
        return;
    }
    while (value > 0 && i < 10) {
        tmp[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        char c[2];
        c[0] = tmp[--i];
        c[1] = '\0';
        append_str(buf, size, pos, c);
    }
}

static int is_safe_topic(const char *s) {
    uint32_t i;
    if (!s || !s[0]) return 0;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (i + 1 >= BUS_TOPIC_MAX) return 0;
        if (c == '\n' || c == '\r' || c == '=') return 0;
        if (c < 33 || c > 126) return 0;
    }
    return 1;
}

static int is_safe_payload(const char *s) {
    uint32_t i;
    if (!s) return 0;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (i + 1 >= BUS_PAYLOAD_MAX) return 0;
        if (c == '\n' || c == '\r') return 0;
        if (c < 32 || c > 126) return 0;
    }
    return 1;
}

static int get_field(const char *msg, const char *key, char *out, uint32_t out_size) {
    uint32_t key_len;
    const char *p;
    if (!msg || !key || !out || out_size == 0) return 0;
    key_len = strlen(key);
    p = msg;
    out[0] = '\0';
    while (*p) {
        const char *line = p;
        const char *line_end = p;
        uint32_t len;
        while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;
        len = (uint32_t)(line_end - line);
        if (len > key_len && memcmp(line, key, key_len) == 0 && line[key_len] == '=') {
            uint32_t value_len = len - key_len - 1;
            uint32_t copy_len = value_len;
            if (copy_len >= out_size) copy_len = out_size - 1;
            memcpy(out, line + key_len + 1, copy_len);
            out[copy_len] = '\0';
            return 1;
        }
        p = line_end;
        while (*p == '\n' || *p == '\r') p++;
    }
    return 0;
}

static int peer_is_trusted_id(const char *device_id) {
    uint32_t count = discovery_peer_count();
    uint32_t i;
    if (!device_id || !device_id[0]) return 0;
    for (i = 0; i < count; i++) {
        const discovery_peer_t *peer = discovery_peer_get(i);
        if (peer && strcmp(peer->device_id, device_id) == 0 && peer->auth_status == DISCOVERY_AUTH_TRUSTED) return 1;
    }
    return 0;
}

static int trusted_peer_ip_by_id(const char *device_id, uint32_t *out_ip) {
    uint32_t count = discovery_peer_count();
    uint32_t i;
    if (!device_id || !device_id[0] || !out_ip) return 0;
    for (i = 0; i < count; i++) {
        const discovery_peer_t *peer = discovery_peer_get(i);
        if (peer && strcmp(peer->device_id, device_id) == 0 && peer->auth_status == DISCOVERY_AUTH_TRUSTED) {
            *out_ip = peer->ip;
            return 1;
        }
    }
    return 0;
}

static int reliable_find(uint16_t port, const char *msg_id) {
    int i;
    if (!msg_id || !msg_id[0]) return -1;
    for (i = 0; i < BUS_RELIABLE_MAX; i++) {
        if (reliable_queue[i].state == BUS_REL_PENDING && reliable_queue[i].port == port && strcmp(reliable_queue[i].msg_id, msg_id) == 0) return i;
    }
    return -1;
}

static int reliable_alloc_slot(void) {
    int oldest = 0;
    int i;
    for (i = 0; i < BUS_RELIABLE_MAX; i++) {
        if (reliable_queue[i].state == BUS_REL_EMPTY || reliable_queue[i].state == BUS_REL_ACKED || reliable_queue[i].state == BUS_REL_FAILED) return i;
        if (reliable_queue[i].first_sent < reliable_queue[oldest].first_sent) oldest = i;
    }
    bus_rel_failed_total++;
    return oldest;
}

static uint32_t reliable_pending_peer_count(const bus_reliable_entry_t *entry) {
    uint32_t pending = 0;
    uint32_t i;
    if (!entry) return 0;
    for (i = 0; i < entry->peer_count && i < BUS_RELIABLE_PEER_MAX; i++) {
        if (entry->peers[i].used && !entry->peers[i].acked) pending++;
    }
    return pending;
}

static int reliable_all_peers_acked(const bus_reliable_entry_t *entry) {
    return entry && entry->peer_count > 0 && reliable_pending_peer_count(entry) == 0;
}

static void reliable_add_expected_peer(bus_reliable_entry_t *entry, const char *device_id) {
    uint32_t i;
    if (!entry || !device_id || !device_id[0]) return;
    for (i = 0; i < entry->peer_count && i < BUS_RELIABLE_PEER_MAX; i++) {
        if (entry->peers[i].used && strcmp(entry->peers[i].device_id, device_id) == 0) return;
    }
    if (entry->peer_count >= BUS_RELIABLE_PEER_MAX) return;
    entry->peers[entry->peer_count].used = 1;
    entry->peers[entry->peer_count].acked = 0;
    safe_copy(entry->peers[entry->peer_count].device_id, sizeof(entry->peers[entry->peer_count].device_id), device_id);
    entry->peer_count++;
}

static void reliable_snapshot_expected_peers(bus_reliable_entry_t *entry, const char *target) {
    uint32_t count = discovery_peer_count();
    uint32_t i;
    if (!entry) return;
    if (target && target[0]) {
        if (peer_is_trusted_id(target)) reliable_add_expected_peer(entry, target);
        return;
    }
    for (i = 0; i < count; i++) {
        const discovery_peer_t *peer = discovery_peer_get(i);
        if (peer && peer->auth_status == DISCOVERY_AUTH_TRUSTED) reliable_add_expected_peer(entry, peer->device_id);
    }
}

static int reliable_send_unicast(uint16_t port, uint32_t dst_ip, const char *packet, uint16_t len) {
    if (!packet || len == 0) return -1;
    return net_send_udp(dst_ip, port, port, (const uint8_t *)packet, len);
}

static int reliable_send_packet_to_peer(const bus_reliable_entry_t *entry, const char *peer_id) {
    uint32_t peer_ip;
    char packet[BUS_RELIABLE_PACKET_MAX];
    char current_target[DISCOVERY_DEVICE_ID_MAX];
    uint32_t pos;
    uint16_t len;
    if (!entry || !peer_id || !peer_id[0] || entry->len == 0) return -1;
    if (!trusted_peer_ip_by_id(peer_id, &peer_ip)) return -1;
    len = entry->len;
    if (len >= sizeof(packet)) len = sizeof(packet) - 1;
    memcpy(packet, entry->packet, len);
    packet[len] = '\0';
    current_target[0] = '\0';
    get_field(packet, "target", current_target, sizeof(current_target));
    if (current_target[0]) {
        if (strcmp(current_target, peer_id) != 0) return -1;
    } else {
        pos = (uint32_t)strlen(packet);
        if (pos > 0 && packet[pos - 1] != '\n') append_str(packet, sizeof(packet), &pos, "\n");
        append_str(packet, sizeof(packet), &pos, "target=");
        append_str(packet, sizeof(packet), &pos, peer_id);
        append_str(packet, sizeof(packet), &pos, "\n");
        len = (uint16_t)strlen(packet);
    }
    return reliable_send_unicast(entry->port, peer_ip, packet, len);
}

static uint32_t reliable_send_pending_peers(const bus_reliable_entry_t *entry) {
    uint32_t sent = 0;
    uint32_t i;
    if (!entry) return 0;
    for (i = 0; i < entry->peer_count && i < BUS_RELIABLE_PEER_MAX; i++) {
        if (!entry->peers[i].used || entry->peers[i].acked) continue;
        if (reliable_send_packet_to_peer(entry, entry->peers[i].device_id) == 0) sent++;
    }
    return sent;
}

static int topic_matches(const char *sub_topic, const char *topic) {
    if (!sub_topic || !topic) return 0;
    if (strcmp(sub_topic, BUS_TOPIC_WILDCARD) == 0) return 1;
    return strcmp(sub_topic, topic) == 0;
}

static int bus_deliver(const bus_message_t *message) {
    int i;
    int delivered = 0;
    if (!message) return 0;
    for (i = 0; i < BUS_MAX_SUBSCRIBERS; i++) {
        if (subscribers[i].used && subscribers[i].handler && topic_matches(subscribers[i].topic, message->topic)) {
            subscribers[i].handler(message, subscribers[i].context);
            delivered++;
        }
    }
    bus_delivered += (uint32_t)delivered;
    return delivered;
}

static void bus_make_msg_id(char *out, uint32_t out_size) {
    char local_id[DISCOVERY_DEVICE_ID_MAX];
    uint32_t pos = 0;
    discovery_get_local_device_id(local_id, sizeof(local_id));
    bus_msg_counter++;
    if (bus_msg_counter == 0) bus_msg_counter = 1;
    if (!out || out_size == 0) return;
    out[0] = '\0';
    append_str(out, out_size, &pos, local_id);
    append_str(out, out_size, &pos, "-");
    append_u32(out, out_size, &pos, bus_msg_counter);
}

static int bus_build_packet(char *packet, uint32_t packet_size, const bus_message_t *message) {
    char sealed_payload[BUS_PAYLOAD_MAX * 2 + 8];
    const char *wire_payload;
    uint32_t pos = 0;
    if (!packet || !message || packet_size == 0) return -1;
    wire_payload = message->payload;
    if (account_encrypt_field(message->payload, sealed_payload, sizeof(sealed_payload)) == 0)
        wire_payload = sealed_payload;
    packet[0] = '\0';
    append_str(packet, packet_size, &pos, BUS_PROTO "\n");
    append_str(packet, packet_size, &pos, "type=PUB\n");
    append_str(packet, packet_size, &pos, "msg_id=");
    append_str(packet, packet_size, &pos, message->msg_id);
    append_str(packet, packet_size, &pos, "\nfrom=");
    append_str(packet, packet_size, &pos, message->from);
    append_str(packet, packet_size, &pos, "\ntopic=");
    append_str(packet, packet_size, &pos, message->topic);
    append_str(packet, packet_size, &pos, "\npayload=");
    append_str(packet, packet_size, &pos, wire_payload);
    append_str(packet, packet_size, &pos, "\n");
    if (pos + 1 >= packet_size) return -1;
    return (int)pos;
}

static int bus_send_remote(const bus_message_t *message) {
    char packet[BUS_PACKET_MAX];
    int len = bus_build_packet(packet, sizeof(packet), message);
    if (len <= 0) return -1;
    if (bus_reliable_send(BUS_PORT, message->msg_id, packet, (uint16_t)len, 0) < 0) return -1;
    bus_remote_published++;
    return 0;
}

static void bus_shell_tap_handler(const bus_message_t *message, void *context) {
    bus_shell_tap_t *tap = (bus_shell_tap_t *)context;
    if (!message || !tap || !tap->used) return;
    tap->count++;
    vga_write("bus event: topic=");
    vga_write(message->topic);
    vga_write(" from=");
    vga_write(message->from);
    vga_write(" payload=");
    vga_write(message->payload);
    vga_write("\n");
}

static void bus_udp_recv(uint32_t src_ip, uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint16_t len) {
    char packet[BUS_PACKET_MAX];
    char type[16];
    bus_message_t message;
    uint16_t copy_len;
    (void)src_port;
    (void)dst_port;
    if (!data || len == 0) return;
    copy_len = len;
    if (copy_len >= sizeof(packet)) copy_len = sizeof(packet) - 1;
    memcpy(packet, data, copy_len);
    packet[copy_len] = '\0';
    if (memcmp(packet, BUS_PROTO, strlen(BUS_PROTO)) != 0) return;

    memset(&message, 0, sizeof(message));
    message.src_ip = src_ip;
    message.remote = 1;
    if (!get_field(packet, "type", type, sizeof(type))) return;
    if (!get_field(packet, "from", message.from, sizeof(message.from))) return;
    if (!peer_is_trusted_id(message.from)) {
        bus_rejected++;
        return;
    }
    if (strcmp(type, "ACK") == 0) {
        char ack_for[BUS_MSG_ID_MAX];
        if (get_field(packet, "ack_for", ack_for, sizeof(ack_for))) bus_reliable_ack(BUS_PORT, ack_for, message.from);
        return;
    }
    if (strcmp(type, "PUB") != 0) return;
    if (!get_field(packet, "msg_id", message.msg_id, sizeof(message.msg_id))) return;
    if (!get_field(packet, "topic", message.topic, sizeof(message.topic))) return;
    if (!get_field(packet, "payload", message.payload, sizeof(message.payload))) message.payload[0] = '\0';

    if (!is_safe_topic(message.topic) || !is_safe_payload(message.payload)) {
        bus_rejected++;
        return;
    }
    if (account_decrypt_field(message.payload, message.payload, sizeof(message.payload)) < 0)
        safe_copy(message.payload, sizeof(message.payload), "<e2e-decrypt-failed>");
    bus_reliable_send_ack(BUS_PORT, BUS_PROTO, message.msg_id, message.from);
    if (bus_reliable_seen_before(BUS_PORT, message.from, message.msg_id)) {
        bus_duplicate_dropped++;
        return;
    }
    bus_received++;
    bus_deliver(&message);
}

void bus_init(void) {
    memset(subscribers, 0, sizeof(subscribers));
    memset(&shell_tap, 0, sizeof(shell_tap));
    memset(reliable_queue, 0, sizeof(reliable_queue));
    memset(reliable_seen, 0, sizeof(reliable_seen));
    bus_msg_counter = 0;
    bus_local_published = 0;
    bus_remote_published = 0;
    bus_received = 0;
    bus_delivered = 0;
    bus_duplicate_dropped = 0;
    bus_rejected = 0;
    bus_reliable_time = 0;
    bus_rel_sent_total = 0;
    bus_rel_acked_total = 0;
    bus_rel_peer_acked_total = 0;
    bus_rel_directed_total = 0;
    bus_rel_failed_total = 0;
    bus_rel_duplicate_total = 0;
    bus_ready = (net_udp_bind(BUS_PORT, bus_udp_recv) == 0) ? 1 : 0;
}

int bus_subscribe(const char *topic, bus_handler_t handler, void *context) {
    int i;
    int free_slot = -1;
    if (!topic || !handler || (!is_safe_topic(topic) && strcmp(topic, BUS_TOPIC_WILDCARD) != 0)) return -1;
    for (i = 0; i < BUS_MAX_SUBSCRIBERS; i++) {
        if (subscribers[i].used && subscribers[i].handler == handler && subscribers[i].context == context && strcmp(subscribers[i].topic, topic) == 0) return 0;
        if (!subscribers[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return -1;
    subscribers[free_slot].used = 1;
    safe_copy(subscribers[free_slot].topic, sizeof(subscribers[free_slot].topic), topic);
    subscribers[free_slot].handler = handler;
    subscribers[free_slot].context = context;
    return 0;
}

int bus_unsubscribe(const char *topic, bus_handler_t handler, void *context) {
    int i;
    if (!topic || !handler) return -1;
    for (i = 0; i < BUS_MAX_SUBSCRIBERS; i++) {
        if (subscribers[i].used && subscribers[i].handler == handler && subscribers[i].context == context && strcmp(subscribers[i].topic, topic) == 0) {
            memset(&subscribers[i], 0, sizeof(subscribers[i]));
            return 0;
        }
    }
    return -1;
}

int bus_publish(const char *topic, const char *payload, uint32_t flags) {
    bus_message_t message;
    if (!bus_ready || !is_safe_topic(topic) || !is_safe_payload(payload)) return -1;
    memset(&message, 0, sizeof(message));
    bus_make_msg_id(message.msg_id, sizeof(message.msg_id));
    discovery_get_local_device_id(message.from, sizeof(message.from));
    safe_copy(message.topic, sizeof(message.topic), topic);
    safe_copy(message.payload, sizeof(message.payload), payload);
    message.remote = 0;
    message.src_ip = 0;

    if (flags & BUS_PUBLISH_LOCAL) {
        bus_local_published++;
        bus_deliver(&message);
    }
    if (flags & BUS_PUBLISH_REMOTE) {
        if (bus_send_remote(&message) < 0) return -1;
    }
    return 0;
}

void bus_print_info(void) {
    uint32_t count = 0;
    int i;
    for (i = 0; i < BUS_MAX_SUBSCRIBERS; i++) {
        if (subscribers[i].used) count++;
    }
    vga_write("bus: proto=OBUS/1 port=");
    print_dec(BUS_PORT);
    vga_write(" ready=");
    vga_write(bus_ready ? "yes" : "no");
    vga_write(" subscribers=");
    print_dec(count);
    vga_write(" e2e=");
    vga_write(account_e2e_enabled() ? "on" : "off");
    vga_write("\nlimits: topic=");
    print_dec(BUS_TOPIC_MAX);
    vga_write(" payload=");
    print_dec(BUS_PAYLOAD_MAX);
    vga_write(" seen=");
    print_dec(BUS_SEEN_MAX);
    vga_write("\n");
}

void bus_print_subscribers(void) {
    int i;
    vga_write("bus subscribers:\n");
    for (i = 0; i < BUS_MAX_SUBSCRIBERS; i++) {
        if (subscribers[i].used) {
            vga_write("  ");
            print_dec((uint32_t)i);
            vga_write(": topic=");
            vga_write(subscribers[i].topic);
            vga_write("\n");
        }
    }
}

void bus_print_stats(void) {
    vga_write("bus stats: local_pub=");
    print_dec(bus_local_published);
    vga_write(" remote_pub=");
    print_dec(bus_remote_published);
    vga_write(" recv=");
    print_dec(bus_received);
    vga_write(" delivered=");
    print_dec(bus_delivered);
    vga_write(" dup_drop=");
    print_dec(bus_duplicate_dropped);
    vga_write(" rejected=");
    print_dec(bus_rejected);
    vga_write("\n");
    if (shell_tap.used) {
        vga_write("shell_tap: topic=");
        vga_write(shell_tap.topic);
        vga_write(" count=");
        print_dec(shell_tap.count);
        vga_write("\n");
    }
}

int bus_shell_subscribe(const char *topic) {
    if (!topic || (!is_safe_topic(topic) && strcmp(topic, BUS_TOPIC_WILDCARD) != 0)) return -1;
    if (shell_tap.used) {
        bus_unsubscribe(shell_tap.topic, bus_shell_tap_handler, &shell_tap);
        memset(&shell_tap, 0, sizeof(shell_tap));
    }
    shell_tap.used = 1;
    shell_tap.count = 0;
    safe_copy(shell_tap.topic, sizeof(shell_tap.topic), topic);
    if (bus_subscribe(shell_tap.topic, bus_shell_tap_handler, &shell_tap) < 0) {
        memset(&shell_tap, 0, sizeof(shell_tap));
        return -1;
    }
    return 0;
}


int bus_reliable_send(uint16_t port, const char *msg_id, const char *packet, uint16_t len, const char *target) {
    int slot;
    if (!msg_id || !msg_id[0] || !packet || len == 0) return -1;
    if (net_send_udp_broadcast(port, port, (const uint8_t *)packet, len) < 0) return -1;
    slot = reliable_alloc_slot();
    memset(&reliable_queue[slot], 0, sizeof(reliable_queue[slot]));
    reliable_queue[slot].state = BUS_REL_PENDING;
    reliable_queue[slot].port = port;
    safe_copy(reliable_queue[slot].msg_id, sizeof(reliable_queue[slot].msg_id), msg_id);
    reliable_snapshot_expected_peers(&reliable_queue[slot], target);
    if (reliable_queue[slot].peer_count == 0) {
        reliable_queue[slot].state = BUS_REL_ACKED;
        bus_rel_acked_total++;
        return 0;
    }
    if (len >= sizeof(reliable_queue[slot].packet)) len = sizeof(reliable_queue[slot].packet) - 1;
    memcpy(reliable_queue[slot].packet, packet, len);
    reliable_queue[slot].packet[len] = '\0';
    reliable_queue[slot].len = len;
    reliable_queue[slot].first_sent = bus_reliable_time;
    reliable_queue[slot].last_sent = bus_reliable_time;
    reliable_queue[slot].attempts = 1;
    bus_rel_sent_total++;
    return 0;
}

int bus_reliable_send_ack(uint16_t port, const char *proto, const char *ack_for, const char *to) {
    char msg[BUS_RELIABLE_PACKET_MAX];
    char local_id[DISCOVERY_DEVICE_ID_MAX];
    uint32_t peer_ip;
    uint32_t pos = 0;
    if (!proto || !proto[0] || !ack_for || !ack_for[0] || !to || !to[0]) return -1;
    if (!trusted_peer_ip_by_id(to, &peer_ip)) {
        bus_rejected++;
        return -1;
    }
    discovery_get_local_device_id(local_id, sizeof(local_id));
    append_str(msg, sizeof(msg), &pos, proto);
    append_str(msg, sizeof(msg), &pos, "\n");
    append_str(msg, sizeof(msg), &pos, "type=ACK\nfrom=");
    append_str(msg, sizeof(msg), &pos, local_id);
    append_str(msg, sizeof(msg), &pos, "\nack_for=");
    append_str(msg, sizeof(msg), &pos, ack_for);
    append_str(msg, sizeof(msg), &pos, "\ntarget=");
    append_str(msg, sizeof(msg), &pos, to);
    append_str(msg, sizeof(msg), &pos, "\n");
    return reliable_send_unicast(port, peer_ip, msg, (uint16_t)strlen(msg));
}

void bus_reliable_ack(uint16_t port, const char *msg_id, const char *from) {
    int slot = reliable_find(port, msg_id);
    uint32_t i;
    if (slot < 0 || !from || !from[0]) return;
    for (i = 0; i < reliable_queue[slot].peer_count && i < BUS_RELIABLE_PEER_MAX; i++) {
        if (reliable_queue[slot].peers[i].used && strcmp(reliable_queue[slot].peers[i].device_id, from) == 0) {
            if (!reliable_queue[slot].peers[i].acked) {
                reliable_queue[slot].peers[i].acked = 1;
                bus_rel_peer_acked_total++;
                if (reliable_all_peers_acked(&reliable_queue[slot])) {
                    reliable_queue[slot].state = BUS_REL_ACKED;
                    bus_rel_acked_total++;
                }
            }
            return;
        }
    }
}

int bus_reliable_seen_before(uint16_t port, const char *from, const char *msg_id) {
    int oldest = 0;
    int i;
    if (!from || !from[0] || !msg_id || !msg_id[0]) return 0;
    for (i = 0; i < BUS_SEEN_MAX; i++) {
        if (reliable_seen[i].used && reliable_seen[i].port == port && strcmp(reliable_seen[i].from, from) == 0 && strcmp(reliable_seen[i].msg_id, msg_id) == 0) {
            reliable_seen[i].seen_at = bus_reliable_time;
            bus_rel_duplicate_total++;
            return 1;
        }
        if (!reliable_seen[i].used) oldest = i;
        else if (reliable_seen[i].seen_at < reliable_seen[oldest].seen_at) oldest = i;
    }
    memset(&reliable_seen[oldest], 0, sizeof(reliable_seen[oldest]));
    reliable_seen[oldest].used = 1;
    reliable_seen[oldest].port = port;
    safe_copy(reliable_seen[oldest].from, sizeof(reliable_seen[oldest].from), from);
    safe_copy(reliable_seen[oldest].msg_id, sizeof(reliable_seen[oldest].msg_id), msg_id);
    reliable_seen[oldest].seen_at = bus_reliable_time;
    return 0;
}

void bus_reliable_tick(uint32_t ticks) {
    int i;
    bus_reliable_time += ticks;
    for (i = 0; i < BUS_RELIABLE_MAX; i++) {
        uint32_t sent;
        if (reliable_queue[i].state != BUS_REL_PENDING) continue;
        if (reliable_all_peers_acked(&reliable_queue[i])) {
            reliable_queue[i].state = BUS_REL_ACKED;
            bus_rel_acked_total++;
            continue;
        }
        if (bus_reliable_time - reliable_queue[i].last_sent < BUS_RELIABLE_RETRY_INTERVAL) continue;
        if (reliable_queue[i].attempts >= BUS_RELIABLE_RETRY_LIMIT) {
            reliable_queue[i].state = BUS_REL_FAILED;
            bus_rel_failed_total++;
            continue;
        }
        sent = reliable_send_pending_peers(&reliable_queue[i]);
        reliable_queue[i].attempts++;
        reliable_queue[i].last_sent = bus_reliable_time;
        bus_rel_sent_total += sent;
        bus_rel_directed_total += sent;
    }
}

int bus_reliable_pending_port(uint16_t port) {
    int count = 0;
    int i;
    for (i = 0; i < BUS_RELIABLE_MAX; i++) {
        if (reliable_queue[i].state == BUS_REL_PENDING && reliable_queue[i].port == port) count++;
    }
    return count;
}

void bus_reliable_print_port(uint16_t port) {
    int i;
    int found = 0;
    vga_write("reliable port="); print_dec(port);
    vga_write(" sent="); print_dec(bus_rel_sent_total);
    vga_write(" acked="); print_dec(bus_rel_acked_total);
    vga_write(" peer_acked="); print_dec(bus_rel_peer_acked_total);
    vga_write(" directed_retry="); print_dec(bus_rel_directed_total);
    vga_write(" failed="); print_dec(bus_rel_failed_total);
    vga_write(" dup_drop="); print_dec(bus_rel_duplicate_total);
    vga_write(" pending="); print_dec((uint32_t)bus_reliable_pending_port(port));
    vga_write("\n");
    for (i = 0; i < BUS_RELIABLE_MAX; i++) {
        uint32_t p;
        if (reliable_queue[i].state != BUS_REL_PENDING || reliable_queue[i].port != port) continue;
        found = 1;
        vga_write("retry "); print_dec((uint32_t)i);
        vga_write(": msg_id="); vga_write(reliable_queue[i].msg_id);
        vga_write(" attempts="); print_dec(reliable_queue[i].attempts);
        vga_write(" peers="); print_dec(reliable_queue[i].peer_count);
        vga_write(" pending_peers="); print_dec(reliable_pending_peer_count(&reliable_queue[i]));
        vga_write(" age="); print_dec(bus_reliable_time - reliable_queue[i].first_sent);
        vga_write(" last="); print_dec(bus_reliable_time - reliable_queue[i].last_sent);
        vga_write("\n");
        for (p = 0; p < reliable_queue[i].peer_count && p < BUS_RELIABLE_PEER_MAX; p++) {
            if (!reliable_queue[i].peers[p].used) continue;
            vga_write("  peer "); print_dec(p);
            vga_write(": "); vga_write(reliable_queue[i].peers[p].device_id);
            vga_write(reliable_queue[i].peers[p].acked ? " acked\n" : " pending\n");
        }
    }
    if (!found) vga_write("reliable: no pending messages for port\n");
}
