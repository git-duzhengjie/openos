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

static void tls_write_u16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void tls_parser_summary_init(tls_parser_summary_t* summary)
{
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));
}

void tls_record_view_init(tls_record_view_t* record)
{
    if (!record) return;
    memset(record, 0, sizeof(*record));
}

int tls_parse_record_view(const uint8_t* data, size_t len, tls_record_view_t* out_record)
{
    uint16_t payload_len;

    if (!data || !out_record || len < TLS_RECORD_HEADER_SIZE) return -1;
    tls_record_view_init(out_record);
    payload_len = tls_read_u16(data + 3u);
    if ((size_t)payload_len > len - TLS_RECORD_HEADER_SIZE) return -1;
    out_record->content_type = data[0];
    out_record->protocol_version = tls_read_u16(data + 1u);
    out_record->payload_len = payload_len;
    out_record->payload = data + TLS_RECORD_HEADER_SIZE;
    out_record->total_len = TLS_RECORD_HEADER_SIZE + (size_t)payload_len;
    return 0;
}

int tls_write_record_header(uint8_t content_type,
                            uint16_t protocol_version,
                            uint16_t payload_len,
                            uint8_t out_header[TLS_RECORD_HEADER_SIZE])
{
    if (!out_header) return -1;
    out_header[0] = content_type;
    tls_write_u16(out_header + 1u, protocol_version);
    tls_write_u16(out_header + 3u, payload_len);
    return 0;
}

void tls_certificate_chain_view_init(tls_certificate_chain_view_t* chain)
{
    if (!chain) return;
    memset(chain, 0, sizeof(*chain));
}

void tls_handshake_transcript_view_init(tls_handshake_transcript_view_t* transcript)
{
    if (!transcript) return;
    memset(transcript, 0, sizeof(*transcript));
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

int tls_parse_certificate_chain(const uint8_t* certificate_body,
                                size_t certificate_body_len,
                                tls_certificate_chain_view_t* out_chain)
{
    size_t pos = 3u;
    size_t list_end;
    uint32_t list_len;
    uint16_t count = 0u;

    if (!certificate_body || !out_chain || certificate_body_len < 3u) return -1;
    tls_certificate_chain_view_init(out_chain);
    list_len = tls_read_u24(certificate_body);
    if (list_len > certificate_body_len - 3u) return -1;
    list_end = 3u + (size_t)list_len;
    out_chain->certificate_bytes = list_len;

    while (pos + 3u <= list_end) {
        uint32_t cert_len = tls_read_u24(certificate_body + pos);
        pos += 3u;
        if (cert_len == 0u || cert_len > list_end - pos) return -1;
        if (out_chain->stored_certificate_count < TLS_PARSER_MAX_CERTIFICATES) {
            uint16_t slot = out_chain->stored_certificate_count;
            out_chain->certificates[slot] = certificate_body + pos;
            out_chain->certificate_lengths[slot] = cert_len;
            out_chain->stored_certificate_count++;
        } else {
            out_chain->is_truncated = 1;
        }
        pos += cert_len;
        if (count < 65535u) count++;
    }

    if (pos != list_end) return -1;
    out_chain->certificate_count = count;
    return 0;
}

static int tls_parse_certificate(const uint8_t* body, size_t len, tls_parser_summary_t* summary)
{
    tls_certificate_chain_view_t chain;

    if (!summary) return -1;
    if (tls_parse_certificate_chain(body, len, &chain) != 0) return -1;
    summary->certificate_bytes = chain.certificate_bytes;
    summary->certificate_count = chain.certificate_count;
    return 0;
}

static int tls_parse_server_key_exchange(const uint8_t* body, size_t len, tls_parser_summary_t* summary)
{
    size_t pos = 0;
    uint8_t public_key_len;

    if (!body || !summary || len < 4u) return -1;
    summary->key_exchange_curve_type = body[pos++];
    summary->key_exchange_named_curve = tls_read_u16(body + pos);
    pos += 2u;
    public_key_len = body[pos++];
    if (public_key_len > len - pos) return -1;
    summary->key_exchange_public_key_length = public_key_len;
    pos += public_key_len;

    if (pos + 4u <= len) {
        summary->key_exchange_signature_algorithm = tls_read_u16(body + pos);
        pos += 2u;
        summary->key_exchange_signature_length = tls_read_u16(body + pos);
    }
    return 0;
}

static int tls_find_certificate_in_handshake_payload(const uint8_t* payload,
                                                     size_t len,
                                                     tls_certificate_chain_view_t* out_chain)
{
    size_t pos = 0;

    if (!payload || !out_chain) return -1;
    while (pos + 4u <= len) {
        uint8_t type = payload[pos];
        uint32_t hlen = tls_read_u24(payload + pos + 1u);
        const uint8_t* body;
        pos += 4u;
        if (hlen > len - pos) return -1;
        body = payload + pos;
        if (type == TLS_HANDSHAKE_CERTIFICATE) {
            return tls_parse_certificate_chain(body, hlen, out_chain);
        }
        pos += hlen;
    }
    return -1;
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
        } else if (type == TLS_HANDSHAKE_SERVER_KEY_EXCHANGE) {
            (void)tls_parse_server_key_exchange(body, hlen, summary);
        }
        pos += hlen;
        parsed++;
    }
    return parsed;
}

