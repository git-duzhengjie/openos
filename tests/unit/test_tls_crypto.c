#include "unit_test.h"

#include <stdint.h>
#include <string.h>

#include "tls_crypto.h"
#include "../../src/kernel/tls_crypto.c"

static void assert_bytes_eq(const uint8_t* expected, const uint8_t* actual, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        ASSERT_EQ_INT(expected[i], actual[i]);
    }
}

static void test_sha256_vectors(void)
{
    uint8_t out[TLS_SHA256_DIGEST_SIZE];
    static const uint8_t sha_empty[32] = {
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55,
    };
    static const uint8_t sha_abc[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad,
    };

    tls_sha256((const uint8_t*)"", 0, out);
    assert_bytes_eq(sha_empty, out, sizeof(sha_empty));

    tls_sha256((const uint8_t*)"abc", 3, out);
    assert_bytes_eq(sha_abc, out, sizeof(sha_abc));
}

static void test_hmac_sha256_vector(void)
{
    uint8_t out[TLS_SHA256_DIGEST_SIZE];
    static const uint8_t key[20] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
    };
    static const uint8_t expected[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7,
    };

    tls_hmac_sha256(key, sizeof(key), (const uint8_t*)"Hi There", 8, out);
    assert_bytes_eq(expected, out, sizeof(expected));
}

static void test_tls12_prf_sha256_vectors(void)
{
    uint8_t secret[16];
    uint8_t seed[16];
    uint8_t out[64];
    uint8_t client_random[TLS12_RANDOM_SIZE];
    uint8_t server_random[TLS12_RANDOM_SIZE];
    uint8_t master_secret[TLS12_MASTER_SECRET_SIZE];
    uint8_t key_block[64];
    static const uint8_t expected_prf[64] = {
        0x6c,0x39,0x54,0xe2,0x0b,0x9a,0x03,0xd7,0x87,0x52,0xe8,0xc2,0x3a,0x44,0xf7,0xd0,
        0xd1,0xd0,0xe5,0x29,0x30,0xa2,0x2c,0x7c,0x5e,0xe1,0x34,0xbd,0x02,0x47,0xca,0xd5,
        0x0e,0x62,0x47,0xf5,0x8a,0x6f,0xa4,0x6a,0x20,0xf7,0x86,0xd2,0x85,0xd1,0xdd,0xa0,
        0x7d,0x51,0xa2,0x11,0x38,0xd2,0x43,0xf8,0x16,0x7e,0x4b,0x6c,0x9e,0x9b,0x4e,0x8d,
    };
    static const uint8_t expected_master_secret[TLS12_MASTER_SECRET_SIZE] = {
        0x46,0xf0,0xf5,0x79,0x12,0x82,0xaf,0x30,0xc7,0xce,0x76,0xf6,0xca,0x17,0x77,0xa7,
        0x02,0x4e,0x19,0x6c,0xac,0xa8,0xa1,0x67,0xf6,0xe3,0x98,0x61,0xff,0xec,0xae,0x2d,
        0xf6,0x84,0x43,0x31,0x62,0x11,0x55,0x9e,0x49,0x65,0x58,0x63,0x3a,0x81,0x32,0x45,
    };
    static const uint8_t expected_key_block[64] = {
        0xcf,0x84,0x36,0x82,0x2c,0x35,0x46,0x3a,0xaf,0x0a,0xe8,0x91,0x5a,0x77,0x4f,0xcb,
        0xc2,0x45,0xf7,0x8a,0x15,0x05,0xc1,0x2f,0xb1,0x5a,0xa1,0x7b,0x8f,0x6c,0x76,0x9d,
        0x33,0x13,0x0d,0xd1,0x12,0xa2,0x6e,0x4a,0x95,0x31,0xa6,0xe0,0xce,0xd0,0xf6,0x41,
        0xcb,0xe0,0x4d,0x5b,0xa3,0x8a,0x5a,0x45,0xcf,0xc8,0x5a,0xaa,0xf7,0x65,0x25,0x3b,
    };

    for (size_t i = 0; i < sizeof(secret); ++i) {
        secret[i] = (uint8_t)i;
        seed[i] = (uint8_t)(0xa0u + i);
    }
    ASSERT_EQ_INT(0, tls12_prf_sha256(secret, sizeof(secret), "test label", seed, sizeof(seed), out, sizeof(out)));
    assert_bytes_eq(expected_prf, out, sizeof(expected_prf));

    for (size_t i = 0; i < TLS12_RANDOM_SIZE; ++i) {
        client_random[i] = (uint8_t)(0x11u + i);
        server_random[i] = (uint8_t)(0x51u + i);
    }
    ASSERT_EQ_INT(0, tls12_derive_master_secret_sha256(secret, sizeof(secret), client_random, server_random, master_secret));
    assert_bytes_eq(expected_master_secret, master_secret, sizeof(expected_master_secret));
    ASSERT_EQ_INT(0, tls12_derive_key_block_sha256(master_secret, server_random, client_random, key_block, sizeof(key_block)));
    assert_bytes_eq(expected_key_block, key_block, sizeof(expected_key_block));
}

