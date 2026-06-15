/* ============================================================
 * openos - cat user command
 * ============================================================ */

#include "openos.h"

#define CAT_BUFFER_SIZE 128

static int cat_fd(int fd, const char *label, int close_fd)
{
    char buf[CAT_BUFFER_SIZE];
    int n;

    for (;;) {
        n = openos_read(fd, buf, sizeof(buf));
        if (n < 0) {
            openos_write_str("cat: read failed ");
            openos_write_str(label);
            openos_write_str("\n");
            if (close_fd)
                openos_close(fd);
            return -1;
        }
        if (n == 0)
            break;
        openos_write_fd(STDOUT_FILENO, buf, n);
    }

    if (close_fd && openos_close(fd) < 0) {
        openos_write_str("cat: close failed ");
        openos_write_str(label);
        openos_write_str("\n");
        return -1;
    }

    return 0;
}

static int cat_file(const char *path)
{
    int fd;

    fd = openos_open(path, O_RDONLY, 0);
    if (fd < 0) {
        openos_write_str("cat: cannot open ");
        openos_write_str(path);
        openos_write_str("\n");
        return -1;
    }

    return cat_fd(fd, path, 1);
}

void _start(int argc, char **argv, char **envp)
{
    int i;
    int failed = 0;

    (void)envp;

    if (argc <= 1) {
        openos_exit(cat_fd(STDIN_FILENO, "stdin", 0) < 0 ? 1 : 0);
    }

    for (i = 1; i < argc; i++) {
        if (cat_file(argv[i]) < 0)
            failed = 1;
    }

    openos_exit(failed ? 1 : 0);
}
