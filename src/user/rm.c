#include "openos.h"

static void rm_usage(void)
{
    openos_write_str("usage: rm file...\n");
}

static int rm_one(const char *path)
{
    openos_stat_t st;

    if (!path || path[0] == '\0') {
        openos_write_str("rm: invalid path\n");
        return -1;
    }

    if (openos_lstat(path, &st) == 0 && (st.mode & FS_DIR) == FS_DIR) {
        openos_write_str("rm: cannot remove directory ");
        openos_write_str(path);
        openos_write_str("\n");
        return -1;
    }

    if (openos_unlink(path) < 0) {
        openos_write_str("rm: cannot remove ");
        openos_write_str(path);
        openos_write_str("\n");
        return -1;
    }

    return 0;
}

void _start(int argc, char **argv, char **envp)
{
    int i;
    int failed = 0;

    (void)envp;

    if (argc < 2) {
        rm_usage();
        openos_exit(1);
    }

    for (i = 1; i < argc; i++) {
        if (rm_one(argv[i]) < 0)
            failed = 1;
    }

    openos_exit(failed ? 1 : 0);
}
