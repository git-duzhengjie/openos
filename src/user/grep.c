/* ============================================================
 * openos - grep user command
 * ============================================================ */

#include "openos.h"

#define GREP_BUFFER_SIZE 128
#define GREP_LINE_SIZE   256

typedef struct grep_options {
    int show_line_number;
    int invert_match;
    int count_only;
} grep_options_t;

typedef struct grep_context {
    const char *pattern;
    const char *label;
    grep_options_t opts;
    char line[GREP_LINE_SIZE];
    int line_len;
    int line_no;
    int matched;
    int match_count;
    int failed;
    int show_filename;
} grep_context_t;

static void grep_usage(void)
{
    openos_write_fd(STDERR_FILENO, "usage: grep [-n] [-v] [-c] PATTERN [FILE...]\n", 45);
}

static int grep_contains(const char *text, int text_len, const char *pattern)
{
    int pattern_len = openos_strlen(pattern);
    int i;
    int j;

    if (pattern_len == 0)
        return 1;
    if (text_len < pattern_len)
        return 0;

    for (i = 0; i <= text_len - pattern_len; i++) {
        for (j = 0; j < pattern_len; j++) {
            if (text[i + j] != pattern[j])
                break;
        }
        if (j == pattern_len)
            return 1;
    }

    return 0;
}

static void grep_write_int(int value)
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

static void grep_emit_line(grep_context_t *ctx)
{
    int is_match = grep_contains(ctx->line, ctx->line_len, ctx->pattern);

    if (ctx->opts.invert_match)
        is_match = !is_match;

    if (is_match) {
        ctx->matched = 1;
        ctx->match_count++;

        if (!ctx->opts.count_only) {
            if (ctx->show_filename && ctx->label) {
                openos_write_fd(STDOUT_FILENO, ctx->label, openos_strlen(ctx->label));
                openos_write_fd(STDOUT_FILENO, ":", 1);
            }
            if (ctx->opts.show_line_number) {
                grep_write_int(ctx->line_no);
                openos_write_fd(STDOUT_FILENO, ":", 1);
            }
            openos_write_fd(STDOUT_FILENO, ctx->line, ctx->line_len);
            openos_write_fd(STDOUT_FILENO, "\n", 1);
        }
    }

    ctx->line_no++;
    ctx->line_len = 0;
}

static void grep_push_char(grep_context_t *ctx, char ch)
{
    if (ch == '\n') {
        grep_emit_line(ctx);
        return;
    }

    if (ctx->line_len < GREP_LINE_SIZE - 1) {
        ctx->line[ctx->line_len++] = ch;
        return;
    }

    ctx->failed = 1;
    grep_emit_line(ctx);
}

static int grep_fd(grep_context_t *ctx, int fd, const char *label, int close_fd)
{
    char buf[GREP_BUFFER_SIZE];
    int n;
    int i;

    ctx->label = label;
    ctx->line_len = 0;
    ctx->line_no = 1;
    ctx->match_count = 0;

    for (;;) {
        n = openos_read(fd, buf, sizeof(buf));
        if (n < 0) {
            openos_write_fd(STDERR_FILENO, "grep: read failed ", 18);
            openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
            openos_write_fd(STDERR_FILENO, "\n", 1);
            if (close_fd)
                openos_close(fd);
            return -1;
        }
        if (n == 0)
            break;

        for (i = 0; i < n; i++)
            grep_push_char(ctx, buf[i]);
    }

    if (ctx->line_len > 0)
        grep_emit_line(ctx);

    if (ctx->opts.count_only) {
        if (ctx->show_filename && label) {
            openos_write_fd(STDOUT_FILENO, label, openos_strlen(label));
            openos_write_fd(STDOUT_FILENO, ":", 1);
        }
        grep_write_int(ctx->match_count);
        openos_write_fd(STDOUT_FILENO, "\n", 1);
    }

    if (close_fd && openos_close(fd) < 0) {
        openos_write_fd(STDERR_FILENO, "grep: close failed ", 19);
        openos_write_fd(STDERR_FILENO, label, openos_strlen(label));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return ctx->failed ? -1 : 0;
}

static int grep_file(grep_context_t *ctx, const char *path)
{
    int fd;

    fd = openos_open(path, O_RDONLY, 0);
    if (fd < 0) {
        openos_write_fd(STDERR_FILENO, "grep: cannot open ", 18);
        openos_write_fd(STDERR_FILENO, path, openos_strlen(path));
        openos_write_fd(STDERR_FILENO, "\n", 1);
        return -1;
    }

    return grep_fd(ctx, fd, path, 1);
}

static int grep_parse_options(int argc, char **argv, grep_options_t *opts, int *pattern_index)
{
    int i = 1;

    opts->show_line_number = 0;
    opts->invert_match = 0;
    opts->count_only = 0;

    while (i < argc) {
        if (openos_strcmp(argv[i], "--help") == 0) {
            grep_usage();
            openos_exit(0);
        } else if (openos_strcmp(argv[i], "-n") == 0) {
            opts->show_line_number = 1;
        } else if (openos_strcmp(argv[i], "-v") == 0) {
            opts->invert_match = 1;
        } else if (openos_strcmp(argv[i], "-c") == 0) {
            opts->count_only = 1;
        } else {
            break;
        }
        i++;
    }

    if (i >= argc)
        return -1;

    *pattern_index = i;
    return 0;
}

void _start(int argc, char **argv, char **envp)
{
    grep_context_t ctx;
    int pattern_index;
    int file_start;
    int file_count;
    int i;
    int failed = 0;

    (void)envp;

    if (grep_parse_options(argc, argv, &ctx.opts, &pattern_index) < 0) {
        grep_usage();
        openos_exit(2);
    }

    ctx.pattern = argv[pattern_index];
    ctx.line_len = 0;
    ctx.line_no = 1;
    ctx.matched = 0;
    ctx.match_count = 0;
    ctx.failed = 0;
    ctx.label = 0;

    file_start = pattern_index + 1;
    file_count = argc - file_start;
    ctx.show_filename = file_count > 1;

    if (file_count <= 0) {
        ctx.show_filename = 0;
        if (grep_fd(&ctx, STDIN_FILENO, "stdin", 0) < 0)
            failed = 1;
    } else {
        for (i = file_start; i < argc; i++) {
            if (grep_file(&ctx, argv[i]) < 0)
                failed = 1;
        }
    }

    if (failed)
        openos_exit(2);
    openos_exit(ctx.matched ? 0 : 1);
}
