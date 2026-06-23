#include "tls_handshake.h"
#include "tls_p256.h"
#include "tls_x509.h"

#define TLS_HANDSHAKE_HEADER_SIZE 4u
#define TLS_HANDSHAKE_RANDOM_OFFSET 6u
#define TLS_HANDSHAKE_SERVER_HELLO_MIN_BODY_SIZE 38u
#define TLS_CHANGE_CIPHER_SPEC_TYPE 1u

static uint32_t tls_hs_read_u24(const uint8_t* data)
{
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | (uint32_t)data[2];
}

static uint16_t tls_hs_read_u16(const uint8_t* data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static int tls_hs_append_u8(uint8_t* buf, size_t cap, size_t* pos, uint8_t v)
{
    if (!buf || !pos || *pos >= cap) {
        return 0;
    }
    buf[(*pos)++] = v;
    return 1;
}

static int tls_hs_append_u16(uint8_t* buf, size_t cap, size_t* pos, uint16_t v)
{
    return tls_hs_append_u8(buf, cap, pos, (uint8_t)(v >> 8)) &&
           tls_hs_append_u8(buf, cap, pos, (uint8_t)(v & 0xffu));
}

static int tls_hs_append_u24(uint8_t* buf, size_t cap, size_t* pos, uint32_t v)
{
    if (v > 0xffffffu) {
        return 0;
    }
    return tls_hs_append_u8(buf, cap, pos, (uint8_t)(v >> 16)) &&
           tls_hs_append_u8(buf, cap, pos, (uint8_t)(v >> 8)) &&
           tls_hs_append_u8(buf, cap, pos, (uint8_t)(v & 0xffu));
}

static int tls_hs_append_bytes(uint8_t* buf,
                               size_t cap,
                               size_t* pos,
                               const uint8_t* src,
                               size_t len)
{
    size_t i;

    if (!buf || !pos || (!src && len > 0u) || *pos > cap || len > cap - *pos) {
        return 0;
    }

    for (i = 0; i < len; ++i) {
        buf[(*pos)++] = src[i];
    }
    return 1;
}

static int tls12_hostname_len(const char* server_name, size_t* out_len)
{
    size_t len = 0u;

    if (!server_name || !out_len) {
        return 0;
    }

    while (server_name[len] != '\0') {
        ++len;
        if (len > 255u) {
            return 0;
        }
    }

    if (len == 0u) {
        return 0;
    }

    *out_len = len;
    return 1;
}

static void tls_zero(void* ptr, size_t len)
{
    uint8_t* bytes = (uint8_t*)ptr;
    size_t i;

    for (i = 0; i < len; ++i) {
        bytes[i] = 0;
    }
}

static size_t tls_hs_cstr_len(const char* s)
{
    size_t n = 0;

    if (!s) {
        return 0;
    }
    while (s[n]) {
        ++n;
    }
    return n;
}

static void tls12_set_last_error(tls12_handshake_context_t* ctx, const char* reason)
{
    size_t i;
    size_t n;

    if (!ctx) {
        return;
    }
    n = tls_hs_cstr_len(reason);
    if (n >= TLS12_HANDSHAKE_LAST_ERROR_SIZE) {
        n = TLS12_HANDSHAKE_LAST_ERROR_SIZE - 1u;
    }
    for (i = 0; i < n; ++i) {
        ctx->last_error[i] = reason[i];
    }
    ctx->last_error[n] = '\0';
}

static void tls_copy(uint8_t* dst, const uint8_t* src, size_t len)
{
    size_t i;

    for (i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
}

static int tls_consttime_eq(const uint8_t* a, const uint8_t* b, size_t len)
{
    uint8_t diff = 0u;
    size_t i;

    if (!a || !b) {
        return 0;
    }

    for (i = 0; i < len; ++i) {
        diff = (uint8_t)(diff | (uint8_t)(a[i] ^ b[i]));
    }
    return diff == 0u;
}

static int tls_handshake_header_valid(const uint8_t* message,
                                      size_t message_len,
                                      uint8_t expected_type,
                                      size_t* body_len)
{
    uint32_t declared_len;

    if (!message || message_len < TLS_HANDSHAKE_HEADER_SIZE || !body_len) {
        return 0;
    }

    if (message[0] != expected_type) {
        return 0;
    }

    declared_len = tls_hs_read_u24(message + 1);
    if (declared_len != message_len - TLS_HANDSHAKE_HEADER_SIZE) {
        return 0;
    }

    *body_len = (size_t)declared_len;
    return 1;
}

int tls12_make_rsa_pre_master_secret(
    uint16_t client_version,
    const uint8_t random_bytes[TLS12_RSA_PRE_MASTER_RANDOM_SIZE],
    uint8_t pre_master_secret[TLS12_RSA_PRE_MASTER_SECRET_SIZE])
{
    if (!random_bytes || !pre_master_secret || client_version != TLS12_VERSION) {
        return 0;
    }

    pre_master_secret[0] = (uint8_t)(client_version >> 8);
    pre_master_secret[1] = (uint8_t)(client_version & 0xffu);
    tls_copy(pre_master_secret + 2u, random_bytes, TLS12_RSA_PRE_MASTER_RANDOM_SIZE);
    return 1;
}

int tls12_build_rsa_client_key_exchange_message(const uint8_t* encrypted_pre_master_secret,
                                                size_t encrypted_pre_master_secret_len,
                                                uint8_t* out_handshake_message,
                                                size_t out_handshake_message_cap,
                                                size_t* out_handshake_message_len)
{
    size_t p = 0u;
    size_t body_len;

    if (out_handshake_message_len) {
        *out_handshake_message_len = 0u;
    }

    if (!encrypted_pre_master_secret || encrypted_pre_master_secret_len == 0u ||
        encrypted_pre_master_secret_len > 0xffffu || !out_handshake_message ||
        !out_handshake_message_len) {
        return 0;
    }

    body_len = 2u + encrypted_pre_master_secret_len;
    if (body_len > 0xffffffu || out_handshake_message_cap < TLS_HANDSHAKE_HEADER_SIZE + body_len) {
        return 0;
    }

    if (!tls_hs_append_u8(out_handshake_message,
                          out_handshake_message_cap,
                          &p,
                          TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE) ||
        !tls_hs_append_u24(out_handshake_message, out_handshake_message_cap, &p, (uint32_t)body_len) ||
        !tls_hs_append_u16(out_handshake_message,
                           out_handshake_message_cap,
                           &p,
                           (uint16_t)encrypted_pre_master_secret_len) ||
        !tls_hs_append_bytes(out_handshake_message,
                             out_handshake_message_cap,
                             &p,
                             encrypted_pre_master_secret,
                             encrypted_pre_master_secret_len)) {
        return 0;
    }

    *out_handshake_message_len = p;
    return 1;
}

int tls12_build_ecdhe_client_key_exchange_message(
    const uint8_t client_public_key[TLS12_ECDHE_P256_PUBLIC_KEY_SIZE],
    uint8_t* out_handshake_message,
    size_t out_handshake_message_cap,
    size_t* out_handshake_message_len)
{
    size_t p = 0u;
    size_t body_len = 1u + TLS12_ECDHE_P256_PUBLIC_KEY_SIZE;
    tls_p256_point_t decoded;

    if (out_handshake_message_len) {
        *out_handshake_message_len = 0u;
    }

    if (!client_public_key || client_public_key[0] != TLS12_EC_POINT_UNCOMPRESSED ||
        !tls_p256_decode_uncompressed(client_public_key, &decoded) ||
        !out_handshake_message || !out_handshake_message_len ||
        out_handshake_message_cap < TLS_HANDSHAKE_HEADER_SIZE + body_len) {
        return 0;
    }

    if (!tls_hs_append_u8(out_handshake_message,
                          out_handshake_message_cap,
                          &p,
                          TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE) ||
        !tls_hs_append_u24(out_handshake_message, out_handshake_message_cap, &p, (uint32_t)body_len) ||
        !tls_hs_append_u8(out_handshake_message,
                          out_handshake_message_cap,
                          &p,
                          TLS12_ECDHE_P256_PUBLIC_KEY_SIZE) ||
        !tls_hs_append_bytes(out_handshake_message,
                             out_handshake_message_cap,
                             &p,
                             client_public_key,
                             TLS12_ECDHE_P256_PUBLIC_KEY_SIZE)) {
        return 0;
    }

    *out_handshake_message_len = p;
    return 1;
}

int tls12_build_client_hello_record(const char* server_name,
                                    const uint8_t client_random[TLS12_CLIENT_RANDOM_SIZE],
                                    uint8_t* out_record,
                                    size_t out_record_cap,
                                    size_t* out_record_len)
{
    static const uint8_t ciphers[] = {
        0xc0, 0x2f,
        0xc0, 0x30,
        0xc0, 0x13,
        0xc0, 0x14,
        0x00, 0x9c,
        0x00, 0x9d,
        0x00, 0x2f,
        0x00, 0xff
    };
    static const uint8_t groups[] = { 0x00, 0x17, 0x00, 0x18, 0x00, 0x19 };
    static const uint8_t ec_points[] = { 0x00 };
    static const uint8_t sigalgs[] = {
        0x04, 0x01,
        0x05, 0x01,
        0x02, 0x01,
        0x04, 0x03,
        0x05, 0x03
    };
    static const uint8_t alpn_http11[] = {
        0x00, 0x09,
        0x08, 'h', 't', 't', 'p', '/', '1', '.', '1'
    };
    size_t p = 0u;
    size_t record_len_pos;
    size_t handshake_len_pos;
    size_t handshake_start;
    size_t ext_len_pos;
    size_t ext_start;
    size_t sni_len;
    size_t handshake_len;
    size_t record_len;

    if (!out_record_len) {
        return 0;
    }
    *out_record_len = 0u;

    if (!client_random || !out_record ||
        !tls12_hostname_len(server_name, &sni_len) || out_record_cap < 128u) {
        return 0;
    }

    if (!tls_hs_append_u8(out_record, out_record_cap, &p, TLS_CONTENT_TYPE_HANDSHAKE)) {
        return 0;
    }
    if (!tls_hs_append_u16(out_record, out_record_cap, &p, 0x0301u)) {
        return 0;
    }
    record_len_pos = p;
    if (!tls_hs_append_u16(out_record, out_record_cap, &p, 0u)) {
        return 0;
    }

    if (!tls_hs_append_u8(out_record, out_record_cap, &p, TLS_HANDSHAKE_CLIENT_HELLO)) {
        return 0;
    }
    handshake_len_pos = p;
    if (!tls_hs_append_u8(out_record, out_record_cap, &p, 0u) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, 0u) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, 0u)) {
        return 0;
    }
    handshake_start = p;

    if (!tls_hs_append_u16(out_record, out_record_cap, &p, TLS12_VERSION) ||
        !tls_hs_append_bytes(out_record, out_record_cap, &p, client_random, TLS12_CLIENT_RANDOM_SIZE) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, 0u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)sizeof(ciphers)) ||
        !tls_hs_append_bytes(out_record, out_record_cap, &p, ciphers, sizeof(ciphers)) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, 1u) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, 0u)) {
        return 0;
    }

    ext_len_pos = p;
    if (!tls_hs_append_u16(out_record, out_record_cap, &p, 0u)) {
        return 0;
    }
    ext_start = p;

    if (!tls_hs_append_u16(out_record, out_record_cap, &p, 0x0000u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)(5u + sni_len)) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)(3u + sni_len)) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, 0u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)sni_len) ||
        !tls_hs_append_bytes(out_record,
                             out_record_cap,
                             &p,
                             (const uint8_t*)server_name,
                             sni_len)) {
        return 0;
    }

    if (!tls_hs_append_u16(out_record, out_record_cap, &p, 0x000au) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)(2u + sizeof(groups))) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)sizeof(groups)) ||
        !tls_hs_append_bytes(out_record, out_record_cap, &p, groups, sizeof(groups))) {
        return 0;
    }

    if (!tls_hs_append_u16(out_record, out_record_cap, &p, 0x000bu) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)(1u + sizeof(ec_points))) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, (uint8_t)sizeof(ec_points)) ||
        !tls_hs_append_bytes(out_record, out_record_cap, &p, ec_points, sizeof(ec_points))) {
        return 0;
    }

    if (!tls_hs_append_u16(out_record, out_record_cap, &p, 0x000du) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)(2u + sizeof(sigalgs))) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)sizeof(sigalgs)) ||
        !tls_hs_append_bytes(out_record, out_record_cap, &p, sigalgs, sizeof(sigalgs))) {
        return 0;
    }

    if (!tls_hs_append_u16(out_record, out_record_cap, &p, 0x0010u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, (uint16_t)sizeof(alpn_http11)) ||
        !tls_hs_append_bytes(out_record, out_record_cap, &p, alpn_http11, sizeof(alpn_http11))) {
        return 0;
    }

    if (!tls_hs_append_u16(out_record, out_record_cap, &p, 0x0016u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, 0u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, 0x0017u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, 0u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, 0xff01u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, 1u) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, 0u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, 0x002bu) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, 3u) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, 2u) ||
        !tls_hs_append_u16(out_record, out_record_cap, &p, TLS12_VERSION)) {
        return 0;
    }

    handshake_len = p - handshake_start;
    record_len = p - TLS_RECORD_HEADER_SIZE;
    if (handshake_len > 0xffffffu || record_len > 0xffffu || p > 0xffffu) {
        return 0;
    }

    out_record[ext_len_pos] = (uint8_t)((p - ext_start) >> 8);
    out_record[ext_len_pos + 1u] = (uint8_t)((p - ext_start) & 0xffu);
    out_record[handshake_len_pos] = (uint8_t)(handshake_len >> 16);
    out_record[handshake_len_pos + 1u] = (uint8_t)(handshake_len >> 8);
    out_record[handshake_len_pos + 2u] = (uint8_t)(handshake_len & 0xffu);
    out_record[record_len_pos] = (uint8_t)(record_len >> 8);
    out_record[record_len_pos + 1u] = (uint8_t)(record_len & 0xffu);
    *out_record_len = p;
    return 1;
}

