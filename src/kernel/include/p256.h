/**
 * P-256 / secp256r1 椭圆曲线实现
 * 用于 TLS 1.2 ECDHE 密钥交换
 */

#ifndef P256_H
#define P256_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 256位域元素表示（4个64位字）
typedef struct {
    uint64_t limbs[4];
} p256_felem_t;

// P-256 点表示（仿射坐标）
typedef struct {
    p256_felem_t x;
    p256_felem_t y;
    bool infinity;
} p256_point_t;

// P-256 密钥对
typedef struct {
    uint8_t private_key[32];   // 私钥 d
    p256_point_t public_key;   // 公钥 Q = d*G
} p256_keypair_t;

/**
 * 初始化 P-256 曲线参数
 */
void p256_init(void);

/**
 * 生成 P-256 密钥对
 * @param keypair 输出密钥对
 * @return 成功返回 true
 */
bool p256_generate_keypair(p256_keypair_t *keypair);

/**
 * 从私钥派生公钥
 * @param private_key 32字节私钥
 * @param public_key 输出公钥
 * @return 成功返回 true
 */
bool p256_derive_public_key(const uint8_t *private_key, p256_point_t *public_key);

/**
 * ECDH 密钥交换：计算 shared_secret = d * Q
 * @param private_key 己方私钥
 * @param peer_public_key 对方公钥
 * @param shared_secret 输出32字节共享密钥
 * @return 成功返回 true
 */
bool p256_ecdh(const uint8_t *private_key, const p256_point_t *peer_public_key, 
               uint8_t *shared_secret);

/**
 * 解析未压缩格式的公钥点
 * @param data 公钥数据（65字节：0x04 + x + y）
 * @param len 数据长度
 * @param point 输出点
 * @return 成功返回 true
 */
bool p256_parse_uncompressed_point(const uint8_t *data, size_t len, p256_point_t *point);

/**
 * 将公钥点序列化为未压缩格式
 * @param point 公钥点
 * @param out 输出缓冲区（65字节）
 * @return 成功返回 true
 */
bool p256_serialize_uncompressed_point(const p256_point_t *point, uint8_t *out);

/**
 * 验证公钥点是否在曲线上
 * @param point 要验证的点
 * @return 在曲线上返回 true
 */
bool p256_validate_point(const p256_point_t *point);

/**
 * 常量时间比较两个32字节值
 * @return 相等返回0
 */
int p256_constant_time_cmp(const uint8_t *a, const uint8_t *b, size_t len);

// 单元测试入口
bool p256_run_tests(void);

#endif // P256_H
