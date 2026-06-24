#ifndef OPENOS_TLS_CRYPTO_H
#define OPENOS_TLS_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define TLS_SHA256_DIGEST_SIZE 32
#define TLS_SHA256_BLOCK_SIZE 64
#define TLS_AES_BLOCK_SIZE 16
#define TLS12_RANDOM_SIZE 32
#define TLS12_MASTER_SECRET_SIZE 48
#define TLS12_VERIFY_DATA_SIZE 12
#define TLS12_RECORD_AAD_SIZE 13
#define TLS12_AEAD_GCM_FIXED_IV_SIZE 4
#define TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE 8
#define TLS12_AEAD_GCM_NONCE_SIZE 12
#define TLS12_AEAD_GCM_TAG_SIZE 16
#define TLS12_AEAD_GCM_RECORD_OVERHEAD (TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE + TLS12_AEAD_GCM_TAG_SIZE)
#define TLS12_RECORD_HEADER_SIZE 5u
#define TLS12_AES_128_GCM_KEY_SIZE 16
#define TLS12_AES_256_GCM_KEY_SIZE 32

typedef struct tls_sha256_ctx {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t data[TLS_SHA256_BLOCK_SIZE];
    size_t data_len;
} tls_sha256_ctx_t;

typedef struct tls12_key_block_layout {
    size_t mac_key_len;
    size_t enc_key_len;
    size_t fixed_iv_len;
} tls12_key_block_layout_t;

typedef struct tls12_key_block_view {
    const uint8_t* client_write_mac_key;
    const uint8_t* server_write_mac_key;
    const uint8_t* client_write_key;
    const uint8_t* server_write_key;
    const uint8_t* client_write_iv;
    const uint8_t* server_write_iv;
    size_t mac_key_len;
    size_t enc_key_len;
    size_t fixed_iv_len;
} tls12_key_block_view_t;

typedef enum tls12_endpoint_role {
    TLS12_ENDPOINT_CLIENT = 1,
    TLS12_ENDPOINT_SERVER = 2,
} tls12_endpoint_role_t;

typedef struct tls12_aes128_gcm_record_keys {
    uint8_t client_write_key[TLS12_AES_128_GCM_KEY_SIZE];
    uint8_t server_write_key[TLS12_AES_128_GCM_KEY_SIZE];
    uint8_t client_write_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE];
    uint8_t server_write_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE];
} tls12_aes128_gcm_record_keys_t;

typedef struct tls12_aes128_gcm_record_layer {
    tls12_endpoint_role_t role;
    uint16_t protocol_version;
    uint64_t next_write_sequence;
    uint64_t next_read_sequence;
    tls12_aes128_gcm_record_keys_t keys;
} tls12_aes128_gcm_record_layer_t;

typedef struct tls12_handshake_transcript {
    tls_sha256_ctx_t sha256;
    int initialized;
} tls12_handshake_transcript_t;

void tls_sha256_init(tls_sha256_ctx_t* ctx);
void tls_sha256_update(tls_sha256_ctx_t* ctx, const uint8_t* data, size_t len);
void tls_sha256_final(tls_sha256_ctx_t* ctx, uint8_t out[TLS_SHA256_DIGEST_SIZE]);
void tls_sha256(const uint8_t* data, size_t len, uint8_t out[TLS_SHA256_DIGEST_SIZE]);

void tls_hmac_sha256(const uint8_t* key, size_t key_len,
                     const uint8_t* data, size_t data_len,
                     uint8_t out[TLS_SHA256_DIGEST_SIZE]);

int tls_hkdf_sha256_extract(const uint8_t* salt, size_t salt_len,
                            const uint8_t* ikm, size_t ikm_len,
                            uint8_t prk[TLS_SHA256_DIGEST_SIZE]);

int tls_hkdf_sha256_expand(const uint8_t prk[TLS_SHA256_DIGEST_SIZE],
                           const uint8_t* info, size_t info_len,
                           uint8_t* out, size_t out_len);