static int tls12_cipher_suite_is_aes128_gcm(uint16_t cipher_suite);
static int tls12_cipher_suite_is_ecdhe_rsa(uint16_t cipher_suite);
static int tls12_parse_server_key_exchange(tls12_handshake_context_t* ctx,
                                           const uint8_t* message,
                                           size_t message_len);
static int tls12_verify_server_key_exchange_signature(tls12_handshake_context_t* ctx,
                                                      const uint8_t* signed_params,
                                                      size_t signed_params_len,
                                                      const uint8_t* signature,
                                                      size_t signature_len);

static int tls12_parse_server_hello_extension(tls12_handshake_context_t* ctx,
                                              uint16_t extension_type,
                                              const uint8_t* extension_data,
                                              size_t extension_len)
{
    size_t alpn_len;
    size_t name_len;

    if (!ctx || (!extension_data && extension_len > 0u)) {
        return 0;
    }

    switch (extension_type) {
    case 0x0017u:
        if (extension_len != 0u) {
            return 0;
        }
        ctx->server_has_extended_master_secret = 1;
        return 1;
    case 0xff01u:
        if (extension_len != 1u || extension_data[0] != 0u) {
            return 0;
        }
        ctx->server_has_renegotiation_info = 1;
        return 1;
    case 0x0010u:
        if (extension_len < 3u) {
            return 0;
        }
        alpn_len = tls_hs_read_u16(extension_data);
        if (alpn_len + 2u != extension_len || alpn_len < 1u) {
            return 0;
        }
        name_len = extension_data[2];
        if (name_len == 0u || name_len + 3u != extension_len ||
            name_len > sizeof(ctx->server_selected_alpn)) {
            return 0;
        }
        tls_copy(ctx->server_selected_alpn, extension_data + 3u, name_len);
        ctx->server_selected_alpn_len = name_len;
        ctx->server_has_alpn = 1;
        return 1;
    case 0x002bu:
        if (extension_len != 2u) {
            return 0;
        }
        ctx->server_supported_version = tls_hs_read_u16(extension_data);
        ctx->server_has_supported_versions = 1;
        return ctx->server_supported_version == TLS12_VERSION;
    default:
        return 1;
    }
}

