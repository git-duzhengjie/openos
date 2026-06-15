#include "openos.h"

static void rmdir_usage(void)
{
    openos_write_str("usage: rmdir dir...\n");
}

static int rmdir_one(const char *path)
{
    openos_stat_t st;

    if (!path || path[0] == '\0') {
        openos_write_str("rmdir: invalid path\n");
        return -1;
    }

    if (openos_lstat(path, &st) < 0) {
        openos_write_str("rmdir: cannot remove ");
        openos_write_str(path);
        openos_write_str("\n");
        return -1;
    }

    if ((st.mode & FS_DIR) != FS_DIR) {
        openos_write_str("rmdir: not a directory ");
        openos_write_str(path);
        openos_write_str("\n");
        return -1;
    }

    if (openos_rmdir(path) < 0) {
        openos_write_str("rmdir: cannot remove ");
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
        rmdir_usage();
        openos_exit(1);
    }

    for (i = 1; i < argc; i++) {
        if (rmdir_one(argv[i]) < 0)
            failed = 1;
    }

    openos_exit(failed ? 1 : 0);
}