int tls_hkdf_sha256(const uint8_t* salt, size_t salt_len,
                    const uint8_t* ikm, size_t ikm_len,
                    const uint8_t* info, size_t info_len,
                    uint8_t* out, size_t out_len);

int tls12_prf_sha256(const uint8_t* secret, size_t secret_len,
                     const char* label,
                     const uint8_t* seed, size_t seed_len,
                     uint8_t* out, size_t out_len);

int tls12_prf_sha256_label_seed(const uint8_t* secret, size_t secret_len,
                                const char* label,
                                const uint8_t* seed_a, size_t seed_a_len,
                                const uint8_t* seed_b, size_t seed_b_len,
                                uint8_t* out, size_t out_len);

int tls12_derive_master_secret_sha256(const uint8_t* pre_master_secret,
                                      size_t pre_master_secret_len,
                                      const uint8_t client_random[TLS12_RANDOM_SIZE],
                                      const uint8_t server_random[TLS12_RANDOM_SIZE],
                                      uint8_t master_secret[TLS12_MASTER_SECRET_SIZE]);

int tls12_derive_extended_master_secret_sha256(
    const uint8_t* pre_master_secret,
    size_t pre_master_secret_len,
    const uint8_t session_hash[TLS_SHA256_DIGEST_SIZE],
    uint8_t master_secret[TLS12_MASTER_SECRET_SIZE]);

int tls12_derive_key_block_sha256(const uint8_t master_secret[TLS12_MASTER_SECRET_SIZE],
                                  const uint8_t server_random[TLS12_RANDOM_SIZE],
                                  const uint8_t client_random[TLS12_RANDOM_SIZE],
                                  uint8_t* key_block,
                                  size_t key_block_len);

int tls12_compute_finished_verify_data_sha256(const uint8_t master_secret[TLS12_MASTER_SECRET_SIZE],
                                              const char* label,
                                              const uint8_t* handshake_messages,
                                              size_t handshake_messages_len,
                                              uint8_t verify_data[TLS12_VERIFY_DATA_SIZE]);

void tls12_handshake_transcript_init(tls12_handshake_transcript_t* transcript);
int tls12_handshake_transcript_update(tls12_handshake_transcript_t* transcript,
                                      const uint8_t* handshake_message,
                                      size_t handshake_message_len);
int tls12_handshake_transcript_hash_sha256(const tls12_handshake_transcript_t* transcript,
                                           uint8_t handshake_hash[TLS_SHA256_DIGEST_SIZE]);
int tls12_compute_finished_verify_data_sha256_from_transcript(
    const uint8_t master_secret[TLS12_MASTER_SECRET_SIZE],
    const char* label,
    const tls12_handshake_transcript_t* transcript,
    uint8_t verify_data[TLS12_VERIFY_DATA_SIZE]);

size_t tls12_key_block_required_len(const tls12_key_block_layout_t* layout);

int tls12_split_key_block(const uint8_t* key_block,
                          size_t key_block_len,
                          const tls12_key_block_layout_t* layout,
                          tls12_key_block_view_t* view);

int tls12_build_record_aad(uint64_t sequence_number,
                           uint8_t content_type,
                           uint16_t protocol_version,
                           uint16_t plaintext_len,
                           uint8_t out[TLS12_RECORD_AAD_SIZE]);

int tls12_build_gcm_explicit_nonce(uint64_t sequence_number,
                                   uint8_t out[TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE]);

int tls12_build_gcm_nonce(const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                          const uint8_t explicit_nonce[TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE],
                          uint8_t out[TLS12_AEAD_GCM_NONCE_SIZE]);

int tls_aes128_encrypt_block(const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                             const uint8_t in[TLS_AES_BLOCK_SIZE],
                             uint8_t out[TLS_AES_BLOCK_SIZE]);

