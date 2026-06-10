#ifndef OPENOS_AI_H
#define OPENOS_AI_H

#include "types.h"

#define AI_MODEL_NAME_MAX 64
#define AI_MODEL_PATH_MAX 128
#define AI_MODEL_FORMAT_MAX 16
#define AI_MODEL_CAPS_MAX 64
#define AI_PROMPT_MAX     256
#define AI_RESPONSE_MAX   512
#define AI_MAX_MODELS     8
#define AI_REPO_PATH_MAX   128
#define AI_MODEL_SHA256_HEX_MAX 65
#define AI_MODEL_SIGNATURE_MAX 129
#define AI_MODEL_SIGN_ALGO_MAX 16
#define AI_MODEL_KEY_ID_MAX 32
#define AI_MANIFEST_MAX    1024
#define AI_INDEX_MAX       1024
#define AI_TRUST_ROOT_PATH_MAX 128
#define AI_TRUST_KEY_ID_MAX 32
#define AI_TRUST_ALGO_MAX 16
#define AI_TRUST_PUBLIC_KEY_MAX 128
#define AI_TRUST_KEY_FILE_MAX 512
#define AI_MAX_TRUSTED_KEYS 8
#define AI_ED25519_PUBLIC_KEY_HEX_LEN 64
#define AI_ED25519_SIGNATURE_HEX_LEN 128

typedef enum ai_task_type {
    AI_TASK_CHAT = 0,
    AI_TASK_SUMMARIZE,
    AI_TASK_CODE,
    AI_TASK_SYSTEM_HELP
} ai_task_type_t;

typedef enum ai_backend_type {
    AI_BACKEND_LOCAL = 0,
    AI_BACKEND_CLOUD,
    AI_BACKEND_HYBRID
} ai_backend_type_t;

typedef enum ai_status {
    AI_STATUS_OK = 0,
    AI_STATUS_NOT_INITIALIZED = -1,
    AI_STATUS_INVALID_ARGUMENT = -2,
    AI_STATUS_BACKEND_UNAVAILABLE = -3,
    AI_STATUS_BUFFER_TOO_SMALL = -4,
    AI_STATUS_NOT_FOUND = -5,
    AI_STATUS_NO_SPACE = -6,
    AI_STATUS_ALREADY_EXISTS = -7,
    AI_STATUS_SECURITY_FAILED = -8,
    AI_STATUS_UNSUPPORTED = -9
} ai_status_t;

typedef struct ai_model_info {
    char name[AI_MODEL_NAME_MAX];
    char path[AI_MODEL_PATH_MAX];
    char format[AI_MODEL_FORMAT_MAX];
    char capabilities[AI_MODEL_CAPS_MAX];
    ai_backend_type_t backend;
    uint32_t context_length;
    uint32_t quant_bits;
    uint32_t loaded;
    uint32_t builtin;
    char sha256[AI_MODEL_SHA256_HEX_MAX];
    char signature[AI_MODEL_SIGNATURE_MAX];
    char sign_algo[AI_MODEL_SIGN_ALGO_MAX];
    char key_id[AI_MODEL_KEY_ID_MAX];
} ai_model_info_t;

typedef struct ai_trusted_key_info {
    char key_id[AI_TRUST_KEY_ID_MAX];
    char algorithm[AI_TRUST_ALGO_MAX];
    char public_key[AI_TRUST_PUBLIC_KEY_MAX];
    uint32_t builtin;
} ai_trusted_key_info_t;

typedef struct ai_ed25519_selftest_info {
    uint32_t executed;
    uint32_t passed;
    uint32_t positive_vectors;
    uint32_t negative_vectors;
    int last_status;
} ai_ed25519_selftest_info_t;

typedef struct ai_request {
    ai_task_type_t task_type;
    ai_backend_type_t backend_preference;
    const char *model;
    const char *prompt;
    const char *system_prompt;
    uint32_t max_tokens;
    uint32_t flags;
} ai_request_t;

typedef struct ai_response {
    int status;
    char text[AI_RESPONSE_MAX];
    uint32_t tokens_used;
    ai_backend_type_t backend_used;
    uint32_t latency_ms;
} ai_response_t;

void ai_init(void);
int ai_is_initialized(void);
const char *ai_backend_name(ai_backend_type_t backend);
ai_backend_type_t ai_get_default_backend(void);
int ai_set_default_backend(ai_backend_type_t backend);
int ai_parse_backend(const char *name, ai_backend_type_t *backend);
int ai_generate(const ai_request_t *request, ai_response_t *response);

int ai_model_register(const ai_model_info_t *model);
int ai_model_count(void);
const ai_model_info_t *ai_model_get(uint32_t index);
const ai_model_info_t *ai_model_find(const char *name);
const ai_model_info_t *ai_model_current(ai_backend_type_t backend);
int ai_model_load(const char *name);
int ai_model_unload(const char *name);
int ai_model_register_manifest(const char *manifest_text);
int ai_model_register_manifest_file(const char *path);
int ai_repo_scan_index(void);
int ai_repo_scan(void);
const char *ai_repo_path(void);
int ai_repo_set_path(const char *path);
const char *ai_trust_root_path(void);
int ai_trust_root_set_path(const char *path);
int ai_trust_key_count(void);
const ai_trusted_key_info_t *ai_trust_key_get(uint32_t index);
const ai_trusted_key_info_t *ai_trust_key_find(const char *key_id, const char *algorithm);
int ai_trust_key_register(const ai_trusted_key_info_t *key);
int ai_trust_key_load_file(const char *path);
int ai_ed25519_verify_sha256_hex(const char *public_key_hex, const char *sha256_hex, const char *signature_hex);
int ai_ed25519_selftest(ai_ed25519_selftest_info_t *info);
void ai_print_ed25519_selftest(void);
int ai_signature_verify_sha256(const char *algorithm, const ai_trusted_key_info_t *key, const char *sha256_hex, const char *signature);
void ai_print_repo(void);
void ai_print_trust(void);
void ai_print_models(void);
void ai_print_info(void);

#endif /* OPENOS_AI_H */