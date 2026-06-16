#include "sync.h"
#include "account.h"
#include "net.h"
#include "discovery.h"
#include "bus.h"
#include "string.h"
#include "vga.h"
#include "serial.h"

#define SYNC_PROTO "OSYNC/1"
#define SYNC_MSG_PUT "PUT"
#define SYNC_MSG_DEL "DEL"
#define SYNC_MSG_TASK_OFFER "TASK_OFFER"
#define SYNC_MSG_TASK_ACCEPT "TASK_ACCEPT"
#define SYNC_MSG_TASK_DONE "TASK_DONE"
#define SYNC_MSG_ACK "ACK"
#define SYNC_DEFAULT_TTL 300U
#define SYNC_MSG_ID_MAX 40
#define SYNC_PACKET_MAX 512

static sync_item_t items[SYNC_MAX_ITEMS];
static sync_task_t tasks[SYNC_MAX_TASKS];
static uint32_t sync_time;
static uint32_t local_version_counter;
static uint32_t local_task_counter;
static uint32_t local_msg_counter;
static int sync_ready;

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

static int is_safe_text(const char *s) {
    uint32_t i;
    if (!s) return 0;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n' || c == '\r' || c == '=') return 0;
        if (c < 32 || c > 126) return 0;
    }
    return 1;
}

static int make_prefixed_key(char *out, uint32_t out_size, const char *prefix, const char *name) {
    uint32_t pos = 0;
    if (!out || out_size == 0 || !prefix || !name || !name[0]) return -1;
    if (!is_safe_text(name)) return -1;
    out[0] = '\0';
    append_str(out, out_size, &pos, prefix);
    append_str(out, out_size, &pos, name);
    if (pos + 1 >= out_size) return -1;
    return 0;
}

static const char *task_state_name(sync_task_state_t state) {
    switch (state) {
        case SYNC_TASK_OFFERED: return "offered";
        case SYNC_TASK_ACCEPTED: return "accepted";
        case SYNC_TASK_DONE: return "done";
        default: return "empty";
    }
}

