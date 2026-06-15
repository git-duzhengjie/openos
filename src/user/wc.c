/* ============================================================
 * openos - wc user command
 * ============================================================ */

#include "openos.h"

#define WC_BUFFER_SIZE 128

typedef struct wc_counts {
    int lines;
    int words;
    int bytes;
} wc_counts_t;

static int wc_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f';
}

static void wc_write_int(int value)
{
    char buf[16];
    int i = 0;
    int j;

    if (value == 0) {
        openos_write_fd(STDOUT_FILENO, "0", 1);
        return;
    }

    if (value < 0) {
        openos_write_fd(STDOUT_FILENO, "-", 1);
        value = -value;
    }

    while (value > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    for (j = i - 1; j >= 0; j--)
        openos_write_fd(STDOUT_FILENO, &buf[j], 1);
}

static void wc_print_counts(const wc_counts_t *counts, const char *label)
{
    wc_write_int(counts->lines);
    openos_write_fd(STDOUT_FILENO, " ", 1);
    wc_write_int(counts->words);
    openos_write_fd(STDOUT_FILENO, " ", 1);
    wc_write_int(counts->bytes);
    if (label) {
        openos_write_fd(STDOUT_FILENO, " ", 1);
        openos_write_fd(STDOUT_FILENO, label, openos_strlen(label));
    }
    openos_write_fd(STDOUT_FILENO, "\n", 1);
}

static int wc_fd(int fd, const char *label, int close_fd, wc_counts_t *out)
{
    char buf[WC_BUFFER_SIZE];
    int n;
    int i;
    int in_word = 0;

    out->lines = 0;
    out->words = 0;
    out->bytes = 0;

    for (;;) {
        n = openos_read(fd, buf, sizeof(buf));
        if (n < 0) {
            openos_write_fd(STDERR_FILENO, "wc: read failed ", 16);
            openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
            openos_write_fd(STDERR_FILENO, "\n", 1);
            if (close_fd)
                openos_close(fd);
            return -1;
        }
        if (n == 0)
            break;

        out->bytes += n;
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n')
                out->lines++;
            if (wc_is_space(buf[i])) {
                in_word = 0;
            } else if (!in_word) {
                out->words++;
                in_word = 1;
            }
        }
    }

    if (close_fd && openos_close(fd) < 0) {
        openos_write_fd(STDERR_FILENO, "wc: close failed ", 17);
        openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return 0;
}

static int wc_file(const char *path, wc_counts_t *counts)
{
    int fd;

    fd = openos_open(path, O_RDONLY, 0);
    if (fd < 0) {
        openos_write_fd(STDERR_FILENO, "wc: cannot open ", 16);
        openos_write_fd(STDERR_FILENO, path, openos_strlen(path));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return wc_fd(fd, path, 1, counts);
}

void _start(int argc, char **argv, char **envp)
{
    wc_counts_t counts;
    wc_counts_t total;
    int i;
    int failed = 0;

    (void)envp;

    if (argc <= 1) {
        if (wc_fd(STDIN_FILENO, "stdin", 0, &counts) < 0)
            openos_exit(1);
        wc_print_counts(&counts, 0);
        openos_exit(0);
    }

    total.lines = 0;
    total.words = 0;
    total.bytes = 0;

    for (i = 1; i < argc; i++) {
        if (wc_file(argv[i], &counts) < 0) {
            failed = 1;
            continue;
        }

        wc_print_counts(&counts, argv[i]);
        total.lines += counts.lines;
        total.words += counts.words;
        total.bytes += counts.bytes;
    }

    if (argc > 2)
        wc_print_counts(&total, "total");

    openos_exit(failed ? 1 : 0);
}
