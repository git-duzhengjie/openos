/* ============================================================
 * openos - spawn/waitpid regression test
 * ============================================================ */

#define SYS_EXIT        1
#define SYS_WRITE       64
#define SYS_WAITPID     223
#define SYS_SPAWN       233
#define WNOHANG         1

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

void _start(void)
{
    int status = -1;
    int child;
    int waited;

    write_str("[waittest] checking spawn error path...\n");
    child = spawn("/bin/does-not-exist");
    if (child >= 0) {
        write_str("[waittest] missing executable unexpectedly spawned\n");
        syscall3(SYS_EXIT, 1, 0, 0);
    }

    write_str("[waittest] spawning /bin/hello and waiting...\n");

    child = spawn("/bin/hello");
    if (child < 0) {
        write_str("[waittest] spawn failed\n");
        syscall3(SYS_EXIT, 2, 0, 0);
    }

    waited = waitpid(child, &status, WNOHANG);
    if (waited < 0) {
        write_str("[waittest] waitpid WNOHANG failed\n");
        syscall3(SYS_EXIT, 3, 0, 0);
    }

    if (waited == 0) {
        waited = waitpid(child, &status, 0);
    }
    if (waited != child) {
        write_str("[waittest] waitpid failed\n");
        syscall3(SYS_EXIT, 4, 0, 0);
    }

    write_str("[waittest] waitpid ok\n");
    syscall3(SYS_EXIT, 0, 0, 0);
}
