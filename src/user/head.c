#include "openos.h"

#define HEAD_BUF_SIZE 128
#define HEAD_DEFAULT_LINES 10

static void head_usage(void)
{
    openos_write_fd(STDERR_FILENO, "usage: head [-n lines] [file...]\n", 32);
}

static int head_write_all(const char *buf, int len)
{
    int off = 0;
    int n;

    while (off < len) {
        n = openos_write_fd(STDOUT_FILENO, buf + off, len - off);
        if (n <= 0)
            return -1;
        off += n;
    }
    return 0;
}

static int head_fd(int fd, const char *label, int close_fd, int max_lines)
{
    char buf[HEAD_BUF_SIZE];
    int n;
    int i;
    int start;
    int lines = 0;

    if (max_lines <= 0) {
        if (close_fd)
            openos_close(fd);
        return 0;
    }

    for (;;) {
        n = openos_read(fd, buf, sizeof(buf));
        if (n < 0) {
            openos_write_fd(STDERR_FILENO, "head: read failed ", 18);
            openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
            openos_write_fd(STDERR_FILENO, "\n", 1);
            if (close_fd)
                openos_close(fd);
            return -1;
        }
        if (n == 0)
            break;

        start = 0;
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                lines++;
                if (lines >= max_lines) {
                    if (head_write_all(buf + start, i - start + 1) < 0)
                        return -1;
                    if (close_fd)
                        openos_close(fd);
                    return 0;
                }
            }
        }

        if (head_write_all(buf, n) < 0) {
            if (close_fd)
                openos_close(fd);
            return -1;
        }
    }

    if (close_fd && openos_close(fd) < 0) {
        openos_write_fd(STDERR_FILENO, "head: close failed ", 19);
        openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return 0;
}

static int head_file(const char *path, int max_lines)
{
    int fd;

    fd = openos_open(path, O_RDONLY, 0);
    if (fd < 0) {
        openos_write_fd(STDERR_FILENO, "head: cannot open ", 18);
        openos_write_fd(STDERR_FILENO, path, openos_strlen(path));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return head_fd(fd, path, 1, max_lines);
}

void _start(int argc, char **argv, char **envp)
{
    int max_lines = HEAD_DEFAULT_LINES;
    int argi = 1;
    int i;
    int failed = 0;

    (void)envp;

    if (argc > 1 && openos_strcmp(argv[1], "-n") == 0) {
        if (argc <= 2) {
            head_usage();
            openos_exit(1);
        }
        max_lines = openos_atoi(argv[2]);
        if (max_lines < 0)
            max_lines = 0;
        argi = 3;
    }

    if (argc <= argi) {
        openos_exit(head_fd(STDIN_FILENO, "stdin", 0, max_lines) < 0 ? 1 : 0);
    }

    for (i = argi; i < argc; i++) {
        if (head_file(argv[i], max_lines) < 0)
            failed = 1;
    }

    openos_exit(failed ? 1 : 0);
}
