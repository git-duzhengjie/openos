#include "openos.h"

static void write_status(void)
{
    int enabled = openos_sandbox_get();
    if (enabled < 0) {
        openos_write_fd(STDERR_FILENO, "sandbox: query failed\n", 22);
        openos_exit(1);
    }
    openos_write_fd(STDOUT_FILENO, "sandbox=", 8);
    if (enabled)
        openos_write_fd(STDOUT_FILENO, "on\n", 3);
    else
        openos_write_fd(STDOUT_FILENO, "off\n", 4);
}

void _start(int argc, char **argv)
{
    if (argc <= 1) {
        write_status();
        openos_exit(0);
    }

    if (openos_strcmp(argv[1], "--help") == 0) {
        openos_write_fd(STDOUT_FILENO, "usage: sandbox [on|status]\n", 27);
        openos_exit(0);
    }

    if (openos_strcmp(argv[1], "status") == 0) {
        write_status();
        openos_exit(0);
    }

    if (openos_strcmp(argv[1], "on") == 0 || openos_strcmp(argv[1], "enable") == 0) {
        if (openos_sandbox_set(1) < 0) {
            openos_write_fd(STDERR_FILENO, "sandbox: enable failed\n", 23);
            openos_exit(1);
        }
        write_status();
        openos_exit(0);
    }

    openos_write_fd(STDERR_FILENO, "sandbox: unknown command\n", 25);
    openos_exit(1);
}
