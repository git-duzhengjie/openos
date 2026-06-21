#include "tls_x509.h"

#include <stddef.h>
#include <stdint.h>

#define TLS_X509_ASN1_CONSTRUCTED 0x20u
#define TLS_X509_TAG_SEQUENCE 0x30u
#define TLS_X509_TAG_INTEGER 0x02u
#define TLS_X509_TAG_BIT_STRING 0x03u
#define TLS_X509_TAG_OCTET_STRING 0x04u
#define TLS_X509_TAG_OID 0x06u
#define TLS_X509_TAG_UTC_TIME 0x17u
#define TLS_X509_TAG_GENERALIZED_TIME 0x18u
#define TLS_X509_TAG_BOOLEAN 0x01u
#define TLS_X509_TAG_CONTEXT_0 0xa0u
#define TLS_X509_TAG_CONTEXT_2 0x82u
#define TLS_X509_TAG_CONTEXT_3 0xa3u

#if defined(OPENOS_UNIT_TEST)
#include <string.h>
#else
static void* memset(void* dst, int value, size_t len)
{
    unsigned char* p = (unsigned char*)dst;
    while (len--) {
        *p++ = (unsigned char)value;
    }
    return dst;
}
#endif

typedef struct tls_x509_tlv {
    uint8_t tag;
    const uint8_t* header;
    size_t header_len;
    const uint8_t* value;
    size_t value_len;
    const uint8_t* end;
} tls_x509_tlv_t;

static int tls_x509_parse_tlv(const uint8_t* data,
                              size_t len,
                              tls_x509_tlv_t* out_tlv)
{
    size_t pos = 0u;
    size_t length_len;
    size_t value_len = 0u;

    if (!data || !out_tlv || len < 2u) {
        return -1;
    }

    out_tlv->tag = data[pos++];
    if ((data[pos] & 0x80u) == 0u) {
        value_len = data[pos++];
    } else {
        length_len = (size_t)(data[pos++] & 0x7fu);
        if (length_len == 0u || length_len > sizeof(size_t) || length_len > len - pos) {
            return -1;
        }
        for (size_t i = 0u; i < length_len; ++i) {
            if (value_len > ((size_t)-1 - data[pos]) / 256u) {
                return -1;
            }
            value_len = (value_len << 8u) | data[pos++];
        }
        if (value_len < 128u) {
            return -1;
        }
    }

    if (value_len > len - pos) {
        return -1;
    }

    out_tlv->header = data;
    out_tlv->header_len = pos;
    out_tlv->value = data + pos;
    out_tlv->value_len = value_len;
    out_tlv->end = data + pos + value_len;
    return 0;
}

static void tls_x509_slice_from_tlv(tls_x509_slice_t* slice, const tls_x509_tlv_t* tlv)
{
    slice->data = tlv->header;
    slice->len = tlv->header_len + tlv->value_len;
}

static int tls_x509_next_tlv(const uint8_t** cursor,
                             const uint8_t* end,
                             tls_x509_tlv_t* out_tlv)
{
    size_t remaining;

    if (!cursor || !*cursor || !end || *cursor > end) {
        return -1;
    }
    remaining = (size_t)(end - *cursor);
    if (tls_x509_parse_tlv(*cursor, remaining, out_tlv) != 0) {
        return -1;
    }
    *cursor = out_tlv->end;
    return 0;
}

static int tls_x509_expect_sequence(const tls_x509_tlv_t* tlv)
{
    return tlv && tlv->tag == TLS_X509_TAG_SEQUENCE &&
           (tlv->tag & TLS_X509_ASN1_CONSTRUCTED) != 0u;
}

static int tls_x509_parse_version(const tls_x509_tlv_t* explicit_version, uint8_t* out_version)
{
    tls_x509_tlv_t integer;

    if (!explicit_version || explicit_version->tag != TLS_X509_TAG_CONTEXT_0 ||
        explicit_version->value_len == 0u) {
        return -1;
    }
    if (tls_x509_parse_tlv(explicit_version->value,
                           explicit_version->value_len,
                           &integer) != 0 ||
        integer.tag != TLS_X509_TAG_INTEGER ||
        integer.value_len != 1u) {
        return -1;
    }
    if (out_version) {
        *out_version = (uint8_t)(integer.value[0] + 1u);
    }
    return 0;
}

static int tls_x509_slice_equal_bytes(const tls_x509_slice_t* slice,
                                      const uint8_t* data,
                                      size_t len)
{
    uint8_t diff = 0u;

    if (!slice || !slice->data || !data || slice->len != len) {
        return 0;
    }
    for (size_t i = 0u; i < len; ++i) {
        diff = (uint8_t)(diff | (uint8_t)(slice->data[i] ^ data[i]));
    }
    return diff == 0u;
}

static int tls_x509_is_validity_sequence(const tls_x509_tlv_t* tlv)
{
    const uint8_t* cursor;
    tls_x509_tlv_t not_before;
    tls_x509_tlv_t not_after;

    if (!tls_x509_expect_sequence(tlv)) {
        return 0;
    }

    cursor = tlv->value;
    if (tls_x509_next_tlv(&cursor, tlv->end, &not_before) != 0 ||
        tls_x509_next_tlv(&cursor, tlv->end, &not_after) != 0 ||
        cursor != tlv->end) {
        return 0;
    }

    return (not_before.tag == TLS_X509_TAG_UTC_TIME ||
            not_before.tag == TLS_X509_TAG_GENERALIZED_TIME) &&
           (not_after.tag == TLS_X509_TAG_UTC_TIME ||
            not_after.tag == TLS_X509_TAG_GENERALIZED_TIME);
}

void tls_x509_certificate_view_init(tls_x509_certificate_view_t* view)
{
    if (!view) return;
    memset(view, 0, sizeof(*view));
    view->version = 1u;
}

int tls_x509_parse_certificate(const uint8_t* cert_der,
                               size_t cert_der_len,
                               tls_x509_certificate_view_t* out_view)
{
    tls_x509_tlv_t cert;
    tls_x509_tlv_t tbs;
    tls_x509_tlv_t signature_algorithm;
    tls_x509_tlv_t signature_value;
    tls_x509_tlv_t field;
    const uint8_t* cursor;
    const uint8_t* tbs_cursor;

    if (!cert_der || !out_view) {
        return -1;
    }
    tls_x509_certificate_view_init(out_view);

    if (tls_x509_parse_tlv(cert_der, cert_der_len, &cert) != 0 ||
        !tls_x509_expect_sequence(&cert) ||
        cert.header + cert.header_len + cert.value_len != cert_der + cert_der_len) {
        return -1;
    }

    cursor = cert.value;
    if (tls_x509_next_tlv(&cursor, cert.end, &tbs) != 0 ||
        tls_x509_next_tlv(&cursor, cert.end, &signature_algorithm) != 0 ||
        tls_x509_next_tlv(&cursor, cert.end, &signature_value) != 0 ||
        cursor != cert.end ||
        !tls_x509_expect_sequence(&tbs) ||
        !tls_x509_expect_sequence(&signature_algorithm) ||
        signature_value.tag != TLS_X509_TAG_BIT_STRING) {
        return -1;
    }

    out_view->certificate_der.data = cert_der;
    out_view->certificate_der.len = cert_der_len;
    tls_x509_slice_from_tlv(&out_view->tbs_certificate_der, &tbs);
    tls_x509_slice_from_tlv(&out_view->signature_algorithm_der, &signature_algorithm);
    tls_x509_slice_from_tlv(&out_view->signature_value_der, &signature_value);

    tbs_cursor = tbs.value;
    if (tls_x509_next_tlv(&tbs_cursor, tbs.end, &field) != 0) {
        return -1;
    }
    if (field.tag == TLS_X509_TAG_CONTEXT_0) {
        uint8_t version = 1u;
        if (tls_x509_parse_version(&field, &version) != 0) {
            return -1;
        }
        out_view->has_explicit_version = 1;
        out_view->version = version;
        if (tls_x509_next_tlv(&tbs_cursor, tbs.end, &field) != 0) {
            return -1;
        }
    }

    if (field.tag != TLS_X509_TAG_INTEGER) {
        return -1;
    }
    tls_x509_slice_from_tlv(&out_view->serial_number_der, &field);

    if (tls_x509_next_tlv(&tbs_cursor, tbs.end, &field) != 0 ||
        !tls_x509_expect_sequence(&field)) {
        return -1;
    }

    if (tls_x509_next_tlv(&tbs_cursor, tbs.end, &field) != 0 ||
        !tls_x509_expect_sequence(&field)) {
        return -1;
    }
    tls_x509_slice_from_tlv(&out_view->issuer_der, &field);

    if (tls_x509_next_tlv(&tbs_cursor, tbs.end, &field) != 0 ||
        !tls_x509_is_validity_sequence(&field)) {
        return -1;
    }
    tls_x509_slice_from_tlv(&out_view->validity_der, &field);

    if (tls_x509_next_tlv(&tbs_cursor, tbs.end, &field) != 0 ||
        !tls_x509_expect_sequence(&field)) {
        return -1;
    }
    tls_x509_slice_from_tlv(&out_view->subject_der, &field);

    if (tls_x509_next_tlv(&tbs_cursor, tbs.end, &field) != 0 ||
        !tls_x509_expect_sequence(&field)) {
        return -1;
    }
    tls_x509_slice_from_tlv(&out_view->subject_public_key_info_der, &field);

    while (tbs_cursor < tbs.end) {
        if (tls_x509_next_tlv(&tbs_cursor, tbs.end, &field) != 0) {
            return -1;
        }
        if (field.tag == TLS_X509_TAG_CONTEXT_3) {
            tls_x509_tlv_t extensions;
            if (field.value_len == 0u ||
                tls_x509_parse_tlv(field.value, field.value_len, &extensions) != 0 ||
                !tls_x509_expect_sequence(&extensions) ||
                extensions.end != field.end) {
                return -1;
            }
            tls_x509_slice_from_tlv(&out_view->extensions_der, &extensions);
            out_view->has_extensions = 1;
        }
    }

    return 0;
}

int tls_x509_parse_oid(const tls_x509_slice_t* oid_der,
                       uint32_t* out_arcs,
                       size_t max_arcs,
                       size_t* out_arc_count)
{
    tls_x509_tlv_t oid_tlv;
    size_t arc_count = 0u;
    uint32_t value = 0u;
    int has_base128_octet = 0;

    if (!oid_der || !oid_der->data || !out_arc_count) {
        return -1;
    }
    *out_arc_count = 0u;
    if (tls_x509_parse_tlv(oid_der->data, oid_der->len, &oid_tlv) != 0 ||
        oid_tlv.tag != TLS_X509_TAG_OID || oid_tlv.value_len == 0u ||
        oid_tlv.header + oid_tlv.header_len + oid_tlv.value_len != oid_der->data + oid_der->len) {
        return -1;
    }
    if (max_arcs > 0u && !out_arcs) {
        return -1;
    }

    {
        uint8_t first = oid_tlv.value[0];
        uint32_t first_arc;
        uint32_t second_arc;
        if (first < 40u) {
            first_arc = 0u;
            second_arc = first;
        } else if (first < 80u) {
            first_arc = 1u;
            second_arc = (uint32_t)(first - 40u);
        } else {
            first_arc = 2u;
            second_arc = (uint32_t)(first - 80u);
        }
        if (arc_count < max_arcs) out_arcs[arc_count] = first_arc;
        ++arc_count;
        if (arc_count < max_arcs) out_arcs[arc_count] = second_arc;
        ++arc_count;
    }

    for (size_t i = 1u; i < oid_tlv.value_len; ++i) {
        uint8_t octet = oid_tlv.value[i];
        if (value > (UINT32_MAX >> 7)) {
            return -1;
        }
        value = (uint32_t)((value << 7) | (uint32_t)(octet & 0x7fu));
        has_base128_octet = 1;
        if ((octet & 0x80u) == 0u) {
            if (arc_count < max_arcs) out_arcs[arc_count] = value;
            ++arc_count;
            value = 0u;
            has_base128_octet = 0;
        }
    }
    if (has_base128_octet || arc_count > max_arcs) {
        return -1;
    }

    *out_arc_count = arc_count;
    return 0;
}

tls_x509_known_oid_t tls_x509_known_oid_from_der(const tls_x509_slice_t* oid_der)
{
    static const uint8_t k_rsa_encryption[] = {0x06u, 0x09u, 0x2au, 0x86u, 0x48u, 0x86u, 0xf7u, 0x0du, 0x01u, 0x01u, 0x01u};
    static const uint8_t k_sha256_with_rsa[] = {0x06u, 0x09u, 0x2au, 0x86u, 0x48u, 0x86u, 0xf7u, 0x0du, 0x01u, 0x01u, 0x0bu};
    static const uint8_t k_sha256[] = {0x06u, 0x09u, 0x60u, 0x86u, 0x48u, 0x01u, 0x65u, 0x03u, 0x04u, 0x02u, 0x01u};
    static const uint8_t k_ec_public_key[] = {0x06u, 0x07u, 0x2au, 0x86u, 0x48u, 0xceu, 0x3du, 0x02u, 0x01u};
    static const uint8_t k_ecdsa_with_sha256[] = {0x06u, 0x08u, 0x2au, 0x86u, 0x48u, 0xceu, 0x3du, 0x04u, 0x03u, 0x02u};
    static const uint8_t k_secp256r1[] = {0x06u, 0x08u, 0x2au, 0x86u, 0x48u, 0xceu, 0x3du, 0x03u, 0x01u, 0x07u};
    static const uint8_t k_basic_constraints[] = {0x06u, 0x03u, 0x55u, 0x1du, 0x13u};
    static const uint8_t k_key_usage[] = {0x06u, 0x03u, 0x55u, 0x1du, 0x0fu};
    static const uint8_t k_subject_alt_name[] = {0x06u, 0x03u, 0x55u, 0x1du, 0x11u};

    if (tls_x509_slice_equal_bytes(oid_der, k_rsa_encryption, sizeof(k_rsa_encryption))) {
        return TLS_X509_OID_RSA_ENCRYPTION;
    }
    if (tls_x509_slice_equal_bytes(oid_der, k_sha256_with_rsa, sizeof(k_sha256_with_rsa))) {
        return TLS_X509_OID_SHA256_WITH_RSA_ENCRYPTION;
    }
    if (tls_x509_slice_equal_bytes(oid_der, k_sha256, sizeof(k_sha256))) {
        return TLS_X509_OID_SHA256;
    }
    if (tls_x509_slice_equal_bytes(oid_der, k_ec_public_key, sizeof(k_ec_public_key))) {
        return TLS_X509_OID_EC_PUBLIC_KEY;
    }
    if (tls_x509_slice_equal_bytes(oid_der, k_ecdsa_with_sha256, sizeof(k_ecdsa_with_sha256))) {
        return TLS_X509_OID_ECDSA_WITH_SHA256;
    }
    if (tls_x509_slice_equal_bytes(oid_der, k_secp256r1, sizeof(k_secp256r1))) {
        return TLS_X509_OID_SECP256R1;
    }
    if (tls_x509_slice_equal_bytes(oid_der, k_basic_constraints, sizeof(k_basic_constraints))) {
        return TLS_X509_OID_BASIC_CONSTRAINTS;
    }
    if (tls_x509_slice_equal_bytes(oid_der, k_key_usage, sizeof(k_key_usage))) {
        return TLS_X509_OID_KEY_USAGE;
    }
    if (tls_x509_slice_equal_bytes(oid_der, k_subject_alt_name, sizeof(k_subject_alt_name))) {
        return TLS_X509_OID_SUBJECT_ALT_NAME;
    }
    return TLS_X509_OID_UNKNOWN;
}

int tls_x509_parse_algorithm_identifier(const tls_x509_slice_t* algorithm_der,
                                        tls_x509_algorithm_identifier_t* out_algorithm)
{
    tls_x509_tlv_t algorithm_seq;
    tls_x509_tlv_t oid_tlv;
    tls_x509_tlv_t parameters_tlv;
    const uint8_t* cursor;

    if (!algorithm_der || !algorithm_der->data || !out_algorithm) {
        return -1;
    }
    memset(out_algorithm, 0, sizeof(*out_algorithm));
    if (tls_x509_parse_tlv(algorithm_der->data, algorithm_der->len, &algorithm_seq) != 0 ||
        !tls_x509_expect_sequence(&algorithm_seq) ||
        algorithm_seq.header + algorithm_seq.header_len + algorithm_seq.value_len != algorithm_der->data + algorithm_der->len) {
        return -1;
    }

    cursor = algorithm_seq.value;
    if (tls_x509_next_tlv(&cursor, algorithm_seq.end, &oid_tlv) != 0 ||
        oid_tlv.tag != TLS_X509_TAG_OID) {
        return -1;
    }
    tls_x509_slice_from_tlv(&out_algorithm->algorithm_oid_der, &oid_tlv);
    out_algorithm->known_oid = tls_x509_known_oid_from_der(&out_algorithm->algorithm_oid_der);

    if (cursor < algorithm_seq.end) {
        if (tls_x509_next_tlv(&cursor, algorithm_seq.end, &parameters_tlv) != 0 || cursor != algorithm_seq.end) {
            return -1;
        }
        tls_x509_slice_from_tlv(&out_algorithm->parameters_der, &parameters_tlv);
        out_algorithm->has_parameters = 1;
    } else {
        out_algorithm->has_parameters = 0;
    }
    return 0;
}

int tls_x509_parse_subject_public_key_info(const tls_x509_certificate_view_t* cert,
                                           tls_x509_subject_public_key_info_t* out_spki)
{
    tls_x509_tlv_t spki_seq;
    tls_x509_tlv_t algorithm_tlv;
    tls_x509_tlv_t key_tlv;
    const uint8_t* cursor;
    tls_x509_slice_t algorithm_der;

    if (!cert || !cert->subject_public_key_info_der.data || !out_spki) {
        return -1;
    }
    memset(out_spki, 0, sizeof(*out_spki));
    if (tls_x509_parse_tlv(cert->subject_public_key_info_der.data, cert->subject_public_key_info_der.len, &spki_seq) != 0 ||
        !tls_x509_expect_sequence(&spki_seq) ||
        spki_seq.header + spki_seq.header_len + spki_seq.value_len != cert->subject_public_key_info_der.data + cert->subject_public_key_info_der.len) {
        return -1;
    }

    cursor = spki_seq.value;
    if (tls_x509_next_tlv(&cursor, spki_seq.end, &algorithm_tlv) != 0 ||
        !tls_x509_expect_sequence(&algorithm_tlv) ||
        tls_x509_next_tlv(&cursor, spki_seq.end, &key_tlv) != 0 ||
        key_tlv.tag != TLS_X509_TAG_BIT_STRING || cursor != spki_seq.end) {
        return -1;
    }
    tls_x509_slice_from_tlv(&algorithm_der, &algorithm_tlv);
    if (tls_x509_parse_algorithm_identifier(&algorithm_der, &out_spki->algorithm) != 0) {
        return -1;
    }
    tls_x509_slice_from_tlv(&out_spki->subject_public_key_der, &key_tlv);
    return 0;
}

int tls_x509_parse_bit_string(const tls_x509_slice_t* bit_string_der,
                              tls_x509_bit_string_t* out_bit_string)
{
    tls_x509_tlv_t bit_string_tlv;

    if (!bit_string_der || !bit_string_der->data || !out_bit_string) {
        return -1;
    }
    memset(out_bit_string, 0, sizeof(*out_bit_string));
    if (tls_x509_parse_tlv(bit_string_der->data, bit_string_der->len, &bit_string_tlv) != 0 ||
        bit_string_tlv.tag != TLS_X509_TAG_BIT_STRING ||
        bit_string_tlv.value_len == 0u ||
        bit_string_tlv.header + bit_string_tlv.header_len + bit_string_tlv.value_len != bit_string_der->data + bit_string_der->len) {
        return -1;
    }
    out_bit_string->unused_bits = bit_string_tlv.value[0];
    if (out_bit_string->unused_bits > 7u) {
        return -1;
    }
    out_bit_string->bytes.data = bit_string_tlv.value + 1u;
    out_bit_string->bytes.len = bit_string_tlv.value_len - 1u;
    return 0;
}

