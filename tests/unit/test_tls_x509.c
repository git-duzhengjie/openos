#include "unit_test.h"

#include <stdint.h>

#include "../../src/kernel/include/tls_x509.h"
#include "../../src/kernel/tls_crypto.c"
#include "../../src/kernel/tls_x509.c"

static const uint8_t k_minimal_x509_der[] = {
    0x30, 0x38,
      0x30, 0x30,
        0xa0, 0x03,
          0x02, 0x01, 0x02,
        0x02, 0x01, 0x05,
        0x30, 0x00,
        0x30, 0x00,
        0x30, 0x1e,
          0x17, 0x0d,
            '2', '5', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z',
          0x17, 0x0d,
            '3', '0', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z',
        0x30, 0x00,
        0x30, 0x00,
      0x30, 0x00,
      0x03, 0x02, 0x00, 0x00,
};

UNIT_TEST_CASE(test_tls_x509_parse_minimal_certificate)
{
    tls_x509_certificate_view_t cert;

    ASSERT_EQ_INT(0, tls_x509_parse_certificate(k_minimal_x509_der,
                                                sizeof(k_minimal_x509_der),
                                                &cert));
    ASSERT_TRUE(cert.has_explicit_version);
    ASSERT_EQ_INT(3, cert.version);
    ASSERT_EQ_SIZE(sizeof(k_minimal_x509_der), cert.certificate_der.len);
    ASSERT_EQ_SIZE(50u, cert.tbs_certificate_der.len);
    ASSERT_EQ_SIZE(3u, cert.serial_number_der.len);
    ASSERT_EQ_SIZE(2u, cert.issuer_der.len);
    ASSERT_EQ_SIZE(32u, cert.validity_der.len);
    ASSERT_EQ_SIZE(2u, cert.subject_der.len);
    ASSERT_EQ_SIZE(2u, cert.subject_public_key_info_der.len);
    ASSERT_EQ_SIZE(2u, cert.signature_algorithm_der.len);
    ASSERT_EQ_SIZE(4u, cert.signature_value_der.len);
    ASSERT_EQ_INT(0x02, cert.serial_number_der.data[0]);
    ASSERT_EQ_INT(0x05, cert.serial_number_der.data[2]);
}

UNIT_TEST_CASE(test_tls_x509_rejects_malformed_length)
{
    uint8_t malformed[sizeof(k_minimal_x509_der)];

    for (size_t i = 0u; i < sizeof(k_minimal_x509_der); ++i) {
        malformed[i] = k_minimal_x509_der[i];
    }
    malformed[1] = 0x39;

    ASSERT_TRUE(tls_x509_parse_certificate(malformed,
                                           sizeof(malformed),
                                           &(tls_x509_certificate_view_t){0}) != 0);
    ASSERT_TRUE(tls_x509_parse_certificate(NULL,
                                           sizeof(k_minimal_x509_der),
                                           &(tls_x509_certificate_view_t){0}) != 0);
}

UNIT_TEST_CASE(test_tls_x509_validity_time_checks)
{
    tls_x509_certificate_view_t cert;
    tls_x509_validity_t validity;
    const tls_x509_time_t before = {2024u, 12u, 31u, 23u, 59u, 59u};
    const tls_x509_time_t start = {2025u, 1u, 1u, 0u, 0u, 0u};
    const tls_x509_time_t middle = {2026u, 6u, 21u, 15u, 0u, 0u};
    const tls_x509_time_t end = {2030u, 1u, 1u, 0u, 0u, 0u};
    const tls_x509_time_t after = {2030u, 1u, 1u, 0u, 0u, 1u};

    ASSERT_EQ_INT(0, tls_x509_parse_certificate(k_minimal_x509_der,
                                                sizeof(k_minimal_x509_der),
                                                &cert));
    ASSERT_EQ_INT(0, tls_x509_parse_validity(&cert, &validity));
    ASSERT_EQ_INT(2025, validity.not_before.year);
    ASSERT_EQ_INT(1, validity.not_before.month);
    ASSERT_EQ_INT(2030, validity.not_after.year);
    ASSERT_EQ_INT(0, tls_x509_compare_time(&start, &validity.not_before));
    ASSERT_TRUE(tls_x509_compare_time(&before, &validity.not_before) < 0);
    ASSERT_TRUE(tls_x509_is_time_within_validity(&cert, &before) == 0);
    ASSERT_TRUE(tls_x509_is_time_within_validity(&cert, &start) != 0);
    ASSERT_TRUE(tls_x509_is_time_within_validity(&cert, &middle) != 0);
    ASSERT_TRUE(tls_x509_is_time_within_validity(&cert, &end) != 0);
    ASSERT_TRUE(tls_x509_is_time_within_validity(&cert, &after) == 0);
}

