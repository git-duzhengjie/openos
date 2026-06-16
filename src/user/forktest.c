/* ============================================================
 * openos - fork/address-space regression test
 * ============================================================ */

#include "openos.h"

static volatile int fork_shared_probe = 1234;

void _start(void)
{
    int status = -1;
    int pid;
    int waited;

    openos_write_str("[forktest] checking fork return/status/address copy...\n");

    pid = openos_fork();
    if (pid < 0)
        openos_fail(1, "[forktest] fork failed\n");

    if (pid == 0) {
        fork_shared_probe = 5678;
        if (fork_shared_probe != 5678)
            openos_exit(11);
        openos_exit(7);
    }

    if (fork_shared_probe != 1234)
        openos_fail(2, "[forktest] child write leaked into parent address space\n");

    waited = openos_waitpid(pid, &status, 0);
    if (waited != pid)
        openos_fail(3, "[forktest] waitpid(child) failed\n");

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 7)
        openos_fail(4, "[forktest] child exit status mismatch\n");

    if (fork_shared_probe != 1234)
        openos_fail(5, "[forktest] parent data changed after wait\n");

    openos_write_str("[forktest] fork ok\n");
    openos_exit(0);
}
