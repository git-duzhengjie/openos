#include "openos.h"

#define SH_MAX_LINE 160
#define SH_MAX_ARGS 16

static void sh_puts(const char *s)
{
    openos_write(1, s, openos_strlen(s));
}

static void sh_prompt(void)
{
    char cwd[128];

    if (openos_getcwd(cwd, sizeof(cwd)) >= 0) {
        sh_puts(cwd);
        sh_puts(" $ ");
    } else {
        sh_puts("openos $ ");
    }
}

static int sh_readline(char *buf, int size)
{
    int pos = 0;
    char ch;

    if (!buf || size <= 1)
        return -1;

    while (pos < size - 1) {
        int n = openos_read(0, &ch, 1);
        if (n <= 0) {
            if (pos == 0)
                return n;
            break;
        }
        if (ch == '\r')
            ch = '\n';
        if (ch == '\n') {
            sh_puts("\n");
            break;
        }
        if (ch == '\b' || ch == 127) {
            if (pos > 0) {
                pos--;
                sh_puts("\b \b");
            }
            continue;
        }
        buf[pos++] = ch;
        openos_write(1, &ch, 1);
    }

    buf[pos] = 0;
    return pos;
}

static int sh_isspace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int sh_parse(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args - 1) {
        while (*p && sh_isspace(*p))
            p++;
        if (!*p || *p == '#')
            break;
        argv[argc++] = p;
        while (*p && !sh_isspace(*p))
            p++;
        if (*p) {
            *p = 0;
            p++;
        }
    }
    argv[argc] = 0;
    return argc;
}

static int sh_streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int sh_run_external(char **argv)
{
    char path[96];
    int pid;
    int status = 0;

    if (!argv[0] || argv[0][0] == 0)
        return 0;

    if (argv[0][0] == '/') {
        pid = openos_spawn(argv[0], argv);
    } else {
        int prefix_len = openos_strlen("/bin/");
        int name_len = openos_strlen(argv[0]);
        if (prefix_len + name_len >= (int)sizeof(path)) {
            sh_puts("sh: command name too long\n");
            return -1;
        }
        openos_strcpy(path, "/bin/");
        openos_strcat(path, argv[0]);
        pid = openos_spawn(path, argv);
    }

    if (pid < 0) {
        sh_puts("sh: command not found: ");
        sh_puts(argv[0]);
        sh_puts("\n");
        return -1;
    }

    if (openos_waitpid(pid, &status, 0) < 0) {
        sh_puts("sh: wait failed\n");
        return -1;
    }
    return status;
}

int main(void)
{
    char line[SH_MAX_LINE];
    char *argv[SH_MAX_ARGS];

    sh_puts("OpenOS user shell\n");

    for (;;) {
        int argc;

        sh_prompt();
        if (sh_readline(line, sizeof(line)) <= 0)
            break;
        argc = sh_parse(line, argv, SH_MAX_ARGS);
        if (argc == 0)
            continue;

        if (sh_streq(argv[0], "exit"))
            return argc > 1 ? openos_atoi(argv[1]) : 0;
        if (sh_streq(argv[0], "cd")) {
            const char *target = argc > 1 ? argv[1] : "/";
            if (openos_chdir(target) < 0) {
                sh_puts("sh: cd failed: ");
                sh_puts(target);
                sh_puts("\n");
            }
            continue;
        }
        if (sh_streq(argv[0], "pwd")) {
            char cwd[128];
            if (openos_getcwd(cwd, sizeof(cwd)) >= 0) {
                sh_puts(cwd);
                sh_puts("\n");
            } else {
                sh_puts("sh: pwd failed\n");
            }
            continue;
        }

        (void)sh_run_external(argv);
    }

    sh_puts("\n");
    return 0;
}