static int tls12_parse_server_hello_extensions(tls12_handshake_context_t* ctx,
                                               const uint8_t* data,
                                               size_t len)
{
    size_t pos = 0u;
    uint16_t extension_type;
    uint16_t extension_len;

    if (!ctx || (!data && len > 0u)) {
        return 0;
    }

    while (pos < len) {
        if (len - pos < 4u) {
            return 0;
        }
        extension_type = tls_hs_read_u16(data + pos);
        extension_len = tls_hs_read_u16(data + pos + 2u);
        pos += 4u;
        if ((size_t)extension_len > len - pos) {
            return 0;
        }
        if (!tls12_parse_server_hello_extension(ctx,
                                                extension_type,
                                                data + pos,
                                                extension_len)) {
            return 0;
        }
        pos += extension_len;
    }

    return pos == len;
}

static int tls12_parse_server_hello(tls12_handshake_context_t* ctx,
                                    const uint8_t* message,
                                    size_t message_len)
{
    size_t body_len;
    size_t body_end;
    size_t pos;
    size_t session_id_len;
    size_t extensions_len;

    if (!ctx || !tls_handshake_header_valid(message,
                                            message_len,
                                            TLS_HANDSHAKE_SERVER_HELLO,
                                            &body_len)) {
        return 0;
    }

    if (body_len < TLS_HANDSHAKE_SERVER_HELLO_MIN_BODY_SIZE) {
        return 0;
    }

    body_end = TLS_HANDSHAKE_HEADER_SIZE + body_len;
    pos = TLS_HANDSHAKE_HEADER_SIZE;
    ctx->negotiated_version = tls_hs_read_u16(message + pos);
    if (ctx->negotiated_version != TLS12_VERSION) {
        return 0;
    }
    pos += 2u;

    tls_copy(ctx->server_random, message + pos, TLS12_RANDOM_SIZE);
    ctx->has_server_random = 1;
    pos += TLS12_RANDOM_SIZE;

    if (pos >= body_end) {
        return 0;
    }
    session_id_len = message[pos];
    pos += 1u;
    if (session_id_len > 32u || pos + session_id_len + 3u > body_end) {
        return 0;
    }

    pos += session_id_len;
    ctx->cipher_suite = tls_hs_read_u16(message + pos);
    if (!tls12_cipher_suite_is_aes128_gcm(ctx->cipher_suite)) {
        return 0;
    }
    pos += 2u;
    ctx->compression_method = message[pos];
    if (ctx->compression_method != 0u) {
        return 0;
    }
    pos += 1u;

    if (pos == body_end) {
        return 1;
    }
    if (body_end - pos < 2u) {
        return 0;
    }
    extensions_len = tls_hs_read_u16(message + pos);
    pos += 2u;
    if (extensions_len != body_end - pos) {
        return 0;
    }

    return tls12_parse_server_hello_extensions(ctx, message + pos, extensions_len);
}

static int tls12_capture_finished(uint8_t* out,
                                  size_t* out_len,
                                  const uint8_t* message,
                                  size_t message_len)
{
    size_t body_len;

    if (!out || !out_len || !tls_handshake_header_valid(message,
                                                        message_len,
                                                        TLS_HANDSHAKE_FINISHED,
                                                        &body_len)) {
        return 0;
    }

    if (body_len > TLS12_HANDSHAKE_MAX_FINISHED_SIZE) {
        return 0;
    }

    tls_copy(out, message + TLS_HANDSHAKE_HEADER_SIZE, body_len);
    *out_len = body_len;
    return 1;
}

