#include "openos.h"

#define TEE_BUF_SIZE 512
#define TEE_MAX_FILES 8

static void tee_usage(void)
{
    openos_write_str("usage: tee file...\n");
    openos_write_str("       tee -a file...  # append is not supported yet\n");
}

static int tee_write_all(int fd, const char *buf, int len)
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
    char buf[TEE_BUF_SIZE];
    int fds[TEE_MAX_FILES];
    int file_count = 0;
    int argi = 1;
    int failed = 0;
    int n;
    int i;

    (void)envp;

    for (i = 0; i < TEE_MAX_FILES; i++)
        fds[i] = -1;

    if (argc > 1 && openos_strcmp(argv[1], "-a") == 0) {
        openos_write_str("tee: -a append mode is not supported yet\n");
        tee_usage();
        openos_exit(1);
    }

    if (argc <= argi) {
        tee_usage();
        openos_exit(1);
    }

    for (i = argi; i < argc; i++) {
        if (file_count >= TEE_MAX_FILES) {
            openos_write_str("tee: too many output files\n");
            failed = 1;
            break;
        }
        fds[file_count] = openos_open(argv[i], O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fds[file_count] < 0) {
            openos_write_str("tee: cannot open ");
            openos_write_str(argv[i]);
            openos_write_str("\n");
            failed = 1;
            continue;
        }
        file_count++;
    }

    while ((n = openos_read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        if (tee_write_all(STDOUT_FILENO, buf, n) < 0)
            failed = 1;
        for (i = 0; i < file_count; i++) {
            if (fds[i] >= 0 && tee_write_all(fds[i], buf, n) < 0) {
                openos_write_str("tee: write failed\n");
                failed = 1;
            }
        }
    }

    if (n < 0) {
        openos_write_str("tee: read failed\n");
        failed = 1;
    }

    for (i = 0; i < file_count; i++) {
        if (fds[i] >= 0 && openos_close(fds[i]) < 0)
            failed = 1;
    }

    openos_exit(failed ? 1 : 0);
}
