#ifndef OPENOS_TLS_X509_H
#define OPENOS_TLS_X509_H

#include <stddef.h>
#include <stdint.h>

#include "tls_crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tls_x509_slice {
    const uint8_t* data;
    size_t len;
} tls_x509_slice_t;

typedef struct tls_x509_certificate_view {
    tls_x509_slice_t certificate_der;
    tls_x509_slice_t tbs_certificate_der;
    tls_x509_slice_t signature_algorithm_der;
    tls_x509_slice_t signature_value_der;
    tls_x509_slice_t serial_number_der;
    tls_x509_slice_t issuer_der;
    tls_x509_slice_t validity_der;
    tls_x509_slice_t subject_der;
    tls_x509_slice_t subject_public_key_info_der;
    tls_x509_slice_t extensions_der;
    uint8_t version;
    int has_explicit_version;
    int has_extensions;
} tls_x509_certificate_view_t;

typedef struct tls_x509_time {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} tls_x509_time_t;

typedef struct tls_x509_validity {
    tls_x509_time_t not_before;
    tls_x509_time_t not_after;
} tls_x509_validity_t;

typedef enum tls_x509_known_oid {
    TLS_X509_OID_UNKNOWN = 0,
    TLS_X509_OID_RSA_ENCRYPTION,
    TLS_X509_OID_SHA256_WITH_RSA_ENCRYPTION,
    TLS_X509_OID_SHA256,
    TLS_X509_OID_EC_PUBLIC_KEY,
    TLS_X509_OID_ECDSA_WITH_SHA256,
    TLS_X509_OID_SECP256R1,
    TLS_X509_OID_BASIC_CONSTRAINTS,
    TLS_X509_OID_KEY_USAGE,
    TLS_X509_OID_SUBJECT_ALT_NAME,
} tls_x509_known_oid_t;

typedef struct tls_x509_algorithm_identifier {
    tls_x509_slice_t algorithm_oid_der;
    tls_x509_slice_t parameters_der;
    tls_x509_known_oid_t known_oid;
    int has_parameters;
} tls_x509_algorithm_identifier_t;

typedef struct tls_x509_subject_public_key_info {
    tls_x509_algorithm_identifier_t algorithm;
    tls_x509_slice_t subject_public_key_der;
} tls_x509_subject_public_key_info_t;

typedef struct tls_x509_bit_string {
    uint8_t unused_bits;
    tls_x509_slice_t bytes;
} tls_x509_bit_string_t;

typedef struct tls_x509_rsa_public_key {
    tls_x509_slice_t modulus_der;
    tls_x509_slice_t public_exponent_der;
} tls_x509_rsa_public_key_t;

typedef struct tls_x509_digest_info {
    tls_x509_algorithm_identifier_t algorithm;
    tls_x509_slice_t digest;
} tls_x509_digest_info_t;

typedef struct tls_x509_extension {
    tls_x509_slice_t oid_der;
    tls_x509_slice_t value_der;
    tls_x509_known_oid_t known_oid;
    int critical;
} tls_x509_extension_t;

typedef struct tls_x509_basic_constraints {
    int present;
    int ca;
    int has_path_len_constraint;
    uint32_t path_len_constraint;
} tls_x509_basic_constraints_t;

typedef struct tls_x509_key_usage {
    int present;
    uint16_t bits;
} tls_x509_key_usage_t;

void tls_x509_certificate_view_init(tls_x509_certificate_view_t* view);
int tls_x509_parse_certificate(const uint8_t* cert_der,
                               size_t cert_der_len,
                               tls_x509_certificate_view_t* out_view);

int tls_x509_parse_oid(const tls_x509_slice_t* oid_der,
                       uint32_t* out_arcs,
                       size_t max_arcs,
                       size_t* out_arc_count);
tls_x509_known_oid_t tls_x509_known_oid_from_der(const tls_x509_slice_t* oid_der);
int tls_x509_parse_algorithm_identifier(const tls_x509_slice_t* algorithm_der,
                                        tls_x509_algorithm_identifier_t* out_algorithm);