int tls_aes128_gcm_encrypt(const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                           const uint8_t nonce[TLS12_AEAD_GCM_NONCE_SIZE],
                           const uint8_t* aad,
                           size_t aad_len,
                           const uint8_t* plaintext,
                           size_t plaintext_len,
                           uint8_t* ciphertext,
                           uint8_t tag[TLS12_AEAD_GCM_TAG_SIZE]);

int tls_aes128_gcm_decrypt(const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                           const uint8_t nonce[TLS12_AEAD_GCM_NONCE_SIZE],
                           const uint8_t* aad,
                           size_t aad_len,
                           const uint8_t* ciphertext,
                           size_t ciphertext_len,
                           const uint8_t tag[TLS12_AEAD_GCM_TAG_SIZE],
                           uint8_t* plaintext);

size_t tls12_aes128_gcm_protected_len(size_t plaintext_len);
size_t tls12_aes128_gcm_wire_record_len(size_t plaintext_len);

int tls12_aes128_gcm_protect_record(uint64_t sequence_number,
                                    uint8_t content_type,
                                    uint16_t protocol_version,
                                    const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                                    const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                                    const uint8_t* plaintext,
                                    size_t plaintext_len,
                                    uint8_t* out_record_payload,
                                    size_t out_record_payload_len,
                                    size_t* written_len);

int tls12_aes128_gcm_unprotect_record(uint64_t sequence_number,
                                      uint8_t content_type,
                                      uint16_t protocol_version,
                                      const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                                      const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                                      const uint8_t* record_payload,
                                      size_t record_payload_len,
                                      uint8_t* plaintext,
                                      size_t plaintext_capacity,
                                      size_t* plaintext_len);

int tls12_aes128_gcm_protect_wire_record(uint64_t sequence_number,
                                         uint8_t content_type,
                                         uint16_t protocol_version,
                                         const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                                         const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                                         const uint8_t* plaintext,
                                         size_t plaintext_len,
                                         uint8_t* out_record,
                                         size_t out_record_len,
                                         size_t* written_len);

int tls12_aes128_gcm_unprotect_wire_record(uint64_t sequence_number,
                                           const uint8_t key[TLS12_AES_128_GCM_KEY_SIZE],
                                           const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE],
                                           const uint8_t* record,
                                           size_t record_len,
                                           uint8_t* plaintext,
                                           size_t plaintext_capacity,
                                           size_t* plaintext_len,
                                           uint8_t* content_type,
                                           uint16_t* protocol_version);

void tls12_aes128_gcm_record_layer_init(tls12_aes128_gcm_record_layer_t* layer);

int tls12_aes128_gcm_record_keys_from_key_block(const tls12_key_block_view_t* view,
                                                tls12_aes128_gcm_record_keys_t* keys);

int tls12_aes128_gcm_record_layer_configure(tls12_aes128_gcm_record_layer_t* layer,
                                            tls12_endpoint_role_t role,
                                            uint16_t protocol_version,
                                            const tls12_aes128_gcm_record_keys_t* keys);

int tls12_aes128_gcm_record_layer_configure_from_key_block(tls12_aes128_gcm_record_layer_t* layer,
                                                           tls12_endpoint_role_t role,
                                                           uint16_t protocol_version,
                                                           const tls12_key_block_view_t* view);

int tls12_aes128_gcm_record_layer_protect(tls12_aes128_gcm_record_layer_t* layer,
                                          uint8_t content_type,
                                          const uint8_t* plaintext,
                                          size_t plaintext_len,
                                          uint8_t* out_record,
                                          size_t out_record_len,
                                          size_t* written_len);

int tls12_aes128_gcm_record_layer_unprotect(tls12_aes128_gcm_record_layer_t* layer,
                                            const uint8_t* record,
                                            size_t record_len,
                                            uint8_t* plaintext,
                                            size_t plaintext_capacity,
                                            size_t* plaintext_len,
                                            uint8_t* content_type);

#endif
