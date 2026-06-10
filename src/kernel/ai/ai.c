#include "ai.h"
#include "serial.h"
#include "string.h"
#include "../fs/vfs.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

static int g_ai_initialized = 0;
static ai_backend_type_t g_default_backend = AI_BACKEND_LOCAL;
static ai_model_info_t g_models[AI_MAX_MODELS];
static uint32_t g_model_count = 0;
static int g_current_model[3] = {-1, -1, -1};
static char g_ai_repo_path[AI_REPO_PATH_MAX] = "/system/models";
static char g_ai_trust_root_path[AI_TRUST_ROOT_PATH_MAX] = "/system/security/ai-trusted-keys";
static ai_trusted_key_info_t g_trusted_keys[AI_MAX_TRUSTED_KEYS];
static uint32_t g_trusted_key_count = 0;
static int g_ai_ed25519_rfc8032_validated = 0;
static int g_ai_ed25519_rfc8032_selftest_done = 0;
static uint32_t g_ai_ed25519_positive_vectors = 0;
static uint32_t g_ai_ed25519_negative_vectors = 0;
static int g_ai_ed25519_selftest_last_status = AI_STATUS_UNSUPPORTED;
static uint32_t g_ai_ed25519_malformed_vectors = 0;


static void ai_copy(char *dst, const char *src, uint32_t max_len)
{
    uint32_t i = 0;

    if (!dst || max_len == 0)
        return;

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    while (src[i] && i + 1 < max_len)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ai_append(char *dst, const char *src, uint32_t max_len)
{
    uint32_t pos = 0;
    uint32_t i = 0;

    if (!dst || !src || max_len == 0)
        return;

    while (dst[pos] && pos + 1 < max_len)
        pos++;

    while (src[i] && pos + 1 < max_len)
        dst[pos++] = src[i++];

    dst[pos] = '\0';
}

static uint32_t ai_estimate_tokens(const char *text)
{
    uint32_t len;

    if (!text)
        return 0;

    len = (uint32_t)strlen(text);
    return (len / 4) + 1;
}


static int ai_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static const char *ai_skip_inline_spaces(const char *s)
{
    while (s && (*s == ' ' || *s == '\t' || *s == '\r'))
        s++;
    return s;
}

static void ai_trim_copy(char *dst, const char *start, const char *end, uint32_t max_len)
{
    uint32_t i = 0;

    if (!dst || max_len == 0)
        return;

    if (!start || !end || end < start)
    {
        dst[0] = '\0';
        return;
    }

    while (start < end && ai_is_space(*start))
        start++;
    while (end > start && ai_is_space(*(end - 1)))
        end--;

    while (start < end && i + 1 < max_len)
        dst[i++] = *start++;
    dst[i] = '\0';
}

static void ai_print_u32(uint32_t value)
{
    char buf[11];
    uint32_t i = 0;

    if (value == 0)
    {
        serial_write("0");
        return;
    }

    while (value > 0 && i < sizeof(buf))
    {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (i > 0)
        serial_putc(buf[--i]);
}

static void ai_manifest_diag(const char *source, uint32_t line_no, const char *message, const char *key, const char *value)
{
    serial_write("ai manifest error");
    if (source && source[0])
    {
        serial_write(" in ");
        serial_write(source);
    }
    if (line_no > 0)
    {
        serial_write(":");
        ai_print_u32(line_no);
    }
    serial_write(": ");
    serial_write(message ? message : "invalid manifest");
    if (key && key[0])
    {
        serial_write(" key=");
        serial_write(key);
    }
    if (value && value[0])
    {
        serial_write(" value=");
        serial_write(value);
    }
    serial_write("\n");
}

static int ai_parse_u32_checked(const char *s, uint32_t *out)
{
    uint32_t value = 0;

    if (!s || !s[0] || !out)
        return AI_STATUS_INVALID_ARGUMENT;

    while (*s)
    {
        if (*s < '0' || *s > '9')
            return AI_STATUS_INVALID_ARGUMENT;
        value = value * 10U + (uint32_t)(*s - '0');
        s++;
    }

    *out = value;
    return AI_STATUS_OK;
}

static int ai_manifest_copy_field(char *dst, const char *value, uint32_t max_len, const char *source, uint32_t line_no, const char *key)
{
    if (!dst || !value || max_len == 0)
        return AI_STATUS_INVALID_ARGUMENT;

    if ((uint32_t)strlen(value) >= max_len)
    {
        ai_manifest_diag(source, line_no, "field value too long", key, value);
        return AI_STATUS_BUFFER_TOO_SMALL;
    }

    ai_copy(dst, value, max_len);
    return AI_STATUS_OK;
}

static int ai_hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int ai_hex_string_valid(const char *hex, uint32_t expected_len)
{
    uint32_t i;

    if (!hex || strlen(hex) != expected_len)
        return 0;

    for (i = 0; i < expected_len; i++)
    {
        if (ai_hex_value(hex[i]) < 0)
            return 0;
    }

    return 1;
}

static int ai_sha256_hex_valid(const char *hex)
{
    return ai_hex_string_valid(hex, AI_MODEL_SHA256_HEX_MAX - 1U);
}

static int ai_sha256_hex_equal(const char *a, const char *b)
{
    uint32_t i;

    if (!a || !b)
        return 0;

    for (i = 0; i < 64; i++)
    {
        if (ai_hex_value(a[i]) != ai_hex_value(b[i]))
            return 0;
    }

    return a[64] == '\0' && b[64] == '\0';
}

static int ai_hex_to_bytes(const char *hex, uint32_t expected_hex_len, uint8_t *out, uint32_t out_len)
{
    uint32_t i;
    int hi;
    int lo;

    if (!hex || !out || expected_hex_len != out_len * 2U)
        return AI_STATUS_INVALID_ARGUMENT;

    if (!ai_hex_string_valid(hex, expected_hex_len))
        return AI_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < out_len; i++)
    {
        hi = ai_hex_value(hex[i * 2U]);
        lo = ai_hex_value(hex[i * 2U + 1U]);
        out[i] = (uint8_t)(((uint32_t)hi << 4) | (uint32_t)lo);
    }

    return AI_STATUS_OK;
}

static uint32_t ai_ct_is_zero_u32(uint32_t x)
{
    return ((x | (0U - x)) >> 31) ^ 1U;
}

static uint32_t ai_ct_select_u32(uint32_t mask, uint32_t a, uint32_t b)
{
    return (a & mask) | (b & ~mask);
}

static int ai_ct_mem_equal(const void *a, const void *b, uint32_t len)
{
    const uint8_t *aa = (const uint8_t *)a;
    const uint8_t *bb = (const uint8_t *)b;
    uint32_t diff = 0;
    uint32_t i;

    if (!a || !b)
        return 0;

    for (i = 0; i < len; i++)
        diff |= (uint32_t)(aa[i] ^ bb[i]);

    return (int)ai_ct_is_zero_u32(diff);
}

static int ai_le_bytes_less_than(const uint8_t *value, const uint8_t *limit, uint32_t len)
{
    uint32_t i;
    uint32_t less = 0;
    uint32_t greater = 0;

    if (!value || !limit || len == 0)
        return 0;

    i = len;
    while (i > 0)
    {
        uint32_t x;
        uint32_t y;
        uint32_t lt;
        uint32_t gt;
        uint32_t undecided;

        i--;
        x = value[i];
        y = limit[i];
        lt = (x - y) >> 31;
        gt = (y - x) >> 31;
        undecided = (~(less | greater)) & 1U;
        less |= lt & undecided;
        greater |= gt & undecided;
    }

    return (int)(less & 1U);
}

static int ai_ed25519_encoded_point_canonical(const uint8_t point[32])
{
    static const uint8_t p_le[32] = {
        0xedU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0x7fU
    };
    uint8_t y[32];

    if (!point)
        return 0;

    memcpy(y, point, sizeof(y));
    y[31] &= 0x7fU;
    return ai_le_bytes_less_than(y, p_le, 32);
}

static int ai_ed25519_scalar_s_canonical(const uint8_t scalar[32])
{
    static const uint8_t l_le[32] = {
        0xedU, 0xd3U, 0xf5U, 0x5cU, 0x1aU, 0x63U, 0x12U, 0x58U,
        0xd6U, 0x9cU, 0xf7U, 0xa2U, 0xdeU, 0xf9U, 0xdeU, 0x14U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x10U
    };

    return scalar && ai_le_bytes_less_than(scalar, l_le, 32);
}

#define AI_FE25519_LIMBS 16U

typedef struct ai_fe25519 {
    uint32_t limb[AI_FE25519_LIMBS];
} ai_fe25519_t;

typedef struct ai_ed25519_point {
    ai_fe25519_t x;
    ai_fe25519_t y;
    ai_fe25519_t z;
    ai_fe25519_t t;
} ai_ed25519_point_t;

typedef struct ai_ed25519_scalar {
    uint8_t bytes[32];
} ai_ed25519_scalar_t;

static const uint32_t g_ai_fe25519_p[AI_FE25519_LIMBS] = {
    0xffedU, 0xffffU, 0xffffU, 0xffffU, 0xffffU, 0xffffU, 0xffffU, 0xffffU,
    0xffffU, 0xffffU, 0xffffU, 0xffffU, 0xffffU, 0xffffU, 0xffffU, 0x7fffU
};

static const uint8_t g_ai_ed25519_d_le[32] = {
    0xa3U, 0x78U, 0x59U, 0x13U, 0xcaU, 0x4dU, 0xebU, 0x75U,
    0xabU, 0xd8U, 0x41U, 0x41U, 0x4dU, 0x0aU, 0x70U, 0x00U,
    0x98U, 0xe8U, 0x79U, 0x77U, 0x79U, 0x40U, 0xc7U, 0x8cU,
    0x73U, 0xfeU, 0x6fU, 0x2bU, 0xeeU, 0x6cU, 0x03U, 0x52U
};

static const uint8_t g_ai_ed25519_l_le[32] = {
    0xedU, 0xd3U, 0xf5U, 0x5cU, 0x1aU, 0x63U, 0x12U, 0x58U,
    0xd6U, 0x9cU, 0xf7U, 0xa2U, 0xdeU, 0xf9U, 0xdeU, 0x14U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x10U
};

static const uint8_t g_ai_ed25519_basepoint_compressed[32] = {
    0x58U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U,
    0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U,
    0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U,
    0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U, 0x66U
};

static void ai_fe25519_zero(ai_fe25519_t *r)
{
    memset(r, 0, sizeof(*r));
}

static void ai_fe25519_one(ai_fe25519_t *r)
{
    ai_fe25519_zero(r);
    r->limb[0] = 1U;
}

static void ai_fe25519_copy(ai_fe25519_t *r, const ai_fe25519_t *a)
{
    memcpy(r, a, sizeof(*r));
}

static int ai_fe25519_ge_p(const ai_fe25519_t *a)
{
    uint32_t i = AI_FE25519_LIMBS;

    while (i > 0U)
    {
        i--;
        if (a->limb[i] > g_ai_fe25519_p[i])
            return 1;
        if (a->limb[i] < g_ai_fe25519_p[i])
            return 0;
    }

    return 1;
}

static void ai_fe25519_sub_p(ai_fe25519_t *a)
{
    uint32_t i;
    uint32_t borrow = 0;

    for (i = 0; i < AI_FE25519_LIMBS; i++)
    {
        uint32_t sub = g_ai_fe25519_p[i] + borrow;
        if (a->limb[i] >= sub)
        {
            a->limb[i] -= sub;
            borrow = 0;
        }
        else
        {
            a->limb[i] = (uint32_t)(0x10000U + a->limb[i] - sub);
            borrow = 1;
        }
    }
}

static void ai_fe25519_normalize(ai_fe25519_t *a)
{
    uint32_t pass;

    for (pass = 0; pass < 5U; pass++)
    {
        uint32_t i;
        uint32_t carry;

        for (i = 0; i + 1U < AI_FE25519_LIMBS; i++)
        {
            carry = a->limb[i] >> 16;
            a->limb[i] &= 0xffffU;
            a->limb[i + 1U] += carry;
        }

        carry = a->limb[15] >> 15;
        a->limb[15] &= 0x7fffU;
        a->limb[0] += carry * 19U;
    }

    while (ai_fe25519_ge_p(a))
        ai_fe25519_sub_p(a);
}

static void ai_fe25519_from_bytes(ai_fe25519_t *r, const uint8_t bytes[32])
{
    uint32_t i;

    for (i = 0; i < AI_FE25519_LIMBS; i++)
        r->limb[i] = (uint32_t)bytes[i * 2U] | ((uint32_t)bytes[i * 2U + 1U] << 8);

    r->limb[15] &= 0x7fffU;
    ai_fe25519_normalize(r);
}

static int ai_fe25519_equal(const ai_fe25519_t *a, const ai_fe25519_t *b)
{
    ai_fe25519_t aa;
    ai_fe25519_t bb;

    ai_fe25519_copy(&aa, a);
    ai_fe25519_copy(&bb, b);
    ai_fe25519_normalize(&aa);
    ai_fe25519_normalize(&bb);
    return ai_ct_mem_equal(&aa, &bb, sizeof(aa));
}

static void ai_fe25519_add(ai_fe25519_t *r, const ai_fe25519_t *a, const ai_fe25519_t *b)
{
    uint32_t i;

    for (i = 0; i < AI_FE25519_LIMBS; i++)
        r->limb[i] = a->limb[i] + b->limb[i];

    ai_fe25519_normalize(r);
}

static void ai_fe25519_sub(ai_fe25519_t *r, const ai_fe25519_t *a, const ai_fe25519_t *b)
{
    uint32_t i;

    for (i = 0; i < AI_FE25519_LIMBS; i++)
        r->limb[i] = a->limb[i] + g_ai_fe25519_p[i] + g_ai_fe25519_p[i] - b->limb[i];

    ai_fe25519_normalize(r);
}

static void ai_fe25519_mul_small(ai_fe25519_t *r, const ai_fe25519_t *a, uint32_t m)
{
    uint32_t i;

    for (i = 0; i < AI_FE25519_LIMBS; i++)
        r->limb[i] = a->limb[i] * m;

    ai_fe25519_normalize(r);
}

static void ai_fe25519_mul(ai_fe25519_t *r, const ai_fe25519_t *a, const ai_fe25519_t *b)
{
    uint64_t tmp[31];
    uint32_t i;
    ai_fe25519_t out;

    memset(tmp, 0, sizeof(tmp));

    for (i = 0; i < AI_FE25519_LIMBS; i++)
    {
        uint32_t j;
        for (j = 0; j < AI_FE25519_LIMBS; j++)
            tmp[i + j] += (uint64_t)a->limb[i] * (uint64_t)b->limb[j];
    }

    i = 31U;
    while (i > 16U)
    {
        i--;
        tmp[i - 16U] += tmp[i] * 38ULL;
    }

    for (i = 0; i < AI_FE25519_LIMBS; i++)
        out.limb[i] = (uint32_t)tmp[i];

    ai_fe25519_normalize(&out);
    ai_fe25519_copy(r, &out);
}

static void ai_fe25519_square(ai_fe25519_t *r, const ai_fe25519_t *a)
{
    ai_fe25519_mul(r, a, a);
}

static void ai_fe25519_neg(ai_fe25519_t *r, const ai_fe25519_t *a)
{
    ai_fe25519_t zero;

    ai_fe25519_zero(&zero);
    ai_fe25519_sub(r, &zero, a);
}

static int ai_fe25519_is_zero(const ai_fe25519_t *a)
{
    ai_fe25519_t zero;

    ai_fe25519_zero(&zero);
    return ai_fe25519_equal(a, &zero);
}

static int ai_fe25519_is_negative(const ai_fe25519_t *a)
{
    ai_fe25519_t aa;

    ai_fe25519_copy(&aa, a);
    ai_fe25519_normalize(&aa);
    return (aa.limb[0] & 1U) != 0U;
}

static int ai_fe25519_exp_bit(const uint8_t exponent[32], uint32_t bit)
{
    return (exponent[bit / 8U] & (uint8_t)(1U << (bit % 8U))) != 0U;
}

static void ai_fe25519_pow(ai_fe25519_t *r, const ai_fe25519_t *a, const uint8_t exponent[32])
{
    ai_fe25519_t result;
    ai_fe25519_t base;
    uint32_t bit;

    ai_fe25519_one(&result);
    ai_fe25519_copy(&base, a);

    for (bit = 0; bit < 255U; bit++)
    {
        if (ai_fe25519_exp_bit(exponent, bit))
            ai_fe25519_mul(&result, &result, &base);
        ai_fe25519_square(&base, &base);
    }

    ai_fe25519_copy(r, &result);
}

static void ai_fe25519_invert(ai_fe25519_t *r, const ai_fe25519_t *a)
{
    static const uint8_t p_minus_2[32] = {
        0xebU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0x7fU
    };

    ai_fe25519_pow(r, a, p_minus_2);
}

static void ai_fe25519_pow_p_minus_5_over_8(ai_fe25519_t *r, const ai_fe25519_t *a)
{
    static const uint8_t p_minus_5_over_8[32] = {
        0xfdU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0x0fU
    };

    ai_fe25519_pow(r, a, p_minus_5_over_8);
}

static int ai_fe25519_sqrt_ratio(ai_fe25519_t *r, const ai_fe25519_t *u, const ai_fe25519_t *v)
{
    static const uint8_t sqrt_m1_le[32] = {
        0xb0U, 0xa0U, 0x0eU, 0x4aU, 0x27U, 0x1bU, 0xeeU, 0xc4U,
        0x78U, 0xe4U, 0x2fU, 0xadU, 0x06U, 0x18U, 0x43U, 0x2fU,
        0xa7U, 0xd7U, 0xfbU, 0x3dU, 0x99U, 0x00U, 0x4dU, 0x2bU,
        0x0bU, 0xdfU, 0xc1U, 0x4fU, 0x80U, 0x24U, 0x83U, 0x2bU
    };
    ai_fe25519_t v2;
    ai_fe25519_t v3;
    ai_fe25519_t v7;
    ai_fe25519_t uv7;
    ai_fe25519_t x;
    ai_fe25519_t vx2;
    ai_fe25519_t check;
    ai_fe25519_t minus_u;
    ai_fe25519_t sqrt_m1;

    ai_fe25519_square(&v2, v);
    ai_fe25519_mul(&v3, &v2, v);
    ai_fe25519_square(&v7, &v2);
    ai_fe25519_mul(&v7, &v7, &v3);
    ai_fe25519_mul(&uv7, u, &v7);
    ai_fe25519_pow_p_minus_5_over_8(&x, &uv7);
    ai_fe25519_mul(&x, &x, &v3);
    ai_fe25519_mul(&x, &x, u);

    ai_fe25519_square(&vx2, &x);
    ai_fe25519_mul(&vx2, &vx2, v);
    ai_fe25519_sub(&check, &vx2, u);
    if (ai_fe25519_is_zero(&check))
    {
        ai_fe25519_copy(r, &x);
        return 1;
    }

    ai_fe25519_neg(&minus_u, u);
    ai_fe25519_sub(&check, &vx2, &minus_u);
    if (!ai_fe25519_is_zero(&check))
        return 0;

    ai_fe25519_from_bytes(&sqrt_m1, sqrt_m1_le);
    ai_fe25519_mul(&x, &x, &sqrt_m1);
    ai_fe25519_copy(r, &x);
    return 1;
}

static int ai_ed25519_scalar_ge_l(const uint8_t scalar[32])
{
    return scalar && !ai_le_bytes_less_than(scalar, g_ai_ed25519_l_le, 32);
}

static void ai_ed25519_scalar_sub_l(uint8_t scalar[32])
{
    uint32_t i;
    uint32_t borrow = 0;

    for (i = 0; i < 32U; i++)
    {
        uint32_t sub = (uint32_t)g_ai_ed25519_l_le[i] + borrow;
        if ((uint32_t)scalar[i] >= sub)
        {
            scalar[i] = (uint8_t)((uint32_t)scalar[i] - sub);
            borrow = 0;
        }
        else
        {
            scalar[i] = (uint8_t)(0x100U + (uint32_t)scalar[i] - sub);
            borrow = 1;
        }
    }
}

static void ai_ed25519_scalar_shift_left_one(uint8_t scalar[32])
{
    uint32_t i;
    uint32_t carry = 0;

    for (i = 0; i < 32U; i++)
    {
        uint32_t value = ((uint32_t)scalar[i] << 1) | carry;
        scalar[i] = (uint8_t)(value & 0xffU);
        carry = (value >> 8) & 1U;
    }
}

static int ai_ed25519_scalar_bit_from_wide(const uint8_t wide[64], uint32_t bit)
{
    return (wide[bit / 8U] & (uint8_t)(1U << (bit % 8U))) != 0U;
}

static int ai_ed25519_scalar_bit(const ai_ed25519_scalar_t *scalar, uint32_t bit)
{
    return (scalar->bytes[bit / 8U] & (uint8_t)(1U << (bit % 8U))) != 0U;
}

static void ai_ed25519_scalar_zero(ai_ed25519_scalar_t *scalar)
{
    memset(scalar->bytes, 0, sizeof(scalar->bytes));
}

static int ai_ed25519_scalar_from_canonical(ai_ed25519_scalar_t *out, const uint8_t bytes[32])
{
    if (!out || !bytes)
        return AI_STATUS_INVALID_ARGUMENT;

    if (!ai_ed25519_scalar_s_canonical(bytes))
        return AI_STATUS_SECURITY_FAILED;

    memcpy(out->bytes, bytes, sizeof(out->bytes));
    return AI_STATUS_OK;
}

static int ai_ed25519_scalar_reduce_wide(ai_ed25519_scalar_t *out, const uint8_t wide[64])
{
    int32_t bit;

    if (!out || !wide)
        return AI_STATUS_INVALID_ARGUMENT;

    ai_ed25519_scalar_zero(out);
    for (bit = 511; bit >= 0; bit--)
    {
        ai_ed25519_scalar_shift_left_one(out->bytes);
        if (ai_ed25519_scalar_bit_from_wide(wide, (uint32_t)bit))
            out->bytes[0] |= 1U;
        if (ai_ed25519_scalar_ge_l(out->bytes))
            ai_ed25519_scalar_sub_l(out->bytes);
    }

    return AI_STATUS_OK;
}

static int ai_ed25519_decompress_point(ai_ed25519_point_t *out, const uint8_t encoded[32])
{
    uint8_t y_bytes[32];
    uint32_t sign;
    ai_fe25519_t y;
    ai_fe25519_t y2;
    ai_fe25519_t one;
    ai_fe25519_t u;
    ai_fe25519_t v;
    ai_fe25519_t d_const;
    ai_fe25519_t x;

    if (!out || !encoded)
        return AI_STATUS_INVALID_ARGUMENT;

    if (!ai_ed25519_encoded_point_canonical(encoded))
        return AI_STATUS_SECURITY_FAILED;

    memcpy(y_bytes, encoded, sizeof(y_bytes));
    sign = (uint32_t)((y_bytes[31] >> 7) & 1U);
    y_bytes[31] &= 0x7fU;

    ai_fe25519_from_bytes(&y, y_bytes);
    ai_fe25519_square(&y2, &y);
    ai_fe25519_one(&one);

    ai_fe25519_sub(&u, &y2, &one);
    ai_fe25519_from_bytes(&d_const, g_ai_ed25519_d_le);
    ai_fe25519_mul(&v, &d_const, &y2);
    ai_fe25519_add(&v, &v, &one);

    if (!ai_fe25519_sqrt_ratio(&x, &u, &v))
        return AI_STATUS_SECURITY_FAILED;

    if (ai_fe25519_is_zero(&x) && sign != 0U)
        return AI_STATUS_SECURITY_FAILED;

    if ((uint32_t)ai_fe25519_is_negative(&x) != sign)
        ai_fe25519_neg(&x, &x);

    ai_fe25519_copy(&out->x, &x);
    ai_fe25519_copy(&out->y, &y);
    ai_fe25519_one(&out->z);
    ai_fe25519_mul(&out->t, &x, &y);
    return AI_STATUS_OK;
}

static int ai_ed25519_point_on_curve(const ai_ed25519_point_t *p)
{
    ai_fe25519_t x2;
    ai_fe25519_t y2;
    ai_fe25519_t lhs;
    ai_fe25519_t rhs;
    ai_fe25519_t one;
    ai_fe25519_t d_const;
    ai_fe25519_t dx2y2;

    ai_fe25519_square(&x2, &p->x);
    ai_fe25519_square(&y2, &p->y);
    ai_fe25519_sub(&lhs, &y2, &x2);

    ai_fe25519_from_bytes(&d_const, g_ai_ed25519_d_le);
    ai_fe25519_mul(&dx2y2, &x2, &y2);
    ai_fe25519_mul(&dx2y2, &dx2y2, &d_const);
    ai_fe25519_one(&one);
    ai_fe25519_add(&rhs, &one, &dx2y2);

    return ai_fe25519_equal(&lhs, &rhs);
}

static void ai_ed25519_point_double(ai_ed25519_point_t *r, const ai_ed25519_point_t *p);
static int ai_ed25519_point_is_identity(const ai_ed25519_point_t *p);

static void ai_ed25519_point_copy(ai_ed25519_point_t *r, const ai_ed25519_point_t *p)
{
    memcpy(r, p, sizeof(*r));
}

static int ai_ed25519_point_equal_projective(const ai_ed25519_point_t *a, const ai_ed25519_point_t *b)
{
    ai_fe25519_t axbz;
    ai_fe25519_t bxaz;
    ai_fe25519_t aybz;
    ai_fe25519_t byaz;
    int x_equal;
    int y_equal;

    ai_fe25519_mul(&axbz, &a->x, &b->z);
    ai_fe25519_mul(&bxaz, &b->x, &a->z);
    ai_fe25519_mul(&aybz, &a->y, &b->z);
    ai_fe25519_mul(&byaz, &b->y, &a->z);

    x_equal = ai_fe25519_equal(&axbz, &bxaz);
    y_equal = ai_fe25519_equal(&aybz, &byaz);
    return x_equal & y_equal;
}

static void ai_ed25519_point_cmov(ai_ed25519_point_t *r, const ai_ed25519_point_t *a, uint32_t select)
{
    uint32_t mask = 0U - (select & 1U);
    uint32_t i;

    for (i = 0; i < AI_FE25519_LIMBS; i++)
    {
        r->x.limb[i] = ai_ct_select_u32(mask, a->x.limb[i], r->x.limb[i]);
        r->y.limb[i] = ai_ct_select_u32(mask, a->y.limb[i], r->y.limb[i]);
        r->z.limb[i] = ai_ct_select_u32(mask, a->z.limb[i], r->z.limb[i]);
        r->t.limb[i] = ai_ct_select_u32(mask, a->t.limb[i], r->t.limb[i]);
    }
}

static int ai_ed25519_point_has_small_order(const ai_ed25519_point_t *p)
{
    ai_ed25519_point_t q;
    uint32_t i;

    if (!p)
        return 1;

    ai_ed25519_point_copy(&q, p);
    for (i = 0; i < 3U; i++)
        ai_ed25519_point_double(&q, &q);

    return ai_ed25519_point_is_identity(&q);
}

static void ai_ed25519_point_identity(ai_ed25519_point_t *p)
{
    ai_fe25519_zero(&p->x);
    ai_fe25519_one(&p->y);
    ai_fe25519_one(&p->z);
    ai_fe25519_zero(&p->t);
}

static int ai_ed25519_point_is_identity(const ai_ed25519_point_t *p)
{
    ai_fe25519_t zero;
    ai_fe25519_t one;

    ai_fe25519_zero(&zero);
    ai_fe25519_one(&one);
    return ai_fe25519_equal(&p->x, &zero) &&
           ai_fe25519_equal(&p->y, &one) &&
           ai_fe25519_equal(&p->z, &one) &&
           ai_fe25519_equal(&p->t, &zero);
}

static void ai_ed25519_point_add(ai_ed25519_point_t *r, const ai_ed25519_point_t *p, const ai_ed25519_point_t *q)
{
    ai_fe25519_t a;
    ai_fe25519_t b;
    ai_fe25519_t c;
    ai_fe25519_t d;
    ai_fe25519_t e;
    ai_fe25519_t f;
    ai_fe25519_t g;
    ai_fe25519_t h;
    ai_fe25519_t p_y_minus_x;
    ai_fe25519_t q_y_minus_x;
    ai_fe25519_t p_y_plus_x;
    ai_fe25519_t q_y_plus_x;
    ai_fe25519_t d_const;
    ai_fe25519_t two_d;

    ai_fe25519_sub(&p_y_minus_x, &p->y, &p->x);
    ai_fe25519_sub(&q_y_minus_x, &q->y, &q->x);
    ai_fe25519_mul(&a, &p_y_minus_x, &q_y_minus_x);

    ai_fe25519_add(&p_y_plus_x, &p->y, &p->x);
    ai_fe25519_add(&q_y_plus_x, &q->y, &q->x);
    ai_fe25519_mul(&b, &p_y_plus_x, &q_y_plus_x);

    ai_fe25519_from_bytes(&d_const, g_ai_ed25519_d_le);
    ai_fe25519_mul_small(&two_d, &d_const, 2U);
    ai_fe25519_mul(&c, &p->t, &q->t);
    ai_fe25519_mul(&c, &c, &two_d);

    ai_fe25519_mul(&d, &p->z, &q->z);
    ai_fe25519_mul_small(&d, &d, 2U);

    ai_fe25519_sub(&e, &b, &a);
    ai_fe25519_sub(&f, &d, &c);
    ai_fe25519_add(&g, &d, &c);
    ai_fe25519_add(&h, &b, &a);

    ai_fe25519_mul(&r->x, &e, &f);
    ai_fe25519_mul(&r->y, &g, &h);
    ai_fe25519_mul(&r->t, &e, &h);
    ai_fe25519_mul(&r->z, &f, &g);
}

static void ai_ed25519_point_double(ai_ed25519_point_t *r, const ai_ed25519_point_t *p)
{
    ai_ed25519_point_add(r, p, p);
}

static void ai_ed25519_scalar_multiply_point(ai_ed25519_point_t *out, const ai_ed25519_scalar_t *scalar, const ai_ed25519_point_t *point)
{
    ai_ed25519_point_t result;
    int32_t bit;

    ai_ed25519_point_identity(&result);
    for (bit = 255; bit >= 0; bit--)
    {
        ai_ed25519_point_t added;

        ai_ed25519_point_double(&result, &result);
        ai_ed25519_point_add(&added, &result, point);
        ai_ed25519_point_cmov(&result, &added, (uint32_t)ai_ed25519_scalar_bit(scalar, (uint32_t)bit));
    }

    ai_ed25519_point_copy(out, &result);
}

static int ai_ed25519_basepoint_scalar_multiply(ai_ed25519_point_t *out, const ai_ed25519_scalar_t *scalar)
{
    ai_ed25519_point_t base;
    int status;

    if (!out || !scalar)
        return AI_STATUS_INVALID_ARGUMENT;

    status = ai_ed25519_decompress_point(&base, g_ai_ed25519_basepoint_compressed);
    if (status != AI_STATUS_OK)
        return status;

    if (!ai_ed25519_point_on_curve(&base))
        return AI_STATUS_SECURITY_FAILED;

    ai_ed25519_scalar_multiply_point(out, scalar, &base);
    return AI_STATUS_OK;
}

static int ai_ed25519_point_scalar_multiply(ai_ed25519_point_t *out, const ai_ed25519_point_t *point, const ai_ed25519_scalar_t *scalar)
{
    if (!out || !point || !scalar)
        return AI_STATUS_INVALID_ARGUMENT;

    ai_ed25519_scalar_multiply_point(out, scalar, point);
    return AI_STATUS_OK;
}

static int ai_ed25519_verify_equation_probe(const ai_ed25519_point_t *r_point, const ai_ed25519_point_t *public_point,
                                            const ai_ed25519_scalar_t *s_scalar, const ai_ed25519_scalar_t *h_scalar)
{
    ai_ed25519_point_t s_base;
    ai_ed25519_point_t h_public;
    ai_ed25519_point_t rhs;
    int status;

    if (!r_point || !public_point || !s_scalar || !h_scalar)
        return AI_STATUS_INVALID_ARGUMENT;

    status = ai_ed25519_basepoint_scalar_multiply(&s_base, s_scalar);
    if (status != AI_STATUS_OK)
        return status;

    status = ai_ed25519_point_scalar_multiply(&h_public, public_point, h_scalar);
    if (status != AI_STATUS_OK)
        return status;

    ai_ed25519_point_add(&rhs, r_point, &h_public);
    return ai_ed25519_point_equal_projective(&s_base, &rhs) ? 1 : 0;
}

static int ai_ed25519_group_selfcheck(void)
{
    ai_fe25519_t one;
    ai_fe25519_t two;
    ai_fe25519_t sq;
    ai_fe25519_t inv_two;
    ai_fe25519_t prod;
    ai_ed25519_point_t id;
    ai_ed25519_point_t sum;
    ai_ed25519_point_t dbl;
    ai_ed25519_scalar_t scalar;
    ai_ed25519_point_t scaled;

    ai_fe25519_one(&one);
    ai_fe25519_add(&two, &one, &one);
    ai_fe25519_square(&sq, &two);
    ai_fe25519_normalize(&sq);
    if (sq.limb[0] != 4U)
        return 0;

    ai_fe25519_invert(&inv_two, &two);
    ai_fe25519_mul(&prod, &two, &inv_two);
    if (!ai_fe25519_equal(&prod, &one))
        return 0;

    ai_ed25519_point_identity(&id);
    ai_ed25519_point_add(&sum, &id, &id);
    ai_ed25519_point_double(&dbl, &id);
    if (!ai_ed25519_point_is_identity(&sum) || !ai_ed25519_point_is_identity(&dbl))
        return 0;

    ai_ed25519_scalar_zero(&scalar);
    if (ai_ed25519_basepoint_scalar_multiply(&scaled, &scalar) != AI_STATUS_OK ||
        !ai_ed25519_point_is_identity(&scaled))
        return 0;

    scalar.bytes[0] = 1U;
    if (ai_ed25519_basepoint_scalar_multiply(&scaled, &scalar) != AI_STATUS_OK ||
        !ai_ed25519_point_on_curve(&scaled))
        return 0;

    {
        static const uint8_t identity_encoded[32] = {
            0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
            0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
            0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
            0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
        };
        ai_ed25519_point_t decoded;

        if (ai_ed25519_decompress_point(&decoded, identity_encoded) != AI_STATUS_OK)
            return 0;
        if (!ai_ed25519_point_is_identity(&decoded) || !ai_ed25519_point_on_curve(&decoded))
            return 0;
    }

    return 1;
}

typedef struct ai_sha512_ctx {
    uint64_t state[8];
    uint64_t bit_len_hi;
    uint64_t bit_len_lo;
    uint8_t data[128];
    uint32_t data_len;
} ai_sha512_ctx_t;

static const uint64_t g_ai_sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static uint64_t ai_sha512_rotr(uint64_t value, uint32_t bits)
{
    return (value >> bits) | (value << (64U - bits));
}

static void ai_sha512_add_bits(ai_sha512_ctx_t *ctx, uint64_t bits)
{
    uint64_t old = ctx->bit_len_lo;

    ctx->bit_len_lo += bits;
    if (ctx->bit_len_lo < old)
        ctx->bit_len_hi++;
}

static void ai_sha512_transform(ai_sha512_ctx_t *ctx, const uint8_t data[128])
{
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
    uint64_t e;
    uint64_t f;
    uint64_t g;
    uint64_t h;
    uint64_t t1;
    uint64_t t2;
    uint64_t m[80];
    uint32_t i;
    uint32_t j;

    for (i = 0, j = 0; i < 16; i++, j += 8)
    {
        m[i] = ((uint64_t)data[j] << 56) | ((uint64_t)data[j + 1] << 48) |
               ((uint64_t)data[j + 2] << 40) | ((uint64_t)data[j + 3] << 32) |
               ((uint64_t)data[j + 4] << 24) | ((uint64_t)data[j + 5] << 16) |
               ((uint64_t)data[j + 6] << 8) | (uint64_t)data[j + 7];
    }

    for (; i < 80; i++)
    {
        uint64_t s0 = ai_sha512_rotr(m[i - 15], 1) ^ ai_sha512_rotr(m[i - 15], 8) ^ (m[i - 15] >> 7);
        uint64_t s1 = ai_sha512_rotr(m[i - 2], 19) ^ ai_sha512_rotr(m[i - 2], 61) ^ (m[i - 2] >> 6);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 80; i++)
    {
        uint64_t s1 = ai_sha512_rotr(e, 14) ^ ai_sha512_rotr(e, 18) ^ ai_sha512_rotr(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t s0 = ai_sha512_rotr(a, 28) ^ ai_sha512_rotr(a, 34) ^ ai_sha512_rotr(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + s1 + ch + g_ai_sha512_k[i] + m[i];
        t2 = s0 + maj;
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

static void ai_sha512_init(ai_sha512_ctx_t *ctx)
{
    ctx->data_len = 0;
    ctx->bit_len_hi = 0;
    ctx->bit_len_lo = 0;
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
}

static void ai_sha512_update(ai_sha512_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++)
    {
        ctx->data[ctx->data_len++] = data[i];
        if (ctx->data_len == 128)
        {
            ai_sha512_transform(ctx, ctx->data);
            ai_sha512_add_bits(ctx, 1024ULL);
            ctx->data_len = 0;
        }
    }
}

static void ai_sha512_final(ai_sha512_ctx_t *ctx, uint8_t hash[64])
{
    uint32_t i = ctx->data_len;
    uint32_t j;

    ai_sha512_add_bits(ctx, (uint64_t)ctx->data_len * 8ULL);

    if (ctx->data_len < 112)
    {
        ctx->data[i++] = 0x80U;
        while (i < 112)
            ctx->data[i++] = 0x00U;
    }
    else
    {
        ctx->data[i++] = 0x80U;
        while (i < 128)
            ctx->data[i++] = 0x00U;
        ai_sha512_transform(ctx, ctx->data);
        memset(ctx->data, 0, 112);
    }

    ctx->data[112] = (uint8_t)(ctx->bit_len_hi >> 56);
    ctx->data[113] = (uint8_t)(ctx->bit_len_hi >> 48);
    ctx->data[114] = (uint8_t)(ctx->bit_len_hi >> 40);
    ctx->data[115] = (uint8_t)(ctx->bit_len_hi >> 32);
    ctx->data[116] = (uint8_t)(ctx->bit_len_hi >> 24);
    ctx->data[117] = (uint8_t)(ctx->bit_len_hi >> 16);
    ctx->data[118] = (uint8_t)(ctx->bit_len_hi >> 8);
    ctx->data[119] = (uint8_t)(ctx->bit_len_hi);
    ctx->data[120] = (uint8_t)(ctx->bit_len_lo >> 56);
    ctx->data[121] = (uint8_t)(ctx->bit_len_lo >> 48);
    ctx->data[122] = (uint8_t)(ctx->bit_len_lo >> 40);
    ctx->data[123] = (uint8_t)(ctx->bit_len_lo >> 32);
    ctx->data[124] = (uint8_t)(ctx->bit_len_lo >> 24);
    ctx->data[125] = (uint8_t)(ctx->bit_len_lo >> 16);
    ctx->data[126] = (uint8_t)(ctx->bit_len_lo >> 8);
    ctx->data[127] = (uint8_t)(ctx->bit_len_lo);

    ai_sha512_transform(ctx, ctx->data);

    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < 8; j++)
            hash[i * 8U + j] = (uint8_t)((ctx->state[i] >> (56U - j * 8U)) & 0xffU);
    }
}

static void ai_ed25519_challenge_hash(const uint8_t r[32], const uint8_t a[32], const uint8_t *message,
                                         uint32_t message_len, uint8_t out[64])
{
    ai_sha512_ctx_t ctx;

    ai_sha512_init(&ctx);
    ai_sha512_update(&ctx, r, 32);
    ai_sha512_update(&ctx, a, 32);
    if (message && message_len > 0)
        ai_sha512_update(&ctx, message, message_len);
    ai_sha512_final(&ctx, out);
}

static int ai_algorithm_is_ed25519(const char *algorithm)
{
    return algorithm && strcmp(algorithm, "ed25519") == 0;
}

static int ai_ed25519_verify_message_bytes(const uint8_t public_key[32], const uint8_t *message, uint32_t message_len,
                                           const uint8_t signature[64])
{
    uint8_t challenge[64];
    ai_ed25519_point_t public_point;
    ai_ed25519_point_t r_point;
    ai_ed25519_scalar_t s_scalar;
    ai_ed25519_scalar_t h_scalar;
    int equation_status;
    int status;

    if (!public_key || (!message && message_len > 0) || !signature)
        return AI_STATUS_INVALID_ARGUMENT;

    if (!ai_ed25519_encoded_point_canonical(public_key))
        return AI_STATUS_SECURITY_FAILED;

    if (!ai_ed25519_encoded_point_canonical(signature))
        return AI_STATUS_SECURITY_FAILED;

    if (!ai_ed25519_scalar_s_canonical(signature + 32))
        return AI_STATUS_SECURITY_FAILED;

    if (!ai_ed25519_group_selfcheck())
        return AI_STATUS_SECURITY_FAILED;

    status = ai_ed25519_decompress_point(&public_point, public_key);
    if (status != AI_STATUS_OK)
        return status;

    status = ai_ed25519_decompress_point(&r_point, signature);
    if (status != AI_STATUS_OK)
        return status;

    if (!ai_ed25519_point_on_curve(&public_point) || !ai_ed25519_point_on_curve(&r_point))
        return AI_STATUS_SECURITY_FAILED;

    if (ai_ed25519_point_has_small_order(&public_point) || ai_ed25519_point_has_small_order(&r_point))
        return AI_STATUS_SECURITY_FAILED;

    status = ai_ed25519_scalar_from_canonical(&s_scalar, signature + 32);
    if (status != AI_STATUS_OK)
        return status;

    ai_ed25519_challenge_hash(signature, public_key, message, message_len, challenge);
    status = ai_ed25519_scalar_reduce_wide(&h_scalar, challenge);
    memset(challenge, 0, sizeof(challenge));
    if (status != AI_STATUS_OK)
    {
        memset(&s_scalar, 0, sizeof(s_scalar));
        return status;
    }

    equation_status = ai_ed25519_verify_equation_probe(&r_point, &public_point, &s_scalar, &h_scalar);
    memset(&s_scalar, 0, sizeof(s_scalar));
    memset(&h_scalar, 0, sizeof(h_scalar));

    if (equation_status < 0)
        return equation_status;
    if (equation_status == 0)
        return AI_STATUS_SECURITY_FAILED;

    return AI_STATUS_OK;
}

static int ai_ed25519_invalid_encoding_selftest(void)
{
    uint8_t invalid_point[32];
    uint8_t signature[64];
    ai_ed25519_point_t point;

    memset(invalid_point, 0xff, sizeof(invalid_point));
    if (ai_ed25519_decompress_point(&point, invalid_point) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;

    memset(invalid_point, 0, sizeof(invalid_point));
    invalid_point[31] = 0x80U;
    if (ai_ed25519_decompress_point(&point, invalid_point) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;

    memset(signature, 0, sizeof(signature));
    signature[32] = 0xedU;
    signature[33] = 0xd3U;
    signature[34] = 0xf5U;
    signature[35] = 0x5cU;
    signature[36] = 0x1aU;
    signature[37] = 0x63U;
    signature[38] = 0x12U;
    signature[39] = 0x58U;
    signature[40] = 0xd6U;
    signature[41] = 0x9cU;
    signature[42] = 0xf7U;
    signature[43] = 0xa2U;
    signature[44] = 0xdeU;
    signature[45] = 0xf9U;
    signature[46] = 0xdeU;
    signature[47] = 0x14U;
    signature[63] = 0x10U;
    if (ai_ed25519_scalar_s_canonical(signature + 32) != 0)
        return AI_STATUS_SECURITY_FAILED;

    return AI_STATUS_OK;
}

static int ai_ed25519_rfc8032_vector_selftest(void)
{
    static const uint8_t vector1_public_key[32] = {
        0xd7U, 0x5aU, 0x98U, 0x01U, 0x82U, 0xb1U, 0x0aU, 0xb7U,
        0xd5U, 0x4bU, 0xfeU, 0xd3U, 0xc9U, 0x64U, 0x07U, 0x3aU,
        0x0eU, 0xe1U, 0x72U, 0xf3U, 0xdaU, 0xa6U, 0x23U, 0x25U,
        0xafU, 0x02U, 0x1aU, 0x68U, 0xf7U, 0x07U, 0x51U, 0x1aU
    };
    static const uint8_t vector1_signature[64] = {
        0xe5U, 0x56U, 0x43U, 0x00U, 0xc3U, 0x60U, 0xacU, 0x72U,
        0x90U, 0x86U, 0xe2U, 0xccU, 0x80U, 0x6eU, 0x82U, 0x8aU,
        0x84U, 0x87U, 0x7fU, 0x1eU, 0xb8U, 0xe5U, 0xd9U, 0x74U,
        0xd8U, 0x73U, 0xe0U, 0x65U, 0x22U, 0x49U, 0x01U, 0x55U,
        0x5fU, 0xb8U, 0x82U, 0x15U, 0x90U, 0xa3U, 0x3bU, 0xacU,
        0xc6U, 0x1eU, 0x39U, 0x70U, 0x1cU, 0xf9U, 0xb4U, 0x6bU,
        0xd2U, 0x5bU, 0xf5U, 0xf0U, 0x59U, 0x5bU, 0xbeU, 0x24U,
        0x65U, 0x51U, 0x41U, 0x43U, 0x8eU, 0x7aU, 0x10U, 0x0bU
    };
    static const uint8_t vector2_public_key[32] = {
        0x3dU, 0x40U, 0x17U, 0xc3U, 0xe8U, 0x43U, 0x89U, 0x5aU,
        0x92U, 0xb7U, 0x0aU, 0xa7U, 0x4dU, 0x1bU, 0x7eU, 0xbcU,
        0x9cU, 0x98U, 0x2cU, 0xcfU, 0x2eU, 0xc4U, 0x96U, 0x8cU,
        0xc0U, 0xcdU, 0x55U, 0xf1U, 0x2aU, 0xf4U, 0x66U, 0x0cU
    };
    static const uint8_t vector2_message[1] = {0x72U};
    static const uint8_t vector2_signature[64] = {
        0x92U, 0xa0U, 0x09U, 0xa9U, 0xf0U, 0xd4U, 0xcaU, 0xb8U,
        0x72U, 0x0eU, 0x82U, 0x0bU, 0x5fU, 0x64U, 0x25U, 0x40U,
        0xa2U, 0xb2U, 0x7bU, 0x54U, 0x16U, 0x50U, 0x3fU, 0x8fU,
        0xb3U, 0x76U, 0x22U, 0x23U, 0xebU, 0xdbU, 0x69U, 0xdaU,
        0x08U, 0x5aU, 0xc1U, 0xe4U, 0x3eU, 0x15U, 0x99U, 0x6eU,
        0x45U, 0x8fU, 0x36U, 0x13U, 0xd0U, 0xf1U, 0x1dU, 0x8cU,
        0x38U, 0x7bU, 0x2eU, 0xaeU, 0xb4U, 0x30U, 0x2aU, 0xeeU,
        0xb0U, 0x0dU, 0x29U, 0x16U, 0x12U, 0xbbU, 0x0cU, 0x00U
    };
    static const uint8_t vector3_public_key[32] = {
        0xfcU, 0x51U, 0xcdU, 0x8eU, 0x62U, 0x18U, 0xa1U, 0xa3U,
        0x8dU, 0xa4U, 0x7eU, 0xd0U, 0x02U, 0x30U, 0xf0U, 0x58U,
        0x08U, 0x16U, 0xedU, 0x13U, 0xbaU, 0x33U, 0x03U, 0xacU,
        0x5dU, 0xebU, 0x91U, 0x15U, 0x48U, 0x90U, 0x80U, 0x25U
    };
    static const uint8_t vector3_message[2] = {
        0xafU, 0x82U
    };
    static const uint8_t vector3_signature[64] = {
        0x62U, 0x91U, 0xd6U, 0x57U, 0xdeU, 0xecU, 0x24U, 0x02U,
        0x48U, 0x27U, 0xe6U, 0x9cU, 0x3aU, 0xbeU, 0x01U, 0xa3U,
        0x0cU, 0xe5U, 0x48U, 0xa2U, 0x84U, 0x74U, 0x3aU, 0x44U,
        0x5eU, 0x36U, 0x80U, 0xd7U, 0xdbU, 0x5aU, 0xc3U, 0xacU,
        0x18U, 0xffU, 0x9bU, 0x53U, 0x8dU, 0x16U, 0xf2U, 0x90U,
        0xaeU, 0x67U, 0xf7U, 0x60U, 0x98U, 0x4dU, 0xc6U, 0x59U,
        0x4aU, 0x7cU, 0x15U, 0xe9U, 0x71U, 0x6eU, 0xd2U, 0x8dU,
        0xc0U, 0x27U, 0xbeU, 0xceU, 0xeaU, 0x1eU, 0xc4U, 0x0aU
    };
    static const uint8_t vector1024_public_key[32] = {
        0x27U, 0x81U, 0x17U, 0xfcU, 0x14U, 0x4cU, 0x72U, 0x34U,
        0x0fU, 0x67U, 0xd0U, 0xf2U, 0x31U, 0x6eU, 0x83U, 0x86U,
        0xceU, 0xffU, 0xbfU, 0x2bU, 0x24U, 0x28U, 0xc9U, 0xc5U,
        0x1fU, 0xefU, 0x7cU, 0x59U, 0x7fU, 0x1dU, 0x42U, 0x6eU
    };
    static const uint8_t vector1024_message[1023] = {
        0x08U, 0xb8U, 0xb2U, 0xb7U, 0x33U, 0x42U, 0x42U, 0x43U,
        0x76U, 0x0fU, 0xe4U, 0x26U, 0xa4U, 0xb5U, 0x49U, 0x08U,
        0x63U, 0x21U, 0x10U, 0xa6U, 0x6cU, 0x2fU, 0x65U, 0x91U,
        0xeaU, 0xbdU, 0x33U, 0x45U, 0xe3U, 0xe4U, 0xebU, 0x98U,
        0xfaU, 0x6eU, 0x26U, 0x4bU, 0xf0U, 0x9eU, 0xfeU, 0x12U,
        0xeeU, 0x50U, 0xf8U, 0xf5U, 0x4eU, 0x9fU, 0x77U, 0xb1U,
        0xe3U, 0x55U, 0xf6U, 0xc5U, 0x05U, 0x44U, 0xe2U, 0x3fU,
        0xb1U, 0x43U, 0x3dU, 0xdfU, 0x73U, 0xbeU, 0x84U, 0xd8U,
        0x79U, 0xdeU, 0x7cU, 0x00U, 0x46U, 0xdcU, 0x49U, 0x96U,
        0xd9U, 0xe7U, 0x73U, 0xf4U, 0xbcU, 0x9eU, 0xfeU, 0x57U,
        0x38U, 0x82U, 0x9aU, 0xdbU, 0x26U, 0xc8U, 0x1bU, 0x37U,
        0xc9U, 0x3aU, 0x1bU, 0x27U, 0x0bU, 0x20U, 0x32U, 0x9dU,
        0x65U, 0x86U, 0x75U, 0xfcU, 0x6eU, 0xa5U, 0x34U, 0xe0U,
        0x81U, 0x0aU, 0x44U, 0x32U, 0x82U, 0x6bU, 0xf5U, 0x8cU,
        0x94U, 0x1eU, 0xfbU, 0x65U, 0xd5U, 0x7aU, 0x33U, 0x8bU,
        0xbdU, 0x2eU, 0x26U, 0x64U, 0x0fU, 0x89U, 0xffU, 0xbcU,
        0x1aU, 0x85U, 0x8eU, 0xfcU, 0xb8U, 0x55U, 0x0eU, 0xe3U,
        0xa5U, 0xe1U, 0x99U, 0x8bU, 0xd1U, 0x77U, 0xe9U, 0x3aU,
        0x73U, 0x63U, 0xc3U, 0x44U, 0xfeU, 0x6bU, 0x19U, 0x9eU,
        0xe5U, 0xd0U, 0x2eU, 0x82U, 0xd5U, 0x22U, 0xc4U, 0xfeU,
        0xbaU, 0x15U, 0x45U, 0x2fU, 0x80U, 0x28U, 0x8aU, 0x82U,
        0x1aU, 0x57U, 0x91U, 0x16U, 0xecU, 0x6dU, 0xadU, 0x2bU,
        0x3bU, 0x31U, 0x0dU, 0xa9U, 0x03U, 0x40U, 0x1aU, 0xa6U,
        0x21U, 0x00U, 0xabU, 0x5dU, 0x1aU, 0x36U, 0x55U, 0x3eU,
        0x06U, 0x20U, 0x3bU, 0x33U, 0x89U, 0x0cU, 0xc9U, 0xb8U,
        0x32U, 0xf7U, 0x9eU, 0xf8U, 0x05U, 0x60U, 0xccU, 0xb9U,
        0xa3U, 0x9cU, 0xe7U, 0x67U, 0x96U, 0x7eU, 0xd6U, 0x28U,
        0xc6U, 0xadU, 0x57U, 0x3cU, 0xb1U, 0x16U, 0xdbU, 0xefU,
        0xefU, 0xd7U, 0x54U, 0x99U, 0xdaU, 0x96U, 0xbdU, 0x68U,
        0xa8U, 0xa9U, 0x7bU, 0x92U, 0x8aU, 0x8bU, 0xbcU, 0x10U,
        0x3bU, 0x66U, 0x21U, 0xfcU, 0xdeU, 0x2bU, 0xecU, 0xa1U,
        0x23U, 0x1dU, 0x20U, 0x6bU, 0xe6U, 0xcdU, 0x9eU, 0xc7U,
        0xafU, 0xf6U, 0xf6U, 0xc9U, 0x4fU, 0xcdU, 0x72U, 0x04U,
        0xedU, 0x34U, 0x55U, 0xc6U, 0x8cU, 0x83U, 0xf4U, 0xa4U,
        0x1dU, 0xa4U, 0xafU, 0x2bU, 0x74U, 0xefU, 0x5cU, 0x53U,
        0xf1U, 0xd8U, 0xacU, 0x70U, 0xbdU, 0xcbU, 0x7eU, 0xd1U,
        0x85U, 0xceU, 0x81U, 0xbdU, 0x84U, 0x35U, 0x9dU, 0x44U,
        0x25U, 0x4dU, 0x95U, 0x62U, 0x9eU, 0x98U, 0x55U, 0xa9U,
        0x4aU, 0x7cU, 0x19U, 0x58U, 0xd1U, 0xf8U, 0xadU, 0xa5U,
        0xd0U, 0x53U, 0x2eU, 0xd8U, 0xa5U, 0xaaU, 0x3fU, 0xb2U,
        0xd1U, 0x7bU, 0xa7U, 0x0eU, 0xb6U, 0x24U, 0x8eU, 0x59U,
        0x4eU, 0x1aU, 0x22U, 0x97U, 0xacU, 0xbbU, 0xb3U, 0x9dU,
        0x50U, 0x2fU, 0x1aU, 0x8cU, 0x6eU, 0xb6U, 0xf1U, 0xceU,
        0x22U, 0xb3U, 0xdeU, 0x1aU, 0x1fU, 0x40U, 0xccU, 0x24U,
        0x55U, 0x41U, 0x19U, 0xa8U, 0x31U, 0xa9U, 0xaaU, 0xd6U,
        0x07U, 0x9cU, 0xadU, 0x88U, 0x42U, 0x5dU, 0xe6U, 0xbdU,
        0xe1U, 0xa9U, 0x18U, 0x7eU, 0xbbU, 0x60U, 0x92U, 0xcfU,
        0x67U, 0xbfU, 0x2bU, 0x13U, 0xfdU, 0x65U, 0xf2U, 0x70U,
        0x88U, 0xd7U, 0x8bU, 0x7eU, 0x88U, 0x3cU, 0x87U, 0x59U,
        0xd2U, 0xc4U, 0xf5U, 0xc6U, 0x5aU, 0xdbU, 0x75U, 0x53U,
        0x87U, 0x8aU, 0xd5U, 0x75U, 0xf9U, 0xfaU, 0xd8U, 0x78U,
        0xe8U, 0x0aU, 0x0cU, 0x9bU, 0xa6U, 0x3bU, 0xcbU, 0xccU,
        0x27U, 0x32U, 0xe6U, 0x94U, 0x85U, 0xbbU, 0xc9U, 0xc9U,
        0x0bU, 0xfbU, 0xd6U, 0x24U, 0x81U, 0xd9U, 0x08U, 0x9bU,
        0xecU, 0xcfU, 0x80U, 0xcfU, 0xe2U, 0xdfU, 0x16U, 0xa2U,
        0xcfU, 0x65U, 0xbdU, 0x92U, 0xddU, 0x59U, 0x7bU, 0x07U,
        0x07U, 0xe0U, 0x91U, 0x7aU, 0xf4U, 0x8bU, 0xbbU, 0x75U,
        0xfeU, 0xd4U, 0x13U, 0xd2U, 0x38U, 0xf5U, 0x55U, 0x5aU,
        0x7aU, 0x56U, 0x9dU, 0x80U, 0xc3U, 0x41U, 0x4aU, 0x8dU,
        0x08U, 0x59U, 0xdcU, 0x65U, 0xa4U, 0x61U, 0x28U, 0xbaU,
        0xb2U, 0x7aU, 0xf8U, 0x7aU, 0x71U, 0x31U, 0x4fU, 0x31U,
        0x8cU, 0x78U, 0x2bU, 0x23U, 0xebU, 0xfeU, 0x80U, 0x8bU,
        0x82U, 0xb0U, 0xceU, 0x26U, 0x40U, 0x1dU, 0x2eU, 0x22U,
        0xf0U, 0x4dU, 0x83U, 0xd1U, 0x25U, 0x5dU, 0xc5U, 0x1aU,
        0xddU, 0xd3U, 0xb7U, 0x5aU, 0x2bU, 0x1aU, 0xe0U, 0x78U,
        0x45U, 0x04U, 0xdfU, 0x54U, 0x3aU, 0xf8U, 0x96U, 0x9bU,
        0xe3U, 0xeaU, 0x70U, 0x82U, 0xffU, 0x7fU, 0xc9U, 0x88U,
        0x8cU, 0x14U, 0x4dU, 0xa2U, 0xafU, 0x58U, 0x42U, 0x9eU,
        0xc9U, 0x60U, 0x31U, 0xdbU, 0xcaU, 0xd3U, 0xdaU, 0xd9U,
        0xafU, 0x0dU, 0xcbU, 0xaaU, 0xafU, 0x26U, 0x8cU, 0xb8U,
        0xfcU, 0xffU, 0xeaU, 0xd9U, 0x4fU, 0x3cU, 0x7cU, 0xa4U,
        0x95U, 0xe0U, 0x56U, 0xa9U, 0xb4U, 0x7aU, 0xcdU, 0xb7U,
        0x51U, 0xfbU, 0x73U, 0xe6U, 0x66U, 0xc6U, 0xc6U, 0x55U,
        0xadU, 0xe8U, 0x29U, 0x72U, 0x97U, 0xd0U, 0x7aU, 0xd1U,
        0xbaU, 0x5eU, 0x43U, 0xf1U, 0xbcU, 0xa3U, 0x23U, 0x01U,
        0x65U, 0x13U, 0x39U, 0xe2U, 0x29U, 0x04U, 0xccU, 0x8cU,
        0x42U, 0xf5U, 0x8cU, 0x30U, 0xc0U, 0x4aU, 0xafU, 0xdbU,
        0x03U, 0x8dU, 0xdaU, 0x08U, 0x47U, 0xddU, 0x98U, 0x8dU,
        0xcdU, 0xa6U, 0xf3U, 0xbfU, 0xd1U, 0x5cU, 0x4bU, 0x4cU,
        0x45U, 0x25U, 0x00U, 0x4aU, 0xa0U, 0x6eU, 0xefU, 0xf8U,
        0xcaU, 0x61U, 0x78U, 0x3aU, 0xacU, 0xecU, 0x57U, 0xfbU,
        0x3dU, 0x1fU, 0x92U, 0xb0U, 0xfeU, 0x2fU, 0xd1U, 0xa8U,
        0x5fU, 0x67U, 0x24U, 0x51U, 0x7bU, 0x65U, 0xe6U, 0x14U,
        0xadU, 0x68U, 0x08U, 0xd6U, 0xf6U, 0xeeU, 0x34U, 0xdfU,
        0xf7U, 0x31U, 0x0fU, 0xdcU, 0x82U, 0xaeU, 0xbfU, 0xd9U,
        0x04U, 0xb0U, 0x1eU, 0x1dU, 0xc5U, 0x4bU, 0x29U, 0x27U,
        0x09U, 0x4bU, 0x2dU, 0xb6U, 0x8dU, 0x6fU, 0x90U, 0x3bU,
        0x68U, 0x40U, 0x1aU, 0xdeU, 0xbfU, 0x5aU, 0x7eU, 0x08U,
        0xd7U, 0x8fU, 0xf4U, 0xefU, 0x5dU, 0x63U, 0x65U, 0x3aU,
        0x65U, 0x04U, 0x0cU, 0xf9U, 0xbfU, 0xd4U, 0xacU, 0xa7U,
        0x98U, 0x4aU, 0x74U, 0xd3U, 0x71U, 0x45U, 0x98U, 0x67U,
        0x80U, 0xfcU, 0x0bU, 0x16U, 0xacU, 0x45U, 0x16U, 0x49U,
        0xdeU, 0x61U, 0x88U, 0xa7U, 0xdbU, 0xdfU, 0x19U, 0x1fU,
        0x64U, 0xb5U, 0xfcU, 0x5eU, 0x2aU, 0xb4U, 0x7bU, 0x57U,
        0xf7U, 0xf7U, 0x27U, 0x6cU, 0xd4U, 0x19U, 0xc1U, 0x7aU,
        0x3cU, 0xa8U, 0xe1U, 0xb9U, 0x39U, 0xaeU, 0x49U, 0xe4U,
        0x88U, 0xacU, 0xbaU, 0x6bU, 0x96U, 0x56U, 0x10U, 0xb5U,
        0x48U, 0x01U, 0x09U, 0xc8U, 0xb1U, 0x7bU, 0x80U, 0xe1U,
        0xb7U, 0xb7U, 0x50U, 0xdfU, 0xc7U, 0x59U, 0x8dU, 0x5dU,
        0x50U, 0x11U, 0xfdU, 0x2dU, 0xccU, 0x56U, 0x00U, 0xa3U,
        0x2eU, 0xf5U, 0xb5U, 0x2aU, 0x1eU, 0xccU, 0x82U, 0x0eU,
        0x30U, 0x8aU, 0xa3U, 0x42U, 0x72U, 0x1aU, 0xacU, 0x09U,
        0x43U, 0xbfU, 0x66U, 0x86U, 0xb6U, 0x4bU, 0x25U, 0x79U,
        0x37U, 0x65U, 0x04U, 0xccU, 0xc4U, 0x93U, 0xd9U, 0x7eU,
        0x6aU, 0xedU, 0x3fU, 0xb0U, 0xf9U, 0xcdU, 0x71U, 0xa4U,
        0x3dU, 0xd4U, 0x97U, 0xf0U, 0x1fU, 0x17U, 0xc0U, 0xe2U,
        0xcbU, 0x37U, 0x97U, 0xaaU, 0x2aU, 0x2fU, 0x25U, 0x66U,
        0x56U, 0x16U, 0x8eU, 0x6cU, 0x49U, 0x6aU, 0xfcU, 0x5fU,
        0xb9U, 0x32U, 0x46U, 0xf6U, 0xb1U, 0x11U, 0x63U, 0x98U,
        0xa3U, 0x46U, 0xf1U, 0xa6U, 0x41U, 0xf3U, 0xb0U, 0x41U,
        0xe9U, 0x89U, 0xf7U, 0x91U, 0x4fU, 0x90U, 0xccU, 0x2cU,
        0x7fU, 0xffU, 0x35U, 0x78U, 0x76U, 0xe5U, 0x06U, 0xb5U,
        0x0dU, 0x33U, 0x4bU, 0xa7U, 0x7cU, 0x22U, 0x5bU, 0xc3U,
        0x07U, 0xbaU, 0x53U, 0x71U, 0x52U, 0xf3U, 0xf1U, 0x61U,
        0x0eU, 0x4eU, 0xafU, 0xe5U, 0x95U, 0xf6U, 0xd9U, 0xd9U,
        0x0dU, 0x11U, 0xfaU, 0xa9U, 0x33U, 0xa1U, 0x5eU, 0xf1U,
        0x36U, 0x95U, 0x46U, 0x86U, 0x8aU, 0x7fU, 0x3aU, 0x45U,
        0xa9U, 0x67U, 0x68U, 0xd4U, 0x0fU, 0xd9U, 0xd0U, 0x34U,
        0x12U, 0xc0U, 0x91U, 0xc6U, 0x31U, 0x5cU, 0xf4U, 0xfdU,
        0xe7U, 0xcbU, 0x68U, 0x60U, 0x69U, 0x37U, 0x38U, 0x0dU,
        0xb2U, 0xeaU, 0xaaU, 0x70U, 0x7bU, 0x4cU, 0x41U, 0x85U,
        0xc3U, 0x2eU, 0xddU, 0xcdU, 0xd3U, 0x06U, 0x70U, 0x5eU,
        0x4dU, 0xc1U, 0xffU, 0xc8U, 0x72U, 0xeeU, 0xeeU, 0x47U,
        0x5aU, 0x64U, 0xdfU, 0xacU, 0x86U, 0xabU, 0xa4U, 0x1cU,
        0x06U, 0x18U, 0x98U, 0x3fU, 0x87U, 0x41U, 0xc5U, 0xefU,
        0x68U, 0xd3U, 0xa1U, 0x01U, 0xe8U, 0xa3U, 0xb8U, 0xcaU,
        0xc6U, 0x0cU, 0x90U, 0x5cU, 0x15U, 0xfcU, 0x91U, 0x08U,
        0x40U, 0xb9U, 0x4cU, 0x00U, 0xa0U, 0xb9U, 0xd0U
    };
    static const uint8_t vector1024_signature[64] = {
        0x0aU, 0xabU, 0x4cU, 0x90U, 0x05U, 0x01U, 0xb3U, 0xe2U,
        0x4dU, 0x7cU, 0xdfU, 0x46U, 0x63U, 0x32U, 0x6aU, 0x3aU,
        0x87U, 0xdfU, 0x5eU, 0x48U, 0x43U, 0xb2U, 0xcbU, 0xdbU,
        0x67U, 0xcbU, 0xf6U, 0xe4U, 0x60U, 0xfeU, 0xc3U, 0x50U,
        0xaaU, 0x53U, 0x71U, 0xb1U, 0x50U, 0x8fU, 0x9fU, 0x45U,
        0x28U, 0xecU, 0xeaU, 0x23U, 0xc4U, 0x36U, 0xd9U, 0x4bU,
        0x5eU, 0x8fU, 0xcdU, 0x4fU, 0x68U, 0x1eU, 0x30U, 0xa6U,
        0xacU, 0x00U, 0xa9U, 0x70U, 0x4aU, 0x18U, 0x8aU, 0x03U
    };
    uint8_t tampered_signature[64];
    uint8_t malformed_signature[64];
    uint8_t small_order_public_key[32];
    uint8_t invalid_public_key[32];
    uint8_t tampered_message[1] = {0x73U};

    g_ai_ed25519_positive_vectors = 0;
    g_ai_ed25519_negative_vectors = 0;
    g_ai_ed25519_malformed_vectors = 0;

    if (ai_ed25519_verify_message_bytes(vector1_public_key, NULL, 0, vector1_signature) != AI_STATUS_OK)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_positive_vectors++;
    if (ai_ed25519_verify_message_bytes(vector2_public_key, vector2_message, sizeof(vector2_message), vector2_signature) != AI_STATUS_OK)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_positive_vectors++;
    if (ai_ed25519_verify_message_bytes(vector3_public_key, vector3_message, sizeof(vector3_message), vector3_signature) != AI_STATUS_OK)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_positive_vectors++;
    if (ai_ed25519_verify_message_bytes(vector1024_public_key, vector1024_message, sizeof(vector1024_message), vector1024_signature) != AI_STATUS_OK)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_positive_vectors++;

    memcpy(tampered_signature, vector2_signature, sizeof(tampered_signature));
    tampered_signature[0] ^= 0x01U;
    if (ai_ed25519_verify_message_bytes(vector2_public_key, vector2_message, sizeof(vector2_message), tampered_signature) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_negative_vectors++;
    if (ai_ed25519_verify_message_bytes(vector2_public_key, tampered_message, sizeof(tampered_message), vector2_signature) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_negative_vectors++;
    if (ai_ed25519_invalid_encoding_selftest() != AI_STATUS_OK)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_negative_vectors += 3U;

    memset(small_order_public_key, 0, sizeof(small_order_public_key));
    small_order_public_key[0] = 0x01U;
    if (ai_ed25519_verify_message_bytes(small_order_public_key, vector2_message, sizeof(vector2_message), vector2_signature) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_negative_vectors++;
    g_ai_ed25519_malformed_vectors++;

    memcpy(malformed_signature, vector2_signature, sizeof(malformed_signature));
    memset(malformed_signature, 0, 32U);
    malformed_signature[0] = 0x01U;
    if (ai_ed25519_verify_message_bytes(vector2_public_key, vector2_message, sizeof(vector2_message), malformed_signature) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_negative_vectors++;
    g_ai_ed25519_malformed_vectors++;

    memcpy(malformed_signature, vector2_signature, sizeof(malformed_signature));
    memset(malformed_signature, 0xff, 32U);
    if (ai_ed25519_verify_message_bytes(vector2_public_key, vector2_message, sizeof(vector2_message), malformed_signature) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_negative_vectors++;
    g_ai_ed25519_malformed_vectors++;

    memcpy(malformed_signature, vector2_signature, sizeof(malformed_signature));
    memset(malformed_signature + 32, 0xff, 32U);
    if (ai_ed25519_verify_message_bytes(vector2_public_key, vector2_message, sizeof(vector2_message), malformed_signature) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_negative_vectors++;
    g_ai_ed25519_malformed_vectors++;

    memcpy(malformed_signature, vector2_signature, sizeof(malformed_signature));
    memcpy(malformed_signature + 32, g_ai_ed25519_l_le, 32U);
    if (ai_ed25519_verify_message_bytes(vector2_public_key, vector2_message, sizeof(vector2_message), malformed_signature) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_negative_vectors++;
    g_ai_ed25519_malformed_vectors++;

    memset(invalid_public_key, 0xff, sizeof(invalid_public_key));
    if (ai_ed25519_verify_message_bytes(invalid_public_key, vector2_message, sizeof(vector2_message), vector2_signature) != AI_STATUS_SECURITY_FAILED)
        return AI_STATUS_SECURITY_FAILED;
    g_ai_ed25519_negative_vectors++;
    g_ai_ed25519_malformed_vectors++;

    return AI_STATUS_OK;
}

static int ai_ed25519_rfc8032_selftest_status(void)
{
    int status;

    if (g_ai_ed25519_rfc8032_selftest_done)
        return g_ai_ed25519_rfc8032_validated ? AI_STATUS_OK : AI_STATUS_SECURITY_FAILED;

    status = ai_ed25519_rfc8032_vector_selftest();
    g_ai_ed25519_selftest_last_status = status;
    g_ai_ed25519_rfc8032_validated = (status == AI_STATUS_OK) ? 1 : 0;
    g_ai_ed25519_rfc8032_selftest_done = 1;
    return status;
}

int ai_ed25519_selftest(ai_ed25519_selftest_info_t *info)
{
    int status;

    status = ai_ed25519_rfc8032_selftest_status();
    if (info)
    {
        info->executed = g_ai_ed25519_rfc8032_selftest_done ? 1U : 0U;
        info->passed = g_ai_ed25519_rfc8032_validated ? 1U : 0U;
        info->positive_vectors = g_ai_ed25519_positive_vectors;
        info->negative_vectors = g_ai_ed25519_negative_vectors;
        info->last_status = g_ai_ed25519_selftest_last_status;
    }
    return status;
}


void ai_print_ed25519_selftest(void)
{
    ai_ed25519_selftest_info_t info;
    int status;

    status = ai_ed25519_selftest(&info);
    serial_write("Ed25519 verifier selftest:\n");
    serial_write("  executed: ");
    serial_write(info.executed ? "yes" : "no");
    serial_write("\n");
    serial_write("  status: ");
    if (status == AI_STATUS_OK)
        serial_write("passed");
    else if (status == AI_STATUS_SECURITY_FAILED)
        serial_write("failed");
    else
        serial_write("unsupported");
    serial_write("\n");
    serial_write("  positive vectors: ");
    ai_print_u32(info.positive_vectors);
    serial_write("\n");
    serial_write("  negative vectors: ");
    ai_print_u32(info.negative_vectors);
    serial_write("\n");
    serial_write("  malformed signature vectors: ");
    ai_print_u32(g_ai_ed25519_malformed_vectors);
    serial_write("\n");
    serial_write("  hardening: constant-time compare/cmov, S canonical, small-order A/R rejection\n");
    serial_write("  coverage: RFC8032 empty, 1-byte, 2-byte, TEST 1024 long message, tampered signature/message, malformed signatures, invalid encodings\n");
    serial_write(info.passed ?
                 "  verifier: enabled for sha256-message signatures\n" :
                 "  verifier: disabled until all vectors pass\n");
}

int ai_ed25519_verify_sha256_hex(const char *public_key_hex, const char *sha256_hex, const char *signature_hex)
{
    uint8_t public_key[32];
    uint8_t message[32];
    uint8_t signature[64];
    int status;

    if (!public_key_hex || !sha256_hex || !signature_hex)
        return AI_STATUS_INVALID_ARGUMENT;

    status = ai_hex_to_bytes(public_key_hex, AI_ED25519_PUBLIC_KEY_HEX_LEN, public_key, sizeof(public_key));
    if (status != AI_STATUS_OK)
        return status;

    status = ai_hex_to_bytes(sha256_hex, AI_MODEL_SHA256_HEX_MAX - 1U, message, sizeof(message));
    if (status != AI_STATUS_OK)
        return status;

    status = ai_hex_to_bytes(signature_hex, AI_ED25519_SIGNATURE_HEX_LEN, signature, sizeof(signature));
    if (status != AI_STATUS_OK)
        return status;

    status = ai_ed25519_rfc8032_selftest_status();
    if (status != AI_STATUS_OK)
        return AI_STATUS_UNSUPPORTED;

    return ai_ed25519_verify_message_bytes(public_key, message, sizeof(message), signature);
}

int ai_signature_verify_sha256(const char *algorithm, const ai_trusted_key_info_t *key, const char *sha256_hex, const char *signature)
{
    if (!algorithm || !key || !sha256_hex || !signature)
        return AI_STATUS_INVALID_ARGUMENT;

    if (!ai_sha256_hex_valid(sha256_hex))
        return AI_STATUS_INVALID_ARGUMENT;

    if (ai_algorithm_is_ed25519(algorithm))
        return ai_ed25519_verify_sha256_hex(key->public_key, sha256_hex, signature);

    return AI_STATUS_UNSUPPORTED;
}

typedef struct ai_sha256_ctx {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t data[64];
    uint32_t data_len;
} ai_sha256_ctx_t;

static const uint32_t g_ai_sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static uint32_t ai_sha256_rotr(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static void ai_sha256_transform(ai_sha256_ctx_t *ctx, const uint8_t data[64])
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t i;
    uint32_t j;
    uint32_t t1;
    uint32_t t2;
    uint32_t m[64];

    for (i = 0, j = 0; i < 16; i++, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) | ((uint32_t)data[j + 2] << 8) | (uint32_t)data[j + 3];

    for (; i < 64; i++)
    {
        uint32_t s0 = ai_sha256_rotr(m[i - 15], 7) ^ ai_sha256_rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = ai_sha256_rotr(m[i - 2], 17) ^ ai_sha256_rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++)
    {
        uint32_t s1 = ai_sha256_rotr(e, 6) ^ ai_sha256_rotr(e, 11) ^ ai_sha256_rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t s0 = ai_sha256_rotr(a, 2) ^ ai_sha256_rotr(a, 13) ^ ai_sha256_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + s1 + ch + g_ai_sha256_k[i] + m[i];
        t2 = s0 + maj;
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

static void ai_sha256_init(ai_sha256_ctx_t *ctx)
{
    ctx->data_len = 0;
    ctx->bit_len = 0;
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

static void ai_sha256_update(ai_sha256_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++)
    {
        ctx->data[ctx->data_len++] = data[i];
        if (ctx->data_len == 64)
        {
            ai_sha256_transform(ctx, ctx->data);
            ctx->bit_len += 512U;
            ctx->data_len = 0;
        }
    }
}

static void ai_sha256_final(ai_sha256_ctx_t *ctx, uint8_t hash[32])
{
    uint32_t i = ctx->data_len;
    uint32_t j;

    if (ctx->data_len < 56)
    {
        ctx->data[i++] = 0x80U;
        while (i < 56)
            ctx->data[i++] = 0x00U;
    }
    else
    {
        ctx->data[i++] = 0x80U;
        while (i < 64)
            ctx->data[i++] = 0x00U;
        ai_sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bit_len += (uint64_t)ctx->data_len * 8U;
    ctx->data[63] = (uint8_t)(ctx->bit_len);
    ctx->data[62] = (uint8_t)(ctx->bit_len >> 8);
    ctx->data[61] = (uint8_t)(ctx->bit_len >> 16);
    ctx->data[60] = (uint8_t)(ctx->bit_len >> 24);
    ctx->data[59] = (uint8_t)(ctx->bit_len >> 32);
    ctx->data[58] = (uint8_t)(ctx->bit_len >> 40);
    ctx->data[57] = (uint8_t)(ctx->bit_len >> 48);
    ctx->data[56] = (uint8_t)(ctx->bit_len >> 56);
    ai_sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 8; j++)
            hash[i + (j * 4)] = (uint8_t)((ctx->state[j] >> (24U - i * 8U)) & 0xffU);
    }
}

static void ai_sha256_to_hex(const uint8_t hash[32], char out[AI_MODEL_SHA256_HEX_MAX])
{
    static const char hex[] = "0123456789abcdef";
    uint32_t i;

    for (i = 0; i < 32; i++)
    {
        out[i * 2U] = hex[(hash[i] >> 4) & 0xfU];
        out[i * 2U + 1U] = hex[hash[i] & 0xfU];
    }
    out[64] = '\0';
}

static int ai_sha256_file_hex(const char *path, char out[AI_MODEL_SHA256_HEX_MAX])
{
    ai_sha256_ctx_t ctx;
    uint8_t hash[32];
    uint8_t buf[512];
    int fd;
    int n;

    if (!path || !out)
        return AI_STATUS_INVALID_ARGUMENT;

    fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0)
        return AI_STATUS_NOT_FOUND;

    ai_sha256_init(&ctx);
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0)
        ai_sha256_update(&ctx, buf, (uint32_t)n);

    vfs_close(fd);
    if (n < 0)
        return AI_STATUS_INVALID_ARGUMENT;

    ai_sha256_final(&ctx, hash);
    ai_sha256_to_hex(hash, out);
    return AI_STATUS_OK;
}

static int ai_trust_key_valid(const ai_trusted_key_info_t *key)
{
    if (!key || !key->key_id[0] || !key->algorithm[0] || !key->public_key[0])
        return 0;
    if (strcmp(key->algorithm, "ed25519") != 0 && strcmp(key->algorithm, "rsa-sha256") != 0)
        return 0;
    return 1;
}

const ai_trusted_key_info_t *ai_trust_key_find(const char *key_id, const char *algorithm)
{
    uint32_t i;

    if (!key_id || !key_id[0] || !algorithm || !algorithm[0])
        return NULL;

    for (i = 0; i < g_trusted_key_count; i++)
    {
        if (strcmp(g_trusted_keys[i].key_id, key_id) == 0 &&
            strcmp(g_trusted_keys[i].algorithm, algorithm) == 0)
            return &g_trusted_keys[i];
    }

    return NULL;
}

static int ai_model_verify_signature_metadata(const ai_model_info_t *model, const char *source)
{
    const ai_trusted_key_info_t *trusted_key;
    int status;

    if (!model->signature[0])
    {
        if (model->sign_algo[0] || model->key_id[0])
        {
            ai_manifest_diag(source, 0, "sign_algo/key_id require signature", "signature", "");
            return AI_STATUS_SECURITY_FAILED;
        }
        return AI_STATUS_OK;
    }

    if (!model->sha256[0])
    {
        ai_manifest_diag(source, 0, "signed model requires sha256 binding", "sha256", "");
        return AI_STATUS_SECURITY_FAILED;
    }
    if (!model->sign_algo[0])
    {
        ai_manifest_diag(source, 0, "signature requires sign_algo", "sign_algo", "");
        return AI_STATUS_SECURITY_FAILED;
    }
    if (!model->key_id[0])
    {
        ai_manifest_diag(source, 0, "signature requires key_id", "key_id", "");
        return AI_STATUS_SECURITY_FAILED;
    }

    trusted_key = ai_trust_key_find(model->key_id, model->sign_algo);
    if (!trusted_key)
    {
        ai_manifest_diag(source, 0, "signature key is not trusted", "key_id", model->key_id);
        return AI_STATUS_SECURITY_FAILED;
    }

    status = ai_signature_verify_sha256(model->sign_algo, trusted_key, model->sha256, model->signature);
    if (status == AI_STATUS_OK)
        return AI_STATUS_OK;

    if (status == AI_STATUS_UNSUPPORTED)
        ai_manifest_diag(source, 0, "signature verifier is not available for algorithm", "sign_algo", model->sign_algo);
    else if (status == AI_STATUS_INVALID_ARGUMENT)
        ai_manifest_diag(source, 0, "invalid signature, digest, or public key encoding", "signature", model->signature);
    else
        ai_manifest_diag(source, 0, "signature verification failed", "signature", model->signature);

    return AI_STATUS_SECURITY_FAILED;
}

static int ai_model_verify_security(const ai_model_info_t *model, const char *source)
{
    char actual[AI_MODEL_SHA256_HEX_MAX];
    int status;

    if (!model)
        return AI_STATUS_INVALID_ARGUMENT;

    status = ai_model_verify_signature_metadata(model, source);
    if (status < 0)
        return status;

    if (!model->sha256[0])
        return AI_STATUS_OK;

    status = ai_sha256_file_hex(model->path, actual);
    if (status < 0)
    {
        ai_manifest_diag(source, 0, "unable to read model file for sha256 verification", "path", model->path);
        return AI_STATUS_SECURITY_FAILED;
    }

    if (!ai_sha256_hex_equal(model->sha256, actual))
    {
        ai_manifest_diag(source, 0, "sha256 mismatch", "sha256", model->sha256);
        ai_manifest_diag(source, 0, "computed sha256", "actual", actual);
        return AI_STATUS_SECURITY_FAILED;
    }

    return AI_STATUS_OK;
}

static int ai_manifest_set_field(ai_model_info_t *model, const char *key, const char *value, const char *source, uint32_t line_no)
{
    ai_backend_type_t backend;
    uint32_t parsed;

    if (!model || !key || !value)
        return AI_STATUS_INVALID_ARGUMENT;

    if (strcmp(key, "name") == 0)
        return ai_manifest_copy_field(model->name, value, AI_MODEL_NAME_MAX, source, line_no, key);
    else if (strcmp(key, "path") == 0)
        return ai_manifest_copy_field(model->path, value, AI_MODEL_PATH_MAX, source, line_no, key);
    else if (strcmp(key, "format") == 0)
        return ai_manifest_copy_field(model->format, value, AI_MODEL_FORMAT_MAX, source, line_no, key);
    else if (strcmp(key, "capabilities") == 0 || strcmp(key, "caps") == 0)
        return ai_manifest_copy_field(model->capabilities, value, AI_MODEL_CAPS_MAX, source, line_no, key);
    else if (strcmp(key, "backend") == 0)
    {
        if (ai_parse_backend(value, &backend) < 0)
        {
            ai_manifest_diag(source, line_no, "invalid backend, expected local/cloud/hybrid", key, value);
            return AI_STATUS_INVALID_ARGUMENT;
        }
        model->backend = backend;
    }
    else if (strcmp(key, "context") == 0 || strcmp(key, "context_length") == 0)
    {
        if (ai_parse_u32_checked(value, &parsed) < 0 || parsed == 0)
        {
            ai_manifest_diag(source, line_no, "invalid positive integer", key, value);
            return AI_STATUS_INVALID_ARGUMENT;
        }
        model->context_length = parsed;
    }
    else if (strcmp(key, "quant") == 0 || strcmp(key, "quant_bits") == 0)
    {
        if (ai_parse_u32_checked(value, &parsed) < 0 || parsed == 0)
        {
            ai_manifest_diag(source, line_no, "invalid positive integer", key, value);
            return AI_STATUS_INVALID_ARGUMENT;
        }
        model->quant_bits = parsed;
    }
    else if (strcmp(key, "loaded") == 0)
    {
        if (ai_parse_u32_checked(value, &parsed) < 0 || parsed > 1U)
        {
            ai_manifest_diag(source, line_no, "invalid boolean, expected 0 or 1", key, value);
            return AI_STATUS_INVALID_ARGUMENT;
        }
        model->loaded = parsed;
    }
    else if (strcmp(key, "sha256") == 0)
    {
        if (!ai_sha256_hex_valid(value))
        {
            ai_manifest_diag(source, line_no, "invalid sha256, expected 64 hex characters", key, value);
            return AI_STATUS_INVALID_ARGUMENT;
        }
        return ai_manifest_copy_field(model->sha256, value, AI_MODEL_SHA256_HEX_MAX, source, line_no, key);
    }
    else if (strcmp(key, "signature") == 0)
        return ai_manifest_copy_field(model->signature, value, AI_MODEL_SIGNATURE_MAX, source, line_no, key);
    else if (strcmp(key, "sign_algo") == 0 || strcmp(key, "signature_algo") == 0)
        return ai_manifest_copy_field(model->sign_algo, value, AI_MODEL_SIGN_ALGO_MAX, source, line_no, key);
    else if (strcmp(key, "key_id") == 0)
        return ai_manifest_copy_field(model->key_id, value, AI_MODEL_KEY_ID_MAX, source, line_no, key);
    else
    {
        ai_manifest_diag(source, line_no, "unknown field", key, value);
        return AI_STATUS_INVALID_ARGUMENT;
    }

    return AI_STATUS_OK;
}

static int ai_backend_valid(ai_backend_type_t backend)
{
    return backend == AI_BACKEND_LOCAL || backend == AI_BACKEND_CLOUD || backend == AI_BACKEND_HYBRID;
}

static int ai_backend_index(ai_backend_type_t backend)
{
    if (!ai_backend_valid(backend))
        return -1;
    return (int)backend;
}

static int ai_model_index_by_name(const char *name)
{
    uint32_t i;

    if (!name)
        return -1;

    for (i = 0; i < g_model_count; i++)
    {
        if (strcmp(g_models[i].name, name) == 0)
            return (int)i;
    }

    return -1;
}

static const ai_model_info_t *ai_select_model(ai_backend_type_t backend, const char *requested_name)
{
    int idx;
    int backend_idx;

    if (requested_name && requested_name[0])
    {
        idx = ai_model_index_by_name(requested_name);
        if (idx >= 0 && g_models[idx].loaded)
            return &g_models[idx];
    }

    backend_idx = ai_backend_index(backend);
    if (backend_idx >= 0 && g_current_model[backend_idx] >= 0)
        return &g_models[g_current_model[backend_idx]];

    return NULL;
}

int ai_trust_key_count(void)
{
    return (int)g_trusted_key_count;
}

const ai_trusted_key_info_t *ai_trust_key_get(uint32_t index)
{
    if (index >= g_trusted_key_count)
        return NULL;
    return &g_trusted_keys[index];
}

int ai_trust_key_register(const ai_trusted_key_info_t *key)
{
    uint32_t i;

    if (!ai_trust_key_valid(key))
        return AI_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < g_trusted_key_count; i++)
    {
        if (strcmp(g_trusted_keys[i].key_id, key->key_id) == 0 &&
            strcmp(g_trusted_keys[i].algorithm, key->algorithm) == 0)
        {
            g_trusted_keys[i] = *key;
            return AI_STATUS_OK;
        }
    }

    if (g_trusted_key_count >= AI_MAX_TRUSTED_KEYS)
        return AI_STATUS_NO_SPACE;

    g_trusted_keys[g_trusted_key_count++] = *key;
    return AI_STATUS_OK;
}

static int ai_trust_key_apply_field(ai_trusted_key_info_t *key, const char *field, const char *value)
{
    if (strcmp(field, "key_id") == 0)
        ai_copy(key->key_id, value, AI_TRUST_KEY_ID_MAX);
    else if (strcmp(field, "algorithm") == 0 || strcmp(field, "algo") == 0 || strcmp(field, "sign_algo") == 0)
        ai_copy(key->algorithm, value, AI_TRUST_ALGO_MAX);
    else if (strcmp(field, "public_key") == 0 || strcmp(field, "key") == 0)
        ai_copy(key->public_key, value, AI_TRUST_PUBLIC_KEY_MAX);
    else
        return AI_STATUS_INVALID_ARGUMENT;
    return AI_STATUS_OK;
}

int ai_trust_key_load_file(const char *path)
{
    char buf[AI_TRUST_KEY_FILE_MAX];
    char field[32];
    char value[AI_TRUST_PUBLIC_KEY_MAX];
    ai_trusted_key_info_t key;
    const char *line;
    const char *line_end;
    const char *eq;
    int fd;
    int n;

    if (!path || !path[0])
        return AI_STATUS_INVALID_ARGUMENT;

    fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0)
        return AI_STATUS_NOT_FOUND;

    n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0)
        return AI_STATUS_INVALID_ARGUMENT;
    buf[n] = '\0';

    memset(&key, 0, sizeof(key));
    line = buf;
    while (*line)
    {
        line = ai_skip_inline_spaces(line);
        if (*line == '\0')
            break;
        line_end = line;
        while (*line_end && *line_end != '\n')
            line_end++;

        if (*line != '#')
        {
            eq = line;
            while (eq < line_end && *eq != '=')
                eq++;
            if (eq < line_end)
            {
                ai_trim_copy(field, line, eq, sizeof(field));
                ai_trim_copy(value, eq + 1, line_end, sizeof(value));
                if (field[0] && value[0] && ai_trust_key_apply_field(&key, field, value) < 0)
                    return AI_STATUS_INVALID_ARGUMENT;
            }
        }

        line = *line_end == '\n' ? line_end + 1 : line_end;
    }

    return ai_trust_key_register(&key);
}

static void ai_register_builtin_trust_keys(void)
{
    ai_trusted_key_info_t key;

    memset(&key, 0, sizeof(key));
    ai_copy(key.key_id, "openos-dev", AI_TRUST_KEY_ID_MAX);
    ai_copy(key.algorithm, "ed25519", AI_TRUST_ALGO_MAX);
    ai_copy(key.public_key, "0000000000000000000000000000000000000000000000000000000000000000", AI_TRUST_PUBLIC_KEY_MAX);
    key.builtin = 1;
    ai_trust_key_register(&key);
}

static void ai_register_builtin_models(void)
{
    ai_model_info_t model;

    memset(&model, 0, sizeof(model));
    ai_copy(model.name, "openos-local-stub", AI_MODEL_NAME_MAX);
    ai_copy(model.path, "/system/models/openos-local-stub.gguf", AI_MODEL_PATH_MAX);
    ai_copy(model.format, "stub", AI_MODEL_FORMAT_MAX);
    ai_copy(model.capabilities, "chat,system_help,summarize", AI_MODEL_CAPS_MAX);
    model.backend = AI_BACKEND_LOCAL;
    model.context_length = 2048;
    model.quant_bits = 4;
    model.loaded = 1;
    model.builtin = 1;
    ai_model_register(&model);

    memset(&model, 0, sizeof(model));
    ai_copy(model.name, "openos-cloud-stub", AI_MODEL_NAME_MAX);
    ai_copy(model.path, "cloud://openos-cloud-stub", AI_MODEL_PATH_MAX);
    ai_copy(model.format, "api", AI_MODEL_FORMAT_MAX);
    ai_copy(model.capabilities, "chat,code,long_context", AI_MODEL_CAPS_MAX);
    model.backend = AI_BACKEND_CLOUD;
    model.context_length = 8192;
    model.quant_bits = 0;
    model.loaded = 1;
    model.builtin = 1;
    ai_model_register(&model);

    memset(&model, 0, sizeof(model));
    ai_copy(model.name, "openos-hybrid-stub", AI_MODEL_NAME_MAX);
    ai_copy(model.path, "hybrid://local-cloud-stub", AI_MODEL_PATH_MAX);
    ai_copy(model.format, "stub", AI_MODEL_FORMAT_MAX);
    ai_copy(model.capabilities, "chat,privacy_filter,cloud协同", AI_MODEL_CAPS_MAX);
    model.backend = AI_BACKEND_HYBRID;
    model.context_length = 4096;
    model.quant_bits = 4;
    model.loaded = 1;
    model.builtin = 1;
    ai_model_register(&model);
}

static int ai_generate_with_prefix(const ai_request_t *request, ai_response_t *response, ai_backend_type_t backend, const char *prefix)
{
    const ai_model_info_t *model;

    model = ai_select_model(backend, request->model);
    if (!model)
        return AI_STATUS_BACKEND_UNAVAILABLE;

    ai_copy(response->text, prefix, AI_RESPONSE_MAX);
    ai_append(response->text, "[", AI_RESPONSE_MAX);
    ai_append(response->text, model->name, AI_RESPONSE_MAX);
    ai_append(response->text, "]: ", AI_RESPONSE_MAX);
    ai_append(response->text, request->prompt, AI_RESPONSE_MAX);
    response->status = AI_STATUS_OK;
    response->tokens_used = ai_estimate_tokens(response->text);
    response->backend_used = backend;
    response->latency_ms = backend == AI_BACKEND_LOCAL ? 1 : (backend == AI_BACKEND_CLOUD ? 10 : 12);
    return AI_STATUS_OK;
}

static int ai_local_generate(const ai_request_t *request, ai_response_t *response)
{
    return ai_generate_with_prefix(request, response, AI_BACKEND_LOCAL, "AI(local) ");
}

static int ai_cloud_generate(const ai_request_t *request, ai_response_t *response)
{
    return ai_generate_with_prefix(request, response, AI_BACKEND_CLOUD, "AI(cloud stub) ");
}

static int ai_hybrid_generate(const ai_request_t *request, ai_response_t *response)
{
    return ai_generate_with_prefix(request, response, AI_BACKEND_HYBRID, "AI(hybrid stub) ");
}

void ai_init(void)
{
    uint32_t i;

    g_default_backend = AI_BACKEND_LOCAL;
    ai_copy(g_ai_repo_path, "/system/models", AI_REPO_PATH_MAX);
    ai_copy(g_ai_trust_root_path, "/system/security/ai-trusted-keys", AI_TRUST_ROOT_PATH_MAX);
    g_trusted_key_count = 0;
    g_model_count = 0;
    for (i = 0; i < 3; i++)
        g_current_model[i] = -1;

    g_ai_initialized = 1;
    ai_register_builtin_trust_keys();
    ai_register_builtin_models();
}

int ai_is_initialized(void)
{
    return g_ai_initialized;
}

const char *ai_backend_name(ai_backend_type_t backend)
{
    switch (backend)
    {
    case AI_BACKEND_LOCAL:
        return "local";
    case AI_BACKEND_CLOUD:
        return "cloud";
    case AI_BACKEND_HYBRID:
        return "hybrid";
    default:
        return "unknown";
    }
}

ai_backend_type_t ai_get_default_backend(void)
{
    return g_default_backend;
}

int ai_set_default_backend(ai_backend_type_t backend)
{
    if (!ai_backend_valid(backend))
        return AI_STATUS_INVALID_ARGUMENT;

    g_default_backend = backend;
    return AI_STATUS_OK;
}

int ai_parse_backend(const char *name, ai_backend_type_t *backend)
{
    if (!name || !backend)
        return AI_STATUS_INVALID_ARGUMENT;

    if (strcmp(name, "local") == 0)
        *backend = AI_BACKEND_LOCAL;
    else if (strcmp(name, "cloud") == 0)
        *backend = AI_BACKEND_CLOUD;
    else if (strcmp(name, "hybrid") == 0)
        *backend = AI_BACKEND_HYBRID;
    else
        return AI_STATUS_INVALID_ARGUMENT;

    return AI_STATUS_OK;
}

int ai_generate(const ai_request_t *request, ai_response_t *response)
{
    ai_backend_type_t backend;

    if (!g_ai_initialized)
        return AI_STATUS_NOT_INITIALIZED;
    if (!request || !response || !request->prompt)
        return AI_STATUS_INVALID_ARGUMENT;

    memset(response, 0, sizeof(*response));
    backend = request->backend_preference;

    if (!ai_backend_valid(backend))
        backend = g_default_backend;

    if (backend == AI_BACKEND_LOCAL)
        return ai_local_generate(request, response);
    if (backend == AI_BACKEND_CLOUD)
        return ai_cloud_generate(request, response);
    if (backend == AI_BACKEND_HYBRID)
        return ai_hybrid_generate(request, response);

    return AI_STATUS_BACKEND_UNAVAILABLE;
}

static void ai_model_fix_current_after_update(int model_idx, ai_backend_type_t old_backend, uint32_t old_loaded, const ai_model_info_t *model)
{
    int old_backend_idx;
    int new_backend_idx;
    int was_current = 0;
    int i;

    if (model_idx < 0 || !model)
        return;

    old_backend_idx = ai_backend_index(old_backend);
    if (old_backend_idx >= 0 && g_current_model[old_backend_idx] == model_idx)
    {
        g_current_model[old_backend_idx] = -1;
        was_current = old_loaded ? 1 : 0;
    }

    for (i = 0; i < 3; i++)
    {
        if (g_current_model[i] == model_idx)
            g_current_model[i] = -1;
    }

    new_backend_idx = ai_backend_index(model->backend);
    if (new_backend_idx >= 0 && model->loaded && (was_current || g_current_model[new_backend_idx] < 0))
        g_current_model[new_backend_idx] = model_idx;
}

int ai_model_register(const ai_model_info_t *model)
{
    int idx;
    int backend_idx;
    ai_backend_type_t old_backend;
    uint32_t old_loaded;

    if (!g_ai_initialized || !model || !model->name[0] || !ai_backend_valid(model->backend))
        return AI_STATUS_INVALID_ARGUMENT;

    idx = ai_model_index_by_name(model->name);
    if (idx >= 0)
    {
        old_backend = g_models[idx].backend;
        old_loaded = g_models[idx].loaded;
        g_models[idx] = *model;
        ai_model_fix_current_after_update(idx, old_backend, old_loaded, &g_models[idx]);
        return AI_STATUS_OK;
    }

    if (g_model_count >= AI_MAX_MODELS)
        return AI_STATUS_NO_SPACE;

    g_models[g_model_count] = *model;
    backend_idx = ai_backend_index(model->backend);
    if (backend_idx >= 0 && model->loaded && g_current_model[backend_idx] < 0)
        g_current_model[backend_idx] = (int)g_model_count;

    g_model_count++;
    return AI_STATUS_OK;
}

int ai_model_count(void)
{
    return (int)g_model_count;
}

const ai_model_info_t *ai_model_get(uint32_t index)
{
    if (index >= g_model_count)
        return NULL;
    return &g_models[index];
}

const ai_model_info_t *ai_model_find(const char *name)
{
    int idx = ai_model_index_by_name(name);

    if (idx < 0)
        return NULL;
    return &g_models[idx];
}

const ai_model_info_t *ai_model_current(ai_backend_type_t backend)
{
    int backend_idx = ai_backend_index(backend);

    if (backend_idx < 0 || g_current_model[backend_idx] < 0)
        return NULL;
    return &g_models[g_current_model[backend_idx]];
}

int ai_model_load(const char *name)
{
    int idx;
    int backend_idx;

    if (!g_ai_initialized || !name)
        return AI_STATUS_INVALID_ARGUMENT;

    idx = ai_model_index_by_name(name);
    if (idx < 0)
        return AI_STATUS_NOT_FOUND;

    g_models[idx].loaded = 1;
    backend_idx = ai_backend_index(g_models[idx].backend);
    if (backend_idx >= 0)
        g_current_model[backend_idx] = idx;

    return AI_STATUS_OK;
}

int ai_model_unload(const char *name)
{
    int idx;
    int backend_idx;
    uint32_t i;

    if (!g_ai_initialized || !name)
        return AI_STATUS_INVALID_ARGUMENT;

    idx = ai_model_index_by_name(name);
    if (idx < 0)
        return AI_STATUS_NOT_FOUND;

    g_models[idx].loaded = 0;
    backend_idx = ai_backend_index(g_models[idx].backend);
    if (backend_idx >= 0 && g_current_model[backend_idx] == idx)
    {
        g_current_model[backend_idx] = -1;
        for (i = 0; i < g_model_count; i++)
        {
            if (g_models[i].backend == g_models[idx].backend && g_models[i].loaded)
            {
                g_current_model[backend_idx] = (int)i;
                break;
            }
        }
    }

    return AI_STATUS_OK;
}


static int ai_model_register_manifest_source(const char *manifest_text, const char *source)
{
    ai_model_info_t model;
    const char *line;
    const char *line_start;
    const char *line_end;
    const char *eq;
    char key[32];
    char value[AI_MODEL_PATH_MAX];
    uint32_t line_no = 1;

    if (!g_ai_initialized || !manifest_text)
        return AI_STATUS_INVALID_ARGUMENT;

    memset(&model, 0, sizeof(model));
    model.backend = AI_BACKEND_LOCAL;
    model.context_length = 2048;
    model.quant_bits = 4;
    model.loaded = 0;
    model.builtin = 0;
    ai_copy(model.format, "stub", AI_MODEL_FORMAT_MAX);
    ai_copy(model.capabilities, "chat", AI_MODEL_CAPS_MAX);

    line = manifest_text;
    while (*line)
    {
        line_start = ai_skip_inline_spaces(line);
        line_end = line;
        while (*line_end && *line_end != '\n')
            line_end++;

        if (line_start < line_end && *line_start != '#')
        {
            eq = line_start;
            while (eq < line_end && *eq != '=')
                eq++;

            if (eq >= line_end)
            {
                ai_manifest_diag(source, line_no, "missing '='", NULL, NULL);
                return AI_STATUS_INVALID_ARGUMENT;
            }

            ai_trim_copy(key, line_start, eq, sizeof(key));
            ai_trim_copy(value, eq + 1, line_end, sizeof(value));
            if (!key[0])
            {
                ai_manifest_diag(source, line_no, "empty key", NULL, NULL);
                return AI_STATUS_INVALID_ARGUMENT;
            }
            if (!value[0])
            {
                ai_manifest_diag(source, line_no, "empty value", key, NULL);
                return AI_STATUS_INVALID_ARGUMENT;
            }
            if (ai_manifest_set_field(&model, key, value, source, line_no) < 0)
                return AI_STATUS_INVALID_ARGUMENT;
        }

        line = *line_end == '\n' ? line_end + 1 : line_end;
        line_no++;
    }

    if (!model.name[0])
    {
        ai_manifest_diag(source, 0, "missing required field", "name", NULL);
        return AI_STATUS_INVALID_ARGUMENT;
    }
    if (!model.path[0])
    {
        ai_manifest_diag(source, 0, "missing required field", "path", NULL);
        return AI_STATUS_INVALID_ARGUMENT;
    }
    if (!ai_backend_valid(model.backend))
    {
        ai_manifest_diag(source, 0, "invalid backend", "backend", NULL);
        return AI_STATUS_INVALID_ARGUMENT;
    }
    if (ai_model_verify_security(&model, source) < 0)
        return AI_STATUS_SECURITY_FAILED;

    return ai_model_register(&model);
}

int ai_model_register_manifest(const char *manifest_text)
{
    return ai_model_register_manifest_source(manifest_text, "<manifest>");
}

int ai_model_register_manifest_file(const char *path)
{
    int fd;
    int n;
    char buf[AI_MANIFEST_MAX];

    if (!g_ai_initialized || !path)
        return AI_STATUS_INVALID_ARGUMENT;

    fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0)
        return AI_STATUS_NOT_FOUND;

    n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0)
        return AI_STATUS_INVALID_ARGUMENT;

    buf[n] = '\0';
    return ai_model_register_manifest_source(buf, path);
}

static int ai_name_is_manifest(const char *name)
{
    const char *suffix = ".manifest";
    uint32_t name_len;
    uint32_t suffix_len;

    if (!name || !name[0])
        return 0;

    if (strcmp(name, "manifest") == 0)
        return 1;

    name_len = (uint32_t)strlen(name);
    suffix_len = (uint32_t)strlen(suffix);
    if (name_len < suffix_len)
        return 0;

    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

static int ai_repo_child_path(const char *repo, const char *name, char *out, uint32_t max_len)
{
    uint32_t repo_len;
    uint32_t name_len;
    uint32_t need_slash;

    if (!repo || !name || !out || max_len == 0)
        return AI_STATUS_INVALID_ARGUMENT;

    if (name[0] == '/')
    {
        if ((uint32_t)strlen(name) + 1U > max_len)
            return AI_STATUS_BUFFER_TOO_SMALL;
        ai_copy(out, name, max_len);
        return AI_STATUS_OK;
    }

    repo_len = (uint32_t)strlen(repo);
    name_len = (uint32_t)strlen(name);
    need_slash = (repo_len > 0 && repo[repo_len - 1] != '/') ? 1U : 0U;

    if (repo_len + need_slash + name_len + 1U > max_len)
        return AI_STATUS_BUFFER_TOO_SMALL;

    ai_copy(out, repo, max_len);
    if (need_slash)
        ai_append(out, "/", max_len);
    ai_append(out, name, max_len);
    return AI_STATUS_OK;
}

static int ai_repo_scan_directory(void)
{
    dentry_t *entry;
    inode_t st;
    int index = 0;
    int registered = 0;
    int status;
    char path[AI_REPO_PATH_MAX + MAX_NAME + 2];

    if (vfs_stat(g_ai_repo_path, &st) < 0 || (st.mode & FS_DIR) == 0)
        return AI_STATUS_NOT_FOUND;

    while ((entry = vfs_readdir(g_ai_repo_path, index)) != NULL)
    {
        index++;
        if (!entry->inode || (entry->inode->mode & FS_DIR) || !ai_name_is_manifest(entry->name))
            continue;

        if (ai_repo_child_path(g_ai_repo_path, entry->name, path, sizeof(path)) < 0)
            continue;

        status = ai_model_register_manifest_file(path);
        if (status == AI_STATUS_OK)
            registered++;
        else if (status == AI_STATUS_NO_SPACE)
            return registered > 0 ? registered : status;
    }

    return registered;
}

static int ai_repo_scan_index_text(const char *index_text)
{
    const char *line;
    const char *line_end;
    char manifest_path[AI_MODEL_PATH_MAX];
    char full_path[AI_REPO_PATH_MAX + AI_MODEL_PATH_MAX + 2];
    int registered = 0;
    int status;

    if (!index_text)
        return AI_STATUS_INVALID_ARGUMENT;

    line = index_text;
    while (*line)
    {
        line = ai_skip_inline_spaces(line);
        if (*line == '\0')
            break;

        line_end = line;
        while (*line_end && *line_end != '\n')
            line_end++;

        if (*line != '#')
        {
            ai_trim_copy(manifest_path, line, line_end, sizeof(manifest_path));
            if (manifest_path[0] && ai_name_is_manifest(manifest_path))
            {
                if (ai_repo_child_path(g_ai_repo_path, manifest_path, full_path, sizeof(full_path)) == AI_STATUS_OK)
                {
                    status = ai_model_register_manifest_file(full_path);
                    if (status == AI_STATUS_OK)
                        registered++;
                    else if (status == AI_STATUS_NO_SPACE)
                        return registered > 0 ? registered : status;
                }
            }
        }

        line = *line_end == '\n' ? line_end + 1 : line_end;
    }

    return registered;
}

int ai_repo_scan_index(void)
{
    char index_path[AI_REPO_PATH_MAX + 16];
    char buf[AI_INDEX_MAX];
    int fd;
    int n;

    if (!g_ai_initialized)
        return AI_STATUS_NOT_INITIALIZED;

    if (ai_repo_child_path(g_ai_repo_path, "models.index", index_path, sizeof(index_path)) < 0)
        return AI_STATUS_BUFFER_TOO_SMALL;

    fd = vfs_open(index_path, O_RDONLY, 0);
    if (fd < 0)
        return AI_STATUS_NOT_FOUND;

    n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0)
        return AI_STATUS_INVALID_ARGUMENT;

    buf[n] = '\0';
    return ai_repo_scan_index_text(buf);
}

int ai_repo_scan(void)
{
    int indexed;

    if (!g_ai_initialized)
        return AI_STATUS_NOT_INITIALIZED;

    indexed = ai_repo_scan_index();
    if (indexed != AI_STATUS_NOT_FOUND)
        return indexed;

    return ai_repo_scan_directory();
}

const char *ai_repo_path(void)
{
    return g_ai_repo_path;
}

int ai_repo_set_path(const char *path)
{
    if (!path || !path[0])
        return AI_STATUS_INVALID_ARGUMENT;

    ai_copy(g_ai_repo_path, path, AI_REPO_PATH_MAX);
    return AI_STATUS_OK;
}

const char *ai_trust_root_path(void)
{
    return g_ai_trust_root_path;
}

int ai_trust_root_set_path(const char *path)
{
    if (!path || !path[0])
        return AI_STATUS_INVALID_ARGUMENT;

    ai_copy(g_ai_trust_root_path, path, AI_TRUST_ROOT_PATH_MAX);
    return AI_STATUS_OK;
}

void ai_print_repo(void)
{
    serial_write("AI model repository:\n");
    serial_write("  path: ");
    serial_write(g_ai_repo_path);
    serial_write("\n");
    serial_write("  index file: models.index, one manifest path per line\n");
    serial_write("  index comments: lines starting with # are ignored\n");
    serial_write("  manifest format: key=value single-model file\n");
    serial_write("  manifest keys: name,path,backend,format,capabilities,context_length,quant_bits,loaded,sha256,signature,sign_algo,key_id\n");
    serial_write("  security: sha256 is verified when present; signed manifests require trusted key metadata and verifier support\n");
    serial_write("  trust root: ");
    serial_write(g_ai_trust_root_path);
    serial_write("\n");
}

void ai_print_trust(void)
{
    uint32_t i;

    serial_write("AI trust root:\n");
    serial_write("  path: ");
    serial_write(g_ai_trust_root_path);
    serial_write("\n");
    serial_write(g_ai_ed25519_rfc8032_validated ?
                 "  verifier: ed25519 RFC8032 vectors passed; sha256-message signature verification enabled\n" :
                 "  verifier: ed25519 RFC8032 vectors wired; verification disabled until selftest passes\n");
    serial_write("  trusted keys:\n");
    if (g_trusted_key_count == 0)
    {
        serial_write("    <none>\n");
        return;
    }

    for (i = 0; i < g_trusted_key_count; i++)
    {
        serial_write("    key_id=");
        serial_write(g_trusted_keys[i].key_id);
        serial_write(" algo=");
        serial_write(g_trusted_keys[i].algorithm);
        serial_write(g_trusted_keys[i].builtin ? " source=builtin" : " source=file");
        serial_write("\n");
    }
}

void ai_print_models(void)
{
    uint32_t i;
    const ai_model_info_t *current;

    serial_write("AI models:\n");
    if (g_model_count == 0)
    {
        serial_write("  <none>\n");
        return;
    }

    for (i = 0; i < g_model_count; i++)
    {
        current = ai_model_current(g_models[i].backend);
        serial_write("  ");
        serial_write(current == &g_models[i] ? "* " : "  ");
        serial_write(g_models[i].name);
        serial_write(" backend=");
        serial_write(ai_backend_name(g_models[i].backend));
        serial_write(" format=");
        serial_write(g_models[i].format);
        serial_write(" state=");
        serial_write(g_models[i].loaded ? "loaded" : "unloaded");
        serial_write(" caps=");
        serial_write(g_models[i].capabilities);
        if (g_models[i].sha256[0])
        {
            serial_write(" sha256=");
            serial_write(g_models[i].sha256);
        }
        if (g_models[i].signature[0])
        {
            serial_write(" signature=present sign_algo=");
            serial_write(g_models[i].sign_algo);
            serial_write(" key_id=");
            serial_write(g_models[i].key_id);
        }
        serial_write("\n");
    }
}

void ai_print_info(void)
{
    const ai_model_info_t *model;

    serial_write("AI engine:\n");
    serial_write("  status: ");
    serial_write(g_ai_initialized ? "initialized" : "not initialized");
    serial_write("\n");
    serial_write("  default backend: ");
    serial_write(ai_backend_name(g_default_backend));
    serial_write("\n");

    model = ai_model_current(AI_BACKEND_LOCAL);
    serial_write("  local backend: ");
    serial_write(model ? "available, model=" : "unavailable");
    if (model)
        serial_write(model->name);
    serial_write("\n");

    model = ai_model_current(AI_BACKEND_CLOUD);
    serial_write("  cloud backend: ");
    serial_write(model ? "stub, model=" : "unavailable");
    if (model)
        serial_write(model->name);
    serial_write("\n");

    model = ai_model_current(AI_BACKEND_HYBRID);
    serial_write("  hybrid mode: ");
    serial_write(model ? "available, model=" : "unavailable");
    if (model)
        serial_write(model->name);
    serial_write("\n");
}