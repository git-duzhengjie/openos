#include "tls_parser.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint16_t tls_read_u16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t tls_read_u24(const uint8_t* p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

void tls_parser_summary_init(tls_parser_summary_t* summary)
{
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));
}

const char* tls_record_type_name(uint8_t type)
{
    switch (type) {
        case TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC: return "ChangeCipherSpec";
        case TLS_CONTENT_TYPE_ALERT: return "Alert";
        case TLS_CONTENT_TYPE_HANDSHAKE: return "Handshake";
        case TLS_CONTENT_TYPE_APPLICATION_DATA: return "ApplicationData";
        default: return "Unknown";
    }
}

const char* tls_handshake_type_name(uint8_t type)
{
    switch (type) {
        case TLS_HANDSHAKE_HELLO_REQUEST: return "HelloRequest";
        case TLS_HANDSHAKE_CLIENT_HELLO: return "ClientHello";
        case TLS_HANDSHAKE_SERVER_HELLO: return "ServerHello";
        case TLS_HANDSHAKE_CERTIFICATE: return "Certificate";
        case TLS_HANDSHAKE_SERVER_KEY_EXCHANGE: return "ServerKeyExchange";
        case TLS_HANDSHAKE_CERTIFICATE_REQUEST: return "CertificateRequest";
        case TLS_HANDSHAKE_SERVER_HELLO_DONE: return "ServerHelloDone";
        case TLS_HANDSHAKE_CERTIFICATE_VERIFY: return "CertificateVerify";
        case TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE: return "ClientKeyExchange";
        case TLS_HANDSHAKE_FINISHED: return "Finished";
        default: return "Unknown";
    }
}

static void tls_record_first(tls_parser_summary_t* summary, uint8_t type, uint16_t version, uint16_t length)
{
    if (!summary) return;
    if (summary->record_type == 0) {
        summary->record_type = type;
        summary->record_version = version;
        summary->record_length = length;
    }
}

static void tls_summary_add_handshake(tls_parser_summary_t* summary, uint8_t type)
{
    if (!summary) return;
    if (summary->handshake_count < TLS_PARSER_MAX_HANDSHAKES) {
        summary->handshake_types[summary->handshake_count] = type;
    }
    if (summary->handshake_count < 255u) summary->handshake_count++;
}

static int tls_parse_server_hello(const uint8_t* body, size_t len, tls_parser_summary_t* summary)
{
    size_t pos;
    uint8_t session_id_len;
    uint16_t extensions_len;

    if (!body || !summary || len < 38u) return -1;
    summary->server_version = tls_read_u16(body);
    pos = 2u + 32u;
    session_id_len = body[pos++];
    if (pos + session_id_len + 3u > len) return -1;
    pos += session_id_len;
    summary->cipher_suite = tls_read_u16(body + pos);
    pos += 2u;
    summary->compression_method = body[pos++];
    if (pos + 2u <= len) {
        extensions_len = tls_read_u16(body + pos);
        if (pos + 2u + extensions_len <= len) {
            summary->extensions_length = extensions_len;
        }
    }
    return 0;
}

static int tls_parse_certificate(const uint8_t* body, size_t len, tls_parser_summary_t* summary)
{
    size_t pos = 3u;
    uint32_t list_len;
    uint16_t count = 0;

    if (!body || !summary || len < 3u) return -1;
    list_len = tls_read_u24(body);
    if (list_len > len - 3u) return -1;
    summary->certificate_bytes = list_len;
    while (pos + 3u <= 3u + list_len) {
        uint32_t cert_len = tls_read_u24(body + pos);
        pos += 3u;
        if (cert_len > 3u + list_len - pos) return -1;
        pos += cert_len;
        if (count < 65535u) count++;
    }
    summary->certificate_count = count;
    return 0;
}

static int tls_parse_handshake_payload(const uint8_t* payload, size_t len, tls_parser_summary_t* summary)
{
    size_t pos = 0;
    int parsed = 0;

    if (!payload || !summary) return -1;
    while (pos + 4u <= len) {
        uint8_t type = payload[pos];
        uint32_t hlen = tls_read_u24(payload + pos + 1u);
        const uint8_t* body;
        pos += 4u;
        if (hlen > len - pos) return parsed > 0 ? parsed : -1;
        body = payload + pos;
        tls_summary_add_handshake(summary, type);
        if (type == TLS_HANDSHAKE_SERVER_HELLO) {
            (void)tls_parse_server_hello(body, hlen, summary);
        } else if (type == TLS_HANDSHAKE_CERTIFICATE) {
            (void)tls_parse_certificate(body, hlen, summary);
        }
        pos += hlen;
        parsed++;
    }
    return parsed;
}

int tls_parse_records(const uint8_t* data, size_t len, tls_parser_summary_t* summary)
{
    size_t pos = 0;
    int records = 0;

    if (!data || !summary) return -1;
    tls_parser_summary_init(summary);
    while (pos + 5u <= len) {
        uint8_t type = data[pos];
        uint16_t version = tls_read_u16(data + pos + 1u);
        uint16_t record_len = tls_read_u16(data + pos + 3u);
        const uint8_t* payload;
        pos += 5u;
        if (record_len > len - pos) return records > 0 ? records : -1;
        payload = data + pos;
        tls_record_first(summary, type, version, record_len);
        if (type == TLS_CONTENT_TYPE_HANDSHAKE) {
            (void)tls_parse_handshake_payload(payload, record_len, summary);
        } else if (type == TLS_CONTENT_TYPE_ALERT && record_len >= 2u) {
            summary->alert_level = payload[0];
            summary->alert_description = payload[1];
        }
        pos += record_len;
        records++;
    }
    return records;
}
