#include "openos.h"

static void touch_usage(void)
{
    openos_write_str("usage: touch file...\n");
}

static int touch_one(const char *path)
{
    openos_stat_t st;
    int fd;

    if (!path || path[0] == '\0') {
        openos_write_str("touch: invalid path\n");
        return -1;
    }

    if (openos_lstat(path, &st) == 0) {
        if ((st.mode & FS_DIR) == FS_DIR) {
            openos_write_str("touch: cannot touch directory ");
            openos_write_str(path);
            openos_write_str("\n");
            return -1;
        }
        return 0;
    }

    fd = openos_open(path, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        openos_write_str("touch: cannot create ");
        openos_write_str(path);
        openos_write_str("\n");
        return -1;
    }

    openos_close(fd);
    return 0;
}

void _start(int argc, char **argv, char **envp)
{
    int i;
    int failed = 0;

    (void)envp;

    if (argc < 2) {
        touch_usage();
        openos_exit(1);
    }

    for (i = 1; i < argc; i++) {
        if (touch_one(argv[i]) < 0)
            failed = 1;
    }

    openos_exit(failed ? 1 : 0);
}