static void test_tls12_finished_verify_data(void)
{
    uint8_t master_secret[TLS12_MASTER_SECRET_SIZE];
    static const uint8_t handshake_messages[] = {
        1, 0, 0, 4, 't', 'e', 's', 't',
        2, 0, 0, 3, 'o', 's', '!',
    };
    uint8_t client_verify[TLS12_VERIFY_DATA_SIZE];
    uint8_t server_verify[TLS12_VERIFY_DATA_SIZE];
    static const uint8_t expected_client[TLS12_VERIFY_DATA_SIZE] = {
        0x2c, 0xb8, 0x45, 0x45, 0xd7, 0xa4,
        0x53, 0x47, 0xb6, 0x30, 0xda, 0xc0,
    };
    static const uint8_t expected_server[TLS12_VERIFY_DATA_SIZE] = {
        0xf0, 0x3f, 0xaa, 0x07, 0x57, 0x57,
        0x59, 0xe4, 0x1d, 0x8b, 0x49, 0x20,
    };

    for (size_t i = 0; i < sizeof(master_secret); ++i) {
        master_secret[i] = (uint8_t)i;
    }
    ASSERT_EQ_INT(0, tls12_compute_finished_verify_data_sha256(master_secret,
                                                               "client finished",
                                                               handshake_messages,
                                                               sizeof(handshake_messages),
                                                               client_verify));
    ASSERT_EQ_INT(0, tls12_compute_finished_verify_data_sha256(master_secret,
                                                               "server finished",
                                                               handshake_messages,
                                                               sizeof(handshake_messages),
                                                               server_verify));
    assert_bytes_eq(expected_client, client_verify, sizeof(expected_client));
    assert_bytes_eq(expected_server, server_verify, sizeof(expected_server));
}

static void test_tls12_handshake_transcript(void)
{
    uint8_t master_secret[TLS12_MASTER_SECRET_SIZE];
    static const uint8_t part1[] = {1, 0, 0, 4, 't', 'e', 's', 't'};
    static const uint8_t part2[] = {2, 0, 0, 3, 'o', 's', '!'};
    static const uint8_t whole[] = {
        1, 0, 0, 4, 't', 'e', 's', 't',
        2, 0, 0, 3, 'o', 's', '!',
    };
    uint8_t transcript_hash[TLS_SHA256_DIGEST_SIZE];
    uint8_t direct_hash[TLS_SHA256_DIGEST_SIZE];
    uint8_t from_transcript[TLS12_VERIFY_DATA_SIZE];
    uint8_t direct[TLS12_VERIFY_DATA_SIZE];
    tls12_handshake_transcript_t transcript = {0};

    for (size_t i = 0; i < sizeof(master_secret); ++i) {
        master_secret[i] = (uint8_t)i;
    }

    ASSERT_EQ_INT(-1, tls12_handshake_transcript_update(&transcript, part1, sizeof(part1)));
    tls12_handshake_transcript_init(&transcript);
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&transcript, part1, sizeof(part1)));
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&transcript, NULL, 0u));
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&transcript, part2, sizeof(part2)));
    ASSERT_EQ_INT(-1, tls12_handshake_transcript_update(&transcript, NULL, 1u));

    ASSERT_EQ_INT(0, tls12_handshake_transcript_hash_sha256(&transcript, transcript_hash));
    tls_sha256(whole, sizeof(whole), direct_hash);
    assert_bytes_eq(direct_hash, transcript_hash, sizeof(direct_hash));

    ASSERT_EQ_INT(0, tls12_compute_finished_verify_data_sha256_from_transcript(master_secret,
                                                                               "client finished",
                                                                               &transcript,
                                                                               from_transcript));
    ASSERT_EQ_INT(0, tls12_compute_finished_verify_data_sha256(master_secret,
                                                               "client finished",
                                                               whole,
                                                               sizeof(whole),
                                                               direct));
    assert_bytes_eq(direct, from_transcript, sizeof(direct));
}

