#include "tls_trust.h"
#include "tls_crypto.h"

#include <stddef.h>
#include <stdint.h>

static const tls_trust_anchor_t tls_trust_builtin_anchor_table[] = {
    {
        "OpenOS development TLS trust anchor v1",
        {
            0x81u, 0xedu, 0x1eu, 0xf8u, 0xe3u, 0xbbu, 0x30u, 0x34u,
            0x74u, 0x57u, 0xa2u, 0x69u, 0xebu, 0x49u, 0xb0u, 0xddu,
            0x93u, 0xdeu, 0x3au, 0x06u, 0xa9u, 0x0au, 0x55u, 0x6au,
            0x78u, 0x58u, 0x91u, 0xb4u, 0xc3u, 0xcbu, 0x9fu, 0xdbu,
        },
    },
    {
        "OpenOS unit-test parseable TLS trust anchor v1",
        {
            0x71u, 0x68u, 0xaeu, 0x50u, 0xa1u, 0x68u, 0x90u, 0xd7u,
            0x94u, 0x44u, 0xc8u, 0xe0u, 0x87u, 0xd2u, 0xc8u, 0x70u,
            0xc4u, 0x72u, 0xcbu, 0x97u, 0xf6u, 0x1fu, 0xd5u, 0xdeu,
            0x11u, 0x22u, 0x30u, 0xc8u, 0xd7u, 0x9bu, 0x29u, 0x67u,
        },
    },
};

int tls_trust_fingerprint_sha256(const uint8_t* cert_der,
                                 size_t cert_der_len,
                                 uint8_t out_sha256[TLS_TRUST_FINGERPRINT_SHA256_LEN])
{
    tls_sha256_ctx_t ctx;

    if (!cert_der || !out_sha256 || cert_der_len == 0u) {
        return -1;
    }

    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, cert_der, cert_der_len);
    tls_sha256_final(&ctx, out_sha256);
    return 0;
}

int tls_trust_fingerprint_equal(const uint8_t a[TLS_TRUST_FINGERPRINT_SHA256_LEN],
                                const uint8_t b[TLS_TRUST_FINGERPRINT_SHA256_LEN])
{
    uint8_t diff = 0u;

    if (!a || !b) {
        return 0;
    }

    for (size_t i = 0; i < TLS_TRUST_FINGERPRINT_SHA256_LEN; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }

    return diff == 0u;
}

const tls_trust_anchor_t* tls_trust_builtin_anchors(size_t* out_count)
{
    if (out_count) {
        *out_count = sizeof(tls_trust_builtin_anchor_table) /
                     sizeof(tls_trust_builtin_anchor_table[0]);
    }
    return tls_trust_builtin_anchor_table;
}

int tls_trust_is_fingerprint_trusted(const uint8_t sha256[TLS_TRUST_FINGERPRINT_SHA256_LEN])
{
    size_t count = 0u;
    const tls_trust_anchor_t* anchors = tls_trust_builtin_anchors(&count);

    if (!sha256) {
        return 0;
    }

    for (size_t i = 0; i < count; ++i) {
        if (tls_trust_fingerprint_equal(sha256, anchors[i].sha256)) {
            return 1;
        }
    }

    return 0;
}

int tls_trust_is_certificate_trusted(const uint8_t* cert_der, size_t cert_der_len)
{
    uint8_t fingerprint[TLS_TRUST_FINGERPRINT_SHA256_LEN];

    if (tls_trust_fingerprint_sha256(cert_der, cert_der_len, fingerprint) != 0) {
        return 0;
    }

    return tls_trust_is_fingerprint_trusted(fingerprint);
}

int tls_trust_is_certificate_chain_anchored(const uint8_t* const* cert_chain_der,
                                            const size_t* cert_chain_der_lens,
                                            size_t cert_chain_count)
{
    const uint8_t* root_der;
    size_t root_der_len;

    if (!cert_chain_der || !cert_chain_der_lens || cert_chain_count == 0u) {
        return 0;
    }

    root_der = cert_chain_der[cert_chain_count - 1u];
    root_der_len = cert_chain_der_lens[cert_chain_count - 1u];
    return tls_trust_is_certificate_trusted(root_der, root_der_len);
}

int tls_trust_is_parsed_certificate_chain_anchored(const tls_certificate_chain_view_t* chain)
{
    if (!chain || chain->certificate_count == 0u || chain->is_truncated) {
        return 0;
    }
    if (chain->stored_certificate_count != chain->certificate_count) {
        return 0;
    }
    return tls_trust_is_certificate_chain_anchored(chain->certificates,
                                                   chain->certificate_lengths,
                                                   chain->stored_certificate_count);
}

