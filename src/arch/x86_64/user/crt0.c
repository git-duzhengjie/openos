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

/*
 * H.4: receive argc/argv from the ABI-defined positions on the user
 * stack (set up by crt0.S which loads them from %rsp into rdi/rsi)
 * and forward them to the user main. Programs that don't care can
 * still cast (argc, argv) to (void) just like Linux user-space.
 */
void openos64_start(int argc, char **argv) {
    int rc = openos64_main(argc, argv);
    openos64_exit(rc);
}
