#include "unit_test.h"

#include <stdint.h>

#include "tls_parser.h"
#include "../../src/kernel/tls_parser.c"

static void test_tls_parser_server_hello_and_certificate(void)
{
    const uint8_t packet[] = {
        0x16, 0x03, 0x03, 0x00, 0x6e,
        0x02, 0x00, 0x00, 0x2e,
        0x03, 0x03,
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
        0x00,
        0x00, 0x9c,
        0x00,
        0x00, 0x06,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
        0x0b, 0x00, 0x00, 0x25,
        0x00, 0x00, 0x22,
        0x00, 0x00, 0x04,
        0xde, 0xad, 0xbe, 0xef,
        0x00, 0x00, 0x18,
        0x30, 0x82, 0x00, 0x14, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
        0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
        0x0c, 0x00, 0x00, 0x0f,
        0x03,
        0x00, 0x17,
        0x04,
        0x11, 0x22, 0x33, 0x44,
        0x04, 0x01,
        0x00, 0x03,
        0xaa, 0xbb, 0xcc,
    };
    tls_parser_summary_t summary;

    ASSERT_EQ_INT(1, tls_parse_records(packet, sizeof(packet), &summary));
    ASSERT_EQ_INT(TLS_CONTENT_TYPE_HANDSHAKE, summary.record_type);
    ASSERT_EQ_INT(0x0303, summary.record_version);
    ASSERT_EQ_INT(0x006e, summary.record_length);
    ASSERT_EQ_INT(3, summary.handshake_count);
    ASSERT_EQ_INT(TLS_HANDSHAKE_SERVER_HELLO, summary.handshake_types[0]);
    ASSERT_EQ_INT(TLS_HANDSHAKE_CERTIFICATE, summary.handshake_types[1]);
    ASSERT_EQ_INT(TLS_HANDSHAKE_SERVER_KEY_EXCHANGE, summary.handshake_types[2]);
    ASSERT_EQ_INT(0x0303, summary.server_version);
    ASSERT_EQ_INT(0x009c, summary.cipher_suite);
    ASSERT_EQ_INT(0, summary.compression_method);
    ASSERT_EQ_INT(6, summary.extensions_length);
    ASSERT_EQ_INT(2, summary.certificate_count);
    ASSERT_EQ_INT(34, summary.certificate_bytes);
    ASSERT_EQ_INT(3, summary.key_exchange_curve_type);
    ASSERT_EQ_INT(0x0017, summary.key_exchange_named_curve);
    ASSERT_EQ_INT(4, summary.key_exchange_public_key_length);
    ASSERT_EQ_INT(0x0401, summary.key_exchange_signature_algorithm);
    ASSERT_EQ_INT(3, summary.key_exchange_signature_length);
}

static void test_tls_parser_alert(void)
{
    const uint8_t packet[] = {0x15, 0x03, 0x03, 0x00, 0x02, 0x02, 0x28};
    tls_parser_summary_t summary;

    ASSERT_EQ_INT(1, tls_parse_records(packet, sizeof(packet), &summary));
    ASSERT_EQ_INT(TLS_CONTENT_TYPE_ALERT, summary.record_type);
    ASSERT_EQ_INT(2, summary.alert_level);
    ASSERT_EQ_INT(40, summary.alert_description);
}

int main(void)
{
    UNIT_TEST_RUN(test_tls_parser_server_hello_and_certificate);
    UNIT_TEST_RUN(test_tls_parser_alert);
    return unit_test_finish();
}