UNIT_TEST_CASE(test_tls_x509_generalized_time_and_invalid_date)
{
    static const uint8_t generalized_der[] = {
        0x18, 0x0f,
          '2', '0', '5', '1', '0', '2', '2', '8', '2', '3', '5', '9', '5', '8', 'Z',
    };
    static const uint8_t invalid_der[] = {
        0x17, 0x0d,
          '2', '5', '0', '2', '2', '9', '0', '0', '0', '0', '0', '0', 'Z',
    };
    tls_x509_time_t time;

    ASSERT_EQ_INT(0, tls_x509_parse_time(&(tls_x509_slice_t){generalized_der, sizeof(generalized_der)},
                                         &time));
    ASSERT_EQ_INT(2051, time.year);
    ASSERT_EQ_INT(2, time.month);
    ASSERT_EQ_INT(28, time.day);
    ASSERT_EQ_INT(23, time.hour);
    ASSERT_EQ_INT(59, time.minute);
    ASSERT_EQ_INT(58, time.second);
    ASSERT_TRUE(tls_x509_parse_time(&(tls_x509_slice_t){invalid_der, sizeof(invalid_der)},
                                    &time) != 0);
}

UNIT_TEST_CASE(test_tls_x509_oid_algorithm_and_spki_parse)
{
    static const uint8_t sha256_rsa_algorithm[] = {
        0x30, 0x0d,
          0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b,
          0x05, 0x00,
    };
    static const uint8_t ec_spki[] = {
        0x30, 0x18,
          0x30, 0x13,
            0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
            0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07,
          0x03, 0x01, 0x00,
    };
    uint32_t arcs[8];
    size_t arc_count = 0u;
    tls_x509_algorithm_identifier_t algorithm;
    tls_x509_subject_public_key_info_t spki;
    tls_x509_certificate_view_t cert;

    ASSERT_EQ_INT(0, tls_x509_parse_oid(&(tls_x509_slice_t){&sha256_rsa_algorithm[2], 11u},
                                       arcs,
                                       8u,
                                       &arc_count));
    ASSERT_EQ_SIZE(7u, arc_count);
    ASSERT_EQ_INT(1, arcs[0]);
    ASSERT_EQ_INT(2, arcs[1]);
    ASSERT_EQ_INT(840, arcs[2]);
    ASSERT_EQ_INT(113549, arcs[3]);
    ASSERT_EQ_INT(1, arcs[4]);
    ASSERT_EQ_INT(1, arcs[5]);
    ASSERT_EQ_INT(11, arcs[6]);

    ASSERT_EQ_INT(0, tls_x509_parse_algorithm_identifier(&(tls_x509_slice_t){sha256_rsa_algorithm,
                                                                              sizeof(sha256_rsa_algorithm)},
                                                        &algorithm));
    ASSERT_EQ_INT(TLS_X509_OID_SHA256_WITH_RSA_ENCRYPTION, algorithm.known_oid);
    ASSERT_TRUE(algorithm.has_parameters);
    ASSERT_EQ_SIZE(2u, algorithm.parameters_der.len);

    tls_x509_certificate_view_init(&cert);
    cert.subject_public_key_info_der.data = ec_spki;
    cert.subject_public_key_info_der.len = sizeof(ec_spki);
    ASSERT_EQ_INT(0, tls_x509_parse_subject_public_key_info(&cert, &spki));
    ASSERT_EQ_INT(TLS_X509_OID_EC_PUBLIC_KEY, spki.algorithm.known_oid);
    ASSERT_TRUE(spki.algorithm.has_parameters);
    ASSERT_EQ_INT(TLS_X509_OID_SECP256R1,
                  tls_x509_known_oid_from_der(&spki.algorithm.parameters_der));
    ASSERT_EQ_SIZE(3u, spki.subject_public_key_der.len);
}

