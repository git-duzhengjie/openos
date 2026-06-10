#ifndef OPENOS_AI_H
#define OPENOS_AI_H

#include "types.h"

#define AI_MODEL_NAME_MAX 64
#define AI_PROMPT_MAX     256
#define AI_RESPONSE_MAX   512

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
    AI_STATUS_BUFFER_TOO_SMALL = -4
} ai_status_t;

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
void ai_print_info(void);

#endif /* OPENOS_AI_H */
