#include "unit_test.h"

#include "tls_handshake.h"
#include "../../src/kernel/tls_crypto.c"
#include "../../src/kernel/tls_parser.c"
#include "../../src/kernel/tls_handshake.c"

#include <string.h>

static const uint8_t k_client_hello[] = {
    0x01, 0x00, 0x00, 0x22,
    0x03, 0x03,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f
};

static const uint8_t k_server_hello[] = {
    0x02, 0x00, 0x00, 0x26,
    0x03, 0x03,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0x00,
    0x00, 0x9c,
    0x00
};

static const uint8_t k_server_hello_with_extensions[] = {
    TLS_HANDSHAKE_SERVER_HELLO, 0x00, 0x00, 0x46,
    0x03, 0x03,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40,
    0x00,
    0x00, 0x9c,
    0x00,
    0x00, 0x1e,
    0x00, 0x17, 0x00, 0x00,
    0xff, 0x01, 0x00, 0x01, 0x00,
    0x00, 0x10, 0x00, 0x0b,
    0x00, 0x09, 0x08, 'h', 't', 't', 'p', '/', '1', '.', '1',
    0x00, 0x2b, 0x00, 0x02, 0x03, 0x03
};

static const uint8_t k_certificate[] = {
    0x0b, 0x00, 0x00, 0x0a,
    0x00, 0x00, 0x07,
    0x00, 0x00, 0x04,
    0x30, 0x82, 0x00, 0x01
};

static const uint8_t k_server_hello_done[] = {
    0x0e, 0x00, 0x00, 0x00
};

static const uint8_t k_client_key_exchange[] = {
    0x10, 0x00, 0x00, 0x06,
    0x00, 0x04,
    0x31, 0x32, 0x33, 0x34
};

static const uint8_t k_change_cipher_spec[] = { 0x01 };

static const uint8_t k_client_finished[] = {
    0x14, 0x00, 0x00, 0x0c,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb
};

static const uint8_t k_server_finished[] = {
    0x14, 0x00, 0x00, 0x0c,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5,
    0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb
};

