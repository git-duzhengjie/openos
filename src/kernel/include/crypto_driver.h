#ifndef OPENOS_CRYPTO_DRIVER_H
#define OPENOS_CRYPTO_DRIVER_H

#include <stddef.h>
#include <stdint.h>

// 加密算法类型
typedef enum {
    CRYPTO_ALG_AES_128_CBC,
    CRYPTO_ALG_AES_256_CBC,
    CRYPTO_ALG_AES_128_GCM,
    CRYPTO_ALG_AES_256_GCM,
    CRYPTO_ALG_SHA256,
    CRYPTO_ALG_SHA384,
    CRYPTO_ALG_P256_ECDH,
    CRYPTO_ALG_P256_ECDSA,
} crypto_alg_t;

// 加密驱动能力标志
#define CRYPTO_CAP_HW_ACCEL 0x01  // 硬件加速
#define CRYPTO_CAP_CONSTANT_TIME 0x02  // 恒定时间实现（抗侧信道）

typedef struct {
    uint32_t capabilities;
    const char* name;
} crypto_driver_info_t;

// P256点结构
typedef struct {
    uint8_t x[32];
    uint8_t y[32];
    int infinity;
} crypto_p256_point_t;

// 驱动接口
typedef struct {
    // 获取驱动信息
    int (*get_info)(crypto_driver_info_t* info);
    
    // SHA-256
    int (*sha256_init)(void** ctx);
    int (*sha256_update)(void* ctx, const uint8_t* data, size_t len);
    int (*sha256_final)(void* ctx, uint8_t hash[32]);
    void (*sha256_free)(void* ctx);
    
    // SHA-384
    int (*sha384_init)(void** ctx);
    int (*sha384_update)(void* ctx, const uint8_t* data, size_t len);
    int (*sha384_final)(void* ctx, uint8_t hash[48]);
    void (*sha384_free)(void* ctx);
    
    // AES-128-GCM
    int (*aes128gcm_set_key)(void** ctx, const uint8_t key[16]);
    int (*aes128gcm_encrypt)(void* ctx, const uint8_t nonce[12],
                             const uint8_t* plaintext, size_t len,
                             const uint8_t* aad, size_t aad_len,
                             uint8_t* ciphertext, uint8_t tag[16]);
    int (*aes128gcm_decrypt)(void* ctx, const uint8_t nonce[12],
                             const uint8_t* ciphertext, size_t len,
                             const uint8_t* aad, size_t aad_len,
                             const uint8_t* plaintext, const uint8_t tag[16]);
    void (*aes128gcm_free)(void* ctx);
    
    // AES-256-GCM
    int (*aes256gcm_set_key)(void** ctx, const uint8_t key[32]);
    int (*aes256gcm_encrypt)(void* ctx, const uint8_t nonce[12],
                             const uint8_t* plaintext, size_t len,
                             const uint8_t* aad, size_t aad_len,
                             uint8_t* ciphertext, uint8_t tag[16]);
    int (*aes256gcm_decrypt)(void* ctx, const uint8_t nonce[12],
                             const uint8_t* ciphertext, size_t len,
                             const uint8_t* aad, size_t aad_len,
                             const uint8_t* plaintext, const uint8_t tag[16]);
    void (*aes256gcm_free)(void* ctx);
    
    // P256 ECDH
    int (*p256_ecdh_base_mul)(const uint8_t scalar[32], crypto_p256_point_t* out);
    int (*p256_ecdh_point_mul)(const crypto_p256_point_t* point,
                               const uint8_t scalar[32],
                               crypto_p256_point_t* out);
    int (*p256_ecdh_shared_secret)(const crypto_p256_point_t* peer_public,
                                   const uint8_t private_key[32],
                                   uint8_t out_x[32]);
    
    // P256 ECDSA 签名验证
    int (*p256_ecdsa_verify)(const crypto_p256_point_t* public_key,
                             const uint8_t hash[32],
                             const uint8_t r[32],
                             const uint8_t s[32]);
} crypto_driver_t;

// 注册/获取驱动
int crypto_driver_register(const crypto_driver_t* driver);
const crypto_driver_t* crypto_driver_get(void);

// 工具函数
int crypto_p256_point_encode(const crypto_p256_point_t* point, uint8_t out[65]);
int crypto_p256_point_decode(const uint8_t in[65], crypto_p256_point_t* out);

#endif
