#include "openos64.h"

int openos64_main(int argc, char **argv) {
    const char msg[] = "hello from OpenOS x86_64 userland\n";
    (void)argc;
    (void)argv;
    openos64_write(OPENOS64_STDOUT_FILENO, msg, openos64_strlen(msg));
    return 0;
}
