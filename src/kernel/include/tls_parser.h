#ifndef OPENOS_TLS_PARSER_H
#define OPENOS_TLS_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define TLS_PARSER_MAX_HANDSHAKES 8

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
int tls_parse_records(const uint8_t* data, size_t len, tls_parser_summary_t* summary);
const char* tls_handshake_type_name(uint8_t type);
const char* tls_record_type_name(uint8_t type);

#endif
