#ifndef OPENOS_TLS_CRYPTO_H
#define OPENOS_TLS_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define TLS_SHA256_DIGEST_SIZE 32
#define TLS_SHA256_BLOCK_SIZE 64

typedef struct tls_sha256_ctx {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t data[TLS_SHA256_BLOCK_SIZE];
    size_t data_len;
} tls_sha256_ctx_t;

void tls_sha256_init(tls_sha256_ctx_t* ctx);
void tls_sha256_update(tls_sha256_ctx_t* ctx, const uint8_t* data, size_t len);
void tls_sha256_final(tls_sha256_ctx_t* ctx, uint8_t out[TLS_SHA256_DIGEST_SIZE]);
void tls_sha256(const uint8_t* data, size_t len, uint8_t out[TLS_SHA256_DIGEST_SIZE]);

void tls_hmac_sha256(const uint8_t* key, size_t key_len,
                     const uint8_t* data, size_t data_len,
                     uint8_t out[TLS_SHA256_DIGEST_SIZE]);

int tls_hkdf_sha256_extract(const uint8_t* salt, size_t salt_len,
                            const uint8_t* ikm, size_t ikm_len,
                            uint8_t prk[TLS_SHA256_DIGEST_SIZE]);

int tls_hkdf_sha256_expand(const uint8_t prk[TLS_SHA256_DIGEST_SIZE],
                           const uint8_t* info, size_t info_len,
                           uint8_t* out, size_t out_len);

int tls_hkdf_sha256(const uint8_t* salt, size_t salt_len,
                    const uint8_t* ikm, size_t ikm_len,
                    const uint8_t* info, size_t info_len,
                    uint8_t* out, size_t out_len);

#endif