UNIT_TEST_CASE(test_tls_x509_rsa_public_key_and_signature_bits)
{
    static const uint8_t rsa_spki[] = {
        0x30, 0x1a,
          0x30, 0x0d,
            0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01,
            0x05, 0x00,
          0x03, 0x09, 0x00,
            0x30, 0x06,
              0x02, 0x01, 0x11,
              0x02, 0x01, 0x03,
    };
    static const uint8_t signature_value[] = {
        0x03, 0x04, 0x00, 0xaa, 0xbb, 0xcc,
    };
    tls_x509_certificate_view_t cert;
    tls_x509_subject_public_key_info_t spki;
    tls_x509_rsa_public_key_t rsa;
    tls_x509_bit_string_t signature;

    tls_x509_certificate_view_init(&cert);
    cert.subject_public_key_info_der.data = rsa_spki;
    cert.subject_public_key_info_der.len = sizeof(rsa_spki);
    cert.signature_value_der.data = signature_value;
    cert.signature_value_der.len = sizeof(signature_value);

    ASSERT_EQ_INT(0, tls_x509_parse_subject_public_key_info(&cert, &spki));
    ASSERT_EQ_INT(TLS_X509_OID_RSA_ENCRYPTION, spki.algorithm.known_oid);
    ASSERT_EQ_INT(0, tls_x509_parse_rsa_public_key_from_spki(&spki, &rsa));
    ASSERT_EQ_SIZE(3u, rsa.modulus_der.len);
    ASSERT_EQ_SIZE(3u, rsa.public_exponent_der.len);
    ASSERT_EQ_INT(0x11, rsa.modulus_der.data[2]);
    ASSERT_EQ_INT(0x03, rsa.public_exponent_der.data[2]);

    ASSERT_EQ_INT(0, tls_x509_parse_signature_value(&cert, &signature));
    ASSERT_EQ_INT(0, signature.unused_bits);
    ASSERT_EQ_SIZE(3u, signature.bytes.len);
    ASSERT_EQ_INT(0xaa, signature.bytes.data[0]);
}

UNIT_TEST_CASE(test_tls_x509_digest_info_sha256)
{
    static const uint8_t digest_info_prefix[] = {
        0x30, 0x31,
          0x30, 0x0d,
            0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
            0x05, 0x00,
          0x04, 0x20,
    };
    uint8_t digest_info[sizeof(digest_info_prefix) + 32u];
    uint8_t digest[32];
    tls_x509_digest_info_t parsed;

    for (size_t i = 0u; i < sizeof(digest_info_prefix); ++i) {
        digest_info[i] = digest_info_prefix[i];
    }
    for (size_t i = 0u; i < sizeof(digest); ++i) {
        digest[i] = (uint8_t)i;
        digest_info[sizeof(digest_info_prefix) + i] = (uint8_t)i;
    }

    ASSERT_EQ_INT(0, tls_x509_parse_digest_info(&(tls_x509_slice_t){digest_info, sizeof(digest_info)},
                                                &parsed));
    ASSERT_EQ_INT(TLS_X509_OID_SHA256, parsed.algorithm.known_oid);
    ASSERT_EQ_SIZE(32u, parsed.digest.len);
    ASSERT_TRUE(tls_x509_digest_info_matches_sha256(&parsed, digest) != 0);
    digest[31] ^= 0x01u;
    ASSERT_TRUE(tls_x509_digest_info_matches_sha256(&parsed, digest) == 0);
}