int tls_x509_parse_rsa_public_key_from_spki(const tls_x509_subject_public_key_info_t* spki,
                                            tls_x509_rsa_public_key_t* out_rsa)
{
    tls_x509_bit_string_t key_bits;
    tls_x509_tlv_t rsa_seq;
    tls_x509_tlv_t modulus;
    tls_x509_tlv_t exponent;
    const uint8_t* cursor;

    if (!spki || !out_rsa || spki->algorithm.known_oid != TLS_X509_OID_RSA_ENCRYPTION) {
        return -1;
    }
    memset(out_rsa, 0, sizeof(*out_rsa));
    if (tls_x509_parse_bit_string(&spki->subject_public_key_der, &key_bits) != 0 ||
        key_bits.unused_bits != 0u ||
        key_bits.bytes.len == 0u) {
        return -1;
    }
    if (tls_x509_parse_tlv(key_bits.bytes.data, key_bits.bytes.len, &rsa_seq) != 0 ||
        !tls_x509_expect_sequence(&rsa_seq) ||
        rsa_seq.header + rsa_seq.header_len + rsa_seq.value_len != key_bits.bytes.data + key_bits.bytes.len) {
        return -1;
    }
    cursor = rsa_seq.value;
    if (tls_x509_next_tlv(&cursor, rsa_seq.end, &modulus) != 0 ||
        tls_x509_next_tlv(&cursor, rsa_seq.end, &exponent) != 0 ||
        cursor != rsa_seq.end ||
        modulus.tag != TLS_X509_TAG_INTEGER ||
        exponent.tag != TLS_X509_TAG_INTEGER ||
        modulus.value_len == 0u ||
        exponent.value_len == 0u) {
        return -1;
    }
    tls_x509_slice_from_tlv(&out_rsa->modulus_der, &modulus);
    tls_x509_slice_from_tlv(&out_rsa->public_exponent_der, &exponent);
    return 0;
}

int tls_x509_parse_signature_value(const tls_x509_certificate_view_t* cert,
                                   tls_x509_bit_string_t* out_signature)
{
    if (!cert || !cert->signature_value_der.data || !out_signature) {
        return -1;
    }
    return tls_x509_parse_bit_string(&cert->signature_value_der, out_signature);
}

int tls_x509_parse_digest_info(const tls_x509_slice_t* digest_info_der,
                               tls_x509_digest_info_t* out_digest_info)
{
    tls_x509_tlv_t digest_info_seq;
    tls_x509_tlv_t algorithm_tlv;
    tls_x509_tlv_t digest_tlv;
    const uint8_t* cursor;
    tls_x509_slice_t algorithm_der;

    if (!digest_info_der || !digest_info_der->data || !out_digest_info) {
        return -1;
    }
    memset(out_digest_info, 0, sizeof(*out_digest_info));
    if (tls_x509_parse_tlv(digest_info_der->data, digest_info_der->len, &digest_info_seq) != 0 ||
        !tls_x509_expect_sequence(&digest_info_seq) ||
        digest_info_seq.header + digest_info_seq.header_len + digest_info_seq.value_len != digest_info_der->data + digest_info_der->len) {
        return -1;
    }
    cursor = digest_info_seq.value;
    if (tls_x509_next_tlv(&cursor, digest_info_seq.end, &algorithm_tlv) != 0 ||
        !tls_x509_expect_sequence(&algorithm_tlv) ||
        tls_x509_next_tlv(&cursor, digest_info_seq.end, &digest_tlv) != 0 ||
        digest_tlv.tag != TLS_X509_TAG_OCTET_STRING ||
        cursor != digest_info_seq.end) {
        return -1;
    }
    tls_x509_slice_from_tlv(&algorithm_der, &algorithm_tlv);
    if (tls_x509_parse_algorithm_identifier(&algorithm_der, &out_digest_info->algorithm) != 0) {
        return -1;
    }
    out_digest_info->digest.data = digest_tlv.value;
    out_digest_info->digest.len = digest_tlv.value_len;
    return 0;
}

int tls_x509_digest_info_matches_sha256(const tls_x509_digest_info_t* digest_info,
                                        const uint8_t digest[32])
{
    uint8_t diff = 0u;

    if (!digest_info || !digest ||
        digest_info->algorithm.known_oid != TLS_X509_OID_SHA256 ||
        digest_info->digest.len != 32u ||
        !digest_info->digest.data) {
        return 0;
    }
    for (size_t i = 0u; i < 32u; ++i) {
        diff = (uint8_t)(diff | (uint8_t)(digest_info->digest.data[i] ^ digest[i]));
    }
    return diff == 0u;
}

int tls_x509_rsa_pkcs1_v15_encoded_message_matches_sha256(const tls_x509_slice_t* encoded_message,
                                                          const uint8_t digest[32])
{
    size_t separator = 0u;
    tls_x509_digest_info_t digest_info;
    tls_x509_slice_t digest_info_der;

    if (!encoded_message || !encoded_message->data || !digest || encoded_message->len < 11u) {
        return 0;
    }
    if (encoded_message->data[0] != 0x00u || encoded_message->data[1] != 0x01u) {
        return 0;
    }
    for (size_t i = 2u; i < encoded_message->len; ++i) {
        if (encoded_message->data[i] == 0x00u) {
            separator = i;
            break;
        }
        if (encoded_message->data[i] != 0xffu) {
            return 0;
        }
    }
    if (separator < 10u || separator + 1u >= encoded_message->len) {
        return 0;
    }

    digest_info_der.data = encoded_message->data + separator + 1u;
    digest_info_der.len = encoded_message->len - separator - 1u;
    if (tls_x509_parse_digest_info(&digest_info_der, &digest_info) != 0) {
        return 0;
    }
    return tls_x509_digest_info_matches_sha256(&digest_info, digest);
}

#define TLS_X509_RSA_MAX_BYTES 256u

static int tls_x509_integer_value_from_der(const tls_x509_slice_t* integer_der,
                                           tls_x509_slice_t* out_value)
{
    tls_x509_tlv_t integer;
    const uint8_t* value;
    size_t value_len;

    if (!integer_der || !integer_der->data || !out_value) {
        return -1;
    }
    if (tls_x509_parse_tlv(integer_der->data, integer_der->len, &integer) != 0 ||
        integer.tag != TLS_X509_TAG_INTEGER || integer.value_len == 0u ||
        integer.header + integer.header_len + integer.value_len != integer_der->data + integer_der->len) {
        return -1;
    }
    value = integer.value;
    value_len = integer.value_len;
    while (value_len > 1u && value[0] == 0x00u) {
        ++value;
        --value_len;
    }
    out_value->data = value;
    out_value->len = value_len;
    return 0;
}

