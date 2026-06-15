/* ============================================================
 * openos - cat user command
 * ============================================================ */

#include "openos.h"

#define CAT_BUFFER_SIZE 128

static int cat_file(const char *path)
{
    char buf[CAT_BUFFER_SIZE];
    int fd;
    int n;

    fd = openos_open(path, O_RDONLY, 0);
    if (fd < 0) {
        openos_write_str("cat: cannot open ");
        openos_write_str(path);
        openos_write_str("\n");
        return -1;
    }

    for (;;) {
        n = openos_read(fd, buf, sizeof(buf));
        if (n < 0) {
            openos_write_str("cat: read failed ");
            openos_write_str(path);
            openos_write_str("\n");
            openos_close(fd);
            return -1;
        }
        if (n == 0)
            break;
        openos_write_fd(1, buf, n);
    }

    if (openos_close(fd) < 0) {
        openos_write_str("cat: close failed ");
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

    if (argc <= 1) {
        openos_write_str("usage: cat file...\n");
        openos_exit(1);
    }

    for (i = 1; i < argc; i++) {
        if (cat_file(argv[i]) < 0)
            failed = 1;
    }

    openos_exit(failed ? 1 : 0);
}
