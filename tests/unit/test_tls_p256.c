#include "unit_test.h"

#include <stdint.h>
#include <string.h>

static void assert_mem_eq(const uint8_t *expected, const uint8_t *actual, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        ASSERT_EQ_INT(expected[i], actual[i]);
    }
}

#include "tls_p256.h"
#include "../../src/kernel/tls_p256.c"

static const uint8_t k_one[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
};

static const uint8_t k_two[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2
};

static const uint8_t k_gx[32] = {
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47,
    0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
    0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
    0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96
};

static const uint8_t k_gy[32] = {
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b,
    0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
    0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
    0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5
};

static const uint8_t k_2gx[32] = {
    0x7c, 0xf2, 0x7b, 0x18, 0x8d, 0x03, 0x4f, 0x7e,
    0x8a, 0x52, 0x38, 0x03, 0x04, 0xb5, 0x1a, 0xc3,
    0xc0, 0x89, 0x69, 0xe2, 0x77, 0xf2, 0x1b, 0x35,
    0xa6, 0x0b, 0x48, 0xfc, 0x47, 0x66, 0x99, 0x78
};

static const uint8_t k_2gy[32] = {
    0x07, 0x77, 0x55, 0x10, 0xdb, 0x8e, 0xd0, 0x40,
    0x29, 0x3d, 0x9a, 0xc6, 0x9f, 0x74, 0x30, 0xdb,
    0xba, 0x7d, 0xad, 0xe6, 0x3c, 0xe9, 0x82, 0x29,
    0x9e, 0x04, 0xb7, 0x9d, 0x22, 0x78, 0x73, 0xd1
};

UNIT_TEST_CASE(reject_invalid_private_keys)
{
    uint8_t zero[32];
    uint8_t order[32] = {
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
        0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51
    };

    memset(zero, 0, sizeof(zero));
    ASSERT_FALSE(tls_p256_is_valid_private_key(zero));
    ASSERT_FALSE(tls_p256_is_valid_private_key(order));
    ASSERT_TRUE(tls_p256_is_valid_private_key(k_one));
}

UNIT_TEST_CASE(base_point_mul_known_vectors)
{
    tls_p256_point_t p;

    ASSERT_TRUE(tls_p256_base_point_mul(k_one, &p));
    ASSERT_FALSE(p.infinity);
    assert_mem_eq(k_gx, p.x, sizeof(k_gx));
    assert_mem_eq(k_gy, p.y, sizeof(k_gy));

    ASSERT_TRUE(tls_p256_base_point_mul(k_two, &p));
    ASSERT_FALSE(p.infinity);
    assert_mem_eq(k_2gx, p.x, sizeof(k_2gx));
    assert_mem_eq(k_2gy, p.y, sizeof(k_2gy));
}

UNIT_TEST_CASE(encode_decode_uncompressed_point)
{
    tls_p256_point_t p;
    tls_p256_point_t decoded;
    uint8_t encoded[TLS_P256_UNCOMPRESSED_POINT_SIZE];

    ASSERT_TRUE(tls_p256_base_point_mul(k_one, &p));
    ASSERT_TRUE(tls_p256_encode_uncompressed(&p, encoded));
    ASSERT_EQ_INT(0x04, encoded[0]);
    ASSERT_TRUE(tls_p256_decode_uncompressed(encoded, &decoded));
    assert_mem_eq(p.x, decoded.x, sizeof(p.x));
    assert_mem_eq(p.y, decoded.y, sizeof(p.y));
}

UNIT_TEST_CASE(ecdh_shared_secret_is_symmetric)
{
    tls_p256_point_t pub_a;
    tls_p256_point_t pub_b;
    uint8_t secret_a[32];
    uint8_t secret_b[32];

    ASSERT_TRUE(tls_p256_base_point_mul(k_one, &pub_a));
    ASSERT_TRUE(tls_p256_base_point_mul(k_two, &pub_b));
    ASSERT_TRUE(tls_p256_ecdh_shared_secret(&pub_b, k_one, secret_a));
    ASSERT_TRUE(tls_p256_ecdh_shared_secret(&pub_a, k_two, secret_b));
    assert_mem_eq(secret_a, secret_b, sizeof(secret_a));
    assert_mem_eq(k_2gx, secret_a, sizeof(k_2gx));
}

int main(void)
{
    UNIT_TEST_RUN(reject_invalid_private_keys);
    UNIT_TEST_RUN(base_point_mul_known_vectors);
    UNIT_TEST_RUN(encode_decode_uncompressed_point);
    UNIT_TEST_RUN(ecdh_shared_secret_is_symmetric);
    return unit_test_finish();
}
