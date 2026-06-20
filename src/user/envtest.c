/* ============================================================
 * openos - envp regression test
 * ============================================================ */

#include "openos.h"

static int arg0_is_envtest(char **argv)
{
    return argv && argv[0] &&
           (openos_strcmp(argv[0], "envtest") == 0 ||
            openos_strcmp(argv[0], "/bin/envtest") == 0);
}

static void check_basic_env(int argc, char **argv, char **envp)
{
    if (argc != 3)
        openos_fail(1, "[envtest] argc mismatch\n");
    if (!arg0_is_envtest(argv))
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
}

static void check_wide_env(int argc, char **argv, char **envp)
{
    static const char *expected_env[] = {
        "USER=openos", "HOME=/", "LANG=zh_CN.UTF-8",
        "OPENOS_CAP=chromium-content", "OPENOS_MODE=capability-test",
        "CHROME_DEVEL_SANDBOX=/bin/openos-sandbox",
        "XDG_CACHE_HOME=/tmp/openos-cache", "XDG_CONFIG_HOME=/tmp/openos-config",
        "FONTCONFIG_PATH=/usr/share/fonts", "TZ=UTC"
    };
    int expected_count = (int)(sizeof(expected_env) / sizeof(expected_env[0]));

    if (argc != 4)
        openos_fail(20, "[envtest] wide argc mismatch\n");
    if (!arg0_is_envtest(argv))
        openos_fail(21, "[envtest] wide argv[0] mismatch\n");
    if (!argv[1] || openos_strcmp(argv[1], "alpha") != 0)
        openos_fail(22, "[envtest] wide argv[1] mismatch\n");
    if (!argv[2] || openos_strcmp(argv[2], "beta") != 0)
        openos_fail(23, "[envtest] wide argv[2] mismatch\n");
    if (!argv[3] || openos_strcmp(argv[3], "wide-env") != 0)
        openos_fail(24, "[envtest] wide argv[3] mismatch\n");
    if (argv[4] != 0)
        openos_fail(25, "[envtest] wide argv terminator mismatch\n");
    if (!envp)
        openos_fail(26, "[envtest] wide envp is null\n");
    for (int i = 0; i < expected_count; i++) {
        if (!envp[i] || openos_strcmp(envp[i], expected_env[i]) != 0)
            openos_fail(30 + i, "[envtest] wide envp mismatch\n");
    }
    if (envp[expected_count] != 0)
        openos_fail(50, "[envtest] wide envp terminator mismatch\n");
}

void _start(int argc, char **argv, char **envp)
{
    openos_write_str("[envtest] checking argc/argv/envp...\n");

    if (argc > 3 && argv && argv[3] && openos_strcmp(argv[3], "wide-env") == 0) {
        check_wide_env(argc, argv, envp);
        openos_write_str("[envtest] wide envp ok\n");
        openos_exit(0);
    }

    check_basic_env(argc, argv, envp);
    openos_write_str("[envtest] envp ok\n");
    openos_exit(0);
}
