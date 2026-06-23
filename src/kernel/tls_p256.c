#include "tls_p256.h"

typedef struct p256_fe {
    uint32_t v[8];
} p256_fe_t;

typedef struct p256_jacobian {
    p256_fe_t x;
    p256_fe_t y;
    p256_fe_t z;
    int infinity;
} p256_jacobian_t;

static const uint32_t P256_P[8] = {
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000001u, 0xffffffffu
};

static const uint32_t P256_N[8] = {
    0xfc632551u, 0xf3b9cac2u, 0xa7179e84u, 0xbce6faadu,
    0xffffffffu, 0xffffffffu, 0x00000000u, 0xffffffffu
};

static const p256_fe_t P256_GX = {{
    0xd898c296u, 0xf4a13945u, 0x2deb33a0u, 0x77037d81u,
    0x63a440f2u, 0xf8bce6e5u, 0xe12c4247u, 0x6b17d1f2u
}};

static const p256_fe_t P256_GY = {{
    0x37bf51f5u, 0xcbb64068u, 0x6b315eceu, 0x2bce3357u,
    0x7c0f9e16u, 0x8ee7eb4au, 0xfe1a7f9bu, 0x4fe342e2u
}};

static const p256_fe_t P256_B = {{
    0x27d2604bu, 0x3bce3c3eu, 0xcc53b0f6u, 0x651d06b0u,
    0x769886bcu, 0xb3ebbd55u, 0xaa3a93e7u, 0x5ac635d8u
}};

static void fe_zero(p256_fe_t* r)
{
    size_t i;
    for (i = 0; i < 8; ++i) {
        r->v[i] = 0;
    }
}

static void fe_one(p256_fe_t* r)
{
    fe_zero(r);
    r->v[0] = 1u;
}

static void fe_copy(p256_fe_t* r, const p256_fe_t* a)
{
    size_t i;
    for (i = 0; i < 8; ++i) {
        r->v[i] = a->v[i];
    }
}

