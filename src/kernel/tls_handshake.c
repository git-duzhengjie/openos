#include "tls_handshake.h"

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

static void tls_zero(void* ptr, size_t len)
{
    uint8_t* bytes = (uint8_t*)ptr;
    size_t i;

    for (i = 0; i < len; ++i) {
        bytes[i] = 0;
    }
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

static int tls12_parse_server_hello(tls12_handshake_context_t* ctx,
                                    const uint8_t* message,
                                    size_t message_len)
{
    size_t body_len;
    size_t pos;
    size_t session_id_len;

    if (!ctx || !tls_handshake_header_valid(message,
                                            message_len,
                                            TLS_HANDSHAKE_SERVER_HELLO,
                                            &body_len)) {
        return 0;
    }

    if (body_len < TLS_HANDSHAKE_SERVER_HELLO_MIN_BODY_SIZE) {
        return 0;
    }

    pos = TLS_HANDSHAKE_HEADER_SIZE;
    ctx->negotiated_version = tls_hs_read_u16(message + pos);
    pos += 2u;

    tls_copy(ctx->server_random, message + pos, TLS12_RANDOM_SIZE);
    ctx->has_server_random = 1;
    pos += TLS12_RANDOM_SIZE;

    session_id_len = message[pos];
    pos += 1u;
    if (pos + session_id_len + 3u > message_len) {
        return 0;
    }

    pos += session_id_len;
    ctx->cipher_suite = tls_hs_read_u16(message + pos);
    pos += 2u;
    ctx->compression_method = message[pos];

    return 1;
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
    return cipher_suite == TLS12_CIPHER_SUITE_RSA_WITH_AES_128_GCM_SHA256;
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

    case TLS_HANDSHAKE_SERVER_HELLO_DONE:
        if (ctx->state != TLS12_HANDSHAKE_STATE_CERTIFICATE_RECEIVED ||
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
        return 0;
    }
}

int tls12_handshake_on_client_key_exchange_sent(tls12_handshake_context_t* ctx,
                                                const uint8_t* handshake_message,
                                                size_t handshake_message_len)
{
    size_t body_len;

    if (!ctx || ctx->state != TLS12_HANDSHAKE_STATE_SERVER_HELLO_DONE_RECEIVED ||
        !tls_handshake_header_valid(handshake_message,
                                    handshake_message_len,
                                    TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE,
                                    &body_len) ||
        !tls12_transcript_add(ctx, handshake_message, handshake_message_len)) {
        if (ctx) {
            ctx->state = TLS12_HANDSHAKE_STATE_ERROR;
        }
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
