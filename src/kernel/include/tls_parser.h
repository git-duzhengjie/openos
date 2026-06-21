#ifndef OPENOS_TLS_PARSER_H
#define OPENOS_TLS_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define TLS_PARSER_MAX_HANDSHAKES 8
#define TLS_PARSER_MAX_CERTIFICATES 8
#define TLS_RECORD_HEADER_SIZE 5u

#define TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC 20u
#define TLS_CONTENT_TYPE_ALERT 21u
#define TLS_CONTENT_TYPE_HANDSHAKE 22u
#define TLS_CONTENT_TYPE_APPLICATION_DATA 23u

#define TLS_HANDSHAKE_HELLO_REQUEST 0u
#define TLS_HANDSHAKE_CLIENT_HELLO 1u
#define TLS_HANDSHAKE_SERVER_HELLO 2u
#define TLS_HANDSHAKE_CERTIFICATE 11u
#define TLS_HANDSHAKE_SERVER_KEY_EXCHANGE 12u
#define TLS_HANDSHAKE_CERTIFICATE_REQUEST 13u
#define TLS_HANDSHAKE_SERVER_HELLO_DONE 14u
#define TLS_HANDSHAKE_CERTIFICATE_VERIFY 15u
#define TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE 16u
#define TLS_HANDSHAKE_FINISHED 20u

typedef struct tls_record_view {
    uint8_t content_type;
    uint16_t protocol_version;
    uint16_t payload_len;
    const uint8_t* payload;
    size_t total_len;
} tls_record_view_t;

typedef struct tls_certificate_chain_view {
    uint16_t certificate_count;
    uint16_t stored_certificate_count;
    uint32_t certificate_bytes;
    const uint8_t* certificates[TLS_PARSER_MAX_CERTIFICATES];
    size_t certificate_lengths[TLS_PARSER_MAX_CERTIFICATES];
    int is_truncated;
} tls_certificate_chain_view_t;

typedef struct tls_handshake_transcript_view {
    const uint8_t* messages[TLS_PARSER_MAX_HANDSHAKES];
    size_t message_lengths[TLS_PARSER_MAX_HANDSHAKES];
    uint8_t message_types[TLS_PARSER_MAX_HANDSHAKES];
    uint8_t message_count;
    uint32_t transcript_bytes;
    int is_truncated;
} tls_handshake_transcript_view_t;

typedef struct tls_parser_summary {
    uint8_t record_type;
    uint16_t record_version;
    uint16_t record_length;
    uint8_t handshake_count;
    uint8_t handshake_types[TLS_PARSER_MAX_HANDSHAKES];
    uint16_t server_version;
    uint16_t cipher_suite;
    uint8_t compression_method;
    uint16_t extensions_length;
    uint16_t certificate_count;
    uint32_t certificate_bytes;
    uint8_t key_exchange_curve_type;
    uint16_t key_exchange_named_curve;
    uint16_t key_exchange_public_key_length;
    uint16_t key_exchange_signature_algorithm;
    uint16_t key_exchange_signature_length;
    uint8_t alert_level;
    uint8_t alert_description;
} tls_parser_summary_t;

void tls_parser_summary_init(tls_parser_summary_t* summary);
void tls_record_view_init(tls_record_view_t* record);
int tls_parse_record_view(const uint8_t* data, size_t len, tls_record_view_t* out_record);
int tls_write_record_header(uint8_t content_type,
                            uint16_t protocol_version,
                            uint16_t payload_len,
                            uint8_t out_header[TLS_RECORD_HEADER_SIZE]);
void tls_certificate_chain_view_init(tls_certificate_chain_view_t* chain);
void tls_handshake_transcript_view_init(tls_handshake_transcript_view_t* transcript);
int tls_parse_certificate_chain(const uint8_t* certificate_body,
                                size_t certificate_body_len,
                                tls_certificate_chain_view_t* out_chain);
int tls_parse_records_certificate_chain(const uint8_t* data,
                                        size_t len,
                                        tls_certificate_chain_view_t* out_chain);
int tls_parse_records_handshake_transcript(const uint8_t* data,
                                           size_t len,
                                           int include_finished,
                                           tls_handshake_transcript_view_t* out_transcript);
int tls_handshake_transcript_copy(const tls_handshake_transcript_view_t* transcript,
                                  uint8_t* out,
                                  size_t out_len,
                                  size_t* out_written);
int tls_parse_records_finished_verify_data(const uint8_t* data,
                                           size_t len,
                                           const uint8_t** out_verify_data,
                                           size_t* out_verify_data_len);
int tls_parse_records(const uint8_t* data, size_t len, tls_parser_summary_t* summary);
const char* tls_handshake_type_name(uint8_t type);
const char* tls_record_type_name(uint8_t type);

#endif
