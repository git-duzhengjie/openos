#include "tls_crypto.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TLS_SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32u - (n))))
#define TLS_SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define TLS_SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define TLS_SHA256_EP0(x) (TLS_SHA256_ROTR((x), 2) ^ TLS_SHA256_ROTR((x), 13) ^ TLS_SHA256_ROTR((x), 22))
#define TLS_SHA256_EP1(x) (TLS_SHA256_ROTR((x), 6) ^ TLS_SHA256_ROTR((x), 11) ^ TLS_SHA256_ROTR((x), 25))
#define TLS_SHA256_SIG0(x) (TLS_SHA256_ROTR((x), 7) ^ TLS_SHA256_ROTR((x), 18) ^ ((x) >> 3))
#define TLS_SHA256_SIG1(x) (TLS_SHA256_ROTR((x), 17) ^ TLS_SHA256_ROTR((x), 19) ^ ((x) >> 10))

static const uint32_t tls_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t tls_sha256_load_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void tls_sha256_store_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void tls_sha256_transform(tls_sha256_ctx_t* ctx, const uint8_t block[64]) {
    uint32_t m[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (size_t i = 0; i < 16; ++i) {
        m[i] = tls_sha256_load_be32(block + i * 4);
    }
    for (size_t i = 16; i < 64; ++i) {
        m[i] = TLS_SHA256_SIG1(m[i - 2]) + m[i - 7] +
               TLS_SHA256_SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (size_t i = 0; i < 64; ++i) {
        uint32_t t1 = h + TLS_SHA256_EP1(e) + TLS_SHA256_CH(e, f, g) + tls_sha256_k[i] + m[i];
        uint32_t t2 = TLS_SHA256_EP0(a) + TLS_SHA256_MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void tls_sha256_init(tls_sha256_ctx_t* ctx) {
    if (!ctx) return;
    ctx->data_len = 0;
    ctx->bit_len = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

void tls_sha256_update(tls_sha256_ctx_t* ctx, const uint8_t* data, size_t len) {
    if (!ctx || (!data && len > 0)) return;
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->data_len++] = data[i];
        if (ctx->data_len == TLS_SHA256_BLOCK_SIZE) {
            tls_sha256_transform(ctx, ctx->data);
            ctx->bit_len += TLS_SHA256_BLOCK_SIZE * 8u;
            ctx->data_len = 0;
        }
    }
}

void tls_sha256_final(tls_sha256_ctx_t* ctx, uint8_t out[TLS_SHA256_DIGEST_SIZE]) {
    size_t i;
    uint64_t bit_len;

    if (!ctx || !out) return;

    i = ctx->data_len;
    ctx->data[i++] = 0x80u;
    if (i > 56) {
        while (i < 64) ctx->data[i++] = 0;
        tls_sha256_transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56) ctx->data[i++] = 0;

    bit_len = ctx->bit_len + ctx->data_len * 8u;
    for (int j = 7; j >= 0; --j) {
        ctx->data[56 + (7 - j)] = (uint8_t)(bit_len >> (j * 8));
    }
    tls_sha256_transform(ctx, ctx->data);

    for (i = 0; i < 8; ++i) {
        tls_sha256_store_be32(out + i * 4, ctx->state[i]);
    }
}

void tls_sha256(const uint8_t* data, size_t len, uint8_t out[TLS_SHA256_DIGEST_SIZE]) {
    tls_sha256_ctx_t ctx;
    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, data, len);
    tls_sha256_final(&ctx, out);
}

void tls_hmac_sha256(const uint8_t* key, size_t key_len,
                     const uint8_t* data, size_t data_len,
                     uint8_t out[TLS_SHA256_DIGEST_SIZE]) {
    uint8_t key_block[TLS_SHA256_BLOCK_SIZE];
    uint8_t inner_pad[TLS_SHA256_BLOCK_SIZE];
    uint8_t outer_pad[TLS_SHA256_BLOCK_SIZE];
    uint8_t inner_hash[TLS_SHA256_DIGEST_SIZE];
    tls_sha256_ctx_t ctx;

    if (!out || (!key && key_len > 0) || (!data && data_len > 0)) return;

    memset(key_block, 0, sizeof(key_block));
    if (key_len > TLS_SHA256_BLOCK_SIZE) {
        tls_sha256(key, key_len, key_block);
    } else if (key_len > 0) {
        memcpy(key_block, key, key_len);
    }

    for (size_t i = 0; i < TLS_SHA256_BLOCK_SIZE; ++i) {
        inner_pad[i] = key_block[i] ^ 0x36u;
        outer_pad[i] = key_block[i] ^ 0x5cu;
    }

    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, inner_pad, sizeof(inner_pad));
    tls_sha256_update(&ctx, data, data_len);
    tls_sha256_final(&ctx, inner_hash);

    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, outer_pad, sizeof(outer_pad));
    tls_sha256_update(&ctx, inner_hash, sizeof(inner_hash));
    tls_sha256_final(&ctx, out);
}

int tls_hkdf_sha256_extract(const uint8_t* salt, size_t salt_len,
                            const uint8_t* ikm, size_t ikm_len,
                            uint8_t prk[TLS_SHA256_DIGEST_SIZE]) {
    uint8_t zero_salt[TLS_SHA256_DIGEST_SIZE];
    if (!prk || (!salt && salt_len > 0) || (!ikm && ikm_len > 0)) return -1;
    if (!salt) {
        memset(zero_salt, 0, sizeof(zero_salt));
        salt = zero_salt;
        salt_len = sizeof(zero_salt);
    }
    tls_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
    return 0;
}

