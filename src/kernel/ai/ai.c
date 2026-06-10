#include "ai.h"
#include "serial.h"
#include "string.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define AI_LOCAL_MODEL_NAME "openos-local-stub"
#define AI_CLOUD_MODEL_NAME "openos-cloud-stub"

static int g_ai_initialized = 0;
static ai_backend_type_t g_default_backend = AI_BACKEND_LOCAL;

static void ai_copy(char *dst, const char *src, uint32_t max_len)
{
    uint32_t i = 0;

    if (!dst || max_len == 0)
        return;

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    while (src[i] && i + 1 < max_len)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ai_append(char *dst, const char *src, uint32_t max_len)
{
    uint32_t pos = 0;
    uint32_t i = 0;

    if (!dst || !src || max_len == 0)
        return;

    while (dst[pos] && pos + 1 < max_len)
        pos++;

    while (src[i] && pos + 1 < max_len)
        dst[pos++] = src[i++];

    dst[pos] = '\0';
}

static uint32_t ai_estimate_tokens(const char *text)
{
    uint32_t len;

    if (!text)
        return 0;

    len = (uint32_t)strlen(text);
    return (len / 4) + 1;
}

static int ai_local_generate(const ai_request_t *request, ai_response_t *response)
{
    ai_copy(response->text, "AI(local): stub response for: ", AI_RESPONSE_MAX);
    ai_append(response->text, request->prompt, AI_RESPONSE_MAX);
    response->status = AI_STATUS_OK;
    response->tokens_used = ai_estimate_tokens(response->text);
    response->backend_used = AI_BACKEND_LOCAL;
    response->latency_ms = 1;
    return AI_STATUS_OK;
}

static int ai_cloud_generate(const ai_request_t *request, ai_response_t *response)
{
    ai_copy(response->text, "AI(cloud): cloud backend is configured as stub; request accepted: ", AI_RESPONSE_MAX);
    ai_append(response->text, request->prompt, AI_RESPONSE_MAX);
    response->status = AI_STATUS_OK;
    response->tokens_used = ai_estimate_tokens(response->text);
    response->backend_used = AI_BACKEND_CLOUD;
    response->latency_ms = 10;
    return AI_STATUS_OK;
}

static int ai_hybrid_generate(const ai_request_t *request, ai_response_t *response)
{
    ai_copy(response->text, "AI(hybrid): local precheck + cloud stub response for: ", AI_RESPONSE_MAX);
    ai_append(response->text, request->prompt, AI_RESPONSE_MAX);
    response->status = AI_STATUS_OK;
    response->tokens_used = ai_estimate_tokens(response->text);
    response->backend_used = AI_BACKEND_HYBRID;
    response->latency_ms = 12;
    return AI_STATUS_OK;
}

void ai_init(void)
{
    g_default_backend = AI_BACKEND_LOCAL;
    g_ai_initialized = 1;
}

int ai_is_initialized(void)
{
    return g_ai_initialized;
}

const char *ai_backend_name(ai_backend_type_t backend)
{
    switch (backend)
    {
    case AI_BACKEND_LOCAL:
        return "local";
    case AI_BACKEND_CLOUD:
        return "cloud";
    case AI_BACKEND_HYBRID:
        return "hybrid";
    default:
        return "unknown";
    }
}

ai_backend_type_t ai_get_default_backend(void)
{
    return g_default_backend;
}

int ai_set_default_backend(ai_backend_type_t backend)
{
    if (backend != AI_BACKEND_LOCAL &&
        backend != AI_BACKEND_CLOUD &&
        backend != AI_BACKEND_HYBRID)
        return AI_STATUS_INVALID_ARGUMENT;

    g_default_backend = backend;
    return AI_STATUS_OK;
}

int ai_parse_backend(const char *name, ai_backend_type_t *backend)
{
    if (!name || !backend)
        return AI_STATUS_INVALID_ARGUMENT;

    if (strcmp(name, "local") == 0)
        *backend = AI_BACKEND_LOCAL;
    else if (strcmp(name, "cloud") == 0)
        *backend = AI_BACKEND_CLOUD;
    else if (strcmp(name, "hybrid") == 0)
        *backend = AI_BACKEND_HYBRID;
    else
        return AI_STATUS_INVALID_ARGUMENT;

    return AI_STATUS_OK;
}

int ai_generate(const ai_request_t *request, ai_response_t *response)
{
    ai_backend_type_t backend;

    if (!g_ai_initialized)
        return AI_STATUS_NOT_INITIALIZED;
    if (!request || !response || !request->prompt)
        return AI_STATUS_INVALID_ARGUMENT;

    memset(response, 0, sizeof(*response));
    backend = request->backend_preference;

    if (backend != AI_BACKEND_LOCAL && backend != AI_BACKEND_CLOUD && backend != AI_BACKEND_HYBRID)
        backend = g_default_backend;

    if (backend == AI_BACKEND_LOCAL)
        return ai_local_generate(request, response);
    if (backend == AI_BACKEND_CLOUD)
        return ai_cloud_generate(request, response);
    if (backend == AI_BACKEND_HYBRID)
        return ai_hybrid_generate(request, response);

    return AI_STATUS_BACKEND_UNAVAILABLE;
}

void ai_print_info(void)
{
    serial_write("AI engine:\n");
    serial_write("  status: ");
    serial_write(g_ai_initialized ? "initialized" : "not initialized");
    serial_write("\n");
    serial_write("  default backend: ");
    serial_write(ai_backend_name(g_default_backend));
    serial_write("\n");
    serial_write("  local backend: available, model=");
    serial_write(AI_LOCAL_MODEL_NAME);
    serial_write("\n");
    serial_write("  cloud backend: stub, model=");
    serial_write(AI_CLOUD_MODEL_NAME);
    serial_write("\n");
    serial_write("  hybrid mode: available\n");
}
