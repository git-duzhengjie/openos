/* ============================================================
 * openos - kernel address protection regression test
 * ============================================================ */

#include "openos.h"

#define KERNEL_ADDR ((void *)0xC0000000u)

int main(int argc, char **argv, char **envp)
{
    char *bad = (char *)KERNEL_ADDR;
    int pipefd[2];
    char *valid_argv[] = { "hello", 0 };

    (void)argc;
    (void)argv;
    (void)envp;

    openos_puts("[kaddrtest] checking kernel address protection...");

    if (openos_write_fd(1, bad, 1) >= 0)
        openos_fail(1, "[kaddrtest] write accepted kernel buffer");

    if (openos_pipe(pipefd) < 0)
        openos_fail(2, "[kaddrtest] pipe failed");

    if (openos_write_fd(pipefd[1], "K", 1) != 1)
        openos_fail(3, "[kaddrtest] pipe write failed");

    if (openos_read(pipefd[0], bad, 1) >= 0)
        openos_fail(4, "[kaddrtest] read accepted kernel buffer");

    openos_close(pipefd[0]);
    openos_close(pipefd[1]);

    if (openos_getcwd(bad, 16) >= 0)
        openos_fail(5, "[kaddrtest] getcwd accepted kernel buffer");

    if (openos_stat("/bin/hello", (openos_stat_t *)bad) >= 0)
        openos_fail(6, "[kaddrtest] stat accepted kernel buffer");

    if (openos_spawn("/bin/hello", (char *const *)bad) >= 0)
        openos_fail(7, "[kaddrtest] spawn accepted kernel argv pointer");

    if (openos_spawn_env("/bin/hello", valid_argv, (char *const *)bad) >= 0)
        openos_fail(8, "[kaddrtest] spawn_env accepted kernel envp pointer");

    openos_puts("[kaddrtest] kernel address protection ok");
    return 0;
}
