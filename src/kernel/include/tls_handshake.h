#ifndef OPENOS_TLS_HANDSHAKE_H
#define OPENOS_TLS_HANDSHAKE_H

#include "tls_crypto.h"
#include "tls_parser.h"

#include <stddef.h>
#include <stdint.h>

#define TLS12_VERSION 0x0303u
#define TLS12_CIPHER_SUITE_RSA_WITH_AES_128_GCM_SHA256 0x009cu
#define TLS12_HANDSHAKE_MAX_FINISHED_SIZE TLS12_VERIFY_DATA_SIZE
#define TLS12_CLIENT_RANDOM_SIZE 32u
#define TLS12_AES128_GCM_KEY_BLOCK_SIZE \
    ((TLS12_AES_128_GCM_KEY_SIZE * 2u) + (TLS12_AEAD_GCM_FIXED_IV_SIZE * 2u))

typedef enum tls12_handshake_state {
    TLS12_HANDSHAKE_STATE_INIT = 0,
    TLS12_HANDSHAKE_STATE_CLIENT_HELLO_SENT,
    TLS12_HANDSHAKE_STATE_SERVER_HELLO_RECEIVED,
    TLS12_HANDSHAKE_STATE_CERTIFICATE_RECEIVED,
    TLS12_HANDSHAKE_STATE_SERVER_HELLO_DONE_RECEIVED,
    TLS12_HANDSHAKE_STATE_CLIENT_KEY_EXCHANGE_SENT,
    TLS12_HANDSHAKE_STATE_CLIENT_CHANGE_CIPHER_SPEC_SENT,
    TLS12_HANDSHAKE_STATE_CLIENT_FINISHED_SENT,
    TLS12_HANDSHAKE_STATE_SERVER_CHANGE_CIPHER_SPEC_RECEIVED,
    TLS12_HANDSHAKE_STATE_SERVER_FINISHED_RECEIVED,
    TLS12_HANDSHAKE_STATE_ESTABLISHED,
    TLS12_HANDSHAKE_STATE_ERROR,
} tls12_handshake_state_t;

typedef struct tls12_handshake_context {
    tls12_handshake_state_t state;
    uint16_t negotiated_version;
    uint16_t cipher_suite;
    uint8_t compression_method;
    uint16_t server_supported_version;
    uint8_t server_selected_alpn[16];
    size_t server_selected_alpn_len;
    int server_has_extended_master_secret;
    int server_has_renegotiation_info;
    int server_has_alpn;
    int server_has_supported_versions;
    uint8_t client_random[TLS12_RANDOM_SIZE];
    uint8_t server_random[TLS12_RANDOM_SIZE];
    uint8_t master_secret[TLS12_MASTER_SECRET_SIZE];
    uint8_t key_block[TLS12_AES128_GCM_KEY_BLOCK_SIZE];
    tls12_aes128_gcm_record_keys_t record_keys;
    tls12_aes128_gcm_record_layer_t record_layer;
    int has_client_random;
    int has_server_random;
    int has_master_secret;
    int has_key_block;
    int has_record_layer;
    int has_certificate;
    int client_change_cipher_spec_sent;
    int server_change_cipher_spec_received;
    uint8_t client_finished[TLS12_HANDSHAKE_MAX_FINISHED_SIZE];
    uint8_t server_finished[TLS12_HANDSHAKE_MAX_FINISHED_SIZE];
    size_t client_finished_len;
    size_t server_finished_len;
    tls_certificate_chain_view_t certificate_chain;
    tls12_handshake_transcript_t transcript;
} tls12_handshake_context_t;

void tls12_handshake_context_init(tls12_handshake_context_t* ctx);
const char* tls12_handshake_state_name(tls12_handshake_state_t state);

int tls12_build_client_hello_record(const char* server_name,
                                    const uint8_t client_random[TLS12_CLIENT_RANDOM_SIZE],
                                    uint8_t* out_record,
                                    size_t out_record_cap,
                                    size_t* out_record_len);

int tls12_handshake_set_master_secret(tls12_handshake_context_t* ctx,
                                      const uint8_t master_secret[TLS12_MASTER_SECRET_SIZE]);

int tls12_handshake_compute_expected_client_finished(
    const tls12_handshake_context_t* ctx,
    uint8_t verify_data[TLS12_VERIFY_DATA_SIZE]);

int tls12_handshake_derive_aes128_gcm_key_block(tls12_handshake_context_t* ctx);

int tls12_handshake_configure_aes128_gcm_record_layer(tls12_handshake_context_t* ctx,
                                                      tls12_endpoint_role_t role);

int tls12_handshake_compute_expected_server_finished(
    const tls12_handshake_context_t* ctx,
    uint8_t verify_data[TLS12_VERIFY_DATA_SIZE]);

int tls12_handshake_on_client_hello_sent(tls12_handshake_context_t* ctx,
                                         const uint8_t* handshake_message,
                                         size_t handshake_message_len);

int tls12_handshake_on_server_handshake(tls12_handshake_context_t* ctx,
                                        const uint8_t* handshake_message,
                                        size_t handshake_message_len);

int tls12_handshake_on_client_key_exchange_sent(tls12_handshake_context_t* ctx,
                                                const uint8_t* handshake_message,
                                                size_t handshake_message_len);

int tls12_handshake_on_client_change_cipher_spec_sent(tls12_handshake_context_t* ctx,
                                                      const uint8_t* message,
                                                      size_t message_len);

int tls12_handshake_on_client_finished_sent(tls12_handshake_context_t* ctx,
                                            const uint8_t* handshake_message,
                                            size_t handshake_message_len);

int tls12_handshake_on_server_change_cipher_spec(tls12_handshake_context_t* ctx,
                                                 const uint8_t* message,
                                                 size_t message_len);

int tls12_handshake_on_server_finished(tls12_handshake_context_t* ctx,
                                       const uint8_t* handshake_message,
                                       size_t handshake_message_len);

int tls12_handshake_get_transcript_hash(const tls12_handshake_context_t* ctx,
                                        uint8_t handshake_hash[TLS_SHA256_DIGEST_SIZE]);

#endif
