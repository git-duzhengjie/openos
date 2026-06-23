#ifndef OPENOS_TLS_P256_H
#define OPENOS_TLS_P256_H

#include <stddef.h>
#include <stdint.h>

#define TLS_P256_SCALAR_SIZE 32u
#define TLS_P256_FE_SIZE 32u
#define TLS_P256_UNCOMPRESSED_POINT_SIZE 65u

typedef struct tls_p256_point {
    uint8_t x[TLS_P256_FE_SIZE];
    uint8_t y[TLS_P256_FE_SIZE];
    int infinity;
} tls_p256_point_t;

int tls_p256_is_valid_private_key(const uint8_t scalar[TLS_P256_SCALAR_SIZE]);
int tls_p256_base_point_mul(const uint8_t scalar[TLS_P256_SCALAR_SIZE], tls_p256_point_t* out);
int tls_p256_point_mul(const tls_p256_point_t* point,
                       const uint8_t scalar[TLS_P256_SCALAR_SIZE],
                       tls_p256_point_t* out);
int tls_p256_encode_uncompressed(const tls_p256_point_t* point,
                                 uint8_t out[TLS_P256_UNCOMPRESSED_POINT_SIZE]);
int tls_p256_decode_uncompressed(const uint8_t in[TLS_P256_UNCOMPRESSED_POINT_SIZE],
                                 tls_p256_point_t* out);
int tls_p256_ecdh_shared_secret(const tls_p256_point_t* peer_public,
                                const uint8_t private_key[TLS_P256_SCALAR_SIZE],
                                uint8_t out_x[TLS_P256_FE_SIZE]);

#endif