int tls_parse_records_certificate_chain(const uint8_t* data,
                                        size_t len,
                                        tls_certificate_chain_view_t* out_chain)
{
    size_t pos = 0;

    if (!data || !out_chain) return -1;
    tls_certificate_chain_view_init(out_chain);
    while (pos + 5u <= len) {
        uint8_t type = data[pos];
        uint16_t record_len = tls_read_u16(data + pos + 3u);
        const uint8_t* payload;
        pos += 5u;
        if (record_len > len - pos) return -1;
        payload = data + pos;
        if (type == TLS_CONTENT_TYPE_HANDSHAKE &&
            tls_find_certificate_in_handshake_payload(payload, record_len, out_chain) == 0) {
            return 0;
        }
        pos += record_len;
    }
    return -1;
}

static void tls_transcript_add_handshake(tls_handshake_transcript_view_t* transcript,
                                         const uint8_t* message,
                                         size_t message_len,
                                         uint8_t message_type)
{
    if (!transcript) return;
    if (message_len > UINT32_MAX - transcript->transcript_bytes) {
        transcript->transcript_bytes = UINT32_MAX;
        transcript->is_truncated = 1;
    } else {
        transcript->transcript_bytes += (uint32_t)message_len;
    }
    if (transcript->message_count < TLS_PARSER_MAX_HANDSHAKES) {
        uint8_t slot = transcript->message_count;
        transcript->messages[slot] = message;
        transcript->message_lengths[slot] = message_len;
        transcript->message_types[slot] = message_type;
        transcript->message_count++;
    } else {
        transcript->is_truncated = 1;
    }
}

int tls_parse_records_handshake_transcript(const uint8_t* data,
                                           size_t len,
                                           int include_finished,
                                           tls_handshake_transcript_view_t* out_transcript)
{
    size_t pos = 0u;

    if (!data || !out_transcript) return -1;
    tls_handshake_transcript_view_init(out_transcript);
    while (pos + 5u <= len) {
        uint8_t type = data[pos];
        uint16_t record_len = tls_read_u16(data + pos + 3u);
        const uint8_t* payload;
        size_t hpos = 0u;
        pos += 5u;
        if (record_len > len - pos) return -1;
        payload = data + pos;
        if (type == TLS_CONTENT_TYPE_HANDSHAKE) {
            while (hpos + 4u <= record_len) {
                uint8_t htype = payload[hpos];
                uint32_t hlen = tls_read_u24(payload + hpos + 1u);
                size_t message_len;
                if (hlen > record_len - hpos - 4u) return -1;
                message_len = 4u + (size_t)hlen;
                if (include_finished || htype != TLS_HANDSHAKE_FINISHED) {
                    tls_transcript_add_handshake(out_transcript, payload + hpos, message_len, htype);
                }
                hpos += message_len;
            }
            if (hpos != record_len) return -1;
        }
        pos += record_len;
    }
    return pos == len ? 0 : -1;
}

int tls_handshake_transcript_copy(const tls_handshake_transcript_view_t* transcript,
                                  uint8_t* out,
                                  size_t out_len,
                                  size_t* out_written)
{
    size_t written = 0u;

    if (!transcript || (!out && out_len > 0u)) return -1;
    for (uint8_t i = 0u; i < transcript->message_count; ++i) {
        size_t len = transcript->message_lengths[i];
        if (!transcript->messages[i] || len > out_len - written) return -1;
        memcpy(out + written, transcript->messages[i], len);
        written += len;
    }
    if (out_written) *out_written = written;
    return written == transcript->transcript_bytes ? 0 : -1;
}

int tls_parse_records_finished_verify_data(const uint8_t* data,
                                           size_t len,
                                           const uint8_t** out_verify_data,
                                           size_t* out_verify_data_len)
{
    size_t pos = 0u;

    if (!data || !out_verify_data || !out_verify_data_len) return -1;
    *out_verify_data = NULL;
    *out_verify_data_len = 0u;
    while (pos + 5u <= len) {
        uint8_t type = data[pos];
        uint16_t record_len = tls_read_u16(data + pos + 3u);
        const uint8_t* payload;
        size_t hpos = 0u;
        pos += 5u;
        if (record_len > len - pos) return -1;
        payload = data + pos;
        if (type == TLS_CONTENT_TYPE_HANDSHAKE) {
            while (hpos + 4u <= record_len) {
                uint8_t htype = payload[hpos];
                uint32_t hlen = tls_read_u24(payload + hpos + 1u);
                if (hlen > record_len - hpos - 4u) return -1;
                if (htype == TLS_HANDSHAKE_FINISHED) {
                    *out_verify_data = payload + hpos + 4u;
                    *out_verify_data_len = (size_t)hlen;
                    return 0;
                }
                hpos += 4u + (size_t)hlen;
            }
            if (hpos != record_len) return -1;
        }
        pos += record_len;
    }
    return -1;
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