static int tls_x509_bn_cmp(const uint8_t* a, const uint8_t* b, size_t len)
{
    for (size_t i = 0u; i < len; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static void tls_x509_bn_sub(uint8_t* out, const uint8_t* a, const uint8_t* b, size_t len)
{
    uint16_t borrow = 0u;
    for (size_t i = len; i > 0u; --i) {
        uint16_t av = a[i - 1u];
        uint16_t bv = (uint16_t)(b[i - 1u] + borrow);
        if (av < bv) {
            out[i - 1u] = (uint8_t)(0x100u + av - bv);
            borrow = 1u;
        } else {
            out[i - 1u] = (uint8_t)(av - bv);
            borrow = 0u;
        }
    }
}

static void tls_x509_bn_add(uint8_t* out, const uint8_t* a, const uint8_t* b, size_t len)
{
    uint16_t carry = 0u;
    for (size_t i = len; i > 0u; --i) {
        uint16_t sum = (uint16_t)(a[i - 1u] + b[i - 1u] + carry);
        out[i - 1u] = (uint8_t)sum;
        carry = (uint16_t)(sum >> 8u);
    }
}

static void tls_x509_bn_copy(uint8_t* out, const uint8_t* in, size_t len)
{
    for (size_t i = 0u; i < len; ++i) {
        out[i] = in[i];
    }
}

static void tls_x509_bn_zero(uint8_t* out, size_t len)
{
    for (size_t i = 0u; i < len; ++i) {
        out[i] = 0u;
    }
}

static void tls_x509_bn_mod_add(uint8_t* out,
                                const uint8_t* a,
                                const uint8_t* b,
                                const uint8_t* mod,
                                size_t len)
{
    uint8_t mod_minus_b[TLS_X509_RSA_MAX_BYTES];

    tls_x509_bn_sub(mod_minus_b, mod, b, len);
    if (tls_x509_bn_cmp(a, mod_minus_b, len) >= 0) {
        tls_x509_bn_sub(out, a, mod_minus_b, len);
    } else {
        tls_x509_bn_add(out, a, b, len);
    }
}

static void tls_x509_bn_mod_mul(uint8_t* out,
                                const uint8_t* a,
                                const uint8_t* b,
                                const uint8_t* mod,
                                size_t len)
{
    uint8_t result[TLS_X509_RSA_MAX_BYTES];
    uint8_t addend[TLS_X509_RSA_MAX_BYTES];
    uint8_t tmp[TLS_X509_RSA_MAX_BYTES];

    tls_x509_bn_zero(result, len);
    tls_x509_bn_copy(addend, a, len);

    for (size_t byte = len; byte > 0u; --byte) {
        uint8_t v = b[byte - 1u];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            if ((v & (uint8_t)(1u << bit)) != 0u) {
                tls_x509_bn_mod_add(tmp, result, addend, mod, len);
                tls_x509_bn_copy(result, tmp, len);
            }
            tls_x509_bn_mod_add(tmp, addend, addend, mod, len);
            tls_x509_bn_copy(addend, tmp, len);
        }
    }

    tls_x509_bn_copy(out, result, len);
}

static void tls_x509_bn_mod_pow(uint8_t* out,
                                const uint8_t* base,
                                const uint8_t* exponent,
                                size_t exponent_len,
                                const uint8_t* mod,
                                size_t len)
{
    uint8_t result[TLS_X509_RSA_MAX_BYTES];
    uint8_t power[TLS_X509_RSA_MAX_BYTES];
    uint8_t tmp[TLS_X509_RSA_MAX_BYTES];

    tls_x509_bn_zero(result, len);
    result[len - 1u] = 1u;
    tls_x509_bn_copy(power, base, len);

    for (size_t byte = exponent_len; byte > 0u; --byte) {
        uint8_t v = exponent[byte - 1u];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            if ((v & (uint8_t)(1u << bit)) != 0u) {
                tls_x509_bn_mod_mul(tmp, result, power, mod, len);
                tls_x509_bn_copy(result, tmp, len);
            }
            tls_x509_bn_mod_mul(tmp, power, power, mod, len);
            tls_x509_bn_copy(power, tmp, len);
        }
    }

    tls_x509_bn_copy(out, result, len);
}

int tls_x509_rsa_verify_pkcs1_v15_sha256(const tls_x509_rsa_public_key_t* public_key,
                                         const tls_x509_bit_string_t* signature,
                                         const uint8_t digest[32])
{
    tls_x509_slice_t modulus_value;
    tls_x509_slice_t exponent_value;
    uint8_t modulus[TLS_X509_RSA_MAX_BYTES];
    uint8_t exponent[TLS_X509_RSA_MAX_BYTES];
    uint8_t sig[TLS_X509_RSA_MAX_BYTES];
    uint8_t encoded[TLS_X509_RSA_MAX_BYTES];
    size_t key_len;

    if (!public_key || !signature || !digest || !signature->bytes.data ||
        signature->unused_bits != 0u ||
        tls_x509_integer_value_from_der(&public_key->modulus_der, &modulus_value) != 0 ||
        tls_x509_integer_value_from_der(&public_key->public_exponent_der, &exponent_value) != 0 ||
        modulus_value.len == 0u || modulus_value.len > TLS_X509_RSA_MAX_BYTES ||
        exponent_value.len == 0u || exponent_value.len > TLS_X509_RSA_MAX_BYTES ||
        signature->bytes.len > modulus_value.len) {
        return 0;
    }

    key_len = modulus_value.len;
    tls_x509_bn_zero(modulus, key_len);
    tls_x509_bn_zero(exponent, key_len);
    tls_x509_bn_zero(sig, key_len);
    for (size_t i = 0u; i < key_len; ++i) {
        modulus[i] = modulus_value.data[i];
    }
    for (size_t i = 0u; i < exponent_value.len; ++i) {
        exponent[key_len - exponent_value.len + i] = exponent_value.data[i];
    }
    for (size_t i = 0u; i < signature->bytes.len; ++i) {
        sig[key_len - signature->bytes.len + i] = signature->bytes.data[i];
    }
    if (tls_x509_bn_cmp(sig, modulus, key_len) >= 0) {
        return 0;
    }

    tls_x509_bn_mod_pow(encoded, sig, exponent, key_len, modulus, key_len);
    return tls_x509_rsa_pkcs1_v15_encoded_message_matches_sha256(
        &(tls_x509_slice_t){encoded, key_len}, digest);
}

int tls_x509_parse_signature_algorithm(const tls_x509_certificate_view_t* cert,
                                       tls_x509_algorithm_identifier_t* out_algorithm)
{
    if (!cert || !cert->signature_algorithm_der.data || !out_algorithm) {
        return -1;
    }
    return tls_x509_parse_algorithm_identifier(&cert->signature_algorithm_der, out_algorithm);
}

