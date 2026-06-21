#include "openos.h"

#define V8SHELL_MAX_VARS 16
#define V8SHELL_NAME_LEN 16

typedef struct js_var {
    char name[V8SHELL_NAME_LEN];
    long value;
} js_var_t;

typedef struct js_ctx {
    const char *p;
    js_var_t vars[V8SHELL_MAX_VARS];
    int var_count;
    int errors;
} js_ctx_t;

static int js_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int js_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

static int js_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int js_alnum(char c)
{
    return js_alpha(c) || js_digit(c);
}

static void js_skip(js_ctx_t *ctx)
{
    while (js_space(*ctx->p)) {
        ctx->p++;
    }
}

static int js_match(js_ctx_t *ctx, const char *kw)
{
    const char *p = ctx->p;
    while (*kw) {
        if (*p++ != *kw++) {
            return 0;
        }
    }
    ctx->p = p;
    return 1;
}

static int js_read_ident(js_ctx_t *ctx, char *name, int cap)
{
    int n = 0;
    js_skip(ctx);
    if (!js_alpha(*ctx->p)) {
        return 0;
    }
    while (js_alnum(*ctx->p)) {
        if (n + 1 < cap) {
            name[n++] = *ctx->p;
        }
        ctx->p++;
    }
    name[n] = '\0';
    return 1;
}

static js_var_t *js_find(js_ctx_t *ctx, const char *name)
{
    int i;
    for (i = 0; i < ctx->var_count; ++i) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            return &ctx->vars[i];
        }
    }
    return 0;
}

static js_var_t *js_set(js_ctx_t *ctx, const char *name, long value)
{
    js_var_t *v = js_find(ctx, name);
    if (v) {
        v->value = value;
        return v;
    }
    if (ctx->var_count >= V8SHELL_MAX_VARS) {
        ctx->errors++;
        printf("v8_shell: too many variables\n");
        return 0;
    }
    v = &ctx->vars[ctx->var_count++];
    strncpy(v->name, name, V8SHELL_NAME_LEN - 1);
    v->name[V8SHELL_NAME_LEN - 1] = '\0';
    v->value = value;
    return v;
}

static long js_expr(js_ctx_t *ctx);

static long js_primary(js_ctx_t *ctx)
{
    long value = 0;
    int sign = 1;
    char name[V8SHELL_NAME_LEN];
    js_var_t *v;

    js_skip(ctx);
    if (*ctx->p == '-') {
        sign = -1;
        ctx->p++;
        js_skip(ctx);
    }
    if (*ctx->p == '(') {
        ctx->p++;
        value = js_expr(ctx);
        js_skip(ctx);
        if (*ctx->p == ')') {
            ctx->p++;
        } else {
            ctx->errors++;
            printf("v8_shell: missing ')'\n");
        }
        return sign * value;
    }
    if (js_digit(*ctx->p)) {
        while (js_digit(*ctx->p)) {
            value = value * 10 + (*ctx->p - '0');
            ctx->p++;
        }
        return sign * value;
    }
    if (js_read_ident(ctx, name, sizeof(name))) {
        v = js_find(ctx, name);
        if (!v) {
            ctx->errors++;
            printf("v8_shell: undefined variable %s\n", name);
            return 0;
        }
        return sign * v->value;
    }
    ctx->errors++;
    printf("v8_shell: expected expression near '%c'\n", *ctx->p ? *ctx->p : '?');
    return 0;
}

static long js_term(js_ctx_t *ctx)
{
    long value = js_primary(ctx);
    for (;;) {
        js_skip(ctx);
        if (*ctx->p == '*') {
            ctx->p++;
            value *= js_primary(ctx);
        } else if (*ctx->p == '/') {
            long rhs;
            ctx->p++;
            rhs = js_primary(ctx);
            if (rhs == 0) {
                ctx->errors++;
                printf("v8_shell: division by zero\n");
                rhs = 1;
            }
            value /= rhs;
        } else {
            return value;
        }
    }
}

static long js_expr(js_ctx_t *ctx)
{
    long value = js_term(ctx);
    for (;;) {
        js_skip(ctx);
        if (*ctx->p == '+') {
            ctx->p++;
            value += js_term(ctx);
        } else if (*ctx->p == '-') {
            ctx->p++;
            value -= js_term(ctx);
        } else {
            return value;
        }
    }
}

static void js_statement(js_ctx_t *ctx)
{
    char name[V8SHELL_NAME_LEN];
    long value;

    js_skip(ctx);
    if (*ctx->p == '\0') {
        return;
    }

    if (js_match(ctx, "print")) {
        js_skip(ctx);
        if (*ctx->p == '(') {
            ctx->p++;
            value = js_expr(ctx);
            js_skip(ctx);
            if (*ctx->p == ')') {
                ctx->p++;
            }
            printf("%ld\n", value);
        } else {
            ctx->errors++;
            printf("v8_shell: print expects '(expr)'\n");
        }
        return;
    }

    if (js_match(ctx, "let") || js_match(ctx, "var") || js_match(ctx, "const")) {
        if (!js_read_ident(ctx, name, sizeof(name))) {
            ctx->errors++;
            printf("v8_shell: declaration expects identifier\n");
            return;
        }
        js_skip(ctx);
        value = 0;
        if (*ctx->p == '=') {
            ctx->p++;
            value = js_expr(ctx);
        }
        js_set(ctx, name, value);
        return;
    }

    if (js_read_ident(ctx, name, sizeof(name))) {
        js_skip(ctx);
        if (*ctx->p == '=') {
            ctx->p++;
            value = js_expr(ctx);
            js_set(ctx, name, value);
            return;
        }
        ctx->errors++;
        printf("v8_shell: unsupported statement starting with %s\n", name);
        return;
    }

    value = js_expr(ctx);
    printf("%ld\n", value);
}

static int js_run(const char *script)
{
    js_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.p = script;
    while (*ctx.p) {
        js_statement(&ctx);
        js_skip(&ctx);
        if (*ctx.p == ';') {
            ctx.p++;
        } else if (*ctx.p) {
            ctx.errors++;
            printf("v8_shell: expected ';' near '%c'\n", *ctx.p);
            break;
        }
    }
    return ctx.errors == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *script = "let answer = 40 + 2; print(answer); print((7 + 5) * 3);";
    if (argc > 1 && argv[1]) {
        script = argv[1];
    }
    printf("v8_shell: jitless baseline JavaScript smoke\n");
    return js_run(script);
}
