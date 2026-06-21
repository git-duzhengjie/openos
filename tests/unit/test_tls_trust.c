#include "unit_test.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../src/kernel/include/tls_trust.h"
#include "../../src/kernel/tls_crypto.c"
#include "../../src/kernel/tls_parser.c"
#include "../../src/kernel/tls_x509.c"
#include "../../src/kernel/tls_trust.c"

UNIT_TEST_CASE(test_tls_trust_anchor_fingerprints)
{
    static const uint8_t dev_anchor_der[] = "OpenOS development TLS trust anchor v1";
    static const uint8_t untrusted_der[] = "OpenOS untrusted TLS peer certificate";
    static const uint8_t leaf_der[] = "OpenOS TLS leaf certificate";
    static const uint8_t intermediate_der[] = "OpenOS TLS intermediate certificate";
    static const uint8_t expected_fingerprint[TLS_TRUST_FINGERPRINT_SHA256_LEN] = {
        0x81u, 0xedu, 0x1eu, 0xf8u, 0xe3u, 0xbbu, 0x30u, 0x34u,
        0x74u, 0x57u, 0xa2u, 0x69u, 0xebu, 0x49u, 0xb0u, 0xddu,
        0x93u, 0xdeu, 0x3au, 0x06u, 0xa9u, 0x0au, 0x55u, 0x6au,
        0x78u, 0x58u, 0x91u, 0xb4u, 0xc3u, 0xcbu, 0x9fu, 0xdbu,
    };
    const uint8_t* trusted_chain[] = {leaf_der, intermediate_der, dev_anchor_der};
    const size_t trusted_chain_lens[] = {
        sizeof(leaf_der) - 1u,
        sizeof(intermediate_der) - 1u,
        sizeof(dev_anchor_der) - 1u,
    };
    const uint8_t* untrusted_chain[] = {leaf_der, intermediate_der, untrusted_der};
    const size_t untrusted_chain_lens[] = {
        sizeof(leaf_der) - 1u,
        sizeof(intermediate_der) - 1u,
        sizeof(untrusted_der) - 1u,
    };
    uint8_t fingerprint[TLS_TRUST_FINGERPRINT_SHA256_LEN];
    size_t anchor_count = 0u;
    const tls_trust_anchor_t* anchors = tls_trust_builtin_anchors(&anchor_count);

    ASSERT_TRUE(anchors != NULL);
    ASSERT_TRUE(anchor_count >= 1u);
    ASSERT_STREQ("OpenOS development TLS trust anchor v1", anchors[0].name);
    ASSERT_TRUE(tls_trust_fingerprint_equal(anchors[0].sha256, expected_fingerprint));

    ASSERT_EQ_INT(0, tls_trust_fingerprint_sha256(dev_anchor_der,
                                                  sizeof(dev_anchor_der) - 1u,
                                                  fingerprint));
    ASSERT_TRUE(tls_trust_fingerprint_equal(fingerprint, expected_fingerprint));
    ASSERT_TRUE(tls_trust_is_fingerprint_trusted(fingerprint));
    ASSERT_TRUE(tls_trust_is_certificate_trusted(dev_anchor_der,
                                                sizeof(dev_anchor_der) - 1u));

    ASSERT_EQ_INT(0, tls_trust_fingerprint_sha256(untrusted_der,
                                                  sizeof(untrusted_der) - 1u,
                                                  fingerprint));
    ASSERT_FALSE(tls_trust_fingerprint_equal(fingerprint, expected_fingerprint));
    ASSERT_FALSE(tls_trust_is_fingerprint_trusted(fingerprint));
    ASSERT_FALSE(tls_trust_is_certificate_trusted(untrusted_der,
                                                 sizeof(untrusted_der) - 1u));

    ASSERT_TRUE(tls_trust_fingerprint_sha256(NULL, sizeof(dev_anchor_der) - 1u, fingerprint) != 0);
    ASSERT_TRUE(tls_trust_fingerprint_sha256(dev_anchor_der, 0u, fingerprint) != 0);
    ASSERT_TRUE(tls_trust_fingerprint_sha256(dev_anchor_der, sizeof(dev_anchor_der) - 1u, NULL) != 0);
    ASSERT_FALSE(tls_trust_fingerprint_equal(NULL, expected_fingerprint));
    ASSERT_FALSE(tls_trust_is_fingerprint_trusted(NULL));
    ASSERT_FALSE(tls_trust_is_certificate_trusted(NULL, sizeof(dev_anchor_der) - 1u));

    ASSERT_TRUE(tls_trust_is_certificate_chain_anchored(trusted_chain,
                                                       trusted_chain_lens,
                                                       3u));
    ASSERT_FALSE(tls_trust_is_certificate_chain_anchored(untrusted_chain,
                                                        untrusted_chain_lens,
                                                        3u));
    ASSERT_FALSE(tls_trust_is_certificate_chain_anchored(NULL, trusted_chain_lens, 3u));
    ASSERT_FALSE(tls_trust_is_certificate_chain_anchored(trusted_chain, NULL, 3u));
    ASSERT_FALSE(tls_trust_is_certificate_chain_anchored(trusted_chain,
                                                        trusted_chain_lens,
                                                        0u));
}

