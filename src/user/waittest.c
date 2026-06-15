/* ============================================================
 * openos - spawn/waitpid regression test
 * ============================================================ */

#include "openos.h"

void _start(void)
{
    int status = -1;
    int child;
    int child2;
    int waited;
    int self;

    openos_write_str("[waittest] checking spawn error path...\n");
    child = openos_spawn("/bin/does-not-exist", 0);
    if (child >= 0)
        openos_fail(1, "[waittest] missing executable unexpectedly spawned\n");

    openos_write_str("[waittest] checking waitpid invalid options...\n");
    waited = openos_waitpid(-1, &status, 2);
    if (waited >= 0)
        openos_fail(2, "[waittest] invalid options unexpectedly accepted\n");

    openos_write_str("[waittest] checking waitpid invalid pid...\n");
    waited = openos_waitpid(-2, &status, WNOHANG);
    if (waited >= 0)
        openos_fail(3, "[waittest] invalid pid unexpectedly accepted\n");

    openos_write_str("[waittest] checking waitpid missing pid...\n");
    waited = openos_waitpid(9999, &status, WNOHANG);
    if (waited >= 0)
        openos_fail(4, "[waittest] missing pid unexpectedly waited\n");

    openos_write_str("[waittest] checking waitpid non-child pid...\n");
    self = openos_getpid();
    waited = openos_waitpid(self, &status, WNOHANG);
    if (waited >= 0)
        openos_fail(5, "[waittest] non-child pid unexpectedly waited\n");

    openos_write_str("[waittest] checking waitpid(-1) without children...\n");
    waited = openos_waitpid(-1, &status, WNOHANG);
    if (waited >= 0)
        openos_fail(6, "[waittest] waitpid(-1) unexpectedly found child\n");

    openos_write_str("[waittest] checking WNOHANG legal child path...\n");
    status = -1;
    child = openos_spawn("/bin/hello", 0);
    if (child < 0)
        openos_fail(7, "[waittest] spawn /bin/hello failed\n");
    waited = openos_waitpid(child, &status, WNOHANG);
    if (waited < 0)
        openos_fail(8, "[waittest] waitpid WNOHANG failed\n");
    if (waited == 0)
        waited = openos_waitpid(child, &status, 0);
    if (waited != child)
        openos_fail(9, "[waittest] waitpid after WNOHANG failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        openos_fail(10, "[waittest] WNOHANG child status failed\n");

    openos_write_str("[waittest] spawning /bin/exit42 and waiting with pid...\n");
    status = -1;
    child = openos_spawn("/bin/exit42", 0);
    if (child < 0)
        openos_fail(11, "[waittest] spawn /bin/exit42 failed\n");

    waited = openos_waitpid(child, &status, 0);
    if (waited != child)
        openos_fail(12, "[waittest] waitpid(child) failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        openos_fail(13, "[waittest] exit status encoding failed\n");

    waited = openos_waitpid(child, &status, WNOHANG);
    if (waited >= 0)
        openos_fail(14, "[waittest] reaped pid unexpectedly waited again\n");

    openos_write_str("[waittest] checking waitpid(-1) any-child path...\n");
    status = -1;
    child = openos_spawn("/bin/exit42", 0);
    if (child < 0)
        openos_fail(15, "[waittest] first any-child spawn failed\n");
    child2 = openos_spawn("/bin/exit42", 0);
    if (child2 < 0)
        openos_fail(16, "[waittest] second any-child spawn failed\n");

    waited = openos_waitpid(-1, &status, 0);
    if (waited != child && waited != child2)
        openos_fail(17, "[waittest] waitpid(-1) failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        openos_fail(18, "[waittest] waitpid(-1) status failed\n");

    waited = openos_waitpid(-1, &status, 0);
    if (waited != child && waited != child2)
        openos_fail(19, "[waittest] waitpid(-1) second child failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        openos_fail(20, "[waittest] waitpid(-1) second status failed\n");

    openos_write_str("[waittest] checking orphan reparent path...\n");
    status = -1;
    child = openos_spawn("/bin/orphan", 0);
    if (child < 0)
        openos_fail(21, "[waittest] spawn /bin/orphan failed\n");

    waited = openos_waitpid(child, &status, 0);
    if (waited != child)
        openos_fail(22, "[waittest] waitpid orphan parent failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 7)
        openos_fail(23, "[waittest] orphan parent status failed\n");

    waited = openos_waitpid(child, &status, WNOHANG);
    if (waited >= 0)
        openos_fail(24, "[waittest] orphan parent waited twice\n");

    openos_write_str("[waittest] checking argv spawn path...\n");
    status = -1;
    char *argtest_argv[] = {"argtest", "alpha", "beta", 0};
    child = openos_spawn("/bin/argtest", argtest_argv);
    if (child < 0)
        openos_fail(25, "[waittest] spawn /bin/argtest failed\n");
    waited = openos_waitpid(child, &status, 0);
    if (waited != child)
        openos_fail(26, "[waittest] waitpid argtest failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        openos_fail(27, "[waittest] argtest status failed\n");

    openos_write_str("[waittest] waitpid ok\n");
    openos_exit(0);
}
