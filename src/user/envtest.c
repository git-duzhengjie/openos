/* ============================================================
 * openos - envp regression test
 * ============================================================ */

#define SYS_EXIT      1
#define SYS_WRITE     64
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

void _start(int argc, char **argv, char **envp)
{
    write_str("[envtest] checking argc/argv/envp...\n");

    if (argc != 3)
        fail(1, "[envtest] argc mismatch\n");
    if (!argv || !argv[0] || strcmp(argv[0], "envtest") != 0)
        fail(2, "[envtest] argv[0] mismatch\n");
    if (!argv[1] || strcmp(argv[1], "alpha") != 0)
        fail(3, "[envtest] argv[1] mismatch\n");
    if (!argv[2] || strcmp(argv[2], "beta") != 0)
        fail(4, "[envtest] argv[2] mismatch\n");
    if (argv[3] != 0)
        fail(5, "[envtest] argv terminator mismatch\n");

    if (!envp)
        fail(6, "[envtest] envp is null\n");
    if (!envp[0] || strcmp(envp[0], "USER=openos") != 0)
        fail(7, "[envtest] envp[0] mismatch\n");
    if (!envp[1] || strcmp(envp[1], "HOME=/") != 0)
        fail(8, "[envtest] envp[1] mismatch\n");
    if (envp[2] != 0)
        fail(9, "[envtest] envp terminator mismatch\n");

    write_str("[envtest] envp ok\n");
    syscall3(SYS_EXIT, 0, 0, 0);
}