int tls_hkdf_sha256_expand(const uint8_t prk[TLS_SHA256_DIGEST_SIZE],
                           const uint8_t* info, size_t info_len,
                           uint8_t* out, size_t out_len) {
    uint8_t previous[TLS_SHA256_DIGEST_SIZE];
    size_t previous_len = 0;
    size_t produced = 0;
    uint8_t counter = 1;

    if (!prk || (!info && info_len > 0) || (!out && out_len > 0)) return -1;
    if (out_len > 255u * TLS_SHA256_DIGEST_SIZE) return -1;

    while (produced < out_len) {
        uint8_t digest[TLS_SHA256_DIGEST_SIZE];
        tls_sha256_ctx_t ctx;
        size_t copy_len;
        uint8_t ipad[TLS_SHA256_BLOCK_SIZE];
        uint8_t opad[TLS_SHA256_BLOCK_SIZE];
        uint8_t key_block[TLS_SHA256_BLOCK_SIZE];

        memset(key_block, 0, sizeof(key_block));
        memcpy(key_block, prk, TLS_SHA256_DIGEST_SIZE);
        for (size_t i = 0; i < TLS_SHA256_BLOCK_SIZE; ++i) {
            ipad[i] = key_block[i] ^ 0x36u;
            opad[i] = key_block[i] ^ 0x5cu;
        }

        tls_sha256_init(&ctx);
        tls_sha256_update(&ctx, ipad, sizeof(ipad));
        if (previous_len) tls_sha256_update(&ctx, previous, previous_len);
        tls_sha256_update(&ctx, info, info_len);
        tls_sha256_update(&ctx, &counter, 1);
        tls_sha256_final(&ctx, digest);

        tls_sha256_init(&ctx);
        tls_sha256_update(&ctx, opad, sizeof(opad));
        tls_sha256_update(&ctx, digest, sizeof(digest));
        tls_sha256_final(&ctx, previous);
        previous_len = TLS_SHA256_DIGEST_SIZE;

        copy_len = out_len - produced;
        if (copy_len > TLS_SHA256_DIGEST_SIZE) copy_len = TLS_SHA256_DIGEST_SIZE;
        memcpy(out + produced, previous, copy_len);
        produced += copy_len;
        ++counter;
    }
    return 0;
}

int tls_hkdf_sha256(const uint8_t* salt, size_t salt_len,
                    const uint8_t* ikm, size_t ikm_len,
                    const uint8_t* info, size_t info_len,
                    uint8_t* out, size_t out_len) {
    uint8_t prk[TLS_SHA256_DIGEST_SIZE];
    if (tls_hkdf_sha256_extract(salt, salt_len, ikm, ikm_len, prk) != 0) return -1;
    return tls_hkdf_sha256_expand(prk, info, info_len, out, out_len);
}

static size_t tls_cstr_len(const char* s)
{
    size_t len = 0u;
    if (!s) return 0u;
    while (s[len] != '\0') ++len;
    return len;
}

static void tls_hmac_sha256_three(const uint8_t* key, size_t key_len,
                                  const uint8_t* a, size_t a_len,
                                  const uint8_t* b, size_t b_len,
                                  const uint8_t* c, size_t c_len,
                                  uint8_t out[TLS_SHA256_DIGEST_SIZE])
{
    uint8_t key_block[TLS_SHA256_BLOCK_SIZE];
    uint8_t inner_pad[TLS_SHA256_BLOCK_SIZE];
    uint8_t outer_pad[TLS_SHA256_BLOCK_SIZE];
    uint8_t inner_hash[TLS_SHA256_DIGEST_SIZE];
    tls_sha256_ctx_t ctx;

    memset(key_block, 0, sizeof(key_block));
    if (key_len > TLS_SHA256_BLOCK_SIZE) {
        tls_sha256(key, key_len, key_block);
    } else if (key_len > 0u) {
        memcpy(key_block, key, key_len);
    }
    for (size_t i = 0u; i < TLS_SHA256_BLOCK_SIZE; ++i) {
        inner_pad[i] = key_block[i] ^ 0x36u;
        outer_pad[i] = key_block[i] ^ 0x5cu;
    }

    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, inner_pad, sizeof(inner_pad));
    tls_sha256_update(&ctx, a, a_len);
    tls_sha256_update(&ctx, b, b_len);
    tls_sha256_update(&ctx, c, c_len);
    tls_sha256_final(&ctx, inner_hash);

    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, outer_pad, sizeof(outer_pad));
    tls_sha256_update(&ctx, inner_hash, sizeof(inner_hash));
    tls_sha256_final(&ctx, out);
}

static int tls12_p_hash_sha256(const uint8_t* secret, size_t secret_len,
                               const uint8_t* seed, size_t seed_len,
                               uint8_t* out, size_t out_len)
{
    uint8_t a_value[TLS_SHA256_DIGEST_SIZE];
    size_t produced = 0u;

    if ((!secret && secret_len > 0u) || (!seed && seed_len > 0u) || (!out && out_len > 0u)) return -1;
    tls_hmac_sha256(secret, secret_len, seed, seed_len, a_value);
    while (produced < out_len) {
        uint8_t digest[TLS_SHA256_DIGEST_SIZE];
        size_t copy_len = out_len - produced;
        tls_hmac_sha256_three(secret, secret_len,
                              a_value, sizeof(a_value),
                              seed, seed_len,
                              NULL, 0u,
                              digest);
        if (copy_len > TLS_SHA256_DIGEST_SIZE) copy_len = TLS_SHA256_DIGEST_SIZE;
        memcpy(out + produced, digest, copy_len);
        produced += copy_len;
        tls_hmac_sha256(secret, secret_len, a_value, sizeof(a_value), a_value);
    }
    return 0;
}