static void test_tls12_aead_record_helpers(void)
{
    uint8_t key_block[40];
    tls12_key_block_layout_t layout = {0u, TLS12_AES_128_GCM_KEY_SIZE, TLS12_AEAD_GCM_FIXED_IV_SIZE};
    tls12_key_block_view_t view;
    uint8_t aad[TLS12_RECORD_AAD_SIZE];
    uint8_t explicit_nonce[TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE];
    uint8_t nonce[TLS12_AEAD_GCM_NONCE_SIZE];
    static const uint8_t expected_aad[TLS12_RECORD_AAD_SIZE] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x17,
        0x03,0x03,
        0x12,0x34,
    };
    static const uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE] = {0xa0,0xa1,0xa2,0xa3};
    static const uint8_t expected_nonce[TLS12_AEAD_GCM_NONCE_SIZE] = {
        0xa0,0xa1,0xa2,0xa3,
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    };

    for (size_t i = 0; i < sizeof(key_block); ++i) {
        key_block[i] = (uint8_t)i;
    }

    ASSERT_EQ_SIZE(sizeof(key_block), tls12_key_block_required_len(&layout));
    ASSERT_EQ_INT(0, tls12_split_key_block(key_block, sizeof(key_block), &layout, &view));
    ASSERT_EQ_SIZE(0u, view.mac_key_len);
    ASSERT_EQ_SIZE(TLS12_AES_128_GCM_KEY_SIZE, view.enc_key_len);
    ASSERT_EQ_SIZE(TLS12_AEAD_GCM_FIXED_IV_SIZE, view.fixed_iv_len);
    ASSERT_TRUE(view.client_write_mac_key == key_block);
    ASSERT_TRUE(view.server_write_mac_key == key_block);
    ASSERT_TRUE(view.client_write_key == key_block);
    ASSERT_TRUE(view.server_write_key == key_block + TLS12_AES_128_GCM_KEY_SIZE);
    ASSERT_TRUE(view.client_write_iv == key_block + (TLS12_AES_128_GCM_KEY_SIZE * 2u));
    ASSERT_TRUE(view.server_write_iv == key_block + (TLS12_AES_128_GCM_KEY_SIZE * 2u) + TLS12_AEAD_GCM_FIXED_IV_SIZE);
    ASSERT_EQ_INT(-1, tls12_split_key_block(key_block, sizeof(key_block) - 1u, &layout, &view));

    {
        tls12_key_block_layout_t empty_layout = {0u, 0u, 0u};
        ASSERT_EQ_SIZE(0u, tls12_key_block_required_len(&empty_layout));
        ASSERT_EQ_INT(0, tls12_split_key_block(NULL, 0u, &empty_layout, &view));
        ASSERT_TRUE(view.client_write_mac_key == NULL);
        ASSERT_TRUE(view.server_write_mac_key == NULL);
        ASSERT_TRUE(view.client_write_key == NULL);
        ASSERT_TRUE(view.server_write_key == NULL);
        ASSERT_TRUE(view.client_write_iv == NULL);
        ASSERT_TRUE(view.server_write_iv == NULL);
    }

    ASSERT_EQ_INT(0, tls12_build_record_aad(0x0102030405060708ULL, 0x17, 0x0303, 0x1234, aad));
    assert_bytes_eq(expected_aad, aad, sizeof(expected_aad));

    ASSERT_EQ_INT(0, tls12_build_gcm_explicit_nonce(0x0102030405060708ULL, explicit_nonce));
    ASSERT_EQ_INT(0, tls12_build_gcm_nonce(fixed_iv, explicit_nonce, nonce));
    assert_bytes_eq(expected_nonce, nonce, sizeof(expected_nonce));
}

