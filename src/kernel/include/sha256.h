/**
 * SHA-256 (FIPS 180-4) 纯软件实现
 *
 * 用途：单用户开机/锁屏密码的哈希、通用完整性校验。
 * 无硬件依赖（不使用 SHA-NI），可在 qemu64 基线运行。
 *
 * 典型用法：
 *   uint8_t digest[SHA256_DIGEST_SIZE];
 *   sha256_ctx_t ctx;
 *   sha256_init(&ctx);
 *   sha256_update(&ctx, data, len);
 *   sha256_final(&ctx, digest);
 * 或一步到位：
 *   sha256(data, len, digest);
 */
#ifndef OPENOS_SHA256_H
#define OPENOS_SHA256_H

#include "types.h"

#define SHA256_DIGEST_SIZE 32  /* 输出 32 字节 (256 bit) */
#define SHA256_BLOCK_SIZE  64  /* 分组 64 字节 (512 bit) */

typedef struct {
    uint32_t state[8];          /* 中间哈希值 H0..H7 */
    uint64_t bitlen;            /* 已处理的消息总比特数 */
    uint8_t  buffer[SHA256_BLOCK_SIZE]; /* 未满一个分组的缓冲 */
    uint32_t buflen;            /* buffer 中已有字节数 */
} sha256_ctx_t;

/* 流式接口 */
void sha256_init(sha256_ctx_t* ctx);
void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, size_t len);
void sha256_final(sha256_ctx_t* ctx, uint8_t out[SHA256_DIGEST_SIZE]);

/* 一步计算：out = SHA256(data[0..len)) */
void sha256(const uint8_t* data, size_t len, uint8_t out[SHA256_DIGEST_SIZE]);

/* 常量时间比较，返回 0 表示相等（防时序侧信道，用于密码哈希比对） */
int sha256_ct_equal(const uint8_t a[SHA256_DIGEST_SIZE],
                    const uint8_t b[SHA256_DIGEST_SIZE]);

#endif /* OPENOS_SHA256_H */
