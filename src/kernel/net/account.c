#include "account.h"
#include "string.h"
#include "vga.h"

#define ACCOUNT_DEFAULT_ID "openos-lab"
#define ACCOUNT_DEFAULT_NAME "OpenOS User"
#define ACCOUNT_DEFAULT_PAIRING "openos-pairing"

typedef struct account_state {
    int configured;
    int e2e_enabled;
    char account_id[ACCOUNT_ID_MAX];
    char display_name[ACCOUNT_NAME_MAX];
    char pairing_code[ACCOUNT_PAIRING_CODE_MAX];
    uint32_t group_key;
    uint32_t key_epoch;
} account_state_t;

static account_state_t account;

static void safe_copy(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < dst_size) {
        unsigned char c = (unsigned char)src[i];
        if (c < 32 || c > 126) break;
        dst[i] = (char)c;
        i++;
    }
    dst[i] = '\0';
}

static int valid_text(const char *s, uint32_t max_len) {
    uint32_t i;
    if (!s || !s[0]) return 0;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (i + 1 >= max_len) return 0;
        if (c < 32 || c > 126) return 0;
    }
    return 1;
}

static uint32_t fnv1a_mix(uint32_t h, const char *s) {
    uint32_t i;
    if (!s) return h;
    for (i = 0; s[i]; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619U;
    }
    return h;
}

static uint32_t derive_key(const char *account_id, const char *pairing_code, uint32_t epoch) {
    uint32_t h = 2166136261U;
    h = fnv1a_mix(h, "OpenOS.Account.E2E.v1");
    h = fnv1a_mix(h, account_id);
    h ^= epoch + 0x9E3779B9U + (h << 6) + (h >> 2);
    h = fnv1a_mix(h, pairing_code);
    if (h == 0) h = 0xA5A55A5AU;
    return h;
}

static void refresh_key(void) {
    account.group_key = derive_key(account.account_id, account.pairing_code, account.key_epoch);
}

