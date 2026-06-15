#include "openos.h"

#define TAIL_BUF_SIZE 256
#define TAIL_STORE_SIZE 8192
#define TAIL_DEFAULT_LINES 10

static void tail_usage(void)
{
    openos_write_fd(STDERR_FILENO, "usage: tail [-n lines] [file...]\n", 32);
}

static int tail_write_all(const char *buf, int len)
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

static void tail_store_append(char *store, int *store_len, const char *buf, int len)
{
    int keep;
    int drop;
    int i;

    if (len >= TAIL_STORE_SIZE) {
        for (i = 0; i < TAIL_STORE_SIZE; i++)
            store[i] = buf[len - TAIL_STORE_SIZE + i];
        *store_len = TAIL_STORE_SIZE;
        return;
    }

    if (*store_len + len > TAIL_STORE_SIZE) {
        drop = *store_len + len - TAIL_STORE_SIZE;
        keep = *store_len - drop;
        for (i = 0; i < keep; i++)
            store[i] = store[drop + i];
        *store_len = keep;
    }

    for (i = 0; i < len; i++)
        store[*store_len + i] = buf[i];
    *store_len += len;
}

static int tail_start_offset(const char *store, int store_len, int max_lines)
{
    int i;
    int lines = 0;

    if (max_lines <= 0)
        return store_len;
    if (store_len <= 0)
        return 0;

    i = store_len - 1;
    if (store[i] == '\n')
        i--;

    for (; i >= 0; i--) {
        if (store[i] == '\n') {
            lines++;
            if (lines >= max_lines)
                return i + 1;
        }
    }

    return 0;
}

static int tail_fd(int fd, const char *label, int close_fd, int max_lines)
{
    char buf[TAIL_BUF_SIZE];
    char store[TAIL_STORE_SIZE];
    int store_len = 0;
    int n;
    int start;

    for (;;) {
        n = openos_read(fd, buf, sizeof(buf));
        if (n < 0) {
            openos_write_fd(STDERR_FILENO, "tail: read failed ", 18);
            openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
            openos_write_fd(STDERR_FILENO, "\n", 1);
            if (close_fd)
                openos_close(fd);
            return -1;
        }
        if (n == 0)
            break;
        tail_store_append(store, &store_len, buf, n);
    }

    start = tail_start_offset(store, store_len, max_lines);
    if (start < store_len && tail_write_all(store + start, store_len - start) < 0) {
        if (close_fd)
            openos_close(fd);
        return -1;
    }

    if (close_fd && openos_close(fd) < 0) {
        openos_write_fd(STDERR_FILENO, "tail: close failed ", 19);
        openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return 0;
}

static int tail_file(const char *path, int max_lines)
{
    int fd;

    fd = openos_open(path, O_RDONLY, 0);
    if (fd < 0) {
        openos_write_fd(STDERR_FILENO, "tail: cannot open ", 18);
        openos_write_fd(STDERR_FILENO, path, openos_strlen(path));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return tail_fd(fd, path, 1, max_lines);
}

void _start(int argc, char **argv, char **envp)
{
    int max_lines = TAIL_DEFAULT_LINES;
    int argi = 1;
    int i;
    int failed = 0;

    (void)envp;

    if (argc > 1 && openos_strcmp(argv[1], "-n") == 0) {
        if (argc <= 2) {
            tail_usage();
            openos_exit(1);
        }
        max_lines = openos_atoi(argv[2]);
        if (max_lines < 0)
            max_lines = 0;
        argi = 3;
    }

    if (argc <= argi) {
        openos_exit(tail_fd(STDIN_FILENO, "stdin", 0, max_lines) < 0 ? 1 : 0);
    }

    for (i = argi; i < argc; i++) {
        if (tail_file(argv[i], max_lines) < 0)
            failed = 1;
    }

    openos_exit(failed ? 1 : 0);
}
