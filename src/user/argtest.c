/* ============================================================
 * openos - argv regression test
 * ============================================================ */

#define SYS_EXIT      1
#define SYS_WRITE     64
#define SYS_WAITPID   7
#define SYS_SPAWN_ENV 235

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
    while (s && s[n])
        n++;
    return n;
}

static int strcmp(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static void write_str(const char *s)
{
    syscall3(SYS_WRITE, 1, (int)s, strlen(s));
}

static void fail(int code, const char *msg)
{
    write_str(msg);
    syscall3(SYS_EXIT, code, 0, 0);
}

void _start(int argc, char **argv)
{
    write_str("[argtest] checking argc/argv...\n");

    if (argc != 3)
        fail(1, "[argtest] argc mismatch\n");
    if (!argv)
        fail(2, "[argtest] argv is null\n");
    if (!argv[0] || strcmp(argv[0], "argtest") != 0)
        fail(3, "[argtest] argv[0] mismatch\n");
    if (!argv[1] || strcmp(argv[1], "alpha") != 0)
        fail(4, "[argtest] argv[1] mismatch\n");
    if (!argv[2] || strcmp(argv[2], "beta") != 0)
        fail(5, "[argtest] argv[2] mismatch\n");
    if (argv[3] != 0)
        fail(6, "[argtest] argv terminator mismatch\n");

    write_str("[argtest] argv ok\n");

    char *child_argv[] = { "envtest", "alpha", "beta", 0 };
    char *child_envp[] = { "USER=openos", "HOME=/", 0 };
    int pid = syscall3(SYS_SPAWN_ENV, (int)"/bin/envtest", (int)child_argv, (int)child_envp);
    if (pid < 0)
        fail(7, "[argtest] spawn_env failed\n");

    int status = 0;
    int waited = syscall3(SYS_WAITPID, pid, (int)&status, 0);
    if (waited != pid)
        fail(8, "[argtest] waitpid envtest failed\n");
    if (status != 0)
        fail(9, "[argtest] envtest exit status mismatch\n");

    write_str("[argtest] spawn_env ok\n");
    syscall3(SYS_EXIT, 0, 0, 0);
}