static int contains_bytes(const uint8_t* haystack,
                          size_t haystack_len,
                          const uint8_t* needle,
                          size_t needle_len)
{
    size_t i;

    if (!haystack || !needle || needle_len == 0u || needle_len > haystack_len) {
        return 0;
    }

    for (i = 0; i <= haystack_len - needle_len; ++i) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

UNIT_TEST_CASE(build_client_hello_record)
{
    static const uint8_t expected_sni[] = {
        0x00, 0x00,
        0x00, 0x10,
        0x00, 0x0e,
        0x00,
        0x00, 0x0b,
        'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm'
    };
    static const uint8_t expected_alpn[] = {
        0x00, 0x10,
        0x00, 0x0b,
        0x00, 0x09,
        0x08, 'h', 't', 't', 'p', '/', '1', '.', '1'
    };
    uint8_t random[TLS12_CLIENT_RANDOM_SIZE];
    uint8_t record[256];
    size_t record_len = 0u;
    size_t i;
    size_t handshake_len;
    size_t record_payload_len;
    size_t ext_len_offset;
    size_t ext_len;

    for (i = 0; i < TLS12_CLIENT_RANDOM_SIZE; ++i) {
        random[i] = (uint8_t)i;
    }

    ASSERT_TRUE(tls12_build_client_hello_record("example.com",
                                                random,
                                                record,
                                                sizeof(record),
                                                &record_len));
    ASSERT_TRUE(record_len > TLS_RECORD_HEADER_SIZE + 4u);
    ASSERT_EQ_INT(TLS_CONTENT_TYPE_HANDSHAKE, record[0]);
    ASSERT_EQ_INT(0x03, record[1]);
    ASSERT_EQ_INT(0x01, record[2]);

    record_payload_len = ((size_t)record[3] << 8) | record[4];
    ASSERT_EQ_SIZE(record_len - TLS_RECORD_HEADER_SIZE, record_payload_len);
    ASSERT_EQ_INT(TLS_HANDSHAKE_CLIENT_HELLO, record[5]);

    handshake_len = ((size_t)record[6] << 16) | ((size_t)record[7] << 8) | record[8];
    ASSERT_EQ_SIZE(record_len - TLS_RECORD_HEADER_SIZE - 4u, handshake_len);
    ASSERT_EQ_INT(0x03, record[9]);
    ASSERT_EQ_INT(0x03, record[10]);
    ASSERT_EQ_INT(0, memcmp(random, record + 11, TLS12_CLIENT_RANDOM_SIZE));
    ASSERT_TRUE(contains_bytes(record, record_len, expected_sni, sizeof(expected_sni)));
    ASSERT_TRUE(contains_bytes(record, record_len, expected_alpn, sizeof(expected_alpn)));

    ext_len_offset = 5u + 4u + 2u + TLS12_CLIENT_RANDOM_SIZE + 1u + 2u + 16u + 1u + 1u;
    ext_len = ((size_t)record[ext_len_offset] << 8) | record[ext_len_offset + 1u];
    ASSERT_EQ_SIZE(record_len - ext_len_offset - 2u, ext_len);

    ASSERT_FALSE(tls12_build_client_hello_record("", random, record, sizeof(record), &record_len));
    ASSERT_FALSE(tls12_build_client_hello_record("example.com", random, record, 32u, &record_len));
}

UNIT_TEST_CASE(build_rsa_client_key_exchange_and_pre_master_secret)
{
    uint8_t random[TLS12_RSA_PRE_MASTER_RANDOM_SIZE];
    uint8_t pre_master[TLS12_RSA_PRE_MASTER_SECRET_SIZE];
    uint8_t encrypted[128];
    uint8_t message[160];
    size_t message_len = 0u;
    size_t i;

    for (i = 0; i < sizeof(random); ++i) {
        random[i] = (uint8_t)(0xa0u + (uint8_t)i);
    }
    for (i = 0; i < sizeof(encrypted); ++i) {
        encrypted[i] = (uint8_t)(0x20u + (uint8_t)i);
    }

    ASSERT_TRUE(tls12_make_rsa_pre_master_secret(TLS12_VERSION, random, pre_master));
    ASSERT_EQ_INT(0x03, pre_master[0]);
    ASSERT_EQ_INT(0x03, pre_master[1]);
    ASSERT_EQ_INT(0, memcmp(random, pre_master + 2u, sizeof(random)));
    ASSERT_FALSE(tls12_make_rsa_pre_master_secret(0x0301u, random, pre_master));

    ASSERT_TRUE(tls12_build_rsa_client_key_exchange_message(encrypted,
                                                            sizeof(encrypted),
                                                            message,
                                                            sizeof(message),
                                                            &message_len));
    ASSERT_EQ_SIZE(4u + 2u + sizeof(encrypted), message_len);
    ASSERT_EQ_INT(TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE, message[0]);
    ASSERT_EQ_INT(0, message[1]);
    ASSERT_EQ_INT(0, message[2]);
    ASSERT_EQ_INT(2u + sizeof(encrypted), message[3]);
    ASSERT_EQ_INT(0, message[4]);
    ASSERT_EQ_INT(sizeof(encrypted), message[5]);
    ASSERT_EQ_INT(0, memcmp(encrypted, message + 6u, sizeof(encrypted)));

    ASSERT_FALSE(tls12_build_rsa_client_key_exchange_message(encrypted,
                                                             0u,
                                                             message,
                                                             sizeof(message),
                                                             &message_len));
    ASSERT_FALSE(tls12_build_rsa_client_key_exchange_message(encrypted,
                                                             sizeof(encrypted),
                                                             message,
                                                             8u,
                                                             &message_len));
}

UNIT_TEST_CASE(expect_full_handshake_progression)
{
    tls12_handshake_context_t ctx;
    uint8_t hash[TLS_SHA256_DIGEST_SIZE];
    uint8_t manual_hash[TLS_SHA256_DIGEST_SIZE];
    tls12_handshake_transcript_t manual;

    tls12_handshake_context_init(&ctx);
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_INIT, ctx.state);
    ASSERT_STREQ("init", tls12_handshake_state_name(ctx.state));

    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx, k_client_hello, sizeof(k_client_hello)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_CLIENT_HELLO_SENT, ctx.state);
    ASSERT_TRUE(ctx.has_client_random);
    ASSERT_EQ_INT(0x10, ctx.client_random[0]);

    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello, sizeof(k_server_hello)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_SERVER_HELLO_RECEIVED, ctx.state);
    ASSERT_TRUE(ctx.has_server_random);
    ASSERT_EQ_INT(TLS12_VERSION, ctx.negotiated_version);
    ASSERT_EQ_INT(0x009c, ctx.cipher_suite);
    ASSERT_EQ_INT(0, ctx.compression_method);
    ASSERT_EQ_INT(0xa0, ctx.server_random[0]);

    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_certificate, sizeof(k_certificate)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_CERTIFICATE_RECEIVED, ctx.state);
    ASSERT_TRUE(ctx.has_certificate);
    ASSERT_EQ_INT(1, ctx.certificate_chain.certificate_count);
    ASSERT_EQ_INT(1, ctx.certificate_chain.stored_certificate_count);
    ASSERT_EQ_SIZE(4u, ctx.certificate_chain.certificate_lengths[0]);
    ASSERT_EQ_INT(0x30, ctx.certificate_chain.certificates[0][0]);

    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello_done, sizeof(k_server_hello_done)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_SERVER_HELLO_DONE_RECEIVED, ctx.state);

    ASSERT_TRUE(tls12_handshake_on_client_key_exchange_sent(&ctx,
                                                            k_client_key_exchange,
                                                            sizeof(k_client_key_exchange)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_CLIENT_KEY_EXCHANGE_SENT, ctx.state);

    ASSERT_TRUE(tls12_handshake_on_client_change_cipher_spec_sent(&ctx,
                                                                  k_change_cipher_spec,
                                                                  sizeof(k_change_cipher_spec)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_CLIENT_CHANGE_CIPHER_SPEC_SENT, ctx.state);
    ASSERT_TRUE(ctx.client_change_cipher_spec_sent);

    ASSERT_TRUE(tls12_handshake_on_client_finished_sent(&ctx, k_client_finished, sizeof(k_client_finished)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_CLIENT_FINISHED_SENT, ctx.state);
    ASSERT_EQ_SIZE(12u, ctx.client_finished_len);
    ASSERT_EQ_INT(0xc0, ctx.client_finished[0]);

    ASSERT_TRUE(tls12_handshake_on_server_change_cipher_spec(&ctx,
                                                             k_change_cipher_spec,
                                                             sizeof(k_change_cipher_spec)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_SERVER_CHANGE_CIPHER_SPEC_RECEIVED, ctx.state);
    ASSERT_TRUE(ctx.server_change_cipher_spec_received);

    ASSERT_TRUE(tls12_handshake_on_server_finished(&ctx, k_server_finished, sizeof(k_server_finished)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ESTABLISHED, ctx.state);
    ASSERT_EQ_SIZE(12u, ctx.server_finished_len);
    ASSERT_EQ_INT(0xd0, ctx.server_finished[0]);
    ASSERT_STREQ("established", tls12_handshake_state_name(ctx.state));

    ASSERT_TRUE(tls12_handshake_get_transcript_hash(&ctx, hash));

    tls12_handshake_transcript_init(&manual);
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&manual, k_client_hello, sizeof(k_client_hello)));
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&manual, k_server_hello, sizeof(k_server_hello)));
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&manual, k_certificate, sizeof(k_certificate)));
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&manual, k_server_hello_done, sizeof(k_server_hello_done)));
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&manual, k_client_key_exchange, sizeof(k_client_key_exchange)));
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&manual, k_client_finished, sizeof(k_client_finished)));
    ASSERT_EQ_INT(0, tls12_handshake_transcript_update(&manual, k_server_finished, sizeof(k_server_finished)));
    ASSERT_EQ_INT(0, tls12_handshake_transcript_hash_sha256(&manual, manual_hash));

    ASSERT_EQ_INT(0, memcmp(hash, manual_hash, sizeof(hash)));
}