static void test_aes128_and_gcm_vectors(void)
{
    uint8_t block_out[TLS_AES_BLOCK_SIZE];
    uint8_t ciphertext[16];
    uint8_t plaintext[16];
    uint8_t tag[TLS12_AEAD_GCM_TAG_SIZE];
    uint8_t tampered_tag[TLS12_AEAD_GCM_TAG_SIZE];
    static const uint8_t aes_key[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    };
    static const uint8_t aes_plain[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
        0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,
    };
    static const uint8_t aes_expected[16] = {
        0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
        0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a,
    };
    static const uint8_t zero_key[16] = {0};
    static const uint8_t zero_nonce[12] = {0};
    static const uint8_t zero_plain[16] = {0};
    static const uint8_t gcm_empty_tag[16] = {
        0x58,0xe2,0xfc,0xce,0xfa,0x7e,0x30,0x61,
        0x36,0x7f,0x1d,0x57,0xa4,0xe7,0x45,0x5a,
    };
    static const uint8_t gcm_cipher_expected[16] = {
        0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,
        0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78,
    };
    static const uint8_t gcm_tag_expected[16] = {
        0xab,0x6e,0x47,0xd4,0x2c,0xec,0x13,0xbd,
        0xf5,0x3a,0x67,0xb2,0x12,0x57,0xbd,0xdf,
    };

    ASSERT_EQ_INT(0, tls_aes128_encrypt_block(aes_key, aes_plain, block_out));
    assert_bytes_eq(aes_expected, block_out, sizeof(aes_expected));

    ASSERT_EQ_INT(0, tls_aes128_gcm_encrypt(zero_key, zero_nonce, NULL, 0u, NULL, 0u, NULL, tag));
    assert_bytes_eq(gcm_empty_tag, tag, sizeof(gcm_empty_tag));

    ASSERT_EQ_INT(0, tls_aes128_gcm_encrypt(zero_key, zero_nonce, NULL, 0u,
                                            zero_plain, sizeof(zero_plain), ciphertext, tag));
    assert_bytes_eq(gcm_cipher_expected, ciphertext, sizeof(gcm_cipher_expected));
    assert_bytes_eq(gcm_tag_expected, tag, sizeof(gcm_tag_expected));

    memset(plaintext, 0xa5, sizeof(plaintext));
    ASSERT_EQ_INT(0, tls_aes128_gcm_decrypt(zero_key, zero_nonce, NULL, 0u,
                                            ciphertext, sizeof(ciphertext), tag, plaintext));
    assert_bytes_eq(zero_plain, plaintext, sizeof(zero_plain));

    memcpy(tampered_tag, tag, sizeof(tampered_tag));
    tampered_tag[0] ^= 0x01u;
    ASSERT_EQ_INT(-2, tls_aes128_gcm_decrypt(zero_key, zero_nonce, NULL, 0u,
                                             ciphertext, sizeof(ciphertext), tampered_tag, plaintext));
}

