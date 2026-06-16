/* ============================================================
 * openos - user AI command
 * ============================================================ */

#include "openos.h"

#define AI_CMD_RESPONSE_MAX 512
#define AI_CMD_PROMPT_MAX   384

static void ai_usage(void)
{
    openos_write_str("usage: ai <prompt>\n");
}

void _start(int argc, char **argv, char **envp)
{
    char prompt[AI_CMD_PROMPT_MAX];
    char response[AI_CMD_RESPONSE_MAX];
    unsigned int used;
    int i;
    int ret;

    (void)envp;

    if (argc < 2) {
        ai_usage();
        openos_exit(1);
    }

    prompt[0] = '\0';
    used = 0;
    for (i = 1; i < argc; i++) {
        unsigned int len = openos_strlen(argv[i]);
        if (used && used + 1 < sizeof(prompt))
            prompt[used++] = ' ';
        if (used + len >= sizeof(prompt))
            len = sizeof(prompt) - used - 1;
        if (len > 0) {
            openos_memcpy(prompt + used, argv[i], len);
            used += len;
        }
        prompt[used] = '\0';
        if (used + 1 >= sizeof(prompt))
            break;
    }

    ret = openos_ai_request(prompt, response, sizeof(response));
    if (ret < 0) {
        openos_write_str("ai: request failed\n");
        openos_exit(1);
    }

    openos_write_str(response);
    openos_write_str("\n");
    openos_exit(0);
}