int tls_trust_is_tls_certificate_record_anchored(const uint8_t* tls_records,
                                                size_t tls_records_len)
{
    tls_certificate_chain_view_t chain;

    if (tls_parse_records_certificate_chain(tls_records, tls_records_len, &chain) != 0) {
        return 0;
    }

    return tls_trust_is_parsed_certificate_chain_anchored(&chain);
}

static int tls_trust_parse_and_check_certificate(const uint8_t* cert_der,
                                                 size_t cert_der_len,
                                                 const tls_x509_time_t* now,
                                                 tls_x509_certificate_view_t* out_cert)
{
    if (!cert_der || cert_der_len == 0u || !now || !out_cert) {
        return 0;
    }
    if (tls_x509_parse_certificate(cert_der, cert_der_len, out_cert) != 0 ||
        !tls_x509_is_time_within_validity(out_cert, now)) {
        return 0;
    }
    return 1;
}

static int tls_trust_check_certificate_algorithms(const tls_x509_certificate_view_t* cert)
{
    tls_x509_algorithm_identifier_t signature_algorithm;
    tls_x509_subject_public_key_info_t spki;

    if (tls_x509_parse_signature_algorithm(cert, &signature_algorithm) != 0 ||
        !tls_x509_is_supported_signature_algorithm(signature_algorithm.known_oid)) {
        return 0;
    }
    if (tls_x509_parse_subject_public_key_info(cert, &spki) != 0 ||
        !tls_x509_is_supported_subject_public_key_algorithm(spki.algorithm.known_oid)) {
        return 0;
    }
    return 1;
}

static int tls_trust_check_issuer_constraints(const tls_x509_certificate_view_t* issuer)
{
    if (!issuer) {
        return 0;
    }
    return tls_x509_certificate_can_sign_certificates(issuer);
}

int tls_trust_validate_certificate_chain(const uint8_t* const* cert_chain_der,
                                         const size_t* cert_chain_der_lens,
                                         size_t cert_chain_count,
                                         const tls_x509_time_t* now)
{
    tls_x509_certificate_view_t current;
    tls_x509_certificate_view_t issuer;

    if (!cert_chain_der || !cert_chain_der_lens || !now || cert_chain_count == 0u) {
        return 0;
    }

    for (size_t i = 0u; i < cert_chain_count; ++i) {
        if (!tls_trust_parse_and_check_certificate(cert_chain_der[i], cert_chain_der_lens[i], now, &current)) {
            return 0;
        }

        if (i + 1u < cert_chain_count) {
            if (!tls_trust_parse_and_check_certificate(cert_chain_der[i + 1u], cert_chain_der_lens[i + 1u], now, &issuer) ||
                !tls_x509_issuer_matches_subject(&current, &issuer)) {
                return 0;
            }
        }
    }

    return tls_trust_is_certificate_chain_anchored(cert_chain_der,
                                                   cert_chain_der_lens,
                                                   cert_chain_count);
}

int tls_trust_validate_certificate_chain_algorithms(const uint8_t* const* cert_chain_der,
                                                    const size_t* cert_chain_der_lens,
                                                    size_t cert_chain_count,
                                                    const tls_x509_time_t* now)
{
    tls_x509_certificate_view_t current;
    tls_x509_certificate_view_t issuer;

    if (!cert_chain_der || !cert_chain_der_lens || !now || cert_chain_count == 0u) {
        return 0;
    }

    for (size_t i = 0u; i < cert_chain_count; ++i) {
        if (!tls_trust_parse_and_check_certificate(cert_chain_der[i], cert_chain_der_lens[i], now, &current) ||
            !tls_trust_check_certificate_algorithms(&current)) {
            return 0;
        }

        if (i + 1u < cert_chain_count) {
            if (!tls_trust_parse_and_check_certificate(cert_chain_der[i + 1u], cert_chain_der_lens[i + 1u], now, &issuer) ||
                !tls_trust_check_issuer_constraints(&issuer) ||
                !tls_x509_issuer_matches_subject(&current, &issuer)) {
                return 0;
            }
        } else if (!tls_trust_check_issuer_constraints(&current)) {
            return 0;
        }
    }

    return tls_trust_is_certificate_chain_anchored(cert_chain_der,
                                                   cert_chain_der_lens,
                                                   cert_chain_count);
}