static int tls12_cipher_suite_is_aes128_gcm(uint16_t cipher_suite)
{
    return cipher_suite == TLS12_CIPHER_SUITE_RSA_WITH_AES_128_GCM_SHA256 ||
           cipher_suite == TLS12_CIPHER_SUITE_ECDHE_RSA_WITH_AES_128_GCM_SHA256;
}

static int tls12_cipher_suite_is_ecdhe_rsa(uint16_t cipher_suite)
{
    return cipher_suite == TLS12_CIPHER_SUITE_ECDHE_RSA_WITH_AES_128_GCM_SHA256;
}

static int tls12_verify_server_key_exchange_signature(tls12_handshake_context_t* ctx,
                                                      const uint8_t* signed_params,
                                                      size_t signed_params_len,
                                                      const uint8_t* signature,
                                                      size_t signature_len)
{
    tls_x509_certificate_view_t leaf;
    tls_x509_subject_public_key_info_t spki;
    tls_x509_rsa_public_key_t public_key;
    tls_x509_bit_string_t signature_bits;
    uint8_t digest[TLS_SHA256_DIGEST_SIZE];
    uint8_t signed_message[TLS12_RANDOM_SIZE + TLS12_RANDOM_SIZE + TLS12_ECDHE_SIGNED_PARAMS_MAX_SIZE];
    size_t signed_message_len;

    if (!ctx || !signed_params || signed_params_len == 0u || !signature || signature_len == 0u ||
        !ctx->has_client_random || !ctx->has_server_random ||
        signed_params_len > TLS12_ECDHE_SIGNED_PARAMS_MAX_SIZE ||
        !ctx->has_certificate || ctx->certificate_chain.stored_certificate_count == 0u) {
        tls12_set_last_error(ctx, "missing certificate or randoms for ECDHE signature verification");
        return 0;
    }

    if (tls_x509_parse_certificate(ctx->certificate_chain.certificates[0],
                                   ctx->certificate_chain.certificate_lengths[0],
                                   &leaf) != 0 ||
        tls_x509_parse_subject_public_key_info(&leaf, &spki) != 0 ||
        spki.algorithm.known_oid != TLS_X509_OID_RSA_ENCRYPTION ||
        tls_x509_parse_rsa_public_key_from_spki(&spki, &public_key) != 0) {
        tls12_set_last_error(ctx, "invalid certificate RSA public key for ECDHE signature");
        return 0;
    }

    tls_copy(signed_message, ctx->client_random, TLS12_RANDOM_SIZE);
    tls_copy(signed_message + TLS12_RANDOM_SIZE, ctx->server_random, TLS12_RANDOM_SIZE);
    tls_copy(signed_message + TLS12_RANDOM_SIZE + TLS12_RANDOM_SIZE, signed_params, signed_params_len);
    signed_message_len = TLS12_RANDOM_SIZE + TLS12_RANDOM_SIZE + signed_params_len;

    tls_sha256(signed_message, signed_message_len, digest);
    signature_bits.unused_bits = 0u;
    signature_bits.bytes.data = signature;
    signature_bits.bytes.len = signature_len;
    if (!tls_x509_rsa_verify_pkcs1_v15_sha256(&public_key, &signature_bits, digest)) {
        tls12_set_last_error(ctx, "invalid ECDHE ServerKeyExchange RSA-SHA256 signature");
        return 0;
    }

    return 1;
}

static int tls12_parse_server_key_exchange(tls12_handshake_context_t* ctx,
                                           const uint8_t* message,
                                           size_t message_len)
{
    size_t body_len;
    size_t pos;
    size_t body_end;
    uint8_t public_key_len;
    uint16_t signature_algorithm;
    uint16_t signature_len;
    size_t signed_params_start;
    size_t signed_params_len;
    tls_p256_point_t server_point;

    if (!ctx || !message ||
        !tls_handshake_header_valid(message,
                                    message_len,
                                    TLS_HANDSHAKE_SERVER_KEY_EXCHANGE,
                                    &body_len)) {
        return 0;
    }

    if (!tls12_cipher_suite_is_ecdhe_rsa(ctx->cipher_suite)) {
        tls12_set_last_error(ctx, "unexpected ServerKeyExchange for selected cipher suite");
        return 0;
    }

    body_end = TLS_HANDSHAKE_HEADER_SIZE + body_len;
    pos = TLS_HANDSHAKE_HEADER_SIZE;
    signed_params_start = pos;

    if (body_end - pos < 4u) {
        tls12_set_last_error(ctx, "truncated ECDHE ServerKeyExchange params");
        return 0;
    }

    ctx->server_key_exchange.curve_type = message[pos++];
    ctx->server_key_exchange.named_curve = tls_hs_read_u16(message + pos);
    pos += 2u;
    public_key_len = message[pos++];

    if (ctx->server_key_exchange.curve_type != TLS12_EC_CURVE_TYPE_NAMED_CURVE ||
        ctx->server_key_exchange.named_curve != TLS12_NAMED_CURVE_SECP256R1 ||
        public_key_len != TLS12_ECDHE_P256_PUBLIC_KEY_SIZE ||
        body_end - pos < (size_t)public_key_len + 4u) {
        tls12_set_last_error(ctx, "unsupported ECDHE ServerKeyExchange curve or point");
        return 0;
    }

    tls_copy(ctx->server_key_exchange.public_key, message + pos, public_key_len);
    ctx->server_key_exchange.public_key_len = public_key_len;
    if (ctx->server_key_exchange.public_key[0] != TLS12_EC_POINT_UNCOMPRESSED ||
        !tls_p256_decode_uncompressed(ctx->server_key_exchange.public_key, &server_point)) {
        tls12_set_last_error(ctx, "invalid ECDHE P-256 server public key");
        return 0;
    }
    pos += public_key_len;

    signature_algorithm = tls_hs_read_u16(message + pos);
    pos += 2u;
    signature_len = tls_hs_read_u16(message + pos);
    pos += 2u;

    signed_params_len = (pos - 4u) - signed_params_start;

    if (signature_algorithm != TLS12_SIGNATURE_ALGORITHM_RSA_PKCS1_SHA256 ||
        signature_len == 0u || body_end - pos != (size_t)signature_len) {
        tls12_set_last_error(ctx, "unsupported ECDHE ServerKeyExchange signature");
        return 0;
    }

    if (!tls12_verify_server_key_exchange_signature(ctx,
                                                    message + signed_params_start,
                                                    signed_params_len,
                                                    message + pos,
                                                    signature_len)) {
        return 0;
    }

    ctx->server_key_exchange.signature_algorithm = signature_algorithm;
    ctx->server_key_exchange.signature = message + pos;
    ctx->server_key_exchange.signature_len = signature_len;
    ctx->has_server_key_exchange = 1;
    return 1;
}