static const uint8_t k_parseable_anchor_der[] = {
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

UNIT_TEST_CASE(test_tls_trust_validates_parseable_chain)
{
    const uint8_t* chain[] = {k_parseable_anchor_der};
    const size_t lens[] = {sizeof(k_parseable_anchor_der)};
    const tls_x509_time_t valid_now = {2026u, 6u, 21u, 15u, 0u, 0u};
    const tls_x509_time_t expired_now = {2031u, 1u, 1u, 0u, 0u, 0u};
    const uint8_t* empty_chain[] = {NULL};
    const size_t empty_lens[] = {0u};

    ASSERT_TRUE(tls_trust_is_certificate_trusted(k_parseable_anchor_der,
                                                 sizeof(k_parseable_anchor_der)));
    ASSERT_TRUE(tls_trust_validate_certificate_chain(chain, lens, 1u, &valid_now));
    ASSERT_FALSE(tls_trust_validate_certificate_chain(chain, lens, 1u, &expired_now));
    ASSERT_FALSE(tls_trust_validate_certificate_chain(empty_chain, empty_lens, 1u, &valid_now));
    ASSERT_FALSE(tls_trust_validate_certificate_chain(chain, lens, 1u, NULL));
}

UNIT_TEST_CASE(test_tls_trust_tls_certificate_record_anchor)
{
    const uint8_t trusted_packet[] = {
        0x16, 0x03, 0x03, 0x00, 0x3d,
        0x0b, 0x00, 0x00, 0x39,
        0x00, 0x00, 0x36,
        0x00, 0x00, 0x03,
        'l', 'e', 'f',
        0x00, 0x00, 0x04,
        'm', 'i', 'd', '1',
        0x00, 0x00, 0x26,
        'O', 'p', 'e', 'n', 'O', 'S', ' ', 'd',
        'e', 'v', 'e', 'l', 'o', 'p', 'm', 'e',
        'n', 't', ' ', 'T', 'L', 'S', ' ', 't',
        'r', 'u', 's', 't', ' ', 'a', 'n', 'c',
        'h', 'o', 'r', ' ', 'v', '1',
    };
    const uint8_t untrusted_packet[] = {
        0x16, 0x03, 0x03, 0x00, 0x3d,
        0x0b, 0x00, 0x00, 0x39,
        0x00, 0x00, 0x36,
        0x00, 0x00, 0x03,
        'l', 'e', 'f',
        0x00, 0x00, 0x04,
        'm', 'i', 'd', '1',
        0x00, 0x00, 0x26,
        'O', 'p', 'e', 'n', 'O', 'S', ' ', 'u',
        'n', 't', 'r', 'u', 's', 't', 'e', 'd',
        ' ', 'T', 'L', 'S', ' ', 'p', 'e', 'e',
        'r', ' ', 'c', 'e', 'r', 't', 'i', 'f',
        'i', 'c', 'a', 't', 'e', '\0',
    };
    tls_certificate_chain_view_t chain;

    ASSERT_EQ_INT(0, tls_parse_records_certificate_chain(trusted_packet,
                                                         sizeof(trusted_packet),
                                                         &chain));
    ASSERT_EQ_INT(3, chain.certificate_count);
    ASSERT_TRUE(tls_trust_is_parsed_certificate_chain_anchored(&chain));
    ASSERT_TRUE(tls_trust_is_tls_certificate_record_anchored(trusted_packet,
                                                            sizeof(trusted_packet)));
    ASSERT_FALSE(tls_trust_is_tls_certificate_record_anchored(untrusted_packet,
                                                             sizeof(untrusted_packet)));
    ASSERT_FALSE(tls_trust_is_tls_certificate_record_anchored(NULL,
                                                             sizeof(trusted_packet)));
}

int main(void)
{
    UNIT_TEST_RUN(test_tls_trust_anchor_fingerprints);
    UNIT_TEST_RUN(test_tls_trust_validates_parseable_chain);
    UNIT_TEST_RUN(test_tls_trust_tls_certificate_record_anchor);
    return unit_test_finish();
}
