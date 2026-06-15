#include "openos.h"

#define CP_BUF_SIZE 512

static void cp_usage(void)
{
    openos_write_str("usage: cp source dest\n");
}

static int cp_write_all(int fd, const char *buf, int len)
{
    int off = 0;
    int n;

    while (off < len) {
        n = openos_write_fd(fd, buf + off, len - off);
        if (n <= 0)
            return -1;
        off += n;
    }
    return 0;
}

void _start(int argc, char **argv, char **envp)
{
    char buf[CP_BUF_SIZE];
    openos_stat_t st;
    int in_fd;
    int out_fd;
    int n;
    int failed = 0;

    (void)envp;

    if (argc != 3) {
        cp_usage();
        openos_exit(1);
    }

    if (openos_lstat(argv[1], &st) < 0) {
        openos_write_str("cp: cannot stat ");
        openos_write_str(argv[1]);
        openos_write_str("\n");
        openos_exit(1);
    }
    if ((st.mode & FS_DIR) == FS_DIR) {
        openos_write_str("cp: source is directory: ");
        openos_write_str(argv[1]);
        openos_write_str("\n");
        openos_exit(1);
    }

    in_fd = openos_open(argv[1], O_RDONLY, 0);
    if (in_fd < 0) {
        openos_write_str("cp: cannot open source ");
        openos_write_str(argv[1]);
        openos_write_str("\n");
        openos_exit(1);
    }

    out_fd = openos_open(argv[2], O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (out_fd < 0) {
        openos_write_str("cp: cannot open dest ");
        openos_write_str(argv[2]);
        openos_write_str("\n");
        openos_close(in_fd);
        openos_exit(1);
    }

    while ((n = openos_read(in_fd, buf, sizeof(buf))) > 0) {
        if (cp_write_all(out_fd, buf, n) < 0) {
            openos_write_str("cp: write failed\n");
            failed = 1;
            break;
        }
    }

    if (n < 0) {
        openos_write_str("cp: read failed\n");
        failed = 1;
    }

    openos_close(out_fd);
    openos_close(in_fd);
    openos_exit(failed ? 1 : 0);
}