static int words_is_zero(const uint32_t* a, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (a[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int words_cmp(const uint32_t* a, const uint32_t* b, size_t n)
{
    size_t i = n;
    while (i > 0) {
        --i;
        if (a[i] > b[i]) {
            return 1;
        }
        if (a[i] < b[i]) {
            return -1;
        }
    }
    return 0;
}

static int fe_is_zero(const p256_fe_t* a)
{
    return words_is_zero(a->v, 8);
}

static int fe_equal(const p256_fe_t* a, const p256_fe_t* b)
{
    return words_cmp(a->v, b->v, 8) == 0;
}

static void be32_to_words(const uint8_t in[32], uint32_t out[8])
{
    size_t i;
    for (i = 0; i < 8; ++i) {
        size_t o = 28u - i * 4u;
        out[i] = ((uint32_t)in[o] << 24) |
                 ((uint32_t)in[o + 1u] << 16) |
                 ((uint32_t)in[o + 2u] << 8) |
                 (uint32_t)in[o + 3u];
    }
}

static void words_to_be32(const uint32_t in[8], uint8_t out[32])
{
    size_t i;
    for (i = 0; i < 8; ++i) {
        size_t o = 28u - i * 4u;
        out[o] = (uint8_t)(in[i] >> 24);
        out[o + 1u] = (uint8_t)(in[i] >> 16);
        out[o + 2u] = (uint8_t)(in[i] >> 8);
        out[o + 3u] = (uint8_t)in[i];
    }
}

static unsigned words_bit_len(const uint32_t* a, size_t n)
{
    size_t i = n;
    while (i > 0) {
        uint32_t w;
        unsigned bits = 0;
        --i;
        w = a[i];
        if (!w) {
            continue;
        }
        while (w) {
            ++bits;
            w >>= 1;
        }
        return (unsigned)(i * 32u + bits);
    }
    return 0;
}

static uint32_t shifted_p_word(unsigned index, unsigned shift)
{
    unsigned word_shift = shift / 32u;
    unsigned bit_shift = shift % 32u;
    uint64_t v = 0;

    if (index >= word_shift && index - word_shift < 8u) {
        v |= ((uint64_t)P256_P[index - word_shift]) << bit_shift;
    }
    if (bit_shift && index > word_shift && index - word_shift - 1u < 8u) {
        v |= ((uint64_t)P256_P[index - word_shift - 1u]) >> (32u - bit_shift);
    }
    return (uint32_t)v;
}

static int words_cmp_shifted_p(const uint32_t* a, size_t n, unsigned shift)
{
    size_t i = n;
    while (i > 0) {
        uint32_t pw;
        --i;
        pw = shifted_p_word((unsigned)i, shift);
        if (a[i] > pw) {
            return 1;
        }
        if (a[i] < pw) {
            return -1;
        }
    }
    return 0;
}

static void words_sub_shifted_p(uint32_t* a, size_t n, unsigned shift)
{
    uint64_t borrow = 0;
    size_t i;
    for (i = 0; i < n; ++i) {
        uint64_t sub = (uint64_t)shifted_p_word((unsigned)i, shift) + borrow;
        uint64_t ai = (uint64_t)a[i];
        a[i] = (uint32_t)(ai - sub);
        borrow = (ai < sub) ? 1u : 0u;
    }
}

static void fe_reduce_words(const uint32_t* in, size_t n, p256_fe_t* out)
{
    uint32_t tmp[17];
    unsigned bits;
    size_t i;

    for (i = 0; i < 17; ++i) {
        tmp[i] = 0;
    }
    if (n > 17u) {
        n = 17u;
    }
    for (i = 0; i < n; ++i) {
        tmp[i] = in[i];
    }

    bits = words_bit_len(tmp, 17);
    while (bits > 256u) {
        unsigned shift = bits - 257u;
        if (words_cmp_shifted_p(tmp, 17, shift) < 0) {
            if (shift == 0) {
                break;
            }
            --shift;
        }
        words_sub_shifted_p(tmp, 17, shift);
        bits = words_bit_len(tmp, 17);
    }
    while (words_cmp(tmp, P256_P, 8) >= 0) {
        uint64_t borrow = 0;
        for (i = 0; i < 8; ++i) {
            uint64_t sub = (uint64_t)P256_P[i] + borrow;
            uint64_t ai = (uint64_t)tmp[i];
            tmp[i] = (uint32_t)(ai - sub);
            borrow = (ai < sub) ? 1u : 0u;
        }
    }
    for (i = 0; i < 8; ++i) {
        out->v[i] = tmp[i];
    }
}

static void fe_from_be(p256_fe_t* r, const uint8_t in[32])
{
    uint32_t w[8];
    be32_to_words(in, w);
    fe_reduce_words(w, 8, r);
}

static void fe_to_be(const p256_fe_t* a, uint8_t out[32])
{
    words_to_be32(a->v, out);
}

static void fe_add(p256_fe_t* r, const p256_fe_t* a, const p256_fe_t* b)
{
    uint32_t tmp[9];
    uint64_t carry = 0;
    size_t i;
    for (i = 0; i < 8; ++i) {
        uint64_t s = (uint64_t)a->v[i] + b->v[i] + carry;
        tmp[i] = (uint32_t)s;
        carry = s >> 32;
    }
    tmp[8] = (uint32_t)carry;
    fe_reduce_words(tmp, 9, r);
}

static void fe_sub(p256_fe_t* r, const p256_fe_t* a, const p256_fe_t* b)
{
    uint32_t tmp[9];
    size_t i;

    if (words_cmp(a->v, b->v, 8) >= 0) {
        uint64_t borrow = 0;
        for (i = 0; i < 8; ++i) {
            uint64_t sub = (uint64_t)b->v[i] + borrow;
            uint64_t ai = (uint64_t)a->v[i];
            tmp[i] = (uint32_t)(ai - sub);
            borrow = (ai < sub) ? 1u : 0u;
        }
        tmp[8] = 0;
    } else {
        uint64_t carry = 0;
        uint64_t borrow = 0;
        for (i = 0; i < 8; ++i) {
            uint64_t s = (uint64_t)a->v[i] + P256_P[i] + carry;
            tmp[i] = (uint32_t)s;
            carry = s >> 32;
        }
        tmp[8] = (uint32_t)carry;
        for (i = 0; i < 8; ++i) {
            uint64_t sub = (uint64_t)b->v[i] + borrow;
            uint64_t ai = (uint64_t)tmp[i];
            tmp[i] = (uint32_t)(ai - sub);
            borrow = (ai < sub) ? 1u : 0u;
        }
        if (borrow) {
            tmp[8] -= 1u;
        }
    }
    fe_reduce_words(tmp, 9, r);
}

static void fe_mul(p256_fe_t* r, const p256_fe_t* a, const p256_fe_t* b)
{
    uint32_t tmp[16];
    size_t i;
    for (i = 0; i < 16; ++i) {
        tmp[i] = 0;
    }
    for (i = 0; i < 8; ++i) {
        uint64_t carry = 0;
        size_t j;
        for (j = 0; j < 8; ++j) {
            uint64_t cur = (uint64_t)tmp[i + j] + carry + (uint64_t)a->v[i] * b->v[j];
            tmp[i + j] = (uint32_t)cur;
            carry = cur >> 32;
        }
        if (i + 8u < 16u) {
            size_t k = i + 8u;
            while (carry && k < 16u) {
                uint64_t cur = (uint64_t)tmp[k] + carry;
                tmp[k] = (uint32_t)cur;
                carry = cur >> 32;
                ++k;
            }
        }
    }
    fe_reduce_words(tmp, 16, r);
}

static void fe_sqr(p256_fe_t* r, const p256_fe_t* a)
{
    fe_mul(r, a, a);
}

static void fe_mul_small(p256_fe_t* r, const p256_fe_t* a, uint32_t m)
{
    uint32_t tmp[9];
    uint64_t carry = 0;
    size_t i;
    for (i = 0; i < 8; ++i) {
        uint64_t cur = (uint64_t)a->v[i] * m + carry;
        tmp[i] = (uint32_t)cur;
        carry = cur >> 32;
    }
    tmp[8] = (uint32_t)carry;
    fe_reduce_words(tmp, 9, r);
}

static int exp_bit_p_minus_2(unsigned bit)
{
    static const uint32_t exp[8] = {
        0xfffffffdu, 0xffffffffu, 0xffffffffu, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000001u, 0xffffffffu
    };
    return (int)((exp[bit / 32u] >> (bit % 32u)) & 1u);
}

static void fe_inv(p256_fe_t* r, const p256_fe_t* a)
{
    p256_fe_t result;
    unsigned bit;
    fe_one(&result);
    for (bit = 256u; bit > 0; --bit) {
        fe_sqr(&result, &result);
        if (exp_bit_p_minus_2(bit - 1u)) {
            fe_mul(&result, &result, a);
        }
    }
    fe_copy(r, &result);
}

static void jacobian_set_infinity(p256_jacobian_t* p)
{
    fe_zero(&p->x);
    fe_one(&p->y);
    fe_zero(&p->z);
    p->infinity = 1;
}

static void jacobian_from_affine(p256_jacobian_t* r, const p256_fe_t* x, const p256_fe_t* y)
{
    fe_copy(&r->x, x);
    fe_copy(&r->y, y);
    fe_one(&r->z);
    r->infinity = 0;
}

static void jacobian_double(p256_jacobian_t* r, const p256_jacobian_t* p)
{
    p256_fe_t xx, yy, yyyy, zz, z4, s, m, t, u, v;

    if (p->infinity || fe_is_zero(&p->y)) {
        jacobian_set_infinity(r);
        return;
    }

    fe_sqr(&xx, &p->x);
    fe_sqr(&yy, &p->y);
    fe_sqr(&yyyy, &yy);
    fe_sqr(&zz, &p->z);
    fe_sqr(&z4, &zz);

    fe_add(&u, &p->x, &yy);
    fe_sqr(&u, &u);
    fe_sub(&u, &u, &xx);
    fe_sub(&u, &u, &yyyy);
    fe_mul_small(&s, &u, 2u);

    fe_sub(&m, &xx, &z4);
    fe_mul_small(&m, &m, 3u);

    fe_sqr(&t, &m);
    fe_mul_small(&u, &s, 2u);
    fe_sub(&r->x, &t, &u);

    fe_sub(&u, &s, &r->x);
    fe_mul(&u, &m, &u);
    fe_mul_small(&v, &yyyy, 8u);
    fe_sub(&r->y, &u, &v);

    fe_add(&u, &p->y, &p->z);
    fe_sqr(&u, &u);
    fe_sub(&u, &u, &yy);
    fe_sub(&r->z, &u, &zz);
    r->infinity = 0;
}

static void jacobian_add_affine(p256_jacobian_t* r,
                                const p256_jacobian_t* p,
                                const p256_fe_t* qx,
                                const p256_fe_t* qy)
{
    p256_fe_t z1z1, u2, s2, h, hh, i, j, rr, v, t, ztmp;

    if (p->infinity) {
        jacobian_from_affine(r, qx, qy);
        return;
    }

    fe_sqr(&z1z1, &p->z);
    fe_mul(&u2, qx, &z1z1);
    fe_mul(&s2, &p->z, &z1z1);
    fe_mul(&s2, qy, &s2);
    fe_sub(&h, &u2, &p->x);
    fe_sub(&rr, &s2, &p->y);

    if (fe_is_zero(&h)) {
        if (fe_is_zero(&rr)) {
            jacobian_double(r, p);
        } else {
            jacobian_set_infinity(r);
        }
        return;
    }

    fe_sqr(&hh, &h);
    fe_mul_small(&i, &hh, 4u);
    fe_mul(&j, &h, &i);
    fe_mul_small(&rr, &rr, 2u);
    fe_mul(&v, &p->x, &i);

    fe_sqr(&t, &rr);
    fe_sub(&t, &t, &j);
    fe_mul_small(&ztmp, &v, 2u);
    fe_sub(&r->x, &t, &ztmp);

    fe_sub(&t, &v, &r->x);
    fe_mul(&t, &rr, &t);
    fe_mul(&ztmp, &p->y, &j);
    fe_mul_small(&ztmp, &ztmp, 2u);
    fe_sub(&r->y, &t, &ztmp);

    fe_add(&ztmp, &p->z, &h);
    fe_sqr(&ztmp, &ztmp);
    fe_sub(&ztmp, &ztmp, &z1z1);
    fe_sub(&r->z, &ztmp, &hh);
    r->infinity = 0;
}

static void jacobian_to_affine(const p256_jacobian_t* p, tls_p256_point_t* out)
{
    p256_fe_t zinv, z2, z3, ax, ay;

    if (p->infinity || fe_is_zero(&p->z)) {
        out->infinity = 1;
        return;
    }
    fe_inv(&zinv, &p->z);
    fe_sqr(&z2, &zinv);
    fe_mul(&z3, &z2, &zinv);
    fe_mul(&ax, &p->x, &z2);
    fe_mul(&ay, &p->y, &z3);
    fe_to_be(&ax, out->x);
    fe_to_be(&ay, out->y);
    out->infinity = 0;
}

static int point_on_curve(const p256_fe_t* x, const p256_fe_t* y)
{
    p256_fe_t lhs, rhs, t;

    fe_sqr(&lhs, y);
    fe_sqr(&rhs, x);
    fe_mul(&rhs, &rhs, x);
    fe_mul_small(&t, x, 3u);
    fe_sub(&rhs, &rhs, &t);
    fe_add(&rhs, &rhs, &P256_B);
    return fe_equal(&lhs, &rhs);
}

int tls_p256_is_valid_private_key(const uint8_t scalar[TLS_P256_SCALAR_SIZE])
{
    uint32_t k[8];
    if (!scalar) {
        return 0;
    }
    be32_to_words(scalar, k);
    if (words_is_zero(k, 8)) {
        return 0;
    }
    if (words_cmp(k, P256_N, 8) >= 0) {
        return 0;
    }
    return 1;
}

int tls_p256_point_mul(const tls_p256_point_t* point,
                       const uint8_t scalar[TLS_P256_SCALAR_SIZE],
                       tls_p256_point_t* out)
{
    p256_fe_t qx, qy;
    p256_jacobian_t acc;
    int bit;

    if (!point || !scalar || !out || point->infinity || !tls_p256_is_valid_private_key(scalar)) {
        return 0;
    }
    fe_from_be(&qx, point->x);
    fe_from_be(&qy, point->y);
    if (!point_on_curve(&qx, &qy)) {
        return 0;
    }

    jacobian_set_infinity(&acc);
    for (bit = 0; bit < 256; ++bit) {
        p256_jacobian_t next;
        if (!acc.infinity) {
            jacobian_double(&next, &acc);
            acc = next;
        }
        if ((scalar[(unsigned)bit / 8u] >> (7u - ((unsigned)bit % 8u))) & 1u) {
            jacobian_add_affine(&next, &acc, &qx, &qy);
            acc = next;
        }
    }
    jacobian_to_affine(&acc, out);
    return !out->infinity;
}

int tls_p256_base_point_mul(const uint8_t scalar[TLS_P256_SCALAR_SIZE], tls_p256_point_t* out)
{
    tls_p256_point_t g;
    if (!out || !scalar) {
        return 0;
    }
    fe_to_be(&P256_GX, g.x);
    fe_to_be(&P256_GY, g.y);
    g.infinity = 0;
    return tls_p256_point_mul(&g, scalar, out);
}

int tls_p256_encode_uncompressed(const tls_p256_point_t* point,
                                 uint8_t out[TLS_P256_UNCOMPRESSED_POINT_SIZE])
{
    size_t i;
    if (!point || !out || point->infinity) {
        return 0;
    }
    out[0] = 0x04u;
    for (i = 0; i < TLS_P256_FE_SIZE; ++i) {
        out[1u + i] = point->x[i];
        out[33u + i] = point->y[i];
    }
    return 1;
}

int tls_p256_decode_uncompressed(const uint8_t in[TLS_P256_UNCOMPRESSED_POINT_SIZE],
                                 tls_p256_point_t* out)
{
    p256_fe_t x, y;
    size_t i;
    if (!in || !out || in[0] != 0x04u) {
        return 0;
    }
    for (i = 0; i < TLS_P256_FE_SIZE; ++i) {
        out->x[i] = in[1u + i];
        out->y[i] = in[33u + i];
    }
    out->infinity = 0;
    fe_from_be(&x, out->x);
    fe_from_be(&y, out->y);
    if (!point_on_curve(&x, &y)) {
        out->infinity = 1;
        return 0;
    }
    return 1;
}

int tls_p256_ecdh_shared_secret(const tls_p256_point_t* peer_public,
                                const uint8_t private_key[TLS_P256_SCALAR_SIZE],
                                uint8_t out_x[TLS_P256_FE_SIZE])
{
    tls_p256_point_t shared;
    size_t i;
    if (!out_x) {
        return 0;
    }
    if (!tls_p256_point_mul(peer_public, private_key, &shared)) {
        return 0;
    }
    for (i = 0; i < TLS_P256_FE_SIZE; ++i) {
        out_x[i] = shared.x[i];
    }
    return 1;
}
