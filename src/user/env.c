#include "openos.h"

static void env_usage(void)
{
    openos_write_fd(STDOUT_FILENO, "usage: env [NAME=VALUE...]\n", 27);
    openos_write_fd(STDOUT_FILENO, "       env --help\n", 17);
}

static int env_is_assignment(const char *s)
{
    int i;

    if (!s || !s[0] || s[0] == '=')
        return 0;

    for (i = 0; s[i]; i++) {
        if (s[i] == '=')
            return 1;
    }

    return 0;
}

static void env_print_line(const char *s)
{
    openos_write_fd(STDOUT_FILENO, s, openos_strlen(s));
    openos_write_fd(STDOUT_FILENO, "\n", 1);
}

void _start(int argc, char **argv, char **envp)
{
    int i;

    if (argc > 1 && openos_strcmp(argv[1], "--help") == 0) {
        env_usage();
        openos_exit(0);
    }

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (!env_is_assignment(argv[i])) {
                openos_write_fd(STDERR_FILENO, "env: unsupported argument ", 26);
                openos_write_fd(STDERR_FILENO, argv[i], openos_strlen(argv[i]));
                openos_write_fd(STDERR_FILENO, "\n", 1);
                env_usage();
                openos_exit(1);
            }
        }
    }

    if (envp) {
        for (i = 0; envp[i]; i++)
            env_print_line(envp[i]);
    }

    for (i = 1; i < argc; i++)
        env_print_line(argv[i]);

    openos_exit(0);
}