static void test_tls12_aes128_gcm_record_protection(void)
{
    uint8_t key[TLS12_AES_128_GCM_KEY_SIZE];
    uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE] = {0xa0,0xa1,0xa2,0xa3};
    static const uint8_t plaintext[] = {'O','p','e','n','O','S',' ','T','L','S',' ','r','e','c','o','r','d'};
    uint8_t protected_payload[128];
    uint8_t decrypted[sizeof(plaintext)];
    size_t written = 0u;
    size_t decrypted_len = 0u;
    uint64_t seq = 0x0102030405060708ULL;
    static const uint8_t expected_explicit_nonce[TLS12_AEAD_GCM_EXPLICIT_NONCE_SIZE] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    };

    for (size_t i = 0; i < sizeof(key); ++i) key[i] = (uint8_t)(0x10u + i);

    ASSERT_EQ_SIZE(sizeof(plaintext) + TLS12_AEAD_GCM_RECORD_OVERHEAD,
                   tls12_aes128_gcm_protected_len(sizeof(plaintext)));
    ASSERT_EQ_INT(0, tls12_aes128_gcm_protect_record(seq,
                                                     0x17,
                                                     0x0303,
                                                     key,
                                                     fixed_iv,
                                                     plaintext,
                                                     sizeof(plaintext),
                                                     protected_payload,
                                                     sizeof(protected_payload),
                                                     &written));
    ASSERT_EQ_SIZE(sizeof(plaintext) + TLS12_AEAD_GCM_RECORD_OVERHEAD, written);
    assert_bytes_eq(expected_explicit_nonce, protected_payload, sizeof(expected_explicit_nonce));

    ASSERT_EQ_INT(0, tls12_aes128_gcm_unprotect_record(seq,
                                                       0x17,
                                                       0x0303,
                                                       key,
                                                       fixed_iv,
                                                       protected_payload,
                                                       written,
                                                       decrypted,
                                                       sizeof(decrypted),
                                                       &decrypted_len));
    ASSERT_EQ_SIZE(sizeof(plaintext), decrypted_len);
    assert_bytes_eq(plaintext, decrypted, sizeof(plaintext));

    ASSERT_EQ_INT(-2, tls12_aes128_gcm_unprotect_record(seq + 1u,
                                                        0x17,
                                                        0x0303,
                                                        key,
                                                        fixed_iv,
                                                        protected_payload,
                                                        written,
                                                        decrypted,
                                                        sizeof(decrypted),
                                                        &decrypted_len));
    ASSERT_EQ_INT(-2, tls12_aes128_gcm_unprotect_record(seq,
                                                        0x16,
                                                        0x0303,
                                                        key,
                                                        fixed_iv,
                                                        protected_payload,
                                                        written,
                                                        decrypted,
                                                        sizeof(decrypted),
                                                        &decrypted_len));
    ASSERT_EQ_INT(-1, tls12_aes128_gcm_unprotect_record(seq,
                                                        0x17,
                                                        0x0303,
                                                        key,
                                                        fixed_iv,
                                                        protected_payload,
                                                        written,
                                                        decrypted,
                                                        sizeof(decrypted) - 1u,
                                                        &decrypted_len));
    ASSERT_EQ_INT(-1, tls12_aes128_gcm_protect_record(seq,
                                                      0x17,
                                                      0x0303,
                                                      key,
                                                      fixed_iv,
                                                      plaintext,
                                                      sizeof(plaintext),
                                                      protected_payload,
                                                      written - 1u,
                                                      &written));
}