static void fill_test_master_secret(uint8_t master_secret[TLS12_MASTER_SECRET_SIZE])
{
    size_t i;

    for (i = 0; i < TLS12_MASTER_SECRET_SIZE; ++i) {
        master_secret[i] = (uint8_t)(0x40u + (uint8_t)i);
    }
}

static void make_finished_message(uint8_t out[16], const uint8_t verify_data[TLS12_VERIFY_DATA_SIZE])
{
    out[0] = TLS_HANDSHAKE_FINISHED;
    out[1] = 0x00;
    out[2] = 0x00;
    out[3] = TLS12_VERIFY_DATA_SIZE;
    memcpy(out + 4, verify_data, TLS12_VERIFY_DATA_SIZE);
}

static void drive_until_client_ccs(tls12_handshake_context_t* ctx)
{
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(ctx, k_client_hello, sizeof(k_client_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(ctx, k_server_hello, sizeof(k_server_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(ctx, k_certificate, sizeof(k_certificate)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(ctx, k_server_hello_done, sizeof(k_server_hello_done)));
    ASSERT_TRUE(tls12_handshake_on_client_key_exchange_sent(ctx,
                                                            k_client_key_exchange,
                                                            sizeof(k_client_key_exchange)));
    ASSERT_TRUE(tls12_handshake_on_client_change_cipher_spec_sent(ctx,
                                                                  k_change_cipher_spec,
                                                                  sizeof(k_change_cipher_spec)));
}

UNIT_TEST_CASE(set_rsa_pre_master_secret_derives_master_secret)
{
    tls12_handshake_context_t ctx;
    uint8_t random[TLS12_RSA_PRE_MASTER_RANDOM_SIZE];
    uint8_t pre_master[TLS12_RSA_PRE_MASTER_SECRET_SIZE];
    uint8_t expected_master[TLS12_MASTER_SECRET_SIZE];
    size_t i;

    for (i = 0; i < sizeof(random); ++i) {
        random[i] = (uint8_t)(0x50u + (uint8_t)i);
    }

    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx, k_client_hello, sizeof(k_client_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello, sizeof(k_server_hello)));
    ASSERT_TRUE(tls12_make_rsa_pre_master_secret(TLS12_VERSION, random, pre_master));
    ASSERT_EQ_INT(0, tls12_derive_master_secret_sha256(pre_master,
                                                       sizeof(pre_master),
                                                       ctx.client_random,
                                                       ctx.server_random,
                                                       expected_master));

    ASSERT_TRUE(tls12_handshake_set_rsa_pre_master_secret(&ctx, pre_master));
    ASSERT_TRUE(ctx.has_pre_master_secret);
    ASSERT_TRUE(ctx.has_master_secret);
    ASSERT_EQ_INT(0, memcmp(pre_master, ctx.pre_master_secret, sizeof(pre_master)));
    ASSERT_EQ_INT(0, memcmp(expected_master, ctx.master_secret, sizeof(expected_master)));
    ASSERT_TRUE(tls12_handshake_derive_aes128_gcm_key_block(&ctx));

    pre_master[1] = 0x01;
    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx, k_client_hello, sizeof(k_client_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello, sizeof(k_server_hello)));
    ASSERT_FALSE(tls12_handshake_set_rsa_pre_master_secret(&ctx, pre_master));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);
}

UNIT_TEST_CASE(reject_malformed_client_key_exchange)
{
    tls12_handshake_context_t ctx;
    static const uint8_t bad_short_body[] = {
        TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE, 0x00, 0x00, 0x01, 0x00
    };
    static const uint8_t bad_vector_len[] = {
        TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE, 0x00, 0x00, 0x03, 0x00, 0x02, 0xaa
    };

    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx, k_client_hello, sizeof(k_client_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello, sizeof(k_server_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_certificate, sizeof(k_certificate)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello_done, sizeof(k_server_hello_done)));
    ASSERT_FALSE(tls12_handshake_on_client_key_exchange_sent(&ctx, bad_short_body, sizeof(bad_short_body)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);

    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx, k_client_hello, sizeof(k_client_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello, sizeof(k_server_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_certificate, sizeof(k_certificate)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello_done, sizeof(k_server_hello_done)));
    ASSERT_FALSE(tls12_handshake_on_client_key_exchange_sent(&ctx, bad_vector_len, sizeof(bad_vector_len)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);
}

UNIT_TEST_CASE(verify_finished_when_master_secret_is_available)
{
    tls12_handshake_context_t ctx;
    uint8_t master_secret[TLS12_MASTER_SECRET_SIZE];
    uint8_t verify_data[TLS12_VERIFY_DATA_SIZE];
    uint8_t client_finished[16];
    uint8_t server_finished[16];

    tls12_handshake_context_init(&ctx);
    fill_test_master_secret(master_secret);
    drive_until_client_ccs(&ctx);

    ASSERT_TRUE(tls12_handshake_set_master_secret(&ctx, master_secret));
    ASSERT_TRUE(ctx.has_master_secret);
    ASSERT_TRUE(tls12_handshake_compute_expected_client_finished(&ctx, verify_data));
    make_finished_message(client_finished, verify_data);

    ASSERT_TRUE(tls12_handshake_on_client_finished_sent(&ctx, client_finished, sizeof(client_finished)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_CLIENT_FINISHED_SENT, ctx.state);

    ASSERT_TRUE(tls12_handshake_on_server_change_cipher_spec(&ctx,
                                                             k_change_cipher_spec,
                                                             sizeof(k_change_cipher_spec)));
    ASSERT_TRUE(tls12_handshake_compute_expected_server_finished(&ctx, verify_data));
    make_finished_message(server_finished, verify_data);

    ASSERT_TRUE(tls12_handshake_on_server_finished(&ctx, server_finished, sizeof(server_finished)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ESTABLISHED, ctx.state);
}

UNIT_TEST_CASE(derive_key_block_and_configure_record_layer)
{
    tls12_handshake_context_t client_ctx;
    tls12_handshake_context_t server_ctx;
    uint8_t master_secret[TLS12_MASTER_SECRET_SIZE];
    const uint8_t plaintext[] = { 'p', 'i', 'n', 'g' };
    uint8_t record[64];
    uint8_t decrypted[16];
    size_t record_len = 0u;
    size_t decrypted_len = 0u;
    uint8_t content_type = 0u;

    tls12_handshake_context_init(&client_ctx);
    tls12_handshake_context_init(&server_ctx);
    fill_test_master_secret(master_secret);

    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&client_ctx,
                                                     k_client_hello,
                                                     sizeof(k_client_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&client_ctx,
                                                    k_server_hello,
                                                    sizeof(k_server_hello)));
    ASSERT_TRUE(tls12_handshake_set_master_secret(&client_ctx, master_secret));
    ASSERT_TRUE(tls12_handshake_derive_aes128_gcm_key_block(&client_ctx));
    ASSERT_TRUE(client_ctx.has_key_block);
    ASSERT_TRUE(tls12_handshake_configure_aes128_gcm_record_layer(&client_ctx,
                                                                  TLS12_ENDPOINT_CLIENT));
    ASSERT_TRUE(client_ctx.has_record_layer);

    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&server_ctx,
                                                     k_client_hello,
                                                     sizeof(k_client_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&server_ctx,
                                                    k_server_hello,
                                                    sizeof(k_server_hello)));
    ASSERT_TRUE(tls12_handshake_set_master_secret(&server_ctx, master_secret));
    ASSERT_TRUE(tls12_handshake_configure_aes128_gcm_record_layer(&server_ctx,
                                                                  TLS12_ENDPOINT_SERVER));
    ASSERT_TRUE(server_ctx.has_key_block);
    ASSERT_TRUE(server_ctx.has_record_layer);

    ASSERT_EQ_INT(0, memcmp(client_ctx.key_block,
                            server_ctx.key_block,
                            TLS12_AES128_GCM_KEY_BLOCK_SIZE));

    ASSERT_EQ_INT(0, tls12_aes128_gcm_record_layer_protect(&client_ctx.record_layer,
                                                           TLS_CONTENT_TYPE_APPLICATION_DATA,
                                                           plaintext,
                                                           sizeof(plaintext),
                                                           record,
                                                           sizeof(record),
                                                           &record_len));
    ASSERT_EQ_SIZE(1u, client_ctx.record_layer.next_write_sequence);

    ASSERT_EQ_INT(0, tls12_aes128_gcm_record_layer_unprotect(&server_ctx.record_layer,
                                                             record,
                                                             record_len,
                                                             decrypted,
                                                             sizeof(decrypted),
                                                             &decrypted_len,
                                                             &content_type));
    ASSERT_EQ_SIZE(sizeof(plaintext), decrypted_len);
    ASSERT_EQ_INT(0, memcmp(plaintext, decrypted, sizeof(plaintext)));
    ASSERT_EQ_INT(TLS_CONTENT_TYPE_APPLICATION_DATA, content_type);
    ASSERT_EQ_SIZE(1u, server_ctx.record_layer.next_read_sequence);
}

UNIT_TEST_CASE(reject_bad_finished_when_master_secret_is_available)
{
    tls12_handshake_context_t ctx;
    uint8_t master_secret[TLS12_MASTER_SECRET_SIZE];
    uint8_t verify_data[TLS12_VERIFY_DATA_SIZE];
    uint8_t client_finished[16];

    tls12_handshake_context_init(&ctx);
    fill_test_master_secret(master_secret);
    drive_until_client_ccs(&ctx);

    ASSERT_TRUE(tls12_handshake_set_master_secret(&ctx, master_secret));
    ASSERT_TRUE(tls12_handshake_compute_expected_client_finished(&ctx, verify_data));
    verify_data[0] ^= 0x80u;
    make_finished_message(client_finished, verify_data);

    ASSERT_FALSE(tls12_handshake_on_client_finished_sent(&ctx, client_finished, sizeof(client_finished)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);
}

UNIT_TEST_CASE(parse_server_hello_extensions)
{
    tls12_handshake_context_t ctx;

    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx,
                                                     k_client_hello,
                                                     sizeof(k_client_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx,
                                                    k_server_hello_with_extensions,
                                                    sizeof(k_server_hello_with_extensions)));
    ASSERT_EQ_INT(TLS12_VERSION, ctx.negotiated_version);
    ASSERT_EQ_INT(TLS12_CIPHER_SUITE_RSA_WITH_AES_128_GCM_SHA256, ctx.cipher_suite);
    ASSERT_EQ_INT(0, ctx.compression_method);
    ASSERT_TRUE(ctx.server_has_extended_master_secret);
    ASSERT_TRUE(ctx.server_has_renegotiation_info);
    ASSERT_TRUE(ctx.server_has_alpn);
    ASSERT_TRUE(ctx.server_has_supported_versions);
    ASSERT_EQ_INT(TLS12_VERSION, ctx.server_supported_version);
    ASSERT_EQ_SIZE(8u, ctx.server_selected_alpn_len);
    ASSERT_EQ_INT(0, memcmp(ctx.server_selected_alpn, "http/1.1", 8u));
}

UNIT_TEST_CASE(reject_invalid_server_hello_negotiation)
{
    tls12_handshake_context_t ctx;
    uint8_t mutated[sizeof(k_server_hello_with_extensions)];

    memcpy(mutated, k_server_hello_with_extensions, sizeof(mutated));
    mutated[4] = 0x03;
    mutated[5] = 0x01;
    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx,
                                                     k_client_hello,
                                                     sizeof(k_client_hello)));
    ASSERT_FALSE(tls12_handshake_on_server_handshake(&ctx, mutated, sizeof(mutated)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);

    memcpy(mutated, k_server_hello_with_extensions, sizeof(mutated));
    mutated[39] = 0x00;
    mutated[40] = 0x2f;
    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx,
                                                     k_client_hello,
                                                     sizeof(k_client_hello)));
    ASSERT_FALSE(tls12_handshake_on_server_handshake(&ctx, mutated, sizeof(mutated)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);

    memcpy(mutated, k_server_hello_with_extensions, sizeof(mutated));
    mutated[41] = 0x01;
    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx,
                                                     k_client_hello,
                                                     sizeof(k_client_hello)));
    ASSERT_FALSE(tls12_handshake_on_server_handshake(&ctx, mutated, sizeof(mutated)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);

    memcpy(mutated, k_server_hello_with_extensions, sizeof(mutated));
    mutated[72] = 0x03;
    mutated[73] = 0x04;
    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx,
                                                     k_client_hello,
                                                     sizeof(k_client_hello)));
    ASSERT_FALSE(tls12_handshake_on_server_handshake(&ctx, mutated, sizeof(mutated)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);
}