int tls12_prf_sha256(const uint8_t* secret, size_t secret_len,
                     const char* label,
                     const uint8_t* seed, size_t seed_len,
                     uint8_t* out, size_t out_len)
{
    uint8_t combined_seed[128];
    size_t label_len;

    if (!label || (!seed && seed_len > 0u) || (!out && out_len > 0u)) return -1;
    label_len = tls_cstr_len(label);
    if (label_len == 0u || label_len + seed_len > sizeof(combined_seed)) return -1;
    memcpy(combined_seed, label, label_len);
    if (seed_len > 0u) memcpy(combined_seed + label_len, seed, seed_len);
    return tls12_p_hash_sha256(secret, secret_len, combined_seed, label_len + seed_len, out, out_len);
}

int tls12_prf_sha256_label_seed(const uint8_t* secret, size_t secret_len,
                                const char* label,
                                const uint8_t* seed_a, size_t seed_a_len,
                                const uint8_t* seed_b, size_t seed_b_len,
                                uint8_t* out, size_t out_len)
{
    uint8_t seed[128];

    if ((!seed_a && seed_a_len > 0u) || (!seed_b && seed_b_len > 0u)) return -1;
    if (seed_a_len + seed_b_len > sizeof(seed)) return -1;
    if (seed_a_len > 0u) memcpy(seed, seed_a, seed_a_len);
    if (seed_b_len > 0u) memcpy(seed + seed_a_len, seed_b, seed_b_len);
    return tls12_prf_sha256(secret, secret_len, label, seed, seed_a_len + seed_b_len, out, out_len);
}

int tls12_derive_master_secret_sha256(const uint8_t* pre_master_secret,
                                      size_t pre_master_secret_len,
                                      const uint8_t client_random[TLS12_RANDOM_SIZE],
                                      const uint8_t server_random[TLS12_RANDOM_SIZE],
                                      uint8_t master_secret[TLS12_MASTER_SECRET_SIZE])
{
    if ((!pre_master_secret && pre_master_secret_len > 0u) || !client_random || !server_random || !master_secret) return -1;
    return tls12_prf_sha256_label_seed(pre_master_secret,
                                       pre_master_secret_len,
                                       "master secret",
                                       client_random,
                                       TLS12_RANDOM_SIZE,
                                       server_random,
                                       TLS12_RANDOM_SIZE,
                                       master_secret,
                                       TLS12_MASTER_SECRET_SIZE);
}

int tls12_derive_key_block_sha256(const uint8_t master_secret[TLS12_MASTER_SECRET_SIZE],
                                  const uint8_t server_random[TLS12_RANDOM_SIZE],
                                  const uint8_t client_random[TLS12_RANDOM_SIZE],
                                  uint8_t* key_block,
                                  size_t key_block_len)
{
    if (!master_secret || !server_random || !client_random || (!key_block && key_block_len > 0u)) return -1;
    return tls12_prf_sha256_label_seed(master_secret,
                                       TLS12_MASTER_SECRET_SIZE,
                                       "key expansion",
                                       server_random,
                                       TLS12_RANDOM_SIZE,
                                       client_random,
                                       TLS12_RANDOM_SIZE,
                                       key_block,
                                       key_block_len);
}

int tls12_compute_finished_verify_data_sha256(const uint8_t master_secret[TLS12_MASTER_SECRET_SIZE],
                                              const char* label,
                                              const uint8_t* handshake_messages,
                                              size_t handshake_messages_len,
                                              uint8_t verify_data[TLS12_VERIFY_DATA_SIZE])
{
    uint8_t handshake_hash[TLS_SHA256_DIGEST_SIZE];

    if (!master_secret || !label || (!handshake_messages && handshake_messages_len > 0u) || !verify_data) return -1;
    tls_sha256(handshake_messages, handshake_messages_len, handshake_hash);
    return tls12_prf_sha256(master_secret,
                            TLS12_MASTER_SECRET_SIZE,
                            label,
                            handshake_hash,
                            sizeof(handshake_hash),
                            verify_data,
                            TLS12_VERIFY_DATA_SIZE);
}

static int tls_size_add_overflow(size_t a, size_t b, size_t* out)
{
    size_t sum = a + b;
    if (sum < a) return -1;
    *out = sum;
    return 0;
}

size_t tls12_key_block_required_len(const tls12_key_block_layout_t* layout)
{
    size_t total;
    size_t pair_len;

    if (!layout) return 0u;
    if (tls_size_add_overflow(layout->mac_key_len, layout->enc_key_len, &pair_len) != 0) return 0u;
    if (tls_size_add_overflow(pair_len, layout->fixed_iv_len, &pair_len) != 0) return 0u;
    if (tls_size_add_overflow(pair_len, pair_len, &total) != 0) return 0u;
    return total;
}

