/* ============================================================
 * openos - envp regression test
 * ============================================================ */

#include "openos.h"

void _start(int argc, char **argv, char **envp)
{
    openos_write_str("[envtest] checking argc/argv/envp...\n");

    if (argc != 3)
        openos_fail(1, "[envtest] argc mismatch\n");
    if (!argv || !argv[0] || openos_strcmp(argv[0], "envtest") != 0)
        openos_fail(2, "[envtest] argv[0] mismatch\n");
    if (!argv[1] || openos_strcmp(argv[1], "alpha") != 0)
        openos_fail(3, "[envtest] argv[1] mismatch\n");
    if (!argv[2] || openos_strcmp(argv[2], "beta") != 0)
        openos_fail(4, "[envtest] argv[2] mismatch\n");
    if (argv[3] != 0)
        openos_fail(5, "[envtest] argv terminator mismatch\n");

    if (!envp)
        openos_fail(6, "[envtest] envp is null\n");
    if (!envp[0] || openos_strcmp(envp[0], "USER=openos") != 0)
        openos_fail(7, "[envtest] envp[0] mismatch\n");
    if (!envp[1] || openos_strcmp(envp[1], "HOME=/") != 0)
        openos_fail(8, "[envtest] envp[1] mismatch\n");
    if (envp[2] != 0)
        openos_fail(9, "[envtest] envp terminator mismatch\n");

    openos_write_str("[envtest] envp ok\n");
    openos_exit(0);
}