int tls_x509_parse_subject_public_key_info(const tls_x509_certificate_view_t* cert,
                                           tls_x509_subject_public_key_info_t* out_spki);
int tls_x509_parse_bit_string(const tls_x509_slice_t* bit_string_der,
                              tls_x509_bit_string_t* out_bit_string);
int tls_x509_parse_rsa_public_key_from_spki(const tls_x509_subject_public_key_info_t* spki,
                                            tls_x509_rsa_public_key_t* out_rsa);
int tls_x509_parse_signature_value(const tls_x509_certificate_view_t* cert,
                                   tls_x509_bit_string_t* out_signature);
int tls_x509_parse_digest_info(const tls_x509_slice_t* digest_info_der,
                               tls_x509_digest_info_t* out_digest_info);
int tls_x509_digest_info_matches_sha256(const tls_x509_digest_info_t* digest_info,
                                        const uint8_t digest[32]);
int tls_x509_rsa_pkcs1_v15_encoded_message_matches_sha256(const tls_x509_slice_t* encoded_message,
                                                          const uint8_t digest[32]);
int tls_x509_rsa_verify_pkcs1_v15_sha256(const tls_x509_rsa_public_key_t* public_key,
                                         const tls_x509_bit_string_t* signature,
                                         const uint8_t digest[32]);
int tls_x509_rsa_encrypt_pkcs1_v15(const tls_x509_rsa_public_key_t* public_key,
                                   const uint8_t* message,
                                   size_t message_len,
                                   const uint8_t* nonzero_padding,
                                   size_t nonzero_padding_len,
                                   uint8_t* out_encrypted,
                                   size_t out_encrypted_cap,
                                   size_t* out_encrypted_len);
int tls_x509_parse_signature_algorithm(const tls_x509_certificate_view_t* cert,
                                       tls_x509_algorithm_identifier_t* out_algorithm);
int tls_x509_verify_certificate_signature_sha256_rsa(const tls_x509_certificate_view_t* child,
                                                     const tls_x509_certificate_view_t* issuer);
int tls_x509_find_extension(const tls_x509_certificate_view_t* cert,
                            tls_x509_known_oid_t oid,
                            tls_x509_extension_t* out_extension);
int tls_x509_parse_basic_constraints(const tls_x509_certificate_view_t* cert,
                                     tls_x509_basic_constraints_t* out_constraints);
int tls_x509_parse_key_usage(const tls_x509_certificate_view_t* cert,
                             tls_x509_key_usage_t* out_key_usage);
int tls_x509_certificate_is_ca(const tls_x509_certificate_view_t* cert);
int tls_x509_certificate_allows_key_cert_sign(const tls_x509_certificate_view_t* cert);
int tls_x509_certificate_can_sign_certificates(const tls_x509_certificate_view_t* cert);
int tls_x509_match_hostname_pattern(const tls_x509_slice_t* pattern, const char* hostname);
int tls_x509_certificate_matches_hostname(const tls_x509_certificate_view_t* cert,
                                          const char* hostname);
int tls_x509_is_supported_signature_algorithm(tls_x509_known_oid_t oid);
int tls_x509_is_supported_subject_public_key_algorithm(tls_x509_known_oid_t oid);
int tls_x509_parse_time(const tls_x509_slice_t* time_der,
                        tls_x509_time_t* out_time);
int tls_x509_parse_validity(const tls_x509_certificate_view_t* cert,
                            tls_x509_validity_t* out_validity);
int tls_x509_compare_time(const tls_x509_time_t* a, const tls_x509_time_t* b);
int tls_x509_is_time_within_validity(const tls_x509_certificate_view_t* cert,
                                     const tls_x509_time_t* now);
int tls_x509_name_equal(const tls_x509_slice_t* a, const tls_x509_slice_t* b);
int tls_x509_issuer_matches_subject(const tls_x509_certificate_view_t* child,
                                    const tls_x509_certificate_view_t* issuer);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_TLS_X509_H */