static char hex_digit(uint8_t value) {
    value &= 0x0F;
    return (value < 10) ? (char)('0' + value) : (char)('A' + value - 10);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint8_t stream_byte(uint32_t key, uint32_t index) {
    uint32_t x = key ^ (0x9E3779B9U * (index + 1U));
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return (uint8_t)(x & 0xFFU);
}

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

static void key_fingerprint(char *out, uint32_t out_size) {
    uint32_t value = account.group_key;
    uint32_t i;
    if (!out || out_size < ACCOUNT_KEY_HEX_LEN + 1) return;
    for (i = 0; i < ACCOUNT_KEY_HEX_LEN; i++) {
        uint32_t shift = 28U - ((i % 8U) * 4U);
        out[i] = hex_digit((uint8_t)((value >> shift) & 0x0FU));
        if (i == 7) value ^= 0xA5C35A3CU;
    }
    out[ACCOUNT_KEY_HEX_LEN] = '\0';
}

void account_init(void) {
    memset(&account, 0, sizeof(account));
    safe_copy(account.account_id, sizeof(account.account_id), ACCOUNT_DEFAULT_ID);
    safe_copy(account.display_name, sizeof(account.display_name), ACCOUNT_DEFAULT_NAME);
    safe_copy(account.pairing_code, sizeof(account.pairing_code), ACCOUNT_DEFAULT_PAIRING);
    account.configured = 1;
    account.e2e_enabled = 1;
    account.key_epoch = 1;
    refresh_key();
    discovery_set_auth_secret(account.pairing_code);
}

int account_configure(const char *account_id, const char *display_name, const char *pairing_code) {
    if (!valid_text(account_id, ACCOUNT_ID_MAX)) return -1;
    if (!valid_text(display_name, ACCOUNT_NAME_MAX)) return -1;
    if (!valid_text(pairing_code, ACCOUNT_PAIRING_CODE_MAX)) return -1;
    safe_copy(account.account_id, sizeof(account.account_id), account_id);
    safe_copy(account.display_name, sizeof(account.display_name), display_name);
    safe_copy(account.pairing_code, sizeof(account.pairing_code), pairing_code);
    account.configured = 1;
    account.e2e_enabled = 1;
    account.key_epoch++;
    if (account.key_epoch == 0) account.key_epoch = 1;
    refresh_key();
    discovery_set_auth_secret(account.pairing_code);
    return 0;
}

int account_set_pairing_code(const char *pairing_code) {
    if (!valid_text(pairing_code, ACCOUNT_PAIRING_CODE_MAX)) return -1;
    safe_copy(account.pairing_code, sizeof(account.pairing_code), pairing_code);
    account.configured = 1;
    account.e2e_enabled = 1;
    account.key_epoch++;
    if (account.key_epoch == 0) account.key_epoch = 1;
    refresh_key();
    discovery_set_auth_secret(account.pairing_code);
    return 0;
}

int account_rotate_key(void) {
    if (!account.configured) return -1;
    account.key_epoch++;
    if (account.key_epoch == 0) account.key_epoch = 1;
    refresh_key();
    return 0;
}

int account_e2e_enabled(void) {
    return account.configured && account.e2e_enabled && account.group_key != 0;
}

void account_get_status(account_status_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->configured = account.configured;
    out->e2e_enabled = account_e2e_enabled();
    safe_copy(out->account_id, sizeof(out->account_id), account.account_id);
    safe_copy(out->display_name, sizeof(out->display_name), account.display_name);
    key_fingerprint(out->key_fingerprint, sizeof(out->key_fingerprint));
}

uint32_t account_group_key(void) {
    return account.group_key;
}

int account_encrypt_field(const char *plain, char *out, uint32_t out_size) {
    uint32_t i;
    uint32_t len;
    uint32_t pos;
    if (!plain || !out || out_size == 0) return -1;
    if (!account_e2e_enabled()) {
        safe_copy(out, out_size, plain);
        return 0;
    }
    len = (uint32_t)strlen(plain);
    if (len == 0) { out[0] = '\0'; return 0; }
    if (5U + len * 2U + 1U > out_size) return -1;
    out[0] = 'E'; out[1] = '2'; out[2] = 'E'; out[3] = '1'; out[4] = ':';
    pos = 5;
    for (i = 0; i < len; i++) {
        uint8_t enc = ((uint8_t)plain[i]) ^ stream_byte(account.group_key, i);
        out[pos++] = hex_digit(enc >> 4);
        out[pos++] = hex_digit(enc);
    }
    out[pos] = '\0';
    return 0;
}

int account_decrypt_field(const char *encoded, char *out, uint32_t out_size) {
    uint32_t i;
    uint32_t hex_len;
    uint32_t plain_len;
    if (!encoded || !out || out_size == 0) return -1;
    if (memcmp(encoded, ACCOUNT_E2E_PREFIX, 5) != 0) {
        safe_copy(out, out_size, encoded);
        return 0;
    }
    if (!account_e2e_enabled()) return -1;
    hex_len = (uint32_t)strlen(encoded + 5);
    if ((hex_len & 1U) != 0) return -1;
    plain_len = hex_len / 2U;
    if (plain_len + 1U > out_size) return -1;
    for (i = 0; i < plain_len; i++) {
        int hi = hex_value(encoded[5 + i * 2]);
        int lo = hex_value(encoded[5 + i * 2 + 1]);
        uint8_t enc;
        uint8_t plain;
        if (hi < 0 || lo < 0) return -1;
        enc = (uint8_t)((hi << 4) | lo);
        plain = enc ^ stream_byte(account.group_key, i);
        if (plain < 32 || plain > 126) return -1;
        out[i] = (char)plain;
    }
    out[plain_len] = '\0';
    return 0;
}

void account_print_info(void) {
    char fp[ACCOUNT_KEY_HEX_LEN + 1];
    key_fingerprint(fp, sizeof(fp));
    vga_write("account: configured=");
    vga_write(account.configured ? "yes" : "no");
    vga_write(" e2e=");
    vga_write(account_e2e_enabled() ? "on" : "off");
    vga_write(" id=");
    vga_write(account.account_id);
    vga_write(" name=");
    vga_write(account.display_name);
    vga_write("\nkey: epoch=");
    print_dec(account.key_epoch);
    vga_write(" fp=");
    vga_write(fp);
    vga_write("\n");
}