int tls_trust_validate_certificate_chain_signatures(const uint8_t* const* cert_chain_der,
                                                    const size_t* cert_chain_der_lens,
                                                    size_t cert_chain_count,
                                                    const tls_x509_time_t* now)
{
    tls_x509_certificate_view_t current;
    tls_x509_certificate_view_t issuer;

    if (!cert_chain_der || !cert_chain_der_lens || !now || cert_chain_count == 0u) {
        return 0;
    }

    for (size_t i = 0u; i < cert_chain_count; ++i) {
        if (!tls_trust_parse_and_check_certificate(cert_chain_der[i], cert_chain_der_lens[i], now, &current) ||
            !tls_trust_check_certificate_algorithms(&current)) {
            return 0;
        }

        if (i + 1u < cert_chain_count) {
            if (!tls_trust_parse_and_check_certificate(cert_chain_der[i + 1u], cert_chain_der_lens[i + 1u], now, &issuer) ||
                !tls_trust_check_issuer_constraints(&issuer) ||
                !tls_x509_issuer_matches_subject(&current, &issuer) ||
                !tls_x509_verify_certificate_signature_sha256_rsa(&current, &issuer)) {
                return 0;
            }
        } else if (!tls_trust_check_issuer_constraints(&current)) {
            return 0;
        }
    }

    return tls_trust_is_certificate_chain_anchored(cert_chain_der,
                                                   cert_chain_der_lens,
                                                   cert_chain_count);
}

int tls_trust_validate_certificate_chain_for_hostname(const uint8_t* const* cert_chain_der,
                                                      const size_t* cert_chain_der_lens,
                                                      size_t cert_chain_count,
                                                      const tls_x509_time_t* now,
                                                      const char* hostname)
{
    tls_x509_certificate_view_t leaf;

    if (!cert_chain_der || !cert_chain_der_lens || !now || !hostname || cert_chain_count == 0u) {
        return 0;
    }
    if (!tls_trust_parse_and_check_certificate(cert_chain_der[0], cert_chain_der_lens[0], now, &leaf) ||
        !tls_x509_certificate_matches_hostname(&leaf, hostname)) {
        return 0;
    }
    return tls_trust_validate_certificate_chain_signatures(cert_chain_der,
                                                           cert_chain_der_lens,
                                                           cert_chain_count,
                                                           now);
}

int tls_trust_validate_parsed_certificate_chain(const tls_certificate_chain_view_t* chain,
                                                const tls_x509_time_t* now)
{
    if (!chain || chain->certificate_count == 0u || chain->is_truncated ||
        chain->stored_certificate_count != chain->certificate_count) {
        return 0;
    }
    return tls_trust_validate_certificate_chain(chain->certificates,
                                                chain->certificate_lengths,
                                                chain->stored_certificate_count,
                                                now);
}

int tls_trust_validate_tls_certificate_record(const uint8_t* tls_records,
                                              size_t tls_records_len,
                                              const tls_x509_time_t* now)
{
    tls_certificate_chain_view_t chain;

    if (tls_parse_records_certificate_chain(tls_records, tls_records_len, &chain) != 0) {
        return 0;
    }
    return tls_trust_validate_parsed_certificate_chain(&chain, now);
}

int tls_trust_validate_tls_certificate_record_for_hostname(const uint8_t* tls_records,
                                                           size_t tls_records_len,
                                                           const tls_x509_time_t* now,
                                                           const char* hostname)
{
    tls_certificate_chain_view_t chain;

    if (tls_parse_records_certificate_chain(tls_records, tls_records_len, &chain) != 0 ||
        chain.certificate_count == 0u || chain.is_truncated ||
        chain.stored_certificate_count != chain.certificate_count) {
        return 0;
    }
    return tls_trust_validate_certificate_chain_for_hostname(chain.certificates,
                                                            chain.certificate_lengths,
                                                            chain.stored_certificate_count,
                                                            now,
                                                            hostname);
}

int tls_trust_validate_tls_certificate_record_signatures(const uint8_t* tls_records,
                                                         size_t tls_records_len,
                                                         const tls_x509_time_t* now)
{
    tls_certificate_chain_view_t chain;

    if (tls_parse_records_certificate_chain(tls_records, tls_records_len, &chain) != 0 ||
        chain.certificate_count == 0u || chain.is_truncated ||
        chain.stored_certificate_count != chain.certificate_count) {
        return 0;
    }
    return tls_trust_validate_certificate_chain_signatures(chain.certificates,
                                                          chain.certificate_lengths,
                                                          chain.stored_certificate_count,
                                                          now);
}
