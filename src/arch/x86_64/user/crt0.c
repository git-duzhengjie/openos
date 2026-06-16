#include "openos64.h"

static const openos64_runtime_info_t openos64_runtime_info = {
    1ULL,
    1ULL,
    (uint64_t)(uintptr_t)&openos64_start
};

openos64_size_t openos64_strlen(const char *text) {
    openos64_size_t len = 0;
    if (!text) {
        return 0;
    }
    while (text[len] != '\0') {
        ++len;
    }
    return len;
}

const openos64_runtime_info_t *openos64_runtime_get_info(void) {
    return &openos64_runtime_info;
}

void openos64_start(void) {
    int rc = openos64_main(0, (char **)0);
    openos64_exit(rc);
}