int tls12_split_key_block(const uint8_t* key_block,
                          size_t key_block_len,
                          const tls12_key_block_layout_t* layout,
                          tls12_key_block_view_t* view)
{
    size_t required_len;
    size_t off = 0u;

    if (!layout || !view) return -1;
    required_len = tls12_key_block_required_len(layout);
    if (required_len == 0u && (layout->mac_key_len != 0u || layout->enc_key_len != 0u || layout->fixed_iv_len != 0u)) return -1;
    if ((!key_block && required_len > 0u) || key_block_len < required_len) return -1;

    view->mac_key_len = layout->mac_key_len;
    view->enc_key_len = layout->enc_key_len;
    view->fixed_iv_len = layout->fixed_iv_len;

    if (required_len == 0u) {
        view->client_write_mac_key = NULL;
        view->server_write_mac_key = NULL;
        view->client_write_key = NULL;
        view->server_write_key = NULL;
        view->client_write_iv = NULL;
        view->server_write_iv = NULL;
        return 0;
    }

    view->client_write_mac_key = key_block + off;
    off += layout->mac_key_len;
    view->server_write_mac_key = key_block + off;
    off += layout->mac_key_len;
    view->client_write_key = key_block + off;
    off += layout->enc_key_len;
    view->server_write_key = key_block + off;
    off += layout->enc_key_len;
    view->client_write_iv = key_block + off;
    off += layout->fixed_iv_len;
    view->server_write_iv = key_block + off;
    return 0;
}

int tls12_build_record_aad(uint64_t sequence_number,
                           uint8_t content_type,
                           uint16_t protocol_version,
                           uint16_t plaintext_len,
                           uint8_t out[TLS12_RECORD_AAD_SIZE])
{
    if (!out) return -1;
    out[0] = (uint8_t)(sequence_number >> 56);
    out[1] = (uint8_t)(sequence_number >> 48);
    out[2] = (uint8_t)(sequence_number >> 40);
    out[3] = (uint8_t)(sequence_number >> 32);
    out[4] = (uint8_t)(sequence_number >> 24);
    out[5] = (uint8_t)(sequence_number >> 16);
    out[6] = (uint8_t)(sequence_number >> 8);
    out[7] = (uint8_t)sequence_number;
    out[8] = content_type;
    out[9] = (uint8_t)(protocol_version >> 8);
    out[10] = (uint8_t)protocol_version;
    out[11] = (uint8_t)(plaintext_len >> 8);
    out[12] = (uint8_t)plaintext_len;
    return 0;
}

int tls12_build_gcm_explicit_nonce(uint64_t sequence_number,
                                   uint8_t out[TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE])
{
    if (!out) return -1;
    out[0] = (uint8_t)(sequence_number >> 56);
    out[1] = (uint8_t)(sequence_number >> 48);
    out[2] = (uint8_t)(sequence_number >> 40);
    out[3] = (uint8_t)(sequence_number >> 32);
    out[4] = (uint8_t)(sequence_number >> 24);
    out[5] = (uint8_t)(sequence_number >> 16);
    out[6] = (uint8_t)(sequence_number >> 8);
    out[7] = (uint8_t)sequence_number;
    return 0;
}

int tls12_build_gcm_nonce(const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                          const uint8_t explicit_nonce[TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE],
                          uint8_t out[TLS12_AEAD_GCM_NONCE_SIZE])
{
    if (!fixed_iv || !explicit_nonce || !out) return -1;
    memcpy(out, fixed_iv, TLS12_AEAD_GCM_FIXED_IV_SIZE);
    memcpy(out + TLS12_AEAD_GCM_FIXED_IV_SIZE, explicit_nonce, TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE);
    return 0;
}

static const uint8_t tls_aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

static uint8_t tls_aes_xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1u) * 0x1bu));
}

static void tls_aes_add_round_key(uint8_t state[16], const uint8_t* round_key)
{
    for (size_t i = 0u; i < 16u; ++i) state[i] ^= round_key[i];
}

static void tls_aes_sub_bytes(uint8_t state[16])
{
    for (size_t i = 0u; i < 16u; ++i) state[i] = tls_aes_sbox[state[i]];
}

