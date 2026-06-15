/* ============================================================
 * openos - argv regression test
 * ============================================================ */

#include "openos.h"

void _start(int argc, char **argv)
{
    openos_write_str("[argtest] checking argc/argv...\n");

    if (argc != 3)
        openos_fail(1, "[argtest] argc mismatch\n");
    if (!argv)
        openos_fail(2, "[argtest] argv is null\n");
    if (!argv[0] || openos_strcmp(argv[0], "argtest") != 0)
        openos_fail(3, "[argtest] argv[0] mismatch\n");
    if (!argv[1] || openos_strcmp(argv[1], "alpha") != 0)
        openos_fail(4, "[argtest] argv[1] mismatch\n");
    if (!argv[2] || openos_strcmp(argv[2], "beta") != 0)
        openos_fail(5, "[argtest] argv[2] mismatch\n");
    if (argv[3] != 0)
        openos_fail(6, "[argtest] argv terminator mismatch\n");

    openos_write_str("[argtest] argv ok\n");

    char *child_argv[] = { "envtest", "alpha", "beta", 0 };
    char *child_envp[] = { "USER=openos", "HOME=/", 0 };
    int pid = openos_spawn_env("/bin/envtest", child_argv, child_envp);
    if (pid < 0)
        openos_fail(7, "[argtest] spawn_env failed\n");

    int status = 0;
    int waited = openos_waitpid(pid, &status, 0);
    if (waited != pid)
        openos_fail(8, "[argtest] waitpid envtest failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        openos_fail(9, "[argtest] envtest exit status mismatch\n");

    openos_write_str("[argtest] spawn_env ok\n");
    openos_exit(0);
}
