/* ============================================================
 * openos - argv regression test
 * ============================================================ */

#include "openos.h"

static int arg0_is_argtest(char **argv)
{
    return argv && argv[0] &&
           (openos_strcmp(argv[0], "argtest") == 0 ||
            openos_strcmp(argv[0], "/bin/argtest") == 0);
}

static void check_basic_args(int argc, char **argv)
{
    if (argc != 3)
        openos_fail(1, "[argtest] argc mismatch\n");
    if (!argv)
        openos_fail(2, "[argtest] argv is null\n");
    if (!arg0_is_argtest(argv))
        openos_fail(3, "[argtest] argv[0] mismatch\n");
    if (!argv[1] || openos_strcmp(argv[1], "alpha") != 0)
        openos_fail(4, "[argtest] argv[1] mismatch\n");
    if (!argv[2] || openos_strcmp(argv[2], "beta") != 0)
        openos_fail(5, "[argtest] argv[2] mismatch\n");
    if (argv[3] != 0)
        openos_fail(6, "[argtest] argv terminator mismatch\n");
}

static void check_wide_args(int argc, char **argv)
{
    static const char *expected[] = {
        "wide", "--user-data-dir=/tmp/openos-chromium-profile",
        "--disable-gpu", "--single-process", "--renderer-process-limit=1",
        "--enable-logging=stderr", "--v=1", "--js-flags=--jitless",
        "--resource-dir=/usr/share/openos/browser/pak", "--cache-dir=/tmp/openos-cache",
        "--lang=zh-CN", "--font-render-hinting=none", "--no-first-run",
        "--disable-background-networking", "--disable-dev-shm-usage",
        "--remote-debugging-port=0", "--ozone-platform=openos",
        "--enable-features=OpenOSNative", "--disable-features=SandboxedRenderer",
        "https://example.openos.local/"
    };
    int expected_count = (int)(sizeof(expected) / sizeof(expected[0]));

    if (argc != expected_count + 1)
        openos_fail(10, "[argtest] wide argc mismatch\n");
    if (!arg0_is_argtest(argv))
        openos_fail(11, "[argtest] wide argv[0] mismatch\n");
    for (int i = 0; i < expected_count; i++) {
        if (!argv[i + 1] || openos_strcmp(argv[i + 1], expected[i]) != 0)
            openos_fail(12 + i, "[argtest] wide argv mismatch\n");
    }
    if (argv[expected_count + 1] != 0)
        openos_fail(40, "[argtest] wide argv terminator mismatch\n");
}

void _start(int argc, char **argv)
{
    openos_write_str("[argtest] checking argc/argv...\n");

    if (argc > 1 && argv && argv[1] && openos_strcmp(argv[1], "wide") == 0) {
        check_wide_args(argc, argv);
        openos_write_str("[argtest] wide argv ok\n");
        openos_exit(0);
    }

    check_basic_args(argc, argv);
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
