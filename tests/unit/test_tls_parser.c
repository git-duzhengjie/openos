#include "unit_test.h"

#include <stddef.h>
#include <stdint.h>

#include "tls_parser.h"
#include "../../src/kernel/tls_parser.c"

static void test_tls_record_view_and_header_writer(void)
{
    const uint8_t packet[] = {
        0x17, 0x03, 0x03, 0x00, 0x04,
        't', 'e', 's', 't',
        0xff, 0xff,
    };
    uint8_t header[TLS_RECORD_HEADER_SIZE];
    tls_record_view_t record;

    ASSERT_EQ_INT(0, tls_parse_record_view(packet, sizeof(packet), &record));
    ASSERT_EQ_INT(TLS_CONTENT_TYPE_APPLICATION_DATA, record.content_type);
    ASSERT_EQ_INT(0x0303, record.protocol_version);
    ASSERT_EQ_INT(4, record.payload_len);
    ASSERT_EQ_SIZE(9u, record.total_len);
    ASSERT_EQ_INT('t', record.payload[0]);
    ASSERT_EQ_INT('t', record.payload[3]);

    ASSERT_EQ_INT(0, tls_write_record_header(TLS_CONTENT_TYPE_HANDSHAKE, 0x0303, 0x1234, header));
    ASSERT_EQ_INT(0x16, header[0]);
    ASSERT_EQ_INT(0x03, header[1]);
    ASSERT_EQ_INT(0x03, header[2]);
    ASSERT_EQ_INT(0x12, header[3]);
    ASSERT_EQ_INT(0x34, header[4]);

    ASSERT_EQ_INT(-1, tls_parse_record_view(packet, 8u, &record));
}

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

static void test_tls_parser_certificate_chain_view(void)
{
    const uint8_t packet[] = {
        0x16, 0x03, 0x03, 0x00, 0x37,
        0x0b, 0x00, 0x00, 0x33,
        0x00, 0x00, 0x30,
        0x00, 0x00, 0x03,
        0x11, 0x22, 0x33,
        0x00, 0x00, 0x04,
        0x44, 0x55, 0x66, 0x77,
        0x00, 0x00, 0x20,
        0x81, 0xed, 0x1e, 0xf8, 0xe3, 0xbb, 0x30, 0x34,
        0x74, 0x57, 0xa2, 0x69, 0xeb, 0x49, 0xb0, 0xdd,
        0x93, 0xde, 0x3a, 0x06, 0xa9, 0x0a, 0x55, 0x6a,
        0x78, 0x58, 0x91, 0xb4, 0xc3, 0xcb, 0x9f, 0xdb,
    };
    tls_certificate_chain_view_t chain;

    ASSERT_EQ_INT(0, tls_parse_records_certificate_chain(packet, sizeof(packet), &chain));
    ASSERT_EQ_INT(3, chain.certificate_count);
    ASSERT_EQ_INT(3, chain.stored_certificate_count);
    ASSERT_EQ_INT(48, chain.certificate_bytes);
    ASSERT_FALSE(chain.is_truncated);
    ASSERT_EQ_SIZE(3u, chain.certificate_lengths[0]);
    ASSERT_EQ_SIZE(4u, chain.certificate_lengths[1]);
    ASSERT_EQ_SIZE(32u, chain.certificate_lengths[2]);
    ASSERT_EQ_INT(0x11, chain.certificates[0][0]);
    ASSERT_EQ_INT(0x77, chain.certificates[1][3]);
    ASSERT_EQ_INT(0x81, chain.certificates[2][0]);
    ASSERT_EQ_INT(0xdb, chain.certificates[2][31]);
}

static void test_tls_parser_handshake_transcript(void)
{
    const uint8_t packet[] = {
        0x16, 0x03, 0x03, 0x00, 0x16,
          0x02, 0x00, 0x00, 0x02, 0x03, 0x03,
          0x0b, 0x00, 0x00, 0x03, 0xaa, 0xbb, 0xcc,
          0x14, 0x00, 0x00, 0x05, 1, 2, 3, 4, 5,
        0x15, 0x03, 0x03, 0x00, 0x02, 0x01, 0x00,
    };
    tls_handshake_transcript_view_t transcript;
    uint8_t copied[32];
    size_t written = 0u;

    ASSERT_EQ_INT(0, tls_parse_records_handshake_transcript(packet,
                                                            sizeof(packet),
                                                            0,
                                                            &transcript));
    ASSERT_EQ_INT(2, transcript.message_count);
    ASSERT_EQ_INT(13, transcript.transcript_bytes);
    ASSERT_FALSE(transcript.is_truncated);
    ASSERT_EQ_INT(TLS_HANDSHAKE_SERVER_HELLO, transcript.message_types[0]);
    ASSERT_EQ_INT(TLS_HANDSHAKE_CERTIFICATE, transcript.message_types[1]);
    ASSERT_EQ_SIZE(6u, transcript.message_lengths[0]);
    ASSERT_EQ_SIZE(7u, transcript.message_lengths[1]);
    ASSERT_EQ_INT(0, tls_handshake_transcript_copy(&transcript,
                                                   copied,
                                                   sizeof(copied),
                                                   &written));
    ASSERT_EQ_SIZE(13u, written);
    ASSERT_EQ_INT(0x02, copied[0]);
    ASSERT_EQ_INT(0x03, copied[4]);
    ASSERT_EQ_INT(0x0b, copied[6]);
    ASSERT_EQ_INT(0xcc, copied[12]);

    ASSERT_EQ_INT(0, tls_parse_records_handshake_transcript(packet,
                                                            sizeof(packet),
                                                            1,
                                                            &transcript));
    ASSERT_EQ_INT(3, transcript.message_count);
    ASSERT_EQ_INT(22, transcript.transcript_bytes);
    ASSERT_EQ_INT(TLS_HANDSHAKE_FINISHED, transcript.message_types[2]);
}

static void test_tls_parser_finished_verify_data(void)
{
    const uint8_t packet[] = {
        0x16, 0x03, 0x03, 0x00, 0x10,
          0x14, 0x00, 0x00, 0x0c,
            0x2c, 0xb8, 0x45, 0x45, 0xd7, 0xa4,
            0x53, 0x47, 0xb6, 0x30, 0xda, 0xc0,
    };
    const uint8_t* verify_data = NULL;
    size_t verify_data_len = 0u;

    ASSERT_EQ_INT(0, tls_parse_records_finished_verify_data(packet,
                                                            sizeof(packet),
                                                            &verify_data,
                                                            &verify_data_len));
    ASSERT_EQ_SIZE(12u, verify_data_len);
    ASSERT_EQ_INT(0x2c, verify_data[0]);
    ASSERT_EQ_INT(0xc0, verify_data[11]);
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
    UNIT_TEST_RUN(test_tls_record_view_and_header_writer);
    UNIT_TEST_RUN(test_tls_parser_server_hello_and_certificate);
    UNIT_TEST_RUN(test_tls_parser_certificate_chain_view);
    UNIT_TEST_RUN(test_tls_parser_handshake_transcript);
    UNIT_TEST_RUN(test_tls_parser_finished_verify_data);
    UNIT_TEST_RUN(test_tls_parser_alert);
    return unit_test_finish();
}