static int tls12_handshake_make_aes128_gcm_key_view(tls12_handshake_context_t* ctx,
                                                    tls12_key_block_view_t* view)
{
    tls12_key_block_layout_t layout;

    if (!ctx || !view || !ctx->has_key_block) {
        return 0;
    }

    layout.mac_key_len = 0u;
    layout.enc_key_len = TLS12_AES_128_GCM_KEY_SIZE;
    layout.fixed_iv_len = TLS12_AEAD_GCM_FIXED_IV_SIZE;

    return tls12_split_key_block(ctx->key_block,
                                 sizeof(ctx->key_block),
                                 &layout,
                                 view) == 0;
}

static int tls12_transcript_add(tls12_handshake_context_t* ctx,
                                const uint8_t* handshake_message,
                                size_t handshake_message_len)
{
    if (!ctx) {
        return 0;
    }

    if (tls12_handshake_transcript_update(&ctx->transcript,
                                          handshake_message,
                                          handshake_message_len) != 0) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    return 1;
}

void tls12_handshake_context_init(tls12_handshake_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    tls_zero(ctx, sizeof(*ctx));
    ctx->state = TLS12_HANDSHAKE_STATE_INIT;
    tls12_handshake_transcript_init(&ctx->transcript);
    tls12_aes128_gcm_record_layer_init(&ctx->record_layer);
}

static int tls12_handshake_compute_expected_finished(
    const tls12_handshake_context_t* ctx,
    const char* label,
    uint8_t verify_data[TLS12_VERIFY_DATA_SIZE])
{
    if (!ctx || !ctx->has_master_secret || !label || !verify_data) {
        return 0;
    }

    return tls12_compute_finished_verify_data_sha256_from_transcript(ctx->master_secret,
                                                                     label,
                                                                     &ctx->transcript,
                                                                     verify_data) == 0;
}

const char* tls12_handshake_last_error(const tls12_handshake_context_t* ctx)
{
    if (!ctx || !ctx->last_error[0]) {
        return "";
    }
    return ctx->last_error;
}

