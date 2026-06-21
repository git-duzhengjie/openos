#ifndef OPENOS_TLS_TRUST_H
#define OPENOS_TLS_TRUST_H

#include <stddef.h>
#include <stdint.h>

#include "tls_parser.h"
#include "tls_x509.h"

#define TLS_TRUST_FINGERPRINT_SHA256_LEN 32u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tls_trust_anchor {
    const char* name;
    const uint8_t sha256[TLS_TRUST_FINGERPRINT_SHA256_LEN];
} tls_trust_anchor_t;

int tls_trust_fingerprint_sha256(const uint8_t* cert_der,
                                 size_t cert_der_len,
                                 uint8_t out_sha256[TLS_TRUST_FINGERPRINT_SHA256_LEN]);

int tls_trust_fingerprint_equal(const uint8_t a[TLS_TRUST_FINGERPRINT_SHA256_LEN],
                                const uint8_t b[TLS_TRUST_FINGERPRINT_SHA256_LEN]);

const tls_trust_anchor_t* tls_trust_builtin_anchors(size_t* out_count);

int tls_trust_is_fingerprint_trusted(const uint8_t sha256[TLS_TRUST_FINGERPRINT_SHA256_LEN]);

int tls_trust_is_certificate_trusted(const uint8_t* cert_der, size_t cert_der_len);

int tls_trust_is_certificate_chain_anchored(const uint8_t* const* cert_chain_der,
                                            const size_t* cert_chain_der_lens,
                                            size_t cert_chain_count);

int tls_trust_is_parsed_certificate_chain_anchored(const tls_certificate_chain_view_t* chain);

int tls_trust_is_tls_certificate_record_anchored(const uint8_t* tls_records,
                                                size_t tls_records_len);

int tls_trust_validate_certificate_chain(const uint8_t* const* cert_chain_der,
                                         const size_t* cert_chain_der_lens,
                                         size_t cert_chain_count,
                                         const tls_x509_time_t* now);

int tls_trust_validate_certificate_chain_algorithms(const uint8_t* const* cert_chain_der,
                                                    const size_t* cert_chain_der_lens,
                                                    size_t cert_chain_count,
                                                    const tls_x509_time_t* now);

int tls_trust_validate_certificate_chain_signatures(const uint8_t* const* cert_chain_der,
                                                    const size_t* cert_chain_der_lens,
                                                    size_t cert_chain_count,
                                                    const tls_x509_time_t* now);

int tls_trust_validate_certificate_chain_for_hostname(const uint8_t* const* cert_chain_der,
                                                      const size_t* cert_chain_der_lens,
                                                      size_t cert_chain_count,
                                                      const tls_x509_time_t* now,
                                                      const char* hostname);

int tls_trust_validate_parsed_certificate_chain(const tls_certificate_chain_view_t* chain,
                                                const tls_x509_time_t* now);

int tls_trust_validate_tls_certificate_record(const uint8_t* tls_records,
                                              size_t tls_records_len,
                                              const tls_x509_time_t* now);

int tls_trust_validate_tls_certificate_record_signatures(const uint8_t* tls_records,
                                                         size_t tls_records_len,
                                                         const tls_x509_time_t* now);

int tls_trust_validate_tls_certificate_record_for_hostname(const uint8_t* tls_records,
                                                           size_t tls_records_len,
                                                           const tls_x509_time_t* now,
                                                           const char* hostname);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_TLS_TRUST_H */