int tls_x509_verify_certificate_signature_sha256_rsa(const tls_x509_certificate_view_t* child,
                                                     const tls_x509_certificate_view_t* issuer)
{
    tls_x509_algorithm_identifier_t child_signature_algorithm;
    tls_x509_subject_public_key_info_t issuer_spki;
    tls_x509_rsa_public_key_t issuer_rsa;
    tls_x509_bit_string_t child_signature;
    uint8_t digest[TLS_SHA256_DIGEST_SIZE];

    if (!child || !issuer ||
        !child->tbs_certificate_der.data || child->tbs_certificate_der.len == 0u) {
        return 0;
    }
    if (tls_x509_parse_signature_algorithm(child, &child_signature_algorithm) != 0 ||
        child_signature_algorithm.known_oid != TLS_X509_OID_SHA256_WITH_RSA_ENCRYPTION) {
        return 0;
    }
    if (tls_x509_parse_subject_public_key_info(issuer, &issuer_spki) != 0 ||
        issuer_spki.algorithm.known_oid != TLS_X509_OID_RSA_ENCRYPTION ||
        tls_x509_parse_rsa_public_key_from_spki(&issuer_spki, &issuer_rsa) != 0 ||
        tls_x509_parse_signature_value(child, &child_signature) != 0) {
        return 0;
    }

    tls_sha256(child->tbs_certificate_der.data,
               child->tbs_certificate_der.len,
               digest);
    return tls_x509_rsa_verify_pkcs1_v15_sha256(&issuer_rsa,
                                                &child_signature,
                                                digest);
}

static int tls_x509_parse_uint32_integer_value(const tls_x509_slice_t* integer_der,
                                               uint32_t* out_value)
{
    tls_x509_slice_t value;
    uint32_t result = 0u;

    if (!out_value || tls_x509_integer_value_from_der(integer_der, &value) != 0 ||
        value.len == 0u || value.len > 4u) {
        return -1;
    }
    for (size_t i = 0u; i < value.len; ++i) {
        result = (uint32_t)((result << 8u) | value.data[i]);
    }
    *out_value = result;
    return 0;
}

int tls_x509_find_extension(const tls_x509_certificate_view_t* cert,
                            tls_x509_known_oid_t oid,
                            tls_x509_extension_t* out_extension)
{
    tls_x509_tlv_t extensions;
    const uint8_t* cursor;

    if (!cert || !out_extension || !cert->has_extensions || !cert->extensions_der.data) {
        return -1;
    }
    memset(out_extension, 0, sizeof(*out_extension));
    if (tls_x509_parse_tlv(cert->extensions_der.data, cert->extensions_der.len, &extensions) != 0 ||
        !tls_x509_expect_sequence(&extensions) ||
        extensions.end != cert->extensions_der.data + cert->extensions_der.len) {
        return -1;
    }

    cursor = extensions.value;
    while (cursor < extensions.end) {
        tls_x509_tlv_t extension_seq;
        tls_x509_tlv_t oid_tlv;
        tls_x509_tlv_t maybe_tlv;
        tls_x509_slice_t oid_der;
        tls_x509_extension_t candidate;
        const uint8_t* ext_cursor;

        if (tls_x509_next_tlv(&cursor, extensions.end, &extension_seq) != 0 ||
            !tls_x509_expect_sequence(&extension_seq)) {
            return -1;
        }
        memset(&candidate, 0, sizeof(candidate));
        ext_cursor = extension_seq.value;
        if (tls_x509_next_tlv(&ext_cursor, extension_seq.end, &oid_tlv) != 0 ||
            oid_tlv.tag != TLS_X509_TAG_OID) {
            return -1;
        }
        tls_x509_slice_from_tlv(&oid_der, &oid_tlv);
        candidate.oid_der = oid_der;
        candidate.known_oid = tls_x509_known_oid_from_der(&oid_der);

        if (tls_x509_next_tlv(&ext_cursor, extension_seq.end, &maybe_tlv) != 0) {
            return -1;
        }
        if (maybe_tlv.tag == TLS_X509_TAG_BOOLEAN) {
            if (maybe_tlv.value_len != 1u) {
                return -1;
            }
            candidate.critical = maybe_tlv.value[0] != 0u;
            if (tls_x509_next_tlv(&ext_cursor, extension_seq.end, &maybe_tlv) != 0) {
                return -1;
            }
        }
        if (maybe_tlv.tag != TLS_X509_TAG_OCTET_STRING || ext_cursor != extension_seq.end) {
            return -1;
        }
        candidate.value_der.data = maybe_tlv.value;
        candidate.value_der.len = maybe_tlv.value_len;

        if (candidate.known_oid == oid) {
            *out_extension = candidate;
            return 0;
        }
    }
    return -1;
}

int tls_x509_parse_basic_constraints(const tls_x509_certificate_view_t* cert,
                                     tls_x509_basic_constraints_t* out_constraints)
{
    tls_x509_extension_t extension;
    tls_x509_tlv_t seq;
    const uint8_t* cursor;

    if (!out_constraints) {
        return -1;
    }
    memset(out_constraints, 0, sizeof(*out_constraints));
    if (tls_x509_find_extension(cert, TLS_X509_OID_BASIC_CONSTRAINTS, &extension) != 0) {
        return 0;
    }
    out_constraints->present = 1;
    if (tls_x509_parse_tlv(extension.value_der.data, extension.value_der.len, &seq) != 0 ||
        !tls_x509_expect_sequence(&seq) ||
        seq.end != extension.value_der.data + extension.value_der.len) {
        return -1;
    }

    cursor = seq.value;
    if (cursor < seq.end) {
        tls_x509_tlv_t field;
        if (tls_x509_next_tlv(&cursor, seq.end, &field) != 0) {
            return -1;
        }
        if (field.tag == TLS_X509_TAG_BOOLEAN) {
            if (field.value_len != 1u) {
                return -1;
            }
            out_constraints->ca = field.value[0] != 0u;
            if (cursor < seq.end) {
                if (tls_x509_next_tlv(&cursor, seq.end, &field) != 0) {
                    return -1;
                }
            } else {
                return 0;
            }
        }
        if (field.tag != TLS_X509_TAG_INTEGER || cursor != seq.end) {
            return -1;
        }
        out_constraints->has_path_len_constraint = 1;
        if (tls_x509_parse_uint32_integer_value(&(tls_x509_slice_t){field.header, field.header_len + field.value_len},
                                                &out_constraints->path_len_constraint) != 0) {
            return -1;
        }
    }
    return 0;
}