static int parse_u32(const char *s, uint32_t *out) {
    uint32_t value = 0;
    uint32_t i = 0;
    if (!s || !s[0] || !out) return -1;
    while (s[i]) {
        if (s[i] < '0' || s[i] > '9') return -1;
        value = value * 10U + (uint32_t)(s[i] - '0');
        i++;
    }
    *out = value;
    return 0;
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

static int find_item_slot(const char *key) {
    int free_slot = -1;
    int i;
    for (i = 0; i < SYNC_MAX_ITEMS; i++) {
        if (items[i].used && strcmp(items[i].key, key) == 0) return i;
        if (!items[i].used && free_slot < 0) free_slot = i;
    }
    return free_slot;
}

static int find_task_slot(const char *task_id) {
    int free_slot = -1;
    int i;
    for (i = 0; i < SYNC_MAX_TASKS; i++) {
        if (tasks[i].used && strcmp(tasks[i].task_id, task_id) == 0) return i;
        if (!tasks[i].used && free_slot < 0) free_slot = i;
    }
    return free_slot;
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

static int src_is_trusted(uint32_t src_ip, const char *from) {
    uint32_t count = discovery_peer_count();
    uint32_t i;
    if (!from || !from[0]) return 0;
    for (i = 0; i < count; i++) {
        const discovery_peer_t *peer = discovery_peer_get(i);
        if (peer && strcmp(peer->device_id, from) == 0 && peer->auth_status == DISCOVERY_AUTH_TRUSTED) {
            if (peer->ip == 0 || src_ip == 0 || peer->ip == src_ip) return 1;
        }
    }
    return 0;
}

static void make_msg_id(char *out, uint32_t out_size) {
    char local_id[DISCOVERY_DEVICE_ID_MAX];
    uint32_t pos = 0;
    if (!out || out_size == 0) return;
    discovery_get_local_device_id(local_id, sizeof(local_id));
    local_msg_counter++;
    append_str(out, out_size, &pos, local_id);
    append_str(out, out_size, &pos, "-");
    append_u32(out, out_size, &pos, local_msg_counter);
}

static int send_packet(const char *type, const char *key, const char *value,
                       const char *task_id, const char *title,
                       const char *payload, const char *target,
                       const char *result, uint32_t version,
                       sync_task_state_t state) {
    char msg[SYNC_PACKET_MAX];
    char local_id[DISCOVERY_DEVICE_ID_MAX];
    char msg_id[SYNC_MSG_ID_MAX];
    char sealed_value[SYNC_ITEM_VALUE_MAX * 2 + 8];
    char sealed_title[SYNC_TASK_TEXT_MAX * 2 + 8];
    char sealed_payload[SYNC_TASK_TEXT_MAX * 2 + 8];
    char sealed_result[SYNC_TASK_TEXT_MAX * 2 + 8];
    const char *wire_value = value;
    const char *wire_title = title;
    const char *wire_payload = payload;
    const char *wire_result = result;
    uint32_t pos = 0;
    uint16_t len;
    if (!sync_ready || !type) return -1;
    if (value && account_encrypt_field(value, sealed_value, sizeof(sealed_value)) == 0) wire_value = sealed_value;
    if (title && account_encrypt_field(title, sealed_title, sizeof(sealed_title)) == 0) wire_title = sealed_title;
    if (payload && account_encrypt_field(payload, sealed_payload, sizeof(sealed_payload)) == 0) wire_payload = sealed_payload;
    if (result && account_encrypt_field(result, sealed_result, sizeof(sealed_result)) == 0) wire_result = sealed_result;
    discovery_get_local_device_id(local_id, sizeof(local_id));
    make_msg_id(msg_id, sizeof(msg_id));
    append_str(msg, sizeof(msg), &pos, SYNC_PROTO "\n");
    append_str(msg, sizeof(msg), &pos, "type="); append_str(msg, sizeof(msg), &pos, type); append_str(msg, sizeof(msg), &pos, "\n");
    append_str(msg, sizeof(msg), &pos, "msg_id="); append_str(msg, sizeof(msg), &pos, msg_id); append_str(msg, sizeof(msg), &pos, "\n");
    append_str(msg, sizeof(msg), &pos, "from="); append_str(msg, sizeof(msg), &pos, local_id); append_str(msg, sizeof(msg), &pos, "\n");
    append_str(msg, sizeof(msg), &pos, "ttl="); append_u32(msg, sizeof(msg), &pos, SYNC_DEFAULT_TTL); append_str(msg, sizeof(msg), &pos, "\n");
    if (key) { append_str(msg, sizeof(msg), &pos, "key="); append_str(msg, sizeof(msg), &pos, key); append_str(msg, sizeof(msg), &pos, "\n"); }
    if (value) { append_str(msg, sizeof(msg), &pos, "value="); append_str(msg, sizeof(msg), &pos, value); append_str(msg, sizeof(msg), &pos, "\n"); }
    if (task_id) { append_str(msg, sizeof(msg), &pos, "task_id="); append_str(msg, sizeof(msg), &pos, task_id); append_str(msg, sizeof(msg), &pos, "\n"); }
    if (title) { append_str(msg, sizeof(msg), &pos, "title="); append_str(msg, sizeof(msg), &pos, title); append_str(msg, sizeof(msg), &pos, "\n"); }
    if (payload) { append_str(msg, sizeof(msg), &pos, "payload="); append_str(msg, sizeof(msg), &pos, payload); append_str(msg, sizeof(msg), &pos, "\n"); }
    if (target) { append_str(msg, sizeof(msg), &pos, "target="); append_str(msg, sizeof(msg), &pos, target); append_str(msg, sizeof(msg), &pos, "\n"); }
    if (result) { append_str(msg, sizeof(msg), &pos, "result="); append_str(msg, sizeof(msg), &pos, result); append_str(msg, sizeof(msg), &pos, "\n"); }
    if (version > 0) { append_str(msg, sizeof(msg), &pos, "version="); append_u32(msg, sizeof(msg), &pos, version); append_str(msg, sizeof(msg), &pos, "\n"); }
    if (state != SYNC_TASK_EMPTY) { append_str(msg, sizeof(msg), &pos, "state="); append_str(msg, sizeof(msg), &pos, task_state_name(state)); append_str(msg, sizeof(msg), &pos, "\n"); }
    len = (uint16_t)strlen(msg);
    return bus_reliable_send(SYNC_PORT, msg_id, msg, len, target);
}

static void apply_put(const char *from, const char *key, const char *value, uint32_t version) {
    char plain_value[SYNC_ITEM_VALUE_MAX];
    int slot;
    if (!key || !key[0] || !value || !is_safe_text(key) || !is_safe_text(value)) return;
    if (account_decrypt_field(value, plain_value, sizeof(plain_value)) < 0)
        safe_copy(plain_value, sizeof(plain_value), "<e2e-decrypt-failed>");
    slot = find_item_slot(key);
    if (slot < 0) return;
    if (items[slot].used && version <= items[slot].version) return;
    memset(&items[slot], 0, sizeof(items[slot]));
    items[slot].used = 1;
    safe_copy(items[slot].key, sizeof(items[slot].key), key);
    safe_copy(items[slot].value, sizeof(items[slot].value), plain_value);
    safe_copy(items[slot].owner, sizeof(items[slot].owner), from);
    items[slot].version = version;
    items[slot].updated_at = sync_time;
}

static void apply_del(const char *key, uint32_t version) {
    int slot;
    if (!key || !key[0]) return;
    slot = find_item_slot(key);
    if (slot < 0 || !items[slot].used) return;
    if (version < items[slot].version) return;
    memset(&items[slot], 0, sizeof(items[slot]));
}

static void apply_task_offer(const char *from, const char *task_id, const char *title,
                             const char *payload, const char *target) {
    char local_id[DISCOVERY_DEVICE_ID_MAX];
    char plain_title[SYNC_TASK_TEXT_MAX];
    char plain_payload[SYNC_TASK_TEXT_MAX];
    int slot;
    if (!task_id || !task_id[0] || !title || !is_safe_text(task_id) || !is_safe_text(title)) return;
    if (account_decrypt_field(title, plain_title, sizeof(plain_title)) < 0)
        safe_copy(plain_title, sizeof(plain_title), "<e2e-decrypt-failed>");
    if (payload && payload[0] && account_decrypt_field(payload, plain_payload, sizeof(plain_payload)) < 0)
        safe_copy(plain_payload, sizeof(plain_payload), "<e2e-decrypt-failed>");
    else
        safe_copy(plain_payload, sizeof(plain_payload), payload);
    discovery_get_local_device_id(local_id, sizeof(local_id));
    if (target && target[0] && strcmp(target, local_id) != 0) return;
    slot = find_task_slot(task_id);
    if (slot < 0) return;
    if (tasks[slot].used && tasks[slot].state == SYNC_TASK_DONE) return;
    memset(&tasks[slot], 0, sizeof(tasks[slot]));
    tasks[slot].used = 1;
    safe_copy(tasks[slot].task_id, sizeof(tasks[slot].task_id), task_id);
    safe_copy(tasks[slot].title, sizeof(tasks[slot].title), plain_title);
    safe_copy(tasks[slot].payload, sizeof(tasks[slot].payload), plain_payload);
    safe_copy(tasks[slot].owner, sizeof(tasks[slot].owner), from);
    safe_copy(tasks[slot].assignee, sizeof(tasks[slot].assignee), target && target[0] ? target : local_id);
    tasks[slot].state = SYNC_TASK_OFFERED;
    tasks[slot].updated_at = sync_time;
}

static void apply_task_accept(const char *from, const char *task_id) {
    int slot;
    if (!task_id || !task_id[0]) return;
    slot = find_task_slot(task_id);
    if (slot < 0 || !tasks[slot].used) return;
    if (tasks[slot].state == SYNC_TASK_DONE) return;
    safe_copy(tasks[slot].assignee, sizeof(tasks[slot].assignee), from);
    tasks[slot].state = SYNC_TASK_ACCEPTED;
    tasks[slot].updated_at = sync_time;
}

static void apply_task_done(const char *from, const char *task_id, const char *result) {
    char plain_result[SYNC_TASK_TEXT_MAX];
    int slot;
    if (!task_id || !task_id[0]) return;
    if (result && result[0] && account_decrypt_field(result, plain_result, sizeof(plain_result)) < 0)
        safe_copy(plain_result, sizeof(plain_result), "<e2e-decrypt-failed>");
    else
        safe_copy(plain_result, sizeof(plain_result), result);
    slot = find_task_slot(task_id);
    if (slot < 0 || !tasks[slot].used) return;
    safe_copy(tasks[slot].assignee, sizeof(tasks[slot].assignee), from);
    safe_copy(tasks[slot].result, sizeof(tasks[slot].result), plain_result);
    tasks[slot].state = SYNC_TASK_DONE;
    tasks[slot].updated_at = sync_time;
}

static void handle_sync_packet(uint32_t src_ip, const uint8_t *data, uint16_t len) {
    char msg[513];
    char type[24];
    char msg_id[SYNC_MSG_ID_MAX];
    char ack_for[SYNC_MSG_ID_MAX];
    char from[DISCOVERY_DEVICE_ID_MAX];
    char key[SYNC_ITEM_KEY_MAX];
    char value[SYNC_ITEM_VALUE_MAX];
    char version_text[16];
    char task_id[SYNC_TASK_ID_MAX];
    char title[SYNC_TASK_TEXT_MAX];
    char payload[SYNC_TASK_TEXT_MAX];
    char target[DISCOVERY_DEVICE_ID_MAX];
    char result[SYNC_TASK_TEXT_MAX];
    uint32_t version = 0;
    if (!data || len == 0 || len >= sizeof(msg)) return;
    memcpy(msg, data, len);
    msg[len] = '\0';
    if (memcmp(msg, SYNC_PROTO, strlen(SYNC_PROTO)) != 0) return;
    if (!get_field(msg, "type", type, sizeof(type))) return;
    if (!get_field(msg, "from", from, sizeof(from))) return;
    if (!src_is_trusted(src_ip, from)) return;
    msg_id[0] = '\0';
    ack_for[0] = '\0';
    get_field(msg, "msg_id", msg_id, sizeof(msg_id));
    get_field(msg, "ack_for", ack_for, sizeof(ack_for));
    get_field(msg, "target", target, sizeof(target));
    if (target[0]) {
        char local_id[DISCOVERY_DEVICE_ID_MAX];
        discovery_get_local_device_id(local_id, sizeof(local_id));
        if (strcmp(target, local_id) != 0) return;
    }
    if (strcmp(type, SYNC_MSG_ACK) == 0) {
        if (ack_for[0]) bus_reliable_ack(SYNC_PORT, ack_for, from);
        return;
    }
    if (msg_id[0]) {
        bus_reliable_send_ack(SYNC_PORT, SYNC_PROTO, msg_id, from);
        if (bus_reliable_seen_before(SYNC_PORT, from, msg_id)) return;
    }
    if (get_field(msg, "version", version_text, sizeof(version_text))) parse_u32(version_text, &version);
    key[0] = '\0'; value[0] = '\0'; task_id[0] = '\0'; title[0] = '\0'; payload[0] = '\0'; target[0] = '\0'; result[0] = '\0';
    get_field(msg, "key", key, sizeof(key));
    get_field(msg, "value", value, sizeof(value));
    get_field(msg, "task_id", task_id, sizeof(task_id));
    get_field(msg, "title", title, sizeof(title));
    get_field(msg, "payload", payload, sizeof(payload));
    get_field(msg, "target", target, sizeof(target));
    get_field(msg, "result", result, sizeof(result));

    if (strcmp(type, SYNC_MSG_PUT) == 0) apply_put(from, key, value, version);
    else if (strcmp(type, SYNC_MSG_DEL) == 0) apply_del(key, version);
    else if (strcmp(type, SYNC_MSG_TASK_OFFER) == 0) apply_task_offer(from, task_id, title, payload, target);
    else if (strcmp(type, SYNC_MSG_TASK_ACCEPT) == 0) apply_task_accept(from, task_id);
    else if (strcmp(type, SYNC_MSG_TASK_DONE) == 0) apply_task_done(from, task_id, result);
}

static void sync_udp_recv(uint32_t src_ip, uint16_t src_port,
                          uint16_t dst_port, const uint8_t *data,
                          uint16_t len) {
    (void)src_port;
    (void)dst_port;
    handle_sync_packet(src_ip, data, len);
}

void sync_init(void) {
    memset(items, 0, sizeof(items));
    memset(tasks, 0, sizeof(tasks));
    sync_time = 0;
    local_version_counter = 1;
    local_task_counter = 1;
    local_msg_counter = 1;
    sync_ready = (net_udp_bind(SYNC_PORT, sync_udp_recv) == 0) ? 1 : 0;
}

int sync_put(const char *key, const char *value) {
    char local_id[DISCOVERY_DEVICE_ID_MAX];
    uint32_t version;
    if (!key || !key[0] || !value || !is_safe_text(key) || !is_safe_text(value)) return -1;
    version = local_version_counter++;
    discovery_get_local_device_id(local_id, sizeof(local_id));
    apply_put(local_id, key, value, version);
    return send_packet(SYNC_MSG_PUT, key, value, 0, 0, 0, 0, 0, version, SYNC_TASK_EMPTY);
}

int sync_delete(const char *key) {
    uint32_t version;
    if (!key || !key[0] || !is_safe_text(key)) return -1;
    version = local_version_counter++;
    apply_del(key, version);
    return send_packet(SYNC_MSG_DEL, key, 0, 0, 0, 0, 0, 0, version, SYNC_TASK_EMPTY);
}

int sync_file_put(const char *path, const char *content) {
    char key[SYNC_ITEM_KEY_MAX];
    if (!content || !is_safe_text(content)) return -1;
    if (make_prefixed_key(key, sizeof(key), "file:", path) < 0) return -1;
    return sync_put(key, content);
}

int sync_file_delete(const char *path) {
    char key[SYNC_ITEM_KEY_MAX];
    if (make_prefixed_key(key, sizeof(key), "file:", path) < 0) return -1;
    return sync_delete(key);
}

int sync_clipboard_set(const char *text) {
    if (!text || !is_safe_text(text)) return -1;
    return sync_put("clip:default", text);
}

int sync_message_send(const char *channel, const char *message) {
    char key[SYNC_ITEM_KEY_MAX];
    if (!message || !is_safe_text(message)) return -1;
    if (make_prefixed_key(key, sizeof(key), "msg:", channel) < 0) return -1;
    return sync_put(key, message);
}

int sync_broadcast_key(const char *key) {
    int slot;
    if (!key || !key[0]) return -1;
    slot = find_item_slot(key);
    if (slot < 0 || !items[slot].used) return -1;
    return send_packet(SYNC_MSG_PUT, items[slot].key, items[slot].value, 0, 0, 0, 0, 0, items[slot].version, SYNC_TASK_EMPTY);
}

int sync_broadcast_all(void) {
    int sent = 0;
    int i;
    for (i = 0; i < SYNC_MAX_ITEMS; i++) {
        if (items[i].used && sync_broadcast_key(items[i].key) == 0) sent++;
    }
    return sent;
}

int sync_task_offer(const char *task_id, const char *title, const char *payload, const char *target_device_id) {
    char generated[SYNC_TASK_ID_MAX];
    char local_id[DISCOVERY_DEVICE_ID_MAX];
    const char *actual_id = task_id;
    int slot;
    uint32_t pos = 0;
    if (!title || !title[0] || !is_safe_text(title) || (payload && !is_safe_text(payload))) return -1;
    if (target_device_id && target_device_id[0] && !peer_is_trusted_id(target_device_id)) return -1;
    if (!actual_id || !actual_id[0]) {
        discovery_get_local_device_id(local_id, sizeof(local_id));
        generated[0] = '\0';
        append_str(generated, sizeof(generated), &pos, "task-");
        append_u32(generated, sizeof(generated), &pos, local_task_counter++);
        actual_id = generated;
    }
    if (!is_safe_text(actual_id)) return -1;
    slot = find_task_slot(actual_id);
    if (slot < 0) return -1;
    discovery_get_local_device_id(local_id, sizeof(local_id));
    memset(&tasks[slot], 0, sizeof(tasks[slot]));
    tasks[slot].used = 1;
    safe_copy(tasks[slot].task_id, sizeof(tasks[slot].task_id), actual_id);
    safe_copy(tasks[slot].title, sizeof(tasks[slot].title), title);
    safe_copy(tasks[slot].payload, sizeof(tasks[slot].payload), payload);
    safe_copy(tasks[slot].owner, sizeof(tasks[slot].owner), local_id);
    safe_copy(tasks[slot].assignee, sizeof(tasks[slot].assignee), target_device_id ? target_device_id : "");
    tasks[slot].state = SYNC_TASK_OFFERED;
    tasks[slot].updated_at = sync_time;
    return send_packet(SYNC_MSG_TASK_OFFER, 0, 0, actual_id, title, payload, target_device_id, 0, 0, SYNC_TASK_OFFERED);
}

int sync_task_accept(const char *task_id) {
    char local_id[DISCOVERY_DEVICE_ID_MAX];
    int slot;
    if (!task_id || !task_id[0]) return -1;
    slot = find_task_slot(task_id);
    if (slot < 0 || !tasks[slot].used || tasks[slot].state != SYNC_TASK_OFFERED) return -1;
    discovery_get_local_device_id(local_id, sizeof(local_id));
    safe_copy(tasks[slot].assignee, sizeof(tasks[slot].assignee), local_id);
    tasks[slot].state = SYNC_TASK_ACCEPTED;
    tasks[slot].updated_at = sync_time;
    return send_packet(SYNC_MSG_TASK_ACCEPT, 0, 0, task_id, 0, 0, 0, 0, 0, SYNC_TASK_ACCEPTED);
}

int sync_task_done(const char *task_id, const char *result) {
    char local_id[DISCOVERY_DEVICE_ID_MAX];
    int slot;
    if (!task_id || !task_id[0] || (result && !is_safe_text(result))) return -1;
    slot = find_task_slot(task_id);
    if (slot < 0 || !tasks[slot].used) return -1;
    discovery_get_local_device_id(local_id, sizeof(local_id));
    safe_copy(tasks[slot].assignee, sizeof(tasks[slot].assignee), local_id);
    safe_copy(tasks[slot].result, sizeof(tasks[slot].result), result);
    tasks[slot].state = SYNC_TASK_DONE;
    tasks[slot].updated_at = sync_time;
    return send_packet(SYNC_MSG_TASK_DONE, 0, 0, task_id, 0, 0, 0, result, 0, SYNC_TASK_DONE);
}

void sync_tick(uint32_t ticks) {
    sync_time += ticks;
}

int sync_retry_pending(void) {
    return bus_reliable_pending_port(SYNC_PORT);
}

void sync_print_info(void) {
    int item_count = 0;
    int task_count = 0;
    int i;
    for (i = 0; i < SYNC_MAX_ITEMS; i++) if (items[i].used) item_count++;
    for (i = 0; i < SYNC_MAX_TASKS; i++) if (tasks[i].used) task_count++;
    vga_write("sync: OSYNC/1 udp/");
    print_dec(SYNC_PORT);
    vga_write(sync_ready ? " ready\n" : " not ready\n");
    vga_write("items: "); print_dec((uint32_t)item_count); vga_write("/" ); print_dec(SYNC_MAX_ITEMS);
    vga_write(" tasks: "); print_dec((uint32_t)task_count); vga_write("/"); print_dec(SYNC_MAX_TASKS); vga_write("\n");
    vga_write(" reliable: bus msg_id+per-peer-ACK directed-retry interval="); print_dec(BUS_RELIABLE_RETRY_INTERVAL); vga_write(" limit="); print_dec(BUS_RELIABLE_RETRY_LIMIT); vga_write(" pending="); print_dec((uint32_t)sync_retry_pending()); vga_write("\n");
    vga_write("security: accepts packets only from discovery trusted peers\n");
}

void sync_print_reliable(void) {
    bus_reliable_print_port(SYNC_PORT);
}

void sync_print_items(void) {
    int found = 0;
    int i;
    for (i = 0; i < SYNC_MAX_ITEMS; i++) {
        if (!items[i].used) continue;
        found = 1;
        vga_write("item "); print_dec((uint32_t)i); vga_write(": ");
        vga_write(items[i].key); vga_write("="); vga_write(items[i].value);
        vga_write(" owner="); vga_write(items[i].owner);
        vga_write(" version="); print_dec(items[i].version);
        vga_write(" age="); print_dec(sync_time - items[i].updated_at);
        vga_write("\n");
    }
    if (!found) vga_write("sync: no items\n");
}

void sync_print_tasks(void) {
    int found = 0;
    int i;
    for (i = 0; i < SYNC_MAX_TASKS; i++) {
        if (!tasks[i].used) continue;
        found = 1;
        vga_write("task "); print_dec((uint32_t)i); vga_write(": id="); vga_write(tasks[i].task_id);
        vga_write(" state="); vga_write(task_state_name(tasks[i].state));
        vga_write(" title="); vga_write(tasks[i].title);
        vga_write(" owner="); vga_write(tasks[i].owner);
        vga_write(" assignee="); vga_write(tasks[i].assignee);
        if (tasks[i].payload[0]) { vga_write(" payload="); vga_write(tasks[i].payload); }
        if (tasks[i].result[0]) { vga_write(" result="); vga_write(tasks[i].result); }
        vga_write("\n");
    }
    if (!found) vga_write("sync: no tasks\n");
}
