/* ============================================================
 * openos - crt0/main entry regression test
 * ============================================================ */

#include "openos.h"

static int has_env(char **envp, const char *expected)
{
    int i;

    if (!envp)
        return 0;
    for (i = 0; envp[i]; i++) {
        if (openos_strcmp(envp[i], expected) == 0)
            return 1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    openos_puts("[maintest] checking crt0 main entry...");

    if (argc != 3)
        openos_fail(1, "[maintest] argc mismatch");
    if (!argv)
        openos_fail(2, "[maintest] argv null");
    if (!argv[0] || openos_strcmp(argv[0], "maintest") != 0)
        openos_fail(3, "[maintest] argv[0] mismatch");
    if (!argv[1] || openos_strcmp(argv[1], "alpha") != 0)
        openos_fail(4, "[maintest] argv[1] mismatch");
    if (!argv[2] || openos_strcmp(argv[2], "beta") != 0)
        openos_fail(5, "[maintest] argv[2] mismatch");
    if (argv[3] != 0)
        openos_fail(6, "[maintest] argv terminator mismatch");
    if (!has_env(envp, "USER=openos"))
        openos_fail(7, "[maintest] envp mismatch");

    openos_printf("[maintest] argc=%d argv1=%s env=ok\n", argc, argv[1]);
    openos_puts("[maintest] crt0 main entry ok");
    return 0;
}