int tls_x509_parse_key_usage(const tls_x509_certificate_view_t* cert,
                             tls_x509_key_usage_t* out_key_usage)
{
    tls_x509_extension_t extension;
    tls_x509_slice_t bit_string_der;
    tls_x509_bit_string_t bit_string;
    uint16_t bits = 0u;

    if (!out_key_usage) {
        return -1;
    }
    memset(out_key_usage, 0, sizeof(*out_key_usage));
    if (tls_x509_find_extension(cert, TLS_X509_OID_KEY_USAGE, &extension) != 0) {
        return 0;
    }
    out_key_usage->present = 1;
    bit_string_der = extension.value_der;
    if (tls_x509_parse_bit_string(&bit_string_der, &bit_string) != 0 ||
        bit_string.bytes.len == 0u || bit_string.bytes.len > 2u) {
        return -1;
    }
    for (size_t i = 0u; i < bit_string.bytes.len; ++i) {
        bits = (uint16_t)(bits | (uint16_t)((uint16_t)bit_string.bytes.data[i] << (8u * i)));
    }
    if (bit_string.unused_bits > 0u) {
        uint8_t mask = (uint8_t)((1u << bit_string.unused_bits) - 1u);
        if ((bit_string.bytes.data[bit_string.bytes.len - 1u] & mask) != 0u) {
            return -1;
        }
    }
    out_key_usage->bits = bits;
    return 0;
}

int tls_x509_certificate_is_ca(const tls_x509_certificate_view_t* cert)
{
    tls_x509_basic_constraints_t constraints;

    if (tls_x509_parse_basic_constraints(cert, &constraints) != 0) {
        return 0;
    }
    return constraints.present && constraints.ca;
}

int tls_x509_certificate_allows_key_cert_sign(const tls_x509_certificate_view_t* cert)
{
    tls_x509_key_usage_t key_usage;

    if (tls_x509_parse_key_usage(cert, &key_usage) != 0) {
        return 0;
    }
    if (!key_usage.present) {
        return 1;
    }
    return (key_usage.bits & 0x20u) != 0u;
}

int tls_x509_certificate_can_sign_certificates(const tls_x509_certificate_view_t* cert)
{
    return tls_x509_certificate_is_ca(cert) &&
           tls_x509_certificate_allows_key_cert_sign(cert);
}

static int tls_x509_ascii_lower(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch + ('a' - 'A');
    }
    return ch;
}

static size_t tls_x509_cstr_len(const char* s)
{
    size_t len = 0u;
    if (!s) {
        return 0u;
    }
    while (s[len] != '\0') {
        ++len;
    }
    return len;
}

static int tls_x509_label_has_dot(const char* s, size_t len)
{
    for (size_t i = 0u; i < len; ++i) {
        if (s[i] == '.') {
            return 1;
        }
    }
    return 0;
}

