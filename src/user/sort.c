#include "openos.h"

#define SORT_READ_BUF_SIZE 256
#define SORT_STORAGE_SIZE 8192
#define SORT_MAX_LINES 256

static char g_sort_storage[SORT_STORAGE_SIZE];
static char *g_sort_lines[SORT_MAX_LINES];
static int g_sort_storage_len = 0;
static int g_sort_line_count = 0;
static int g_sort_failed = 0;

static void sort_usage(void)
{
    openos_write_fd(STDERR_FILENO, "usage: sort [file...]\n", 22);
}

static int sort_write_all(const char *buf, int len)
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

static int sort_line_compare(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    while (*a && *b) {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca != cb)
            return (int)ca - (int)cb;
        a++;
        b++;
    }

    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int sort_add_line(const char *buf, int len)
{
    int i;

    if (g_sort_line_count >= SORT_MAX_LINES) {
        openos_write_fd(STDERR_FILENO, "sort: too many lines\n", 21);
        return -1;
    }

    if (len + 1 > SORT_STORAGE_SIZE - g_sort_storage_len) {
        openos_write_fd(STDERR_FILENO, "sort: input too large\n", 22);
        return -1;
    }

    g_sort_lines[g_sort_line_count++] = &g_sort_storage[g_sort_storage_len];
    for (i = 0; i < len; i++)
        g_sort_storage[g_sort_storage_len++] = buf[i];
    g_sort_storage[g_sort_storage_len++] = '\0';
    return 0;
}

static int sort_read_fd(int fd, const char *label, int close_fd)
{
    char read_buf[SORT_READ_BUF_SIZE];
    char line_buf[SORT_READ_BUF_SIZE];
    int line_len = 0;
    int n;
    int i;

    for (;;) {
        n = openos_read(fd, read_buf, sizeof(read_buf));
        if (n < 0) {
            openos_write_fd(STDERR_FILENO, "sort: read failed ", 18);
            openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
            openos_write_fd(STDERR_FILENO, "\n", 1);
            if (close_fd)
                openos_close(fd);
            return -1;
        }
        if (n == 0)
            break;

        for (i = 0; i < n; i++) {
            if (line_len >= (int)sizeof(line_buf) - 2) {
                openos_write_fd(STDERR_FILENO, "sort: line too long\n", 20);
                if (close_fd)
                    openos_close(fd);
                return -1;
            }
            line_buf[line_len++] = read_buf[i];
            if (read_buf[i] == '\n') {
                if (sort_add_line(line_buf, line_len) < 0) {
                    if (close_fd)
                        openos_close(fd);
                    return -1;
                }
                line_len = 0;
            }
        }
    }

    if (line_len > 0) {
        if (sort_add_line(line_buf, line_len) < 0) {
            if (close_fd)
                openos_close(fd);
            return -1;
        }
    }

    if (close_fd && openos_close(fd) < 0) {
        openos_write_fd(STDERR_FILENO, "sort: close failed ", 19);
        openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return 0;
}

static int sort_read_file(const char *path)
{
    int fd;

    fd = openos_open(path, O_RDONLY, 0);
    if (fd < 0) {
        openos_write_fd(STDERR_FILENO, "sort: cannot open ", 18);
        openos_write_fd(STDERR_FILENO, path, openos_strlen(path));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return sort_read_fd(fd, path, 1);
}

static void sort_lines(void)
{
    int i;
    int j;
    char *tmp;

    for (i = 1; i < g_sort_line_count; i++) {
        tmp = g_sort_lines[i];
        j = i - 1;
        while (j >= 0 && sort_line_compare(g_sort_lines[j], tmp) > 0) {
            g_sort_lines[j + 1] = g_sort_lines[j];
            j--;
        }
        g_sort_lines[j + 1] = tmp;
    }
}

static int sort_output_lines(void)
{
    int i;

    for (i = 0; i < g_sort_line_count; i++) {
        if (sort_write_all(g_sort_lines[i], openos_strlen(g_sort_lines[i])) < 0)
            return -1;
    }

    return 0;
}

void _start(int argc, char **argv, char **envp)
{
    int i;

    (void)envp;

    if (argc > 1 && openos_strcmp(argv[1], "--help") == 0) {
        sort_usage();
        openos_exit(0);
    }

    if (argc <= 1) {
        if (sort_read_fd(STDIN_FILENO, "stdin", 0) < 0)
            g_sort_failed = 1;
    } else {
        for (i = 1; i < argc; i++) {
            if (sort_read_file(argv[i]) < 0)
                g_sort_failed = 1;
        }
    }

    if (!g_sort_failed) {
        sort_lines();
        if (sort_output_lines() < 0)
            g_sort_failed = 1;
    }

    openos_exit(g_sort_failed ? 1 : 0);
}
