/* ============================================================
 * openos - grep user command
 * ============================================================ */

#include "openos.h"

#define GREP_BUFFER_SIZE 128
#define GREP_LINE_SIZE   256

typedef struct grep_context {
    const char *pattern;
    char line[GREP_LINE_SIZE];
    int line_len;
    int matched;
    int failed;
} grep_context_t;

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

static void grep_emit_line(grep_context_t *ctx)
{
    if (grep_contains(ctx->line, ctx->line_len, ctx->pattern)) {
        openos_write_fd(STDOUT_FILENO, ctx->line, ctx->line_len);
        openos_write_fd(STDOUT_FILENO, "\n", 1);
        ctx->matched = 1;
    }
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

    ctx->line_len = 0;
    return grep_fd(ctx, fd, path, 1);
}

void _start(int argc, char **argv, char **envp)
{
    grep_context_t ctx;
    int i;
    int failed = 0;

    (void)envp;

    if (argc < 2) {
        openos_write_fd(STDERR_FILENO, "usage: grep PATTERN [FILE...]\n", 29);
        openos_exit(2);
    }

    ctx.pattern = argv[1];
    ctx.line_len = 0;
    ctx.matched = 0;
    ctx.failed = 0;

    if (argc == 2) {
        if (grep_fd(&ctx, STDIN_FILENO, "stdin", 0) < 0)
            failed = 1;
    } else {
        for (i = 2; i < argc; i++) {
            if (grep_file(&ctx, argv[i]) < 0)
                failed = 1;
        }
    }

    if (failed)
        openos_exit(2);
    openos_exit(ctx.matched ? 0 : 1);
}