static int tls_x509_ascii_slice_equals_hostname(const tls_x509_slice_t* pattern,
                                                size_t pattern_offset,
                                                const char* hostname,
                                                size_t hostname_offset)
{
    size_t pattern_len;
    size_t hostname_len;

    if (!pattern || !pattern->data || !hostname || pattern_offset > pattern->len) {
        return 0;
    }
    pattern_len = pattern->len - pattern_offset;
    hostname_len = tls_x509_cstr_len(hostname + hostname_offset);
    if (pattern_len != hostname_len) {
        return 0;
    }
    for (size_t i = 0u; i < pattern_len; ++i) {
        int a = tls_x509_ascii_lower(pattern->data[pattern_offset + i]);
        int b = tls_x509_ascii_lower((unsigned char)hostname[hostname_offset + i]);
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

int tls_x509_match_hostname_pattern(const tls_x509_slice_t* pattern, const char* hostname)
{
    size_t hostname_len;
    size_t suffix_len;
    size_t wildcard_label_len;

    if (!pattern || !pattern->data || pattern->len == 0u || !hostname || hostname[0] == '\0') {
        return 0;
    }
    hostname_len = tls_x509_cstr_len(hostname);
    if (hostname_len == 0u) {
        return 0;
    }

    if (pattern->data[0] != '*') {
        return tls_x509_ascii_slice_equals_hostname(pattern, 0u, hostname, 0u);
    }
    if (pattern->len < 3u || pattern->data[1] != '.') {
        return 0;
    }
    suffix_len = pattern->len - 1u;
    if (hostname_len <= suffix_len) {
        return 0;
    }
    wildcard_label_len = hostname_len - suffix_len;
    if (wildcard_label_len == 0u || tls_x509_label_has_dot(hostname, wildcard_label_len)) {
        return 0;
    }
    return tls_x509_ascii_slice_equals_hostname(pattern, 1u, hostname, wildcard_label_len);
}

int tls_x509_certificate_matches_hostname(const tls_x509_certificate_view_t* cert,
                                          const char* hostname)
{
    tls_x509_extension_t san;
    tls_x509_tlv_t names;
    const uint8_t* cursor;
    int saw_dns_name = 0;

    if (!cert || !hostname) {
        return 0;
    }
    if (tls_x509_find_extension(cert, TLS_X509_OID_SUBJECT_ALT_NAME, &san) != 0) {
        return 0;
    }
    if (tls_x509_parse_tlv(san.value_der.data, san.value_der.len, &names) != 0 ||
        !tls_x509_expect_sequence(&names) ||
        names.end != san.value_der.data + san.value_der.len) {
        return 0;
    }
    cursor = names.value;
    while (cursor < names.end) {
        tls_x509_tlv_t name;
        tls_x509_slice_t dns_name;
        if (tls_x509_next_tlv(&cursor, names.end, &name) != 0) {
            return 0;
        }
        if (name.tag == TLS_X509_TAG_CONTEXT_2) {
            saw_dns_name = 1;
            dns_name.data = name.value;
            dns_name.len = name.value_len;
            if (tls_x509_match_hostname_pattern(&dns_name, hostname)) {
                return 1;
            }
        }
    }
    (void)saw_dns_name;
    return 0;
}

int tls_x509_is_supported_signature_algorithm(tls_x509_known_oid_t oid)
{
    return oid == TLS_X509_OID_SHA256_WITH_RSA_ENCRYPTION ||
           oid == TLS_X509_OID_ECDSA_WITH_SHA256;
}

int tls_x509_is_supported_subject_public_key_algorithm(tls_x509_known_oid_t oid)
{
    return oid == TLS_X509_OID_RSA_ENCRYPTION ||
           oid == TLS_X509_OID_EC_PUBLIC_KEY;
}

static int tls_x509_is_digit(uint8_t c)
{
    return c >= (uint8_t)'0' && c <= (uint8_t)'9';
}

static int tls_x509_parse_decimal(const uint8_t* data, size_t len, uint16_t* out_value)
{
    uint16_t value = 0u;

    if (!data || !out_value || len == 0u) {
        return -1;
    }
    for (size_t i = 0u; i < len; ++i) {
        if (!tls_x509_is_digit(data[i])) {
            return -1;
        }
        value = (uint16_t)(value * 10u + (uint16_t)(data[i] - (uint8_t)'0'));
    }
    *out_value = value;
    return 0;
}

static int tls_x509_is_leap_year(uint16_t year)
{
    if ((year % 400u) == 0u) return 1;
    if ((year % 100u) == 0u) return 0;
    return (year % 4u) == 0u;
}

static uint8_t tls_x509_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t k_days[] = {
        31u, 28u, 31u, 30u, 31u, 30u,
        31u, 31u, 30u, 31u, 30u, 31u,
    };

    if (month < 1u || month > 12u) {
        return 0u;
    }
    if (month == 2u && tls_x509_is_leap_year(year)) {
        return 29u;
    }
    return k_days[month - 1u];
}

static int tls_x509_validate_time(const tls_x509_time_t* time)
{
    uint8_t max_day;

    if (!time) {
        return -1;
    }
    max_day = tls_x509_days_in_month(time->year, time->month);
    if (time->year == 0u || max_day == 0u || time->day < 1u || time->day > max_day ||
        time->hour > 23u || time->minute > 59u || time->second > 59u) {
        return -1;
    }
    return 0;
}

int tls_x509_parse_time(const tls_x509_slice_t* time_der,
                        tls_x509_time_t* out_time)
{
    tls_x509_tlv_t time_tlv;
    uint16_t value;

    if (!time_der || !time_der->data || !out_time) {
        return -1;
    }
    if (tls_x509_parse_tlv(time_der->data, time_der->len, &time_tlv) != 0 ||
        time_tlv.header + time_tlv.header_len + time_tlv.value_len != time_der->data + time_der->len) {
        return -1;
    }

    memset(out_time, 0, sizeof(*out_time));
    if (time_tlv.tag == TLS_X509_TAG_UTC_TIME) {
        if (time_tlv.value_len != 13u || time_tlv.value[12] != (uint8_t)'Z') {
            return -1;
        }
        if (tls_x509_parse_decimal(time_tlv.value, 2u, &value) != 0) return -1;
        out_time->year = (uint16_t)(value >= 50u ? 1900u + value : 2000u + value);
        if (tls_x509_parse_decimal(time_tlv.value + 2u, 2u, &value) != 0) return -1;
        out_time->month = (uint8_t)value;
        if (tls_x509_parse_decimal(time_tlv.value + 4u, 2u, &value) != 0) return -1;
        out_time->day = (uint8_t)value;
        if (tls_x509_parse_decimal(time_tlv.value + 6u, 2u, &value) != 0) return -1;
        out_time->hour = (uint8_t)value;
        if (tls_x509_parse_decimal(time_tlv.value + 8u, 2u, &value) != 0) return -1;
        out_time->minute = (uint8_t)value;
        if (tls_x509_parse_decimal(time_tlv.value + 10u, 2u, &value) != 0) return -1;
        out_time->second = (uint8_t)value;
    } else if (time_tlv.tag == TLS_X509_TAG_GENERALIZED_TIME) {
        if (time_tlv.value_len != 15u || time_tlv.value[14] != (uint8_t)'Z') {
            return -1;
        }
        if (tls_x509_parse_decimal(time_tlv.value, 4u, &value) != 0) return -1;
        out_time->year = value;
        if (tls_x509_parse_decimal(time_tlv.value + 4u, 2u, &value) != 0) return -1;
        out_time->month = (uint8_t)value;
        if (tls_x509_parse_decimal(time_tlv.value + 6u, 2u, &value) != 0) return -1;
        out_time->day = (uint8_t)value;
        if (tls_x509_parse_decimal(time_tlv.value + 8u, 2u, &value) != 0) return -1;
        out_time->hour = (uint8_t)value;
        if (tls_x509_parse_decimal(time_tlv.value + 10u, 2u, &value) != 0) return -1;
        out_time->minute = (uint8_t)value;
        if (tls_x509_parse_decimal(time_tlv.value + 12u, 2u, &value) != 0) return -1;
        out_time->second = (uint8_t)value;
    } else {
        return -1;
    }

    return tls_x509_validate_time(out_time);
}

int tls_x509_parse_validity(const tls_x509_certificate_view_t* cert,
                            tls_x509_validity_t* out_validity)
{
    tls_x509_tlv_t validity;
    tls_x509_tlv_t not_before;
    tls_x509_tlv_t not_after;
    tls_x509_slice_t not_before_slice;
    tls_x509_slice_t not_after_slice;
    const uint8_t* cursor;

    if (!cert || !cert->validity_der.data || !out_validity) {
        return -1;
    }
    if (tls_x509_parse_tlv(cert->validity_der.data, cert->validity_der.len, &validity) != 0 ||
        !tls_x509_expect_sequence(&validity) ||
        validity.header + validity.header_len + validity.value_len != cert->validity_der.data + cert->validity_der.len) {
        return -1;
    }

    cursor = validity.value;
    if (tls_x509_next_tlv(&cursor, validity.end, &not_before) != 0 ||
        tls_x509_next_tlv(&cursor, validity.end, &not_after) != 0 ||
        cursor != validity.end) {
        return -1;
    }

    tls_x509_slice_from_tlv(&not_before_slice, &not_before);
    tls_x509_slice_from_tlv(&not_after_slice, &not_after);
    if (tls_x509_parse_time(&not_before_slice, &out_validity->not_before) != 0 ||
        tls_x509_parse_time(&not_after_slice, &out_validity->not_after) != 0 ||
        tls_x509_compare_time(&out_validity->not_before, &out_validity->not_after) > 0) {
        return -1;
    }
    return 0;
}

int tls_x509_compare_time(const tls_x509_time_t* a, const tls_x509_time_t* b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->year != b->year) return a->year < b->year ? -1 : 1;
    if (a->month != b->month) return a->month < b->month ? -1 : 1;
    if (a->day != b->day) return a->day < b->day ? -1 : 1;
    if (a->hour != b->hour) return a->hour < b->hour ? -1 : 1;
    if (a->minute != b->minute) return a->minute < b->minute ? -1 : 1;
    if (a->second != b->second) return a->second < b->second ? -1 : 1;
    return 0;
}

int tls_x509_is_time_within_validity(const tls_x509_certificate_view_t* cert,
                                     const tls_x509_time_t* now)
{
    tls_x509_validity_t validity;

    if (!now || tls_x509_validate_time(now) != 0 ||
        tls_x509_parse_validity(cert, &validity) != 0) {
        return 0;
    }
    return tls_x509_compare_time(now, &validity.not_before) >= 0 &&
           tls_x509_compare_time(now, &validity.not_after) <= 0;
}

int tls_x509_name_equal(const tls_x509_slice_t* a, const tls_x509_slice_t* b)
{
    uint8_t diff = 0u;

    if (!a || !b || !a->data || !b->data || a->len != b->len) {
        return 0;
    }
    for (size_t i = 0u; i < a->len; ++i) {
        diff = (uint8_t)(diff | (uint8_t)(a->data[i] ^ b->data[i]));
    }
    return diff == 0u;
}

int tls_x509_issuer_matches_subject(const tls_x509_certificate_view_t* child,
                                    const tls_x509_certificate_view_t* issuer)
{
    if (!child || !issuer) {
        return 0;
    }
    return tls_x509_name_equal(&child->issuer_der, &issuer->subject_der);
}