static void test_tls12_aes128_gcm_wire_record_protection(void)
{
    uint8_t key[TLS12_AES_128_GCM_KEY_SIZE];
    uint8_t fixed_iv[TLS12_AEAD_GCM_FIXED_IV_SIZE] = {0xb0,0xb1,0xb2,0xb3};
    static const uint8_t plaintext[] = {'w','i','r','e',' ','r','e','c','o','r','d'};
    uint8_t record[128];
    uint8_t decrypted[sizeof(plaintext)];
    size_t written = 0u;
    size_t decrypted_len = 0u;
    uint8_t content_type = 0u;
    uint16_t version = 0u;
    uint64_t seq = 9u;

    for (size_t i = 0; i < sizeof(key); ++i) key[i] = (uint8_t)(0x30u + i);

    ASSERT_EQ_SIZE(sizeof(plaintext) + TLS12_AEAD_GCM_RECORD_OVERHEAD + TLS12_RECORD_HEADER_SIZE,
                   tls12_aes128_gcm_wire_record_len(sizeof(plaintext)));
    ASSERT_EQ_INT(0, tls12_aes128_gcm_protect_wire_record(seq,
                                                          0x17,
                                                          0x0303,
                                                          key,
                                                          fixed_iv,
                                                          plaintext,
                                                          sizeof(plaintext),
                                                          record,
                                                          sizeof(record),
                                                          &written));
    ASSERT_EQ_SIZE(tls12_aes128_gcm_wire_record_len(sizeof(plaintext)), written);
    ASSERT_EQ_INT(0x17, record[0]);
    ASSERT_EQ_INT(0x03, record[1]);
    ASSERT_EQ_INT(0x03, record[2]);
    ASSERT_EQ_INT(0x00, record[3]);
    ASSERT_EQ_INT(sizeof(plaintext) + TLS12_AEAD_GCM_RECORD_OVERHEAD, record[4]);

    ASSERT_EQ_INT(0, tls12_aes128_gcm_unprotect_wire_record(seq,
                                                            key,
                                                            fixed_iv,
                                                            record,
                                                            written,
                                                            decrypted,
                                                            sizeof(decrypted),
                                                            &decrypted_len,
                                                            &content_type,
                                                            &version));
    ASSERT_EQ_SIZE(sizeof(plaintext), decrypted_len);
    ASSERT_EQ_INT(0x17, content_type);
    ASSERT_EQ_INT(0x0303, version);
    assert_bytes_eq(plaintext, decrypted, sizeof(plaintext));

    record[0] ^= 0x01u;
    ASSERT_EQ_INT(-2, tls12_aes128_gcm_unprotect_wire_record(seq,
                                                             key,
                                                             fixed_iv,
                                                             record,
                                                             written,
                                                             decrypted,
                                                             sizeof(decrypted),
                                                             &decrypted_len,
                                                             &content_type,
                                                             &version));
    record[0] ^= 0x01u;
    record[4] ^= 0x01u;
    ASSERT_EQ_INT(-1, tls12_aes128_gcm_unprotect_wire_record(seq,
                                                             key,
                                                             fixed_iv,
                                                             record,
                                                             written,
                                                             decrypted,
                                                             sizeof(decrypted),
                                                             &decrypted_len,
                                                             &content_type,
                                                             &version));
    ASSERT_EQ_INT(-1, tls12_aes128_gcm_protect_wire_record(seq,
                                                           0x17,
                                                           0x0303,
                                                           key,
                                                           fixed_iv,
                                                           plaintext,
                                                           sizeof(plaintext),
                                                           record,
                                                           written - 1u,
                                                           &written));
}

static void test_tls12_aes128_gcm_record_layer_context_roundtrip(void)
{
    uint8_t key_block[40];
    tls12_key_block_layout_t layout = {0u, TLS12_AES_128_GCM_KEY_SIZE, TLS12_AEAD_GCM_FIXED_IV_SIZE};
    tls12_key_block_view_t view;
    tls12_aes128_gcm_record_layer_t client;
    tls12_aes128_gcm_record_layer_t server;
    uint8_t record1[128];
    uint8_t record2[128];
    uint8_t plaintext_out[32];
    size_t record1_len = 0u;
    size_t record2_len = 0u;
    size_t plaintext_len = 0u;
    uint8_t content_type = 0u;
    static const uint8_t msg1[] = {'h','e','l','l','o'};
    static const uint8_t msg2[] = {'w','o','r','l','d','!'};

    for (size_t i = 0; i < sizeof(key_block); ++i) key_block[i] = (uint8_t)(0x40u + i);
    ASSERT_EQ_INT(0, tls12_split_key_block(key_block, sizeof(key_block), &layout, &view));
    ASSERT_EQ_INT(0, tls12_aes128_gcm_record_layer_configure_from_key_block(&client, TLS12_ENDPOINT_CLIENT, 0x0303, &view));
    ASSERT_EQ_INT(0, tls12_aes128_gcm_record_layer_configure_from_key_block(&server, TLS12_ENDPOINT_SERVER, 0x0303, &view));
    ASSERT_EQ_INT(TLS12_ENDPOINT_CLIENT, client.role);
    ASSERT_EQ_INT(TLS12_ENDPOINT_SERVER, server.role);

    ASSERT_EQ_INT(0, tls12_aes128_gcm_record_layer_protect(&client,
                                                           0x17,
                                                           msg1,
                                                           sizeof(msg1),
                                                           record1,
                                                           sizeof(record1),
                                                           &record1_len));
    ASSERT_EQ_SIZE(1u, client.next_write_sequence);
    ASSERT_EQ_INT(0, tls12_aes128_gcm_record_layer_unprotect(&server,
                                                             record1,
                                                             record1_len,
                                                             plaintext_out,
                                                             sizeof(plaintext_out),
                                                             &plaintext_len,
                                                             &content_type));
    ASSERT_EQ_SIZE(1u, server.next_read_sequence);
    ASSERT_EQ_INT(0x17, content_type);
    ASSERT_EQ_SIZE(sizeof(msg1), plaintext_len);
    assert_bytes_eq(msg1, plaintext_out, sizeof(msg1));

    ASSERT_EQ_INT(0, tls12_aes128_gcm_record_layer_protect(&client,
                                                           0x17,
                                                           msg2,
                                                           sizeof(msg2),
                                                           record2,
                                                           sizeof(record2),
                                                           &record2_len));
    ASSERT_EQ_SIZE(2u, client.next_write_sequence);

    record2[record2_len - 1u] ^= 0x01u;
    ASSERT_EQ_INT(-2, tls12_aes128_gcm_record_layer_unprotect(&server,
                                                              record2,
                                                              record2_len,
                                                              plaintext_out,
                                                              sizeof(plaintext_out),
                                                              &plaintext_len,
                                                              &content_type));
    ASSERT_EQ_SIZE(1u, server.next_read_sequence);
    record2[record2_len - 1u] ^= 0x01u;

    ASSERT_EQ_INT(0, tls12_aes128_gcm_record_layer_unprotect(&server,
                                                             record2,
                                                             record2_len,
                                                             plaintext_out,
                                                             sizeof(plaintext_out),
                                                             &plaintext_len,
                                                             &content_type));
    ASSERT_EQ_SIZE(2u, server.next_read_sequence);
    ASSERT_EQ_SIZE(sizeof(msg2), plaintext_len);
    assert_bytes_eq(msg2, plaintext_out, sizeof(msg2));
}