UNIT_TEST_CASE(reject_out_of_order_messages)
{
    tls12_handshake_context_t ctx;

    tls12_handshake_context_init(&ctx);
    ASSERT_FALSE(tls12_handshake_on_server_handshake(&ctx, k_server_hello, sizeof(k_server_hello)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);
}

UNIT_TEST_CASE(reject_bad_change_cipher_spec)
{
    tls12_handshake_context_t ctx;
    const uint8_t bad_ccs[] = { 0x02 };

    tls12_handshake_context_init(&ctx);
    ASSERT_TRUE(tls12_handshake_on_client_hello_sent(&ctx, k_client_hello, sizeof(k_client_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello, sizeof(k_server_hello)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_certificate, sizeof(k_certificate)));
    ASSERT_TRUE(tls12_handshake_on_server_handshake(&ctx, k_server_hello_done, sizeof(k_server_hello_done)));
    ASSERT_TRUE(tls12_handshake_on_client_key_exchange_sent(&ctx,
                                                            k_client_key_exchange,
                                                            sizeof(k_client_key_exchange)));
    ASSERT_FALSE(tls12_handshake_on_client_change_cipher_spec_sent(&ctx, bad_ccs, sizeof(bad_ccs)));
    ASSERT_EQ_INT(TLS12_HANDSHAKE_STATE_ERROR, ctx.state);
}

int main(void)
{
    UNIT_TEST_RUN(build_client_hello_record);
    UNIT_TEST_RUN(build_rsa_client_key_exchange_and_pre_master_secret);
    UNIT_TEST_RUN(expect_full_handshake_progression);
    UNIT_TEST_RUN(set_rsa_pre_master_secret_derives_master_secret);
    UNIT_TEST_RUN(reject_malformed_client_key_exchange);
    UNIT_TEST_RUN(verify_finished_when_master_secret_is_available);
    UNIT_TEST_RUN(derive_key_block_and_configure_record_layer);
    UNIT_TEST_RUN(reject_bad_finished_when_master_secret_is_available);
    UNIT_TEST_RUN(parse_server_hello_extensions);
    UNIT_TEST_RUN(reject_invalid_server_hello_negotiation);
    UNIT_TEST_RUN(reject_out_of_order_messages);
    UNIT_TEST_RUN(reject_bad_change_cipher_spec);
    return unit_test_finish();
}