const char* tls12_handshake_state_name(tls12_handshake_state_t state)
{
    switch (state) {
    case TLS12_HANDSHAKE_STATE_INIT:
        return "init";
    case TLS12_HANDSHAKE_STATE_CLIENT_HELLO_SENT:
        return "client_hello_sent";
    case TLS12_HANDSHAKE_STATE_SERVER_HELLO_RECEIVED:
        return "server_hello_received";
    case TLS12_HANDSHAKE_STATE_CERTIFICATE_RECEIVED:
        return "certificate_received";
    case TLS12_HANDSHAKE_STATE_SERVER_HELLO_DONE_RECEIVED:
        return "server_hello_done_received";
    case TLS12_HANDSHAKE_STATE_CLIENT_KEY_EXCHANGE_SENT:
        return "client_key_exchange_sent";
    case TLS12_HANDSHAKE_STATE_CLIENT_CHANGE_CIPHER_SPEC_SENT:
        return "client_change_cipher_spec_sent";
    case TLS12_HANDSHAKE_STATE_CLIENT_FINISHED_SENT:
        return "client_finished_sent";
    case TLS12_HANDSHAKE_STATE_SERVER_CHANGE_CIPHER_SPEC_RECEIVED:
        return "server_change_cipher_spec_received";
    case TLS12_HANDSHAKE_STATE_SERVER_FINISHED_RECEIVED:
        return "server_finished_received";
    case TLS12_HANDSHAKE_STATE_ESTABLISHED:
        return "established";
    case TLS12_HANDSHAKE_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

int tls12_handshake_set_rsa_pre_master_secret(
    tls12_handshake_context_t* ctx,
    const uint8_t pre_master_secret[TLS12_RSA_PRE_MASTER_SECRET_SIZE])
{
    uint16_t encoded_version;

    if (!ctx || !pre_master_secret || ctx->state == TLS12_HANDSHAKE_STATE_ERROR ||
        !ctx->has_client_random || !ctx->has_server_random) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    encoded_version = tls_hs_read_u16(pre_master_secret);
    if (encoded_version != TLS12_VERSION) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    tls_copy(ctx->pre_master_secret, pre_master_secret, TLS12_RSA_PRE_MASTER_SECRET_SIZE);
    ctx->pre_master_secret_len = TLS12_RSA_PRE_MASTER_SECRET_SIZE;
    ctx->has_pre_master_secret = 1;

    if (tls12_derive_master_secret_sha256(ctx->pre_master_secret,
                                          TLS12_RSA_PRE_MASTER_SECRET_SIZE,
                                          ctx->client_random,
                                          ctx->server_random,
                                          ctx->master_secret) != 0) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    ctx->has_master_secret = 1;
    ctx->has_key_block = 0;
    ctx->has_record_layer = 0;
    tls12_aes128_gcm_record_layer_init(&ctx->record_layer);
    return 1;
}

int tls12_handshake_set_ecdhe_pre_master_secret(
    tls12_handshake_context_t* ctx,
    const uint8_t shared_secret[TLS12_ECDHE_PRE_MASTER_SECRET_SIZE])
{
    if (!ctx || !shared_secret || ctx->state == TLS12_HANDSHAKE_STATE_ERROR ||
        !ctx->has_client_random || !ctx->has_server_random ||
        !tls12_cipher_suite_is_ecdhe_rsa(ctx->cipher_suite)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    tls_copy(ctx->pre_master_secret, shared_secret, TLS12_ECDHE_PRE_MASTER_SECRET_SIZE);
    ctx->pre_master_secret_len = TLS12_ECDHE_PRE_MASTER_SECRET_SIZE;
    ctx->has_pre_master_secret = 1;

    if (tls12_derive_master_secret_sha256(ctx->pre_master_secret,
                                          ctx->pre_master_secret_len,
                                          ctx->client_random,
                                          ctx->server_random,
                                          ctx->master_secret) != 0) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    ctx->has_master_secret = 1;
    ctx->has_key_block = 0;
    ctx->has_record_layer = 0;
    tls12_aes128_gcm_record_layer_init(&ctx->record_layer);
    return 1;
}

int tls12_handshake_build_ecdhe_client_key_exchange(
    tls12_handshake_context_t* ctx,
    const uint8_t client_private_key[TLS12_ECDHE_PRE_MASTER_SECRET_SIZE],
    uint8_t* out_handshake_message,
    size_t out_handshake_message_cap,
    size_t* out_handshake_message_len)
{
    tls_p256_point_t client_public;
    tls_p256_point_t server_public;
    uint8_t encoded_public[TLS12_ECDHE_P256_PUBLIC_KEY_SIZE];
    uint8_t shared_secret[TLS12_ECDHE_PRE_MASTER_SECRET_SIZE];

    if (out_handshake_message_len) {
        *out_handshake_message_len = 0u;
    }

    if (!ctx || ctx->state != TLS12_HANDSHAKE_STATE_SERVER_HELLO_DONE_RECEIVED ||
        !client_private_key || !out_handshake_message || !out_handshake_message_len ||
        !ctx->has_server_key_exchange ||
        ctx->server_key_exchange.public_key_len != TLS12_ECDHE_P256_PUBLIC_KEY_SIZE ||
        !tls_p256_is_valid_private_key(client_private_key) ||
        !tls_p256_decode_uncompressed(ctx->server_key_exchange.public_key, &server_public) ||
        !tls_p256_base_point_mul(client_private_key, &client_public) ||
        !tls_p256_encode_uncompressed(&client_public, encoded_public) ||
        !tls_p256_ecdh_shared_secret(&server_public, client_private_key, shared_secret) ||
        !tls12_build_ecdhe_client_key_exchange_message(encoded_public,
                                                       out_handshake_message,
                                                       out_handshake_message_cap,
                                                       out_handshake_message_len) ||
        !tls12_handshake_set_ecdhe_pre_master_secret(ctx, shared_secret) ||
        !tls12_handshake_on_client_key_exchange_sent(ctx,
                                                     out_handshake_message,
                                                     *out_handshake_message_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    return 1;
}

int tls12_handshake_set_master_secret(tls12_handshake_context_t* ctx,
                                      const uint8_t master_secret[TLS12_MASTER_SECRET_SIZE])
{
    if (!ctx || !master_secret || ctx->state == TLS12_HANDSHAKE_STATE_ERROR) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    tls_copy(ctx->master_secret, master_secret, TLS12_MASTER_SECRET_SIZE);
    ctx->has_master_secret = 1;
    ctx->has_key_block = 0;
    ctx->has_record_layer = 0;
    tls12_aes128_gcm_record_layer_init(&ctx->record_layer);
    return 1;
}

int tls12_handshake_compute_expected_client_finished(
    const tls12_handshake_context_t* ctx,
    uint8_t verify_data[TLS12_VERIFY_DATA_SIZE])
{
    return tls12_handshake_compute_expected_finished(ctx, "client finished", verify_data);
}

int tls12_handshake_compute_expected_server_finished(
    const tls12_handshake_context_t* ctx,
    uint8_t verify_data[TLS12_VERIFY_DATA_SIZE])
{
    return tls12_handshake_compute_expected_finished(ctx, "server finished", verify_data);
}

int tls12_handshake_derive_aes128_gcm_key_block(tls12_handshake_context_t* ctx)
{
    tls12_key_block_view_t view;

    if (!ctx || ctx->state == TLS12_HANDSHAKE_STATE_ERROR ||
        !ctx->has_master_secret || !ctx->has_client_random || !ctx->has_server_random ||
        !tls12_cipher_suite_is_aes128_gcm(ctx->cipher_suite)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    if (tls12_derive_key_block_sha256(ctx->master_secret,
                                      ctx->server_random,
                                      ctx->client_random,
                                      ctx->key_block,
                                      sizeof(ctx->key_block)) != 0) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    ctx->has_key_block = 1;
    if (!tls12_handshake_make_aes128_gcm_key_view(ctx, &view) ||
        tls12_aes128_gcm_record_keys_from_key_block(&view, &ctx->record_keys) != 0) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    return 1;
}

int tls12_handshake_configure_aes128_gcm_record_layer(tls12_handshake_context_t* ctx,
                                                      tls12_endpoint_role_t role)
{
    if (!ctx || ctx->state == TLS12_HANDSHAKE_STATE_ERROR) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    if (!ctx->has_key_block && !tls12_handshake_derive_aes128_gcm_key_block(ctx)) {
        return 0;
    }

    if (tls12_aes128_gcm_record_layer_configure(&ctx->record_layer,
                                                role,
                                                ctx->negotiated_version,
                                                &ctx->record_keys) != 0) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    ctx->has_record_layer = 1;
    return 1;
}

static int tls12_parse_plain_record(const uint8_t* record,
                                      size_t record_len,
                                      uint8_t expected_type,
                                      const uint8_t** out_fragment,
                                      size_t* out_fragment_len)
{
    uint16_t fragment_len;

    if (!record || record_len < TLS_RECORD_HEADER_SIZE || !out_fragment || !out_fragment_len) {
        return 0;
    }
    if (record[0] != expected_type || tls_hs_read_u16(record + 1u) != TLS12_VERSION) {
        return 0;
    }
    fragment_len = tls_hs_read_u16(record + 3u);
    if ((size_t)fragment_len != record_len - TLS_RECORD_HEADER_SIZE) {
        return 0;
    }

    *out_fragment = record + TLS_RECORD_HEADER_SIZE;
    *out_fragment_len = fragment_len;
    return 1;
}

static int tls12_build_finished_handshake_message(const uint8_t verify_data[TLS12_VERIFY_DATA_SIZE],
                                                  uint8_t* out_message,
                                                  size_t out_message_cap,
                                                  size_t* out_message_len)
{
    size_t p = 0u;

    if (out_message_len) {
        *out_message_len = 0u;
    }
    if (!verify_data || !out_message || !out_message_len ||
        out_message_cap < TLS_HANDSHAKE_HEADER_SIZE + TLS12_VERIFY_DATA_SIZE) {
        return 0;
    }

    if (!tls_hs_append_u8(out_message, out_message_cap, &p, TLS_HANDSHAKE_FINISHED) ||
        !tls_hs_append_u24(out_message, out_message_cap, &p, TLS12_VERIFY_DATA_SIZE) ||
        !tls_hs_append_bytes(out_message, out_message_cap, &p, verify_data, TLS12_VERIFY_DATA_SIZE)) {
        return 0;
    }

    *out_message_len = p;
    return 1;
}

static int tls12_build_record_header(uint8_t content_type,
                                     size_t fragment_len,
                                     uint8_t* out_record,
                                     size_t out_record_cap,
                                     size_t* pos)
{
    if (fragment_len > 0xffffu) {
        return 0;
    }
    return tls_hs_append_u8(out_record, out_record_cap, pos, content_type) &&
           tls_hs_append_u16(out_record, out_record_cap, pos, TLS12_VERSION) &&
           tls_hs_append_u16(out_record, out_record_cap, pos, (uint16_t)fragment_len);
}

int tls12_build_change_cipher_spec_record(uint8_t* out_record,
                                          size_t out_record_cap,
                                          size_t* out_record_len)
{
    size_t p = 0u;

    if (out_record_len) {
        *out_record_len = 0u;
    }
    if (!out_record || !out_record_len) {
        return 0;
    }
    if (!tls12_build_record_header(TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC, 1u, out_record, out_record_cap, &p) ||
        !tls_hs_append_u8(out_record, out_record_cap, &p, TLS_CHANGE_CIPHER_SPEC_TYPE)) {
        return 0;
    }

    *out_record_len = p;
    return 1;
}

int tls12_handshake_build_client_change_cipher_spec_record(tls12_handshake_context_t* ctx,
                                                           uint8_t* out_record,
                                                           size_t out_record_cap,
                                                           size_t* out_record_len)
{
    uint8_t ccs = TLS_CHANGE_CIPHER_SPEC_TYPE;

    if (!ctx || ctx->state != TLS12_HANDSHAKE_STATE_CLIENT_KEY_EXCHANGE_SENT ||
        !tls12_build_change_cipher_spec_record(out_record, out_record_cap, out_record_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    return tls12_handshake_on_client_change_cipher_spec_sent(ctx, &ccs, 1u);
}

int tls12_handshake_build_client_finished_record(tls12_handshake_context_t* ctx,
                                                 uint8_t* out_record,
                                                 size_t out_record_cap,
                                                 size_t* out_record_len)
{
    uint8_t verify_data[TLS12_VERIFY_DATA_SIZE];
    uint8_t finished_message[TLS_HANDSHAKE_HEADER_SIZE + TLS12_VERIFY_DATA_SIZE];
    uint8_t encrypted[128u];
    size_t finished_message_len = 0u;
    size_t encrypted_len = 0u;
    size_t p = 0u;

    if (out_record_len) {
        *out_record_len = 0u;
    }
    if (!ctx || !out_record || !out_record_len ||
        ctx->state != TLS12_HANDSHAKE_STATE_CLIENT_CHANGE_CIPHER_SPEC_SENT ||
        !ctx->has_record_layer || !ctx->has_master_secret ||
        !tls12_handshake_compute_expected_client_finished(ctx, verify_data) ||
        !tls12_build_finished_handshake_message(verify_data,
                                                finished_message,
                                                sizeof(finished_message),
                                                &finished_message_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    if (tls12_aes128_gcm_record_layer_protect(&ctx->record_layer,
                                              TLS_CONTENT_TYPE_HANDSHAKE,
                                              finished_message,
                                              finished_message_len,
                                              encrypted,
                                              sizeof(encrypted),
                                              &encrypted_len) != 0 ||
        encrypted_len > out_record_cap ||
        !tls_hs_append_bytes(out_record, out_record_cap, &p, encrypted, encrypted_len) ||
        !tls12_handshake_on_client_finished_sent(ctx, finished_message, finished_message_len)) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    *out_record_len = p;
    return 1;
}

int tls12_handshake_on_server_change_cipher_spec_record(tls12_handshake_context_t* ctx,
                                                        const uint8_t* record,
                                                        size_t record_len)
{
    const uint8_t* fragment;
    size_t fragment_len;

    if (!ctx || !tls12_parse_plain_record(record,
                                          record_len,
                                          TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC,
                                          &fragment,
                                          &fragment_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    return tls12_handshake_on_server_change_cipher_spec(ctx, fragment, fragment_len);
}

int tls12_handshake_on_server_finished_record(tls12_handshake_context_t* ctx,
                                              const uint8_t* record,
                                              size_t record_len,
                                              uint8_t* out_handshake_message,
                                              size_t out_handshake_message_cap,
                                              size_t* out_handshake_message_len)
{
    size_t plain_len = 0u;
    uint8_t content_type = 0u;

    if (out_handshake_message_len) {
        *out_handshake_message_len = 0u;
    }
    if (!ctx || !out_handshake_message || !out_handshake_message_len ||
        !ctx->has_record_layer ||
        tls12_aes128_gcm_record_layer_unprotect(&ctx->record_layer,
                                                record,
                                                record_len,
                                                out_handshake_message,
                                                out_handshake_message_cap,
                                                &plain_len,
                                                &content_type) != 0 ||
        content_type != TLS_CONTENT_TYPE_HANDSHAKE ||
        !tls12_handshake_on_server_finished(ctx, out_handshake_message, plain_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    *out_handshake_message_len = plain_len;
    return 1;
}

int tls12_handshake_on_client_hello_sent(tls12_handshake_context_t* ctx,
                                         const uint8_t* handshake_message,
                                         size_t handshake_message_len)
{
    size_t body_len;

    if (!ctx || ctx->state != TLS12_HANDSHAKE_STATE_INIT ||
        !tls_handshake_header_valid(handshake_message,
                                    handshake_message_len,
                                    TLS_HANDSHAKE_CLIENT_HELLO,
                                    &body_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    if (body_len < 34u) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    tls_copy(ctx->client_random,
             handshake_message + TLS_HANDSHAKE_RANDOM_OFFSET,
             TLS12_RANDOM_SIZE);
    ctx->has_client_random = 1;

    if (!tls12_transcript_add(ctx, handshake_message, handshake_message_len)) {
        return 0;
    }

    ctx->state = TLS12_HANDSHAKE_STATE_CLIENT_HELLO_SENT;
    return 1;
}

int tls12_handshake_on_server_handshake(tls12_handshake_context_t* ctx,
                                        const uint8_t* handshake_message,
                                        size_t handshake_message_len)
{
    tls_certificate_chain_view_t chain;
    size_t body_len;

    if (!ctx || !handshake_message || handshake_message_len < TLS_HANDSHAKE_HEADER_SIZE) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
            tls12_set_last_error(ctx, "malformed server handshake");
        }
        return 0;
    }

    switch (handshake_message[0]) {
    case TLS_HANDSHAKE_SERVER_HELLO:
        if (ctx->state != TLS12_HANDSHAKE_STATE_CLIENT_HELLO_SENT ||
            !tls12_parse_server_hello(ctx, handshake_message, handshake_message_len) ||
            !tls12_transcript_add(ctx, handshake_message, handshake_message_len)) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
            return 0;
        }
        ctx->state = TLS12_HANDSHAKE_STATE_SERVER_HELLO_RECEIVED;
        return 1;

    case TLS_HANDSHAKE_CERTIFICATE:
        if (ctx->state != TLS12_HANDSHAKE_STATE_SERVER_HELLO_RECEIVED ||
            !tls_handshake_header_valid(handshake_message,
                                        handshake_message_len,
                                        TLS_HANDSHAKE_CERTIFICATE,
                                        &body_len) ||
            tls_parse_certificate_chain(handshake_message + TLS_HANDSHAKE_HEADER_SIZE,
                                        body_len,
                                        &chain) != 0 ||
            !tls12_transcript_add(ctx, handshake_message, handshake_message_len)) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
            return 0;
        }
        ctx->certificate_chain = chain;
        ctx->has_certificate = 1;
        ctx->state = TLS12_HANDSHAKE_STATE_CERTIFICATE_RECEIVED;
        return 1;

    case TLS_HANDSHAKE_SERVER_KEY_EXCHANGE:
        if (ctx->state != TLS12_HANDSHAKE_STATE_CERTIFICATE_RECEIVED ||
            !tls12_parse_server_key_exchange(ctx, handshake_message, handshake_message_len) ||
            !tls12_transcript_add(ctx, handshake_message, handshake_message_len)) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
            return 0;
        }
        return 1;

    case TLS_HANDSHAKE_SERVER_HELLO_DONE:
        if (ctx->state != TLS12_HANDSHAKE_STATE_CERTIFICATE_RECEIVED ||
            (tls12_cipher_suite_is_ecdhe_rsa(ctx->cipher_suite) && !ctx->has_server_key_exchange) ||
            !tls_handshake_header_valid(handshake_message,
                                        handshake_message_len,
                                        TLS_HANDSHAKE_SERVER_HELLO_DONE,
                                        &body_len) ||
            body_len != 0u ||
            !tls12_transcript_add(ctx, handshake_message, handshake_message_len)) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
            return 0;
        }
        ctx->state = TLS12_HANDSHAKE_STATE_SERVER_HELLO_DONE_RECEIVED;
        return 1;

    default:
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        tls12_set_last_error(ctx, "unsupported server handshake message");
        return 0;
    }
}

int tls12_handshake_on_client_key_exchange_sent(tls12_handshake_context_t* ctx,
                                                const uint8_t* handshake_message,
                                                size_t handshake_message_len)
{
    size_t body_len;
    int body_ok = 0;

    if (!ctx || ctx->state != TLS12_HANDSHAKE_STATE_SERVER_HELLO_DONE_RECEIVED ||
        !tls_handshake_header_valid(handshake_message,
                                    handshake_message_len,
                                    TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE,
                                    &body_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    if (tls12_cipher_suite_is_ecdhe_rsa(ctx->cipher_suite)) {
        body_ok = body_len == 1u + TLS12_ECDHE_P256_PUBLIC_KEY_SIZE &&
                  handshake_message[TLS_HANDSHAKE_HEADER_SIZE] == TLS12_ECDHE_P256_PUBLIC_KEY_SIZE &&
                  handshake_message[TLS_HANDSHAKE_HEADER_SIZE + 1u] == TLS12_EC_POINT_UNCOMPRESSED;
    } else {
        body_ok = body_len >= 2u &&
                  (size_t)tls_hs_read_u16(handshake_message + TLS_HANDSHAKE_HEADER_SIZE) == body_len - 2u;
    }

    if (!body_ok || !tls12_transcript_add(ctx, handshake_message, handshake_message_len)) {
        ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        return 0;
    }

    ctx->state = TLS12_HANDSHAKE_STATE_CLIENT_KEY_EXCHANGE_SENT;
    return 1;
}

int tls12_handshake_on_client_change_cipher_spec_sent(tls12_handshake_context_t* ctx,
                                                      const uint8_t* message,
                                                      size_t message_len)
{
    if (!ctx || ctx->state != TLS12_HANDSHAKE_STATE_CLIENT_KEY_EXCHANGE_SENT ||
        !message || message_len != 1u || message[0] != TLS_CHANGE_CIPHER_SPEC_TYPE) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    ctx->client_change_cipher_spec_sent = 1;
    ctx->state = TLS12_HANDSHAKE_STATE_CLIENT_CHANGE_CIPHER_SPEC_SENT;
    return 1;
}

int tls12_handshake_on_client_finished_sent(tls12_handshake_context_t* ctx,
                                            const uint8_t* handshake_message,
                                            size_t handshake_message_len)
{
    uint8_t expected[TLS12_VERIFY_DATA_SIZE];

    if (!ctx || ctx->state != TLS12_HANDSHAKE_STATE_CLIENT_CHANGE_CIPHER_SPEC_SENT ||
        !tls12_capture_finished(ctx->client_finished,
                                &ctx->client_finished_len,
                                handshake_message,
                                handshake_message_len) ||
        (ctx->has_master_secret &&
         (ctx->client_finished_len != TLS12_VERIFY_DATA_SIZE ||
          !tls12_handshake_compute_expected_client_finished(ctx, expected) ||
          !tls_consttime_eq(ctx->client_finished, expected, TLS12_VERIFY_DATA_SIZE))) ||
        !tls12_transcript_add(ctx, handshake_message, handshake_message_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    ctx->state = TLS12_HANDSHAKE_STATE_CLIENT_FINISHED_SENT;
    return 1;
}

int tls12_handshake_on_server_change_cipher_spec(tls12_handshake_context_t* ctx,
                                                 const uint8_t* message,
                                                 size_t message_len)
{
    if (!ctx || ctx->state != TLS12_HANDSHAKE_STATE_CLIENT_FINISHED_SENT ||
        !message || message_len != 1u || message[0] != TLS_CHANGE_CIPHER_SPEC_TYPE) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    ctx->server_change_cipher_spec_received = 1;
    ctx->state = TLS12_HANDSHAKE_STATE_SERVER_CHANGE_CIPHER_SPEC_RECEIVED;
    return 1;
}

int tls12_handshake_on_server_finished(tls12_handshake_context_t* ctx,
                                       const uint8_t* handshake_message,
                                       size_t handshake_message_len)
{
    uint8_t expected[TLS12_VERIFY_DATA_SIZE];

    if (!ctx || ctx->state != TLS12_HANDSHAKE_STATE_SERVER_CHANGE_CIPHER_SPEC_RECEIVED ||
        !tls12_capture_finished(ctx->server_finished,
                                &ctx->server_finished_len,
                                handshake_message,
                                handshake_message_len) ||
        (ctx->has_master_secret &&
         (ctx->server_finished_len != TLS12_VERIFY_DATA_SIZE ||
          !tls12_handshake_compute_expected_server_finished(ctx, expected) ||
          !tls_consttime_eq(ctx->server_finished, expected, TLS12_VERIFY_DATA_SIZE))) ||
        !tls12_transcript_add(ctx, handshake_message, handshake_message_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
        return 0;
    }

    ctx->state = TLS12_HANDSHAKE_STATE_SERVER_FINISHED_RECEIVED;
    ctx->state = TLS12_HANDSHAKE_STATE_ESTABLISHED;
    return 1;
}

int tls12_handshake_get_transcript_hash(const tls12_handshake_context_t* ctx,
                                        uint8_t handshake_hash[TLS_SHA256_DIGEST_SIZE])
{
    if (!ctx) {
        return 0;
    }

    return tls12_handshake_transcript_hash_sha256(&ctx->transcript, handshake_hash) == 0;
}
