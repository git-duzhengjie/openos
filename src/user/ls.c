/* ============================================================
 * openos - ls user command
 * ============================================================ */

#include "openos.h"

static const char *openos_basename(const char *path)
{
    int i;
    int last = 0;

    if (!path || !path[0])
        return path;

    for (i = 0; path[i]; i++) {
        if (path[i] == '/' && path[i + 1])
            last = i + 1;
    }
    return path + last;
}

static void ls_print_entry(const openos_dirent_t *de)
{
    if (!de)
        return;

    openos_write_str(de->name);
    if ((de->mode & FS_DIR) == FS_DIR)
        openos_write_str("/");
    openos_write_str("\n");
}

static int ls_list_dir(const char *path)
{
    openos_DIR *dir;
    openos_dirent_t *de;

    dir = openos_opendir(path);
    if (!dir)
        return -1;

    while ((de = openos_readdir(dir)) != 0)
        ls_print_entry(de);

    return openos_closedir(dir);
}

static int ls_one(const char *path)
{
    openos_stat_t st;

    if (!path || !path[0])
        path = ".";

    if (openos_stat(path, &st) < 0) {
        openos_write_str("ls: cannot access ");
        openos_write_str(path);
        openos_write_str("\n");
        return -1;
    }

    if ((st.mode & FS_DIR) == FS_DIR)
        return ls_list_dir(path);

    openos_write_str(openos_basename(path));
    openos_write_str("\n");
    return 0;
}

void _start(int argc, char **argv, char **envp)
{
    int i;
    int failed = 0;

    (void)envp;

    if (argc <= 1) {
        if (ls_one(".") < 0)
            failed = 1;
    } else {
        for (i = 1; i < argc; i++) {
            if (argc > 2) {
                if (i > 1)
                    openos_write_str("\n");
                openos_write_str(argv[i]);
                openos_write_str(":\n");
            }
            if (ls_one(argv[i]) < 0)
                failed = 1;
        }
    }

    openos_exit(failed ? 1 : 0);
}