UNIT_TEST_CASE(test_tls_x509_rsa_pkcs1_v15_encoded_message_sha256)
{
    static const uint8_t digest_info_prefix[] = {
        0x30, 0x31,
          0x30, 0x0d,
            0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
            0x05, 0x00,
          0x04, 0x20,
    };
    uint8_t encoded_message[2u + 8u + 1u + sizeof(digest_info_prefix) + 32u];
    uint8_t digest[32];
    size_t offset = 0u;

    encoded_message[offset++] = 0x00u;
    encoded_message[offset++] = 0x01u;
    for (size_t i = 0u; i < 8u; ++i) {
        encoded_message[offset++] = 0xffu;
    }
    encoded_message[offset++] = 0x00u;
    for (size_t i = 0u; i < sizeof(digest_info_prefix); ++i) {
        encoded_message[offset++] = digest_info_prefix[i];
    }
    for (size_t i = 0u; i < sizeof(digest); ++i) {
        digest[i] = (uint8_t)(0xa0u + i);
        encoded_message[offset++] = digest[i];
    }

    ASSERT_EQ_SIZE(sizeof(encoded_message), offset);
    ASSERT_TRUE(tls_x509_rsa_pkcs1_v15_encoded_message_matches_sha256(
                    &(tls_x509_slice_t){encoded_message, sizeof(encoded_message)}, digest) != 0);
    encoded_message[2] = 0xfeu;
    ASSERT_TRUE(tls_x509_rsa_pkcs1_v15_encoded_message_matches_sha256(
                    &(tls_x509_slice_t){encoded_message, sizeof(encoded_message)}, digest) == 0);
}

UNIT_TEST_CASE(test_tls_x509_extensions_basic_constraints_and_key_usage)
{
    static const uint8_t cert_with_extensions[] = {
        0x30, 0x5d,
          0x30, 0x55,
            0xa0, 0x03,
              0x02, 0x01, 0x02,
            0x02, 0x01, 0x05,
            0x30, 0x00,
            0x30, 0x00,
            0x30, 0x1e,
              0x17, 0x0d,
                '2', '5', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z',
              0x17, 0x0d,
                '3', '0', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z',
            0x30, 0x00,
            0x30, 0x00,
            0xa3, 0x23,
              0x30, 0x21,
                0x30, 0x0f,
                  0x06, 0x03, 0x55, 0x1d, 0x13,
                  0x01, 0x01, 0xff,
                  0x04, 0x05,
                    0x30, 0x03, 0x01, 0x01, 0xff,
                0x30, 0x0e,
                  0x06, 0x03, 0x55, 0x1d, 0x0f,
                  0x01, 0x01, 0xff,
                  0x04, 0x04,
                    0x03, 0x02, 0x00, 0x20,
          0x30, 0x00,
          0x03, 0x02, 0x00, 0x00,
    };
    tls_x509_certificate_view_t cert;
    tls_x509_extension_t extension;
    tls_x509_basic_constraints_t constraints;
    tls_x509_key_usage_t key_usage;

    ASSERT_EQ_INT(0, tls_x509_parse_certificate(cert_with_extensions,
                                                sizeof(cert_with_extensions),
                                                &cert));
    ASSERT_TRUE(cert.has_extensions);
    ASSERT_EQ_SIZE(35u, cert.extensions_der.len);
    ASSERT_EQ_INT(0, tls_x509_find_extension(&cert,
                                             TLS_X509_OID_BASIC_CONSTRAINTS,
                                             &extension));
    ASSERT_TRUE(extension.critical);
    ASSERT_EQ_INT(TLS_X509_OID_BASIC_CONSTRAINTS, extension.known_oid);
    ASSERT_EQ_SIZE(5u, extension.value_der.len);
    ASSERT_EQ_INT(0, tls_x509_parse_basic_constraints(&cert, &constraints));
    ASSERT_TRUE(constraints.present);
    ASSERT_TRUE(constraints.ca);
    ASSERT_FALSE(constraints.has_path_len_constraint);
    ASSERT_EQ_INT(0, tls_x509_parse_key_usage(&cert, &key_usage));
    ASSERT_TRUE(key_usage.present);
    ASSERT_EQ_INT(0x20, key_usage.bits);
    ASSERT_TRUE(tls_x509_certificate_is_ca(&cert));
    ASSERT_TRUE(tls_x509_certificate_allows_key_cert_sign(&cert));
    ASSERT_TRUE(tls_x509_certificate_can_sign_certificates(&cert));
}