static void tls_aes_shift_rows(uint8_t s[16])
{
    uint8_t t;
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

static void tls_aes_mix_columns(uint8_t s[16])
{
    for (size_t c = 0u; c < 4u; ++c) {
        uint8_t* a = s + c * 4u;
        uint8_t t = (uint8_t)(a[0] ^ a[1] ^ a[2] ^ a[3]);
        uint8_t u = a[0];
        a[0] ^= t ^ tls_aes_xtime((uint8_t)(a[0] ^ a[1]));
        a[1] ^= t ^ tls_aes_xtime((uint8_t)(a[1] ^ a[2]));
        a[2] ^= t ^ tls_aes_xtime((uint8_t)(a[2] ^ a[3]));
        a[3] ^= t ^ tls_aes_xtime((uint8_t)(a[3] ^ u));
    }
}

static void tls_aes128_key_expand(const uint8_t key[16], uint8_t round_keys[176])
{
    static const uint8_t rcon[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
    uint8_t temp[4];
    size_t bytes = 16u;
    size_t rcon_index = 0u;

    memcpy(round_keys, key, 16u);
    while (bytes < 176u) {
        for (size_t i = 0u; i < 4u; ++i) temp[i] = round_keys[bytes - 4u + i];
        if ((bytes % 16u) == 0u) {
            uint8_t k = temp[0];
            temp[0] = (uint8_t)(tls_aes_sbox[temp[1]] ^ rcon[rcon_index++]);
            temp[1] = tls_aes_sbox[temp[2]];
            temp[2] = tls_aes_sbox[temp[3]];
            temp[3] = tls_aes_sbox[k];
        }
        for (size_t i = 0u; i < 4u; ++i) {
            round_keys[bytes] = (uint8_t)(round_keys[bytes - 16u] ^ temp[i]);
            ++bytes;
        }
    }
}

int tls_aes128_encrypt_block(const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                             const uint8_t in[TLS_AES_BLOCK_SIZE],
                             uint8_t out[TLS_AES_BLOCK_SIZE])
{
    uint8_t round_keys[176];
    uint8_t state[16];

    if (!key || !in || !out) return -1;
    tls_aes128_key_expand(key, round_keys);
    memcpy(state, in, sizeof(state));
    tls_aes_add_round_key(state, round_keys);
    for (size_t round = 1u; round < 10u; ++round) {
        tls_aes_sub_bytes(state);
        tls_aes_shift_rows(state);
        tls_aes_mix_columns(state);
        tls_aes_add_round_key(state, round_keys + round * 16u);
    }
    tls_aes_sub_bytes(state);
    tls_aes_shift_rows(state);
    tls_aes_add_round_key(state, round_keys + 160u);
    memcpy(out, state, sizeof(state));
    return 0;
}

static void tls_gcm_xor_block(uint8_t out[16], const uint8_t in[16])
{
    for (size_t i = 0u; i < 16u; ++i) out[i] ^= in[i];
}

static void tls_gcm_right_shift_one(uint8_t v[16])
{
    uint8_t carry = 0u;
    for (size_t i = 0u; i < 16u; ++i) {
        uint8_t next_carry = (uint8_t)(v[i] & 1u);
        v[i] = (uint8_t)((v[i] >> 1) | (uint8_t)(carry << 7));
        carry = next_carry;
    }
}

static void tls_gcm_multiply(const uint8_t x[16], const uint8_t y[16], uint8_t out[16])
{
    uint8_t z[16];
    uint8_t v[16];

    memset(z, 0, sizeof(z));
    memcpy(v, y, sizeof(v));
    for (size_t i = 0u; i < 128u; ++i) {
        uint8_t bit = (uint8_t)((x[i / 8u] >> (7u - (i % 8u))) & 1u);
        uint8_t lsb = (uint8_t)(v[15] & 1u);
        if (bit) tls_gcm_xor_block(z, v);
        tls_gcm_right_shift_one(v);
        if (lsb) v[0] ^= 0xe1u;
    }
    memcpy(out, z, sizeof(z));
}

static void tls_store_be64(uint8_t out[8], uint64_t v)
{
    for (size_t i = 0u; i < 8u; ++i) out[i] = (uint8_t)(v >> (56u - i * 8u));
}

static int tls_gcm_ghash(const uint8_t h[16],
                         const uint8_t* aad,
                         size_t aad_len,
                         const uint8_t* ciphertext,
                         size_t ciphertext_len,
                         uint8_t out[16])
{
    uint8_t y[16];
    uint8_t block[16];
    uint64_t aad_len64;
    uint64_t ciphertext_len64;
    uint64_t aad_bits;
    uint64_t ciphertext_bits;
    size_t offset;

    if ((!aad && aad_len > 0u) || (!ciphertext && ciphertext_len > 0u) || !out) return -1;
    aad_len64 = (uint64_t)aad_len;
    ciphertext_len64 = (uint64_t)ciphertext_len;
    if (aad_len64 > UINT64_MAX / 8u || ciphertext_len64 > UINT64_MAX / 8u) return -1;
    aad_bits = aad_len64 * 8u;
    ciphertext_bits = ciphertext_len64 * 8u;

    memset(y, 0, sizeof(y));
    offset = 0u;
    while (offset < aad_len) {
        size_t chunk = aad_len - offset;
        if (chunk > 16u) chunk = 16u;
        memset(block, 0, sizeof(block));
        memcpy(block, aad + offset, chunk);
        tls_gcm_xor_block(y, block);
        tls_gcm_multiply(y, h, y);
        offset += chunk;
    }

    offset = 0u;
    while (offset < ciphertext_len) {
        size_t chunk = ciphertext_len - offset;
        if (chunk > 16u) chunk = 16u;
        memset(block, 0, sizeof(block));
        memcpy(block, ciphertext + offset, chunk);
        tls_gcm_xor_block(y, block);
        tls_gcm_multiply(y, h, y);
        offset += chunk;
    }

    memset(block, 0, sizeof(block));
    tls_store_be64(block, aad_bits);
    tls_store_be64(block + 8u, ciphertext_bits);
    tls_gcm_xor_block(y, block);
    tls_gcm_multiply(y, h, y);
    memcpy(out, y, sizeof(y));
    return 0;
}

static void tls_gcm_inc32(uint8_t counter[16])
{
    uint32_t value = ((uint32_t)counter[12] << 24) | ((uint32_t)counter[13] << 16) |
                     ((uint32_t)counter[14] << 8) | (uint32_t)counter[15];
    ++value;
    counter[12] = (uint8_t)(value >> 24);
    counter[13] = (uint8_t)(value >> 16);
    counter[14] = (uint8_t)(value >> 8);
    counter[15] = (uint8_t)value;
}

static uint8_t tls_constant_time_eq_16(const uint8_t a[16], const uint8_t b[16])
{
    uint8_t diff = 0u;
    for (size_t i = 0u; i < 16u; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return (uint8_t)(diff == 0u);
}

static int tls_aes128_gcm_crypt(const uint8_t key[16],
                                const uint8_t nonce[12],
                                const uint8_t* input,
                                size_t input_len,
                                uint8_t* output)
{
    uint8_t counter[16];
    uint8_t stream[16];
    size_t offset = 0u;

    if (!key || !nonce || (!input && input_len > 0u) || (!output && input_len > 0u)) return -1;
    memcpy(counter, nonce, 12u);
    counter[12] = 0u;
    counter[13] = 0u;
    counter[14] = 0u;
    counter[15] = 1u;
    while (offset < input_len) {
        size_t chunk = input_len - offset;
        tls_gcm_inc32(counter);
        if (tls_aes128_encrypt_block(key, counter, stream) != 0) return -1;
        if (chunk > 16u) chunk = 16u;
        for (size_t i = 0u; i < chunk; ++i) output[offset + i] = (uint8_t)(input[offset + i] ^ stream[i]);
        offset += chunk;
    }
    return 0;
}

int tls_aes128_gcm_encrypt(const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                           const uint8_t nonce[TLS12_AEAD_GCM_NONCE_SIZE],
                           const uint8_t* aad,
                           size_t aad_len,
                           const uint8_t* plaintext,
                           size_t plaintext_len,
                           uint8_t* ciphertext,
                           uint8_t tag[TLS12_AEAD_GCM_TAG_SIZE])
{
    uint8_t zero[16];
    uint8_t h[16];
    uint8_t j0[16];
    uint8_t s[16];
    uint8_t e_j0[16];

    if (!key || !nonce || (!aad && aad_len > 0u) || (!plaintext && plaintext_len > 0u) ||
        (!ciphertext && plaintext_len > 0u) || !tag) return -1;
    memset(zero, 0, sizeof(zero));
    if (tls_aes128_encrypt_block(key, zero, h) != 0) return -1;
    if (tls_aes128_gcm_crypt(key, nonce, plaintext, plaintext_len, ciphertext) != 0) return -1;
    if (tls_gcm_ghash(h, aad, aad_len, ciphertext, plaintext_len, s) != 0) return -1;
    memcpy(j0, nonce, 12u);
    j0[12] = 0u; j0[13] = 0u; j0[14] = 0u; j0[15] = 1u;
    if (tls_aes128_encrypt_block(key, j0, e_j0) != 0) return -1;
    for (size_t i = 0u; i < 16u; ++i) tag[i] = (uint8_t)(e_j0[i] ^ s[i]);
    return 0;
}

int tls_aes128_gcm_decrypt(const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                           const uint8_t nonce[TLS12_AEAD_GCM_NONCE_SIZE],
                           const uint8_t* aad,
                           size_t aad_len,
                           const uint8_t* ciphertext,
                           size_t ciphertext_len,
                           const uint8_t tag[TLS12_AEAD_GCM_TAG_SIZE],
                           uint8_t* plaintext)
{
    uint8_t zero[16];
    uint8_t h[16];
    uint8_t j0[16];
    uint8_t s[16];
    uint8_t e_j0[16];
    uint8_t expected_tag[16];

    if (!key || !nonce || (!aad && aad_len > 0u) || (!ciphertext && ciphertext_len > 0u) ||
        !tag || (!plaintext && ciphertext_len > 0u)) return -1;
    memset(zero, 0, sizeof(zero));
    if (tls_aes128_encrypt_block(key, zero, h) != 0) return -1;
    if (tls_gcm_ghash(h, aad, aad_len, ciphertext, ciphertext_len, s) != 0) return -1;
    memcpy(j0, nonce, 12u);
    j0[12] = 0u; j0[13] = 0u; j0[14] = 0u; j0[15] = 1u;
    if (tls_aes128_encrypt_block(key, j0, e_j0) != 0) return -1;
    for (size_t i = 0u; i < 16u; ++i) expected_tag[i] = (uint8_t)(e_j0[i] ^ s[i]);
    if (!tls_constant_time_eq_16(expected_tag, tag)) {
        if (plaintext && ciphertext_len > 0u) memset(plaintext, 0, ciphertext_len);
        return -2;
    }
    return tls_aes128_gcm_crypt(key, nonce, ciphertext, ciphertext_len, plaintext);
}

size_t tls12_aes128_gcm_protected_len(size_t plaintext_len)
{
    size_t out_len;
    if (tls_size_add_overflow(plaintext_len, TLS12_AEAD_GCM_RECORD_OVERHEAD, &out_len) != 0) return 0u;
    return out_len;
}

size_t tls12_aes128_gcm_wire_record_len(size_t plaintext_len)
{
    size_t protected_len = tls12_aes128_gcm_protected_len(plaintext_len);
    size_t wire_len;
    if (protected_len == 0u || protected_len > UINT16_MAX) return 0u;
    if (tls_size_add_overflow(protected_len, TLS12_RECORD_HEADER_SIZE, &wire_len) != 0) return 0u;
    return wire_len;
}

int tls12_aes128_gcm_protect_record(uint64_t sequence_number,
                                    uint8_t content_type,
                                    uint16_t protocol_version,
                                    const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                                    const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                                    const uint8_t* plaintext,
                                    size_t plaintext_len,
                                    uint8_t* out_record_payload,
                                    size_t out_record_payload_len,
                                    size_t* written_len)
{
    uint8_t explicit_nonce[TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE];
    uint8_t nonce[TLS12_AEAD_GCM_NONCE_SIZE];
    uint8_t aad[TLS12_RECORD_AAD_SIZE];
    size_t required_len;
    uint8_t* ciphertext;
    uint8_t* tag;

    if (written_len) *written_len = 0u;
    if (!key || !fixed_iv || (!plaintext && plaintext_len > 0u) || !out_record_payload) return -1;
    if (plaintext_len > UINT16_MAX) return -1;
    required_len = tls12_aes128_gcm_protected_len(plaintext_len);
    if (required_len == 0u || out_record_payload_len < required_len) return -1;
    if (tls12_build_gcm_explicit_nonce(sequence_number, explicit_nonce) != 0) return -1;
    if (tls12_build_gcm_nonce(fixed_iv, explicit_nonce, nonce) != 0) return -1;
    if (tls12_build_record_aad(sequence_number, content_type, protocol_version, (uint16_t)plaintext_len, aad) != 0) return -1;

    memcpy(out_record_payload, explicit_nonce, TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE);
    ciphertext = out_record_payload + TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE;
    tag = ciphertext + plaintext_len;
    if (tls_aes128_gcm_encrypt(key, nonce, aad, sizeof(aad), plaintext, plaintext_len, ciphertext, tag) != 0) return -1;
    if (written_len) *written_len = required_len;
    return 0;
}

int tls12_aes128_gcm_unprotect_record(uint64_t sequence_number,
                                      uint8_t content_type,
                                      uint16_t protocol_version,
                                      const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                                      const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                                      const uint8_t* record_payload,
                                      size_t record_payload_len,
                                      uint8_t* plaintext,
                                      size_t plaintext_capacity,
                                      size_t* plaintext_len)
{
    uint8_t nonce[TLS12_AEAD_GCM_NONCE_SIZE];
    uint8_t aad[TLS12_RECORD_AAD_SIZE];
    const uint8_t* explicit_nonce;
    const uint8_t* ciphertext;
    const uint8_t* tag;
    size_t ciphertext_len;

    if (plaintext_len) *plaintext_len = 0u;
    if (!key || !fixed_iv || !record_payload) return -1;
    if (record_payload_len < TLS12_AEAD_GCM_RECORD_OVERHEAD) return -1;
    ciphertext_len = record_payload_len - TLS12_AEAD_GCM_RECORD_OVERHEAD;
    if (ciphertext_len > UINT16_MAX || (!plaintext && ciphertext_len > 0u) || plaintext_capacity < ciphertext_len) return -1;

    explicit_nonce = record_payload;
    ciphertext = record_payload + TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE;
    tag = ciphertext + ciphertext_len;
    if (tls12_build_gcm_nonce(fixed_iv, explicit_nonce, nonce) != 0) return -1;
    if (tls12_build_record_aad(sequence_number, content_type, protocol_version, (uint16_t)ciphertext_len, aad) != 0) return -1;
    if (tls_aes128_gcm_decrypt(key, nonce, aad, sizeof(aad), ciphertext, ciphertext_len, tag, plaintext) != 0) return -2;
    if (plaintext_len) *plaintext_len = ciphertext_len;
    return 0;
}

static uint16_t tls12_read_be16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void tls12_write_be16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

int tls12_aes128_gcm_protect_wire_record(uint64_t sequence_number,
                                         uint8_t content_type,
                                         uint16_t protocol_version,
                                         const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                                         const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                                         const uint8_t* plaintext,
                                         size_t plaintext_len,
                                         uint8_t* out_record,
                                         size_t out_record_len,
                                         size_t* written_len)
{
    size_t payload_len;
    size_t wire_len;
    size_t payload_written = 0u;

    if (written_len) *written_len = 0u;
    if (!key || !fixed_iv || (!plaintext && plaintext_len > 0u) || !out_record) return -1;
    payload_len = tls12_aes128_gcm_protected_len(plaintext_len);
    wire_len = tls12_aes128_gcm_wire_record_len(plaintext_len);
    if (payload_len == 0u || wire_len == 0u || out_record_len < wire_len) return -1;

    out_record[0] = content_type;
    tls12_write_be16(out_record + 1u, protocol_version);
    tls12_write_be16(out_record + 3u, (uint16_t)payload_len);
    if (tls12_aes128_gcm_protect_record(sequence_number,
                                        content_type,
                                        protocol_version,
                                        key,
                                        fixed_iv,
                                        plaintext,
                                        plaintext_len,
                                        out_record + TLS12_RECORD_HEADER_SIZE,
                                        out_record_len - TLS12_RECORD_HEADER_SIZE,
                                        &payload_written) != 0) {
        return -1;
    }
    if (payload_written != payload_len) return -1;
    if (written_len) *written_len = wire_len;
    return 0;
}

int tls12_aes128_gcm_unprotect_wire_record(uint64_t sequence_number,
                                           const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                                           const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                                           const uint8_t* record,
                                           size_t record_len,
                                           uint8_t* plaintext,
                                           size_t plaintext_capacity,
                                           size_t* plaintext_len,
                                           uint8_t* content_type,
                                           uint16_t* protocol_version)
{
    uint8_t parsed_type;
    uint16_t parsed_version;
    uint16_t payload_len;
    size_t out_len = 0u;
    int rc;

    if (plaintext_len) *plaintext_len = 0u;
    if (content_type) *content_type = 0u;
    if (protocol_version) *protocol_version = 0u;
    if (!key || !fixed_iv || !record || record_len < TLS12_RECORD_HEADER_SIZE) return -1;

    parsed_type = record[0];
    parsed_version = tls12_read_be16(record + 1u);
    payload_len = tls12_read_be16(record + 3u);
    if ((size_t)payload_len != record_len - TLS12_RECORD_HEADER_SIZE) return -1;

    rc = tls12_aes128_gcm_unprotect_record(sequence_number,
                                           parsed_type,
                                           parsed_version,
                                           key,
                                           fixed_iv,
                                           record + TLS12_RECORD_HEADER_SIZE,
                                           payload_len,
                                           plaintext,
                                           plaintext_capacity,
                                           &out_len);
    if (rc != 0) return rc;
    if (plaintext_len) *plaintext_len = out_len;
    if (content_type) *content_type = parsed_type;
    if (protocol_version) *protocol_version = parsed_version;
    return 0;
}

static const uint8_t* tls12_record_write_key_for_role(const tls12_aes128_gcm_record_layer_t* layer)
{
    return layer->role == TLS12_ENDPOINT_CLIENT ? layer->keys.client_write_key : layer->keys.server_write_key;
}

static const uint8_t* tls12_record_write_iv_for_role(const tls12_aes128_gcm_record_layer_t* layer)
{
    return layer->role == TLS12_ENDPOINT_CLIENT ? layer->keys.client_write_iv : layer->keys.server_write_iv;
}

static const uint8_t* tls12_record_read_key_for_role(const tls12_aes128_gcm_record_layer_t* layer)
{
    return layer->role == TLS12_ENDPOINT_CLIENT ? layer->keys.server_write_key : layer->keys.client_write_key;
}

static const uint8_t* tls12_record_read_iv_for_role(const tls12_aes128_gcm_record_layer_t* layer)
{
    return layer->role == TLS12_ENDPOINT_CLIENT ? layer->keys.server_write_iv : layer->keys.client_write_iv;
}

static int tls12_record_role_valid(tls12_endpoint_role_t role)
{
    return role == TLS12_ENDPOINT_CLIENT || role == TLS12_ENDPOINT_SERVER;
}

void tls12_aes128_gcm_record_layer_init(tls12_aes128_gcm_record_layer_t* layer)
{
    if (!layer) return;
    memset(layer, 0, sizeof(*layer));
}

int tls12_aes128_gcm_record_keys_from_key_block(const tls12_key_block_view_t* view,
                                                tls12_aes128_gcm_record_keys_t* keys)
{
    if (!view || !keys) return -1;
    if (!view->client_write_key || !view->server_write_key || !view->client_write_iv || !view->server_write_iv) return -1;
    if (view->enc_key_len != TLS12_AES_128_GCM_KEY_SIZE || view->fixed_iv_len != TLS12_AEAD_GCM_FIXED_IV_SIZE) return -1;
    memcpy(keys->client_write_key, view->client_write_key, TLS12_AES_128_GCM_KEY_SIZE);
    memcpy(keys->server_write_key, view->server_write_key, TLS12_AES_128_GCM_KEY_SIZE);
    memcpy(keys->client_write_iv, view->client_write_iv, TLS12_AEAD_GCM_FIXED_IV_SIZE);
    memcpy(keys->server_write_iv, view->server_write_iv, TLS12_AEAD_GCM_FIXED_IV_SIZE);
    return 0;
}

int tls12_aes128_gcm_record_layer_configure(tls12_aes128_gcm_record_layer_t* layer,
                                            tls12_endpoint_role_t role,
                                            uint16_t protocol_version,
                                            const tls12_aes128_gcm_record_keys_t* keys)
{
    if (!layer || !keys || !tls12_record_role_valid(role)) return -1;
    tls12_aes128_gcm_record_layer_init(layer);
    layer->role = role;
    layer->protocol_version = protocol_version;
    memcpy(&layer->keys, keys, sizeof(layer->keys));
    return 0;
}

int tls12_aes128_gcm_record_layer_configure_from_key_block(tls12_aes128_gcm_record_layer_t* layer,
                                                           tls12_endpoint_role_t role,
                                                           uint16_t protocol_version,
                                                           const tls12_key_block_view_t* view)
{
    tls12_aes128_gcm_record_keys_t keys;
    if (tls12_aes128_gcm_record_keys_from_key_block(view, &keys) != 0) return -1;
    return tls12_aes128_gcm_record_layer_configure(layer, role, protocol_version, &keys);
}

int tls12_aes128_gcm_record_layer_protect(tls12_aes128_gcm_record_layer_t* layer,
                                          uint8_t content_type,
                                          const uint8_t* plaintext,
                                          size_t plaintext_len,
                                          uint8_t* out_record,
                                          size_t out_record_len,
                                          size_t* written_len)
{
    int rc;

    if (written_len) *written_len = 0u;
    if (!layer || !tls12_record_role_valid(layer->role)) return -1;
    if (layer->next_write_sequence == UINT64_MAX) return -1;
    rc = tls12_aes128_gcm_protect_wire_record(layer->next_write_sequence,
                                              content_type,
                                              layer->protocol_version,
                                              tls12_record_write_key_for_role(layer),
                                              tls12_record_write_iv_for_role(layer),
                                              plaintext,
                                              plaintext_len,
                                              out_record,
                                              out_record_len,
                                              written_len);
    if (rc != 0) return rc;
    layer->next_write_sequence++;
    return 0;
}

int tls12_aes128_gcm_record_layer_unprotect(tls12_aes128_gcm_record_layer_t* layer,
                                            const uint8_t* record,
                                            size_t record_len,
                                            uint8_t* plaintext,
                                            size_t plaintext_capacity,
                                            size_t* plaintext_len,
                                            uint8_t* content_type)
{
    uint16_t version = 0u;
    int rc;

    if (plaintext_len) *plaintext_len = 0u;
    if (content_type) *content_type = 0u;
    if (!layer || !tls12_record_role_valid(layer->role)) return -1;
    if (layer->next_read_sequence == UINT64_MAX) return -1;
    rc = tls12_aes128_gcm_unprotect_wire_record(layer->next_read_sequence,
                                                tls12_record_read_key_for_role(layer),
                                                tls12_record_read_iv_for_role(layer),
                                                record,
                                                record_len,
                                                plaintext,
                                                plaintext_capacity,
                                                plaintext_len,
                                                content_type,
                                                &version);
    if (rc != 0) return rc;
    if (version != layer->protocol_version) return -2;
    layer->next_read_sequence++;
    return 0;
}

