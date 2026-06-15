/* ============================================================
 * openos - spawn/waitpid regression test
 * ============================================================ */

#define SYS_EXIT        1
#define SYS_GETPID      20
#define SYS_WRITE       64
#define SYS_WAITPID     223
#define SYS_SPAWN       233
#define WNOHANG         1

#define WIFEXITED(status)      (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)    (((status) >> 8) & 0xff)

static int syscall3(int num, int a, int b, int c)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c)
        : "memory"
    );
    return ret;
}

static int strlen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void write_str(const char *s)
{
    syscall3(SYS_WRITE, 1, (int)s, strlen(s));
}

static int spawn(const char *path)
{
    return syscall3(SYS_SPAWN, (int)path, 0, 0);
}

static int waitpid(int pid, int *status, int options)
{
    return syscall3(SYS_WAITPID, pid, (int)status, options);
}

static void fail(int code, const char *msg)
{
    write_str(msg);
    syscall3(SYS_EXIT, code, 0, 0);
}

void _start(void)
{
    int status = -1;
    int child;
    int child2;
    int waited;
    int self;

    write_str("[waittest] checking spawn error path...\n");
    child = spawn("/bin/does-not-exist");
    if (child >= 0)
        fail(1, "[waittest] missing executable unexpectedly spawned\n");

    write_str("[waittest] checking waitpid invalid options...\n");
    waited = waitpid(-1, &status, 2);
    if (waited >= 0)
        fail(2, "[waittest] invalid options unexpectedly accepted\n");

    write_str("[waittest] checking waitpid invalid pid...\n");
    waited = waitpid(-2, &status, WNOHANG);
    if (waited >= 0)
        fail(3, "[waittest] invalid pid unexpectedly accepted\n");

    write_str("[waittest] checking waitpid missing pid...\n");
    waited = waitpid(9999, &status, WNOHANG);
    if (waited >= 0)
        fail(4, "[waittest] missing pid unexpectedly waited\n");

    write_str("[waittest] checking waitpid non-child pid...\n");
    self = syscall3(SYS_GETPID, 0, 0, 0);
    waited = waitpid(self, &status, WNOHANG);
    if (waited >= 0)
        fail(5, "[waittest] non-child pid unexpectedly waited\n");

    write_str("[waittest] checking waitpid(-1) without children...\n");
    waited = waitpid(-1, &status, WNOHANG);
    if (waited >= 0)
        fail(6, "[waittest] waitpid(-1) unexpectedly found child\n");

    write_str("[waittest] checking WNOHANG legal child path...\n");
    status = -1;
    child = spawn("/bin/hello");
    if (child < 0)
        fail(7, "[waittest] spawn /bin/hello failed\n");
    waited = waitpid(child, &status, WNOHANG);
    if (waited < 0)
        fail(8, "[waittest] waitpid WNOHANG failed\n");
    if (waited == 0)
        waited = waitpid(child, &status, 0);
    if (waited != child)
        fail(9, "[waittest] waitpid after WNOHANG failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        fail(10, "[waittest] WNOHANG child status failed\n");

    write_str("[waittest] spawning /bin/exit42 and waiting with pid...\n");
    status = -1;
    child = spawn("/bin/exit42");
    if (child < 0)
        fail(11, "[waittest] spawn /bin/exit42 failed\n");

    waited = waitpid(child, &status, 0);
    if (waited != child)
        fail(12, "[waittest] waitpid(child) failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        fail(13, "[waittest] exit status encoding failed\n");

    waited = waitpid(child, &status, WNOHANG);
    if (waited >= 0)
        fail(14, "[waittest] reaped pid unexpectedly waited again\n");

    write_str("[waittest] checking waitpid(-1) any-child path...\n");
    status = -1;
    child = spawn("/bin/exit42");
    if (child < 0)
        fail(15, "[waittest] first any-child spawn failed\n");
    child2 = spawn("/bin/exit42");
    if (child2 < 0)
        fail(16, "[waittest] second any-child spawn failed\n");

    waited = waitpid(-1, &status, 0);
    if (waited != child && waited != child2)
        fail(17, "[waittest] waitpid(-1) failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        fail(18, "[waittest] waitpid(-1) status failed\n");

    waited = waitpid(-1, &status, 0);
    if (waited != child && waited != child2)
        fail(19, "[waittest] waitpid(-1) second child failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        fail(20, "[waittest] waitpid(-1) second status failed\n");

    write_str("[waittest] waitpid ok\n");
    syscall3(SYS_EXIT, 0, 0, 0);
}