UNIT_TEST_CASE(test_tls_x509_subject_alt_name_hostname_match)
{
    static const uint8_t cert_with_san[] = {
        0x30, 0x5c,
          0x30, 0x54,
            0xa0, 0x03,
              0x02, 0x01, 0x02,
            0x02, 0x01, 0x06,
            0x30, 0x00,
            0x30, 0x00,
            0x30, 0x1e,
              0x17, 0x0d,
                '2', '5', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z',
              0x17, 0x0d,
                '3', '0', '0', '1', '0', '1', '0', '0', '0', '0', '0', '0', 'Z',
            0x30, 0x00,
            0x30, 0x00,
            0xa3, 0x22,
              0x30, 0x20,
                0x30, 0x1e,
                  0x06, 0x03, 0x55, 0x1d, 0x11,
                  0x04, 0x17,
                    0x30, 0x15,
                      0x82, 0x0b,
                        'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm',
                      0x82, 0x06,
                        '*', '.', 't', 'e', 's', 't',
          0x30, 0x00,
          0x03, 0x02, 0x00, 0x00,
    };
    tls_x509_certificate_view_t cert;
    tls_x509_slice_t wildcard;

    ASSERT_EQ_INT(0, tls_x509_parse_certificate(cert_with_san,
                                                sizeof(cert_with_san),
                                                &cert));
    ASSERT_TRUE(tls_x509_certificate_matches_hostname(&cert, "example.com"));
    ASSERT_TRUE(tls_x509_certificate_matches_hostname(&cert, "WWW.TEST"));
    ASSERT_FALSE(tls_x509_certificate_matches_hostname(&cert, "deep.www.test"));
    ASSERT_FALSE(tls_x509_certificate_matches_hostname(&cert, "example.org"));

    wildcard.data = (const uint8_t*)"*.example.com";
    wildcard.len = 13u;
    ASSERT_TRUE(tls_x509_match_hostname_pattern(&wildcard, "api.example.com"));
    ASSERT_FALSE(tls_x509_match_hostname_pattern(&wildcard, "deep.api.example.com"));
    ASSERT_FALSE(tls_x509_match_hostname_pattern(&wildcard, "example.com"));
}

UNIT_TEST_CASE(test_tls_x509_name_and_issuer_subject_match)
{
    static const uint8_t name_a[] = {0x30, 0x03, 0x31, 0x01, 0x00};
    static const uint8_t name_b[] = {0x30, 0x03, 0x31, 0x01, 0x01};
    tls_x509_certificate_view_t child;
    tls_x509_certificate_view_t issuer;

    tls_x509_certificate_view_init(&child);
    tls_x509_certificate_view_init(&issuer);
    child.issuer_der.data = name_a;
    child.issuer_der.len = sizeof(name_a);
    issuer.subject_der.data = name_a;
    issuer.subject_der.len = sizeof(name_a);

    ASSERT_TRUE(tls_x509_name_equal(&child.issuer_der, &issuer.subject_der) != 0);
    ASSERT_TRUE(tls_x509_issuer_matches_subject(&child, &issuer) != 0);

    issuer.subject_der.data = name_b;
    issuer.subject_der.len = sizeof(name_b);
    ASSERT_TRUE(tls_x509_name_equal(&child.issuer_der, &issuer.subject_der) == 0);
    ASSERT_TRUE(tls_x509_issuer_matches_subject(&child, &issuer) == 0);
}

int main(void)
{
    UNIT_TEST_RUN(test_tls_x509_parse_minimal_certificate);
    UNIT_TEST_RUN(test_tls_x509_rejects_malformed_length);
    UNIT_TEST_RUN(test_tls_x509_validity_time_checks);
    UNIT_TEST_RUN(test_tls_x509_generalized_time_and_invalid_date);
    UNIT_TEST_RUN(test_tls_x509_oid_algorithm_and_spki_parse);
    UNIT_TEST_RUN(test_tls_x509_rsa_public_key_and_signature_bits);
    UNIT_TEST_RUN(test_tls_x509_digest_info_sha256);
    UNIT_TEST_RUN(test_tls_x509_rsa_pkcs1_v15_encoded_message_sha256);
    UNIT_TEST_RUN(test_tls_x509_extensions_basic_constraints_and_key_usage);
    UNIT_TEST_RUN(test_tls_x509_subject_alt_name_hostname_match);
    UNIT_TEST_RUN(test_tls_x509_name_and_issuer_subject_match);
    return unit_test_finish();
}
