#include "discovery.h"
#include "string.h"
#include "vga.h"

#define DISCOVERY_MAGIC "ODDP/1"
#define DISCOVERY_DEFAULT_TTL 300U
#define DISCOVERY_PACKET_MAX 256U

static discovery_peer_t peers[DISCOVERY_MAX_PEERS];
static char local_device_id[DISCOVERY_DEVICE_ID_MAX];
static char local_name[DISCOVERY_NAME_MAX];
static char local_os[DISCOVERY_OS_MAX];
static char local_capabilities[DISCOVERY_CAPS_MAX];
static uint32_t discovery_time;
static int discovery_ready;
static char auth_secret[DISCOVERY_AUTH_SECRET_MAX];
static uint32_t auth_counter;
static int auth_enabled;

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

static void safe_copy(char *dst, uint32_t dst_len, const char *src) {
    uint32_t i;
    if (!dst || dst_len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1U < dst_len && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int str_equal_n(const char *a, const char *b, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
    }
    return 1;
}

static void append_str(char *buf, uint32_t buf_len, uint32_t *pos, const char *s) {
    while (*s && *pos + 1U < buf_len) {
        buf[*pos] = *s;
        (*pos)++;
        s++;
    }
    if (*pos < buf_len) buf[*pos] = '\0';
}

static void append_u32(char *buf, uint32_t buf_len, uint32_t *pos, uint32_t value) {
    char tmp[11];
    int i = 0;
    int j;
    if (value == 0) {
        append_str(buf, buf_len, pos, "0");
        return;
    }
    while (value > 0 && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (j = i - 1; j >= 0; j--) {
        char one[2];
        one[0] = tmp[j];
        one[1] = '\0';
        append_str(buf, buf_len, pos, one);
    }
}

static uint32_t parse_u32(const char *s) {
    uint32_t value = 0;
    while (*s >= '0' && *s <= '9') {
        value = value * 10U + (uint32_t)(*s - '0');
        s++;
    }
    return value;
}

static int parse_line(const char *line, uint32_t line_len,
                      char *key, uint32_t key_len,
                      char *value, uint32_t value_len) {
    uint32_t i = 0;
    uint32_t k = 0;
    uint32_t v = 0;
    while (i < line_len && line[i] != '=') {
        if (k + 1U < key_len) key[k++] = line[i];
        i++;
    }
    key[k] = '\0';
    if (i >= line_len || line[i] != '=') {
        value[0] = '\0';
        return -1;
    }
    i++;
    while (i < line_len) {
        if (v + 1U < value_len) value[v++] = line[i];
        i++;
    }
    value[v] = '\0';
    return 0;
}

static int is_local_device_id(const char *device_id) {
    return strcmp(device_id, local_device_id) == 0;
}

static int find_peer_slot(const char *device_id) {
    int i;
    int free_slot = -1;
    for (i = 0; i < DISCOVERY_MAX_PEERS; i++) {
        if (peers[i].used && strcmp(peers[i].device_id, device_id) == 0) return i;
        if (!peers[i].used && free_slot < 0) free_slot = i;
    }
    return free_slot >= 0 ? free_slot : 0;
}

static void upsert_peer(uint32_t src_ip, const char *device_id, const char *name,
                        const char *os, const char *caps, uint32_t ttl) {
    int slot;
    if (!device_id || device_id[0] == '\0' || is_local_device_id(device_id)) return;
    slot = find_peer_slot(device_id);
    if (!peers[slot].used || strcmp(peers[slot].device_id, device_id) != 0) {
        memset(&peers[slot], 0, sizeof(peers[slot]));
    }
    peers[slot].used = 1;
    safe_copy(peers[slot].device_id, sizeof(peers[slot].device_id), device_id);
    safe_copy(peers[slot].name, sizeof(peers[slot].name), name && name[0] ? name : "unknown");
    safe_copy(peers[slot].os, sizeof(peers[slot].os), os && os[0] ? os : "unknown");
    safe_copy(peers[slot].capabilities, sizeof(peers[slot].capabilities), caps && caps[0] ? caps : "");
    peers[slot].ip = src_ip;
    peers[slot].last_seen = discovery_time;
    peers[slot].ttl = ttl ? ttl : DISCOVERY_DEFAULT_TTL;
}

static void remove_peer(const char *device_id) {
    int i;
    if (!device_id || device_id[0] == '\0') return;
    for (i = 0; i < DISCOVERY_MAX_PEERS; i++) {
        if (peers[i].used && strcmp(peers[i].device_id, device_id) == 0) {
            memset(&peers[i], 0, sizeof(peers[i]));
            return;
        }
    }
}


#define SHA256_BLOCK_SIZE 64U
#define SHA256_DIGEST_SIZE 32U

typedef struct discovery_sha256_ctx {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t data[64];
    uint32_t data_len;
} discovery_sha256_ctx_t;

static const uint32_t sha256_k[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

static uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32U - n)); }
static uint32_t ch32(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ ((~x) & z); }
static uint32_t maj32(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static uint32_t ep0(uint32_t x) { return rotr32(x, 2U) ^ rotr32(x, 13U) ^ rotr32(x, 22U); }
static uint32_t ep1(uint32_t x) { return rotr32(x, 6U) ^ rotr32(x, 11U) ^ rotr32(x, 25U); }
static uint32_t sig0(uint32_t x) { return rotr32(x, 7U) ^ rotr32(x, 18U) ^ (x >> 3U); }
static uint32_t sig1(uint32_t x) { return rotr32(x, 17U) ^ rotr32(x, 19U) ^ (x >> 10U); }

static void discovery_sha256_transform(discovery_sha256_ctx_t *ctx, const uint8_t data[64]) {
    uint32_t m[64];
    uint32_t a,b,c,d,e,f,g,h,t1,t2;
    uint32_t i;
    for (i = 0; i < 16U; i++) {
        m[i] = ((uint32_t)data[i * 4U] << 24) | ((uint32_t)data[i * 4U + 1U] << 16) |
               ((uint32_t)data[i * 4U + 2U] << 8) | (uint32_t)data[i * 4U + 3U];
    }
    for (i = 16U; i < 64U; i++) m[i] = sig1(m[i-2U]) + m[i-7U] + sig0(m[i-15U]) + m[i-16U];
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (i = 0; i < 64U; i++) {
        t1 = h + ep1(e) + ch32(e,f,g) + sha256_k[i] + m[i];
        t2 = ep0(a) + maj32(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void discovery_sha256_init(discovery_sha256_ctx_t *ctx) {
    ctx->data_len = 0; ctx->bit_len = 0;
    ctx->state[0]=0x6a09e667U; ctx->state[1]=0xbb67ae85U; ctx->state[2]=0x3c6ef372U; ctx->state[3]=0xa54ff53aU;
    ctx->state[4]=0x510e527fU; ctx->state[5]=0x9b05688cU; ctx->state[6]=0x1f83d9abU; ctx->state[7]=0x5be0cd19U;
}

static void discovery_sha256_update(discovery_sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) {
        ctx->data[ctx->data_len++] = data[i];
        if (ctx->data_len == 64U) {
            discovery_sha256_transform(ctx, ctx->data);
            ctx->bit_len += 512U;
            ctx->data_len = 0;
        }
    }
}

static void discovery_sha256_final(discovery_sha256_ctx_t *ctx, uint8_t hash[32]) {
    uint32_t i = ctx->data_len;
    if (ctx->data_len < 56U) {
        ctx->data[i++] = 0x80U;
        while (i < 56U) ctx->data[i++] = 0x00U;
    } else {
        ctx->data[i++] = 0x80U;
        while (i < 64U) ctx->data[i++] = 0x00U;
        discovery_sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56U);
    }
    ctx->bit_len += (uint64_t)ctx->data_len * 8ULL;
    ctx->data[63] = (uint8_t)(ctx->bit_len);
    ctx->data[62] = (uint8_t)(ctx->bit_len >> 8);
    ctx->data[61] = (uint8_t)(ctx->bit_len >> 16);
    ctx->data[60] = (uint8_t)(ctx->bit_len >> 24);
    ctx->data[59] = (uint8_t)(ctx->bit_len >> 32);
    ctx->data[58] = (uint8_t)(ctx->bit_len >> 40);
    ctx->data[57] = (uint8_t)(ctx->bit_len >> 48);
    ctx->data[56] = (uint8_t)(ctx->bit_len >> 56);
    discovery_sha256_transform(ctx, ctx->data);
    for (i = 0; i < 4U; i++) {
        hash[i]      = (uint8_t)((ctx->state[0] >> (24U - i * 8U)) & 0xffU);
        hash[i + 4U] = (uint8_t)((ctx->state[1] >> (24U - i * 8U)) & 0xffU);
        hash[i + 8U] = (uint8_t)((ctx->state[2] >> (24U - i * 8U)) & 0xffU);
        hash[i +12U] = (uint8_t)((ctx->state[3] >> (24U - i * 8U)) & 0xffU);
        hash[i +16U] = (uint8_t)((ctx->state[4] >> (24U - i * 8U)) & 0xffU);
        hash[i +20U] = (uint8_t)((ctx->state[5] >> (24U - i * 8U)) & 0xffU);
        hash[i +24U] = (uint8_t)((ctx->state[6] >> (24U - i * 8U)) & 0xffU);
        hash[i +28U] = (uint8_t)((ctx->state[7] >> (24U - i * 8U)) & 0xffU);
    }
}

static void bytes_to_hex(const uint8_t *bytes, uint32_t len, char *hex, uint32_t hex_len) {
    static const char digits[] = "0123456789abcdef";
    uint32_t i;
    if (!hex || hex_len == 0) return;
    for (i = 0; i < len && i * 2U + 1U < hex_len; i++) {
        hex[i * 2U] = digits[(bytes[i] >> 4) & 0x0fU];
        hex[i * 2U + 1U] = digits[bytes[i] & 0x0fU];
    }
    hex[(i * 2U < hex_len) ? i * 2U : hex_len - 1U] = '\0';
}

static void hmac_sha256_text(const char *key, const char *message, uint8_t out[32]) {
    uint8_t k0[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    uint8_t inner[32];
    uint32_t i;
    discovery_sha256_ctx_t ctx;
    memset(k0, 0, sizeof(k0));
    if (strlen(key) > SHA256_BLOCK_SIZE) {
        discovery_sha256_init(&ctx);
        discovery_sha256_update(&ctx, (const uint8_t *)key, (uint32_t)strlen(key));
        discovery_sha256_final(&ctx, k0);
    } else {
        memcpy(k0, key, strlen(key));
    }
    for (i = 0; i < 64U; i++) { ipad[i] = (uint8_t)(k0[i] ^ 0x36U); opad[i] = (uint8_t)(k0[i] ^ 0x5cU); }
    discovery_sha256_init(&ctx);
    discovery_sha256_update(&ctx, ipad, 64U);
    discovery_sha256_update(&ctx, (const uint8_t *)message, (uint32_t)strlen(message));
    discovery_sha256_final(&ctx, inner);
    discovery_sha256_init(&ctx);
    discovery_sha256_update(&ctx, opad, 64U);
    discovery_sha256_update(&ctx, inner, 32U);
    discovery_sha256_final(&ctx, out);
}

static void build_auth_message(char *buf, uint32_t len, const char *prefix,
                               const char *challenger, const char *responder,
                               const char *nonce) {
    uint32_t pos = 0;
    buf[0] = '\0';
    append_str(buf, len, &pos, prefix);
    append_str(buf, len, &pos, "|");
    append_str(buf, len, &pos, challenger);
    append_str(buf, len, &pos, "|");
    append_str(buf, len, &pos, responder);
    append_str(buf, len, &pos, "|");
    append_str(buf, len, &pos, nonce);
}

static void compute_auth_proof(const char *prefix, const char *challenger,
                               const char *responder, const char *nonce,
                               char proof_hex[DISCOVERY_AUTH_PROOF_HEX_LEN + 1]) {
    char msg[160];
    uint8_t mac[32];
    build_auth_message(msg, sizeof(msg), prefix, challenger, responder, nonce);
    hmac_sha256_text(auth_secret, msg, mac);
    bytes_to_hex(mac, sizeof(mac), proof_hex, DISCOVERY_AUTH_PROOF_HEX_LEN + 1U);
}

static int proof_equal(const char *a, const char *b) {
    uint32_t diff = 0;
    uint32_t i;
    if (!a || !b) return 0;
    for (i = 0; i < DISCOVERY_AUTH_PROOF_HEX_LEN; i++) diff |= (uint32_t)((uint8_t)a[i] ^ (uint8_t)b[i]);
    return diff == 0U && a[DISCOVERY_AUTH_PROOF_HEX_LEN] == '\0' && b[DISCOVERY_AUTH_PROOF_HEX_LEN] == '\0';
}

static void make_nonce(char nonce[DISCOVERY_AUTH_NONCE_HEX_LEN + 1], uint32_t peer_ip) {
    uint8_t raw[8];
    uint64_t value = ((uint64_t)discovery_time << 32) ^ ((uint64_t)peer_ip) ^ ((uint64_t)auth_counter++ * 0x9e3779b97f4a7c15ULL);
    uint32_t i;
    for (i = 0; i < 8U; i++) raw[i] = (uint8_t)(value >> (i * 8U));
    bytes_to_hex(raw, sizeof(raw), nonce, DISCOVERY_AUTH_NONCE_HEX_LEN + 1U);
}

static const char *auth_status_name(discovery_auth_status_t status) {
    switch (status) {
        case DISCOVERY_AUTH_CHALLENGE_SENT: return "challenge-sent";
        case DISCOVERY_AUTH_RESPONSE_SENT: return "response-sent";
        case DISCOVERY_AUTH_TRUSTED: return "trusted";
        case DISCOVERY_AUTH_FAILED: return "failed";
        case DISCOVERY_AUTH_NONE:
        default: return "none";
    }
}

static const char *message_type_name(discovery_message_type_t type) {
    if (type == DISCOVERY_MSG_QUERY) return "QUERY";
    if (type == DISCOVERY_MSG_BYE) return "BYE";
    if (type == DISCOVERY_MSG_AUTH_CHALLENGE) return "AUTH_CHALLENGE";
    if (type == DISCOVERY_MSG_AUTH_RESPONSE) return "AUTH_RESPONSE";
    if (type == DISCOVERY_MSG_AUTH_OK) return "AUTH_OK";
    return "HELLO";
}

static uint16_t build_packet_ext(discovery_message_type_t type, char *buf, uint32_t buf_len,
                                 const char *target, const char *nonce, const char *proof) {
    uint32_t pos = 0;
    if (!buf || buf_len == 0) return 0;
    buf[0] = '\0';
    append_str(buf, buf_len, &pos, DISCOVERY_MAGIC "\n");
    append_str(buf, buf_len, &pos, "type=");
    append_str(buf, buf_len, &pos, message_type_name(type));
    append_str(buf, buf_len, &pos, "\n");
    append_str(buf, buf_len, &pos, "id=");
    append_str(buf, buf_len, &pos, local_device_id);
    append_str(buf, buf_len, &pos, "\n");
    if (target && target[0]) {
        append_str(buf, buf_len, &pos, "target=");
        append_str(buf, buf_len, &pos, target);
        append_str(buf, buf_len, &pos, "\n");
    }
    append_str(buf, buf_len, &pos, "name=");
    append_str(buf, buf_len, &pos, local_name);
    append_str(buf, buf_len, &pos, "\n");
    append_str(buf, buf_len, &pos, "os=");
    append_str(buf, buf_len, &pos, local_os);
    append_str(buf, buf_len, &pos, "\n");
    append_str(buf, buf_len, &pos, "caps=");
    append_str(buf, buf_len, &pos, local_capabilities);
    append_str(buf, buf_len, &pos, "\n");
    append_str(buf, buf_len, &pos, "ttl=");
    append_u32(buf, buf_len, &pos, DISCOVERY_DEFAULT_TTL);
    append_str(buf, buf_len, &pos, "\n");
    if (nonce && nonce[0]) {
        append_str(buf, buf_len, &pos, "nonce=");
        append_str(buf, buf_len, &pos, nonce);
        append_str(buf, buf_len, &pos, "\n");
    }
    if (proof && proof[0]) {
        append_str(buf, buf_len, &pos, "proof=");
        append_str(buf, buf_len, &pos, proof);
        append_str(buf, buf_len, &pos, "\n");
    }
    return (uint16_t)pos;
}

static uint16_t build_packet(discovery_message_type_t type, char *buf, uint32_t buf_len) {
    return build_packet_ext(type, buf, buf_len, 0, 0, 0);
}

static int send_packet(discovery_message_type_t type) {
    char packet[DISCOVERY_PACKET_MAX];
    uint16_t len;
    len = build_packet(type, packet, sizeof(packet));
    if (len == 0) return -1;
    return net_send_udp_broadcast(DISCOVERY_PORT, DISCOVERY_PORT,
                                  (const uint8_t *)packet, len);
}

static int send_auth_packet(discovery_message_type_t type, const char *target,
                            const char *nonce, const char *proof) {
    char packet[DISCOVERY_PACKET_MAX];
    uint16_t len;
    len = build_packet_ext(type, packet, sizeof(packet), target, nonce, proof);
    if (len == 0) return -1;
    return net_send_udp_broadcast(DISCOVERY_PORT, DISCOVERY_PORT,
                                  (const uint8_t *)packet, len);
}

static void handle_discovery_packet(uint32_t src_ip, const uint8_t *data, uint16_t len) {
    char type[16];
    char device_id[DISCOVERY_DEVICE_ID_MAX];
    char name[DISCOVERY_NAME_MAX];
    char os[DISCOVERY_OS_MAX];
    char caps[DISCOVERY_CAPS_MAX];
    char target[DISCOVERY_DEVICE_ID_MAX];
    char nonce[DISCOVERY_AUTH_NONCE_HEX_LEN + 1];
    char proof[DISCOVERY_AUTH_PROOF_HEX_LEN + 1];
    uint32_t ttl = DISCOVERY_DEFAULT_TTL;
    uint32_t pos;

    if (!data || len < 7U) return;
    if (!str_equal_n((const char *)data, DISCOVERY_MAGIC, 6U)) return;

    type[0] = '\0';
    device_id[0] = '\0';
    name[0] = '\0';
    os[0] = '\0';
    caps[0] = '\0';
    target[0] = '\0';
    nonce[0] = '\0';
    proof[0] = '\0';

    pos = 0;
    while (pos < len) {
        uint32_t line_start = pos;
        uint32_t line_len;
        char key[16];
        char value[80];
        while (pos < len && data[pos] != '\n' && data[pos] != '\r') pos++;
        line_len = pos - line_start;
        while (pos < len && (data[pos] == '\n' || data[pos] == '\r')) pos++;
        if (line_len == 0) continue;
        if (line_len == 6U && str_equal_n((const char *)data + line_start, DISCOVERY_MAGIC, 6U)) continue;
        if (parse_line((const char *)data + line_start, line_len, key, sizeof(key), value, sizeof(value)) < 0) continue;
        if (strcmp(key, "type") == 0) safe_copy(type, sizeof(type), value);
        else if (strcmp(key, "id") == 0) safe_copy(device_id, sizeof(device_id), value);
        else if (strcmp(key, "name") == 0) safe_copy(name, sizeof(name), value);
        else if (strcmp(key, "os") == 0) safe_copy(os, sizeof(os), value);
        else if (strcmp(key, "caps") == 0) safe_copy(caps, sizeof(caps), value);
        else if (strcmp(key, "target") == 0) safe_copy(target, sizeof(target), value);
        else if (strcmp(key, "nonce") == 0) safe_copy(nonce, sizeof(nonce), value);
        else if (strcmp(key, "proof") == 0) safe_copy(proof, sizeof(proof), value);
        else if (strcmp(key, "ttl") == 0) ttl = parse_u32(value);
    }

    if ((strcmp(type, "AUTH_CHALLENGE") == 0 || strcmp(type, "AUTH_RESPONSE") == 0 || strcmp(type, "AUTH_OK") == 0) &&
        target[0] != '\0' && !is_local_device_id(target)) {
        return;
    }

    if (strcmp(type, "AUTH_CHALLENGE") == 0) {
        int slot;
        char response_proof[DISCOVERY_AUTH_PROOF_HEX_LEN + 1];
        if (!auth_enabled || nonce[0] == '\0') return;
        upsert_peer(src_ip, device_id, name, os, caps, ttl);
        slot = find_peer_slot(device_id);
        safe_copy(peers[slot].auth_nonce, sizeof(peers[slot].auth_nonce), nonce);
        peers[slot].auth_status = DISCOVERY_AUTH_RESPONSE_SENT;
        peers[slot].auth_last_seen = discovery_time;
        compute_auth_proof("ODDP-AUTH", device_id, local_device_id, nonce, response_proof);
        (void)send_auth_packet(DISCOVERY_MSG_AUTH_RESPONSE, device_id, nonce, response_proof);
        return;
    }

    if (strcmp(type, "AUTH_RESPONSE") == 0) {
        int slot;
        char expected[DISCOVERY_AUTH_PROOF_HEX_LEN + 1];
        char ok_proof[DISCOVERY_AUTH_PROOF_HEX_LEN + 1];
        if (!auth_enabled || proof[0] == '\0' || nonce[0] == '\0') return;
        slot = find_peer_slot(device_id);
        if (!peers[slot].used || peers[slot].auth_nonce[0] == '\0') return;
        compute_auth_proof("ODDP-AUTH", local_device_id, device_id, peers[slot].auth_nonce, expected);
        if (strcmp(nonce, peers[slot].auth_nonce) == 0 && proof_equal(proof, expected)) {
            peers[slot].auth_status = DISCOVERY_AUTH_TRUSTED;
            peers[slot].auth_last_seen = discovery_time;
            compute_auth_proof("ODDP-OK", local_device_id, device_id, peers[slot].auth_nonce, ok_proof);
            (void)send_auth_packet(DISCOVERY_MSG_AUTH_OK, device_id, peers[slot].auth_nonce, ok_proof);
        } else {
            peers[slot].auth_status = DISCOVERY_AUTH_FAILED;
            peers[slot].auth_failures++;
        }
        return;
    }

    if (strcmp(type, "AUTH_OK") == 0) {
        int slot;
        char expected[DISCOVERY_AUTH_PROOF_HEX_LEN + 1];
        if (!auth_enabled || proof[0] == '\0' || nonce[0] == '\0') return;
        slot = find_peer_slot(device_id);
        if (!peers[slot].used || peers[slot].auth_nonce[0] == '\0') return;
        compute_auth_proof("ODDP-OK", device_id, local_device_id, peers[slot].auth_nonce, expected);
        if (strcmp(nonce, peers[slot].auth_nonce) == 0 && proof_equal(proof, expected)) {
            peers[slot].auth_status = DISCOVERY_AUTH_TRUSTED;
            peers[slot].auth_last_seen = discovery_time;
        } else {
            peers[slot].auth_status = DISCOVERY_AUTH_FAILED;
            peers[slot].auth_failures++;
        }
        return;
    }

    if (strcmp(type, "BYE") == 0) {
        remove_peer(device_id);
        return;
    }

    if (strcmp(type, "HELLO") == 0 || strcmp(type, "QUERY") == 0) {
        upsert_peer(src_ip, device_id, name, os, caps, ttl);
        if (strcmp(type, "QUERY") == 0 && !is_local_device_id(device_id)) {
            discovery_announce();
        }
    }
}

static void discovery_udp_recv(uint32_t src_ip, uint16_t src_port,
                               uint16_t dst_port, const uint8_t *data,
                               uint16_t len) {
    (void)src_port;
    (void)dst_port;
    handle_discovery_packet(src_ip, data, len);
}

void discovery_init(void) {
    net_device_t *dev;
    memset(peers, 0, sizeof(peers));
    discovery_time = 0;
    auth_counter = 1;
    auth_enabled = 0;
    auth_secret[0] = '\0';
    safe_copy(local_name, sizeof(local_name), "openos-device");
    safe_copy(local_os, sizeof(local_os), "openos");
    safe_copy(local_capabilities, sizeof(local_capabilities), "shell,ai,net,devmgr");
    safe_copy(local_device_id, sizeof(local_device_id), "openos-00000000");

    dev = net_get_default_device();
    if (dev) {
        char id[DISCOVERY_DEVICE_ID_MAX];
        uint32_t pos = 0;
        id[0] = '\0';
        append_str(id, sizeof(id), &pos, "openos-");
        append_u32(id, sizeof(id), &pos, dev->ip);
        safe_copy(local_device_id, sizeof(local_device_id), id);
    }

    discovery_ready = (net_udp_bind(DISCOVERY_PORT, discovery_udp_recv) == 0) ? 1 : 0;
}

int discovery_set_local_name(const char *name) {
    if (!name || !name[0]) return -1;
    safe_copy(local_name, sizeof(local_name), name);
    return 0;
}

int discovery_set_local_capabilities(const char *capabilities) {
    if (!capabilities) return -1;
    safe_copy(local_capabilities, sizeof(local_capabilities), capabilities);
    return 0;
}

int discovery_announce(void) {
    if (!discovery_ready) return -1;
    return send_packet(DISCOVERY_MSG_HELLO);
}

int discovery_query(void) {
    if (!discovery_ready) return -1;
    return send_packet(DISCOVERY_MSG_QUERY);
}

int discovery_goodbye(void) {
    if (!discovery_ready) return -1;
    return send_packet(DISCOVERY_MSG_BYE);
}

int discovery_set_auth_secret(const char *secret) {
    if (!secret || !secret[0]) {
        auth_secret[0] = '\0';
        auth_enabled = 0;
        return -1;
    }
    safe_copy(auth_secret, sizeof(auth_secret), secret);
    auth_enabled = 1;
    return 0;
}

int discovery_auth_peer(const char *device_id) {
    int slot;
    if (!discovery_ready || !auth_enabled || !device_id || !device_id[0]) return -1;
    slot = find_peer_slot(device_id);
    if (!peers[slot].used || strcmp(peers[slot].device_id, device_id) != 0) return -1;
    make_nonce(peers[slot].auth_nonce, peers[slot].ip);
    peers[slot].auth_status = DISCOVERY_AUTH_CHALLENGE_SENT;
    peers[slot].auth_last_seen = discovery_time;
    return send_auth_packet(DISCOVERY_MSG_AUTH_CHALLENGE, device_id, peers[slot].auth_nonce, 0);
}

void discovery_tick(uint32_t ticks) {
    int i;
    discovery_time += ticks;
    for (i = 0; i < DISCOVERY_MAX_PEERS; i++) {
        if (peers[i].used && discovery_time - peers[i].last_seen > peers[i].ttl) {
            memset(&peers[i], 0, sizeof(peers[i]));
        }
    }
}

uint32_t discovery_peer_count(void) {
    uint32_t count = 0;
    int i;
    for (i = 0; i < DISCOVERY_MAX_PEERS; i++) {
        if (peers[i].used) count++;
    }
    return count;
}

const discovery_peer_t *discovery_peer_get(uint32_t index) {
    uint32_t seen = 0;
    int i;
    for (i = 0; i < DISCOVERY_MAX_PEERS; i++) {
        if (peers[i].used) {
            if (seen == index) return &peers[i];
            seen++;
        }
    }
    return 0;
}

void discovery_print_info(void) {
    vga_write("discovery: ODDP/1 udp/");
    print_dec(DISCOVERY_PORT);
    vga_write(discovery_ready ? " ready\n" : " not ready\n");
    vga_write("local id: ");
    vga_write(local_device_id);
    vga_write("\nname: ");
    vga_write(local_name);
    vga_write("\nos: ");
    vga_write(local_os);
    vga_write("\ncapabilities: ");
    vga_write(local_capabilities);
    vga_write("\npeers: ");
    print_dec(discovery_peer_count());
    vga_write("\n");
}

void discovery_print_peers(void) {
    uint32_t count = discovery_peer_count();
    uint32_t i;
    if (count == 0) {
        vga_write("discovery: no peers\n");
        return;
    }
    for (i = 0; i < count; i++) {
        const discovery_peer_t *peer = discovery_peer_get(i);
        char ip[16];
        if (!peer) continue;
        net_format_ipv4(peer->ip, ip);
        vga_write("peer ");
        print_dec(i);
        vga_write(": ");
        vga_write(peer->name);
        vga_write(" id=");
        vga_write(peer->device_id);
        vga_write(" ip=");
        vga_write(ip);
        vga_write(" os=");
        vga_write(peer->os);
        vga_write(" caps=");
        vga_write(peer->capabilities);
        vga_write(" age=");
        print_dec(discovery_time - peer->last_seen);
        vga_write("\n");
    }
}


void discovery_print_auth(void) {
    int i;
    vga_write("Discovery auth: ");
    vga_write(auth_enabled ? "enabled" : "disabled");
    vga_putc('\n');
    if (!auth_enabled) {
        vga_write("  set shared secret with: discovery auth_secret <secret>\n");
    }
    for (i = 0; i < DISCOVERY_MAX_PEERS; i++) {
        if (!peers[i].used) continue;
        vga_write("  ");
        vga_write(peers[i].device_id);
        vga_write(" -> ");
        vga_write(auth_status_name(peers[i].auth_status));
        if (peers[i].auth_failures) { vga_write(" failures="); print_dec(peers[i].auth_failures); }
        vga_putc('\n');
    }
}