static void test_hkdf_sha256_vectors(void)
{
    uint8_t out[82];
    static const uint8_t ikm[22] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
    };
    static const uint8_t salt[13] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,
    };
    static const uint8_t info[10] = {
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,
    };
    static const uint8_t prk_expected[32] = {
        0x07,0x77,0x09,0x36,0x2c,0x2e,0x32,0xdf,0x0d,0xdc,0x3f,0x0d,0xc4,0x7b,0xba,0x63,
        0x90,0xb6,0xc7,0x3b,0xb5,0x0f,0x9c,0x31,0x22,0xec,0x84,0x4a,0xd7,0xc2,0xb3,0xe5,
    };
    static const uint8_t okm_expected[42] = {
        0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a,
        0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,0xec,0xc4,0xc5,0xbf,
        0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65,
    };

    ASSERT_EQ_INT(0, tls_hkdf_sha256_extract(salt, sizeof(salt), ikm, sizeof(ikm), out));
    assert_bytes_eq(prk_expected, out, sizeof(prk_expected));

    memset(out, 0, sizeof(out));
    ASSERT_EQ_INT(0, tls_hkdf_sha256_expand(prk_expected, info, sizeof(info), out, 42));
    assert_bytes_eq(okm_expected, out, sizeof(okm_expected));
}

int main(void)
{
    UNIT_TEST_RUN(test_sha256_vectors);
    UNIT_TEST_RUN(test_hmac_sha256_vector);
    UNIT_TEST_RUN(test_tls12_prf_sha256_vectors);
    UNIT_TEST_RUN(test_tls12_finished_verify_data);
    UNIT_TEST_RUN(test_tls12_handshake_transcript);
    UNIT_TEST_RUN(test_tls12_aead_record_helpers);
    UNIT_TEST_RUN(test_aes128_and_gcm_vectors);
    UNIT_TEST_RUN(test_tls12_aes128_gcm_record_protection);
    UNIT_TEST_RUN(test_tls12_aes128_gcm_wire_record_protection);
    UNIT_TEST_RUN(test_tls12_aes128_gcm_record_layer_context_roundtrip);
    UNIT_TEST_RUN(test_hkdf_sha256_vectors);
    return unit_test_finish();
}
