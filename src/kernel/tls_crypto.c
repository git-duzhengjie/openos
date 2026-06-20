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
