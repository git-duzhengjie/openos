/* ============================================================
 * openos - kill command
 * ============================================================ */

#include "openos.h"

static void write_fd_str(int fd, const char *s)
{
    openos_write_fd(fd, s, openos_strlen(s));
}

static void write_fd_int(int fd, int value)
{
    char buf[16];
    if (openos_itoa(value, buf, 10))
        write_fd_str(fd, buf);
}

static int parse_positive_int(const char *s, int *out)
{
    int v = 0;

    if (!s || !s[0] || !out)
        return -1;
    for (int i = 0; s[i]; i++) {
        if (!openos_isdigit(s[i]))
            return -1;
        v = v * 10 + (s[i] - '0');
        if (v <= 0)
            return -1;
    }
    *out = v;
    return 0;
}

static int parse_signal(const char *s, int *out)
{
    if (!s || !out)
        return -1;
    if (s[0] == '-')
        s++;
    if (openos_strcmp(s, "9") == 0 || openos_strcmp(s, "KILL") == 0 || openos_strcmp(s, "SIGKILL") == 0) {
        *out = 9;
        return 0;
    }
    if (openos_strcmp(s, "15") == 0 || openos_strcmp(s, "TERM") == 0 || openos_strcmp(s, "SIGTERM") == 0) {
        *out = 15;
        return 0;
    }
    if (openos_strcmp(s, "0") == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static void usage(void)
{
    write_fd_str(STDERR_FILENO, "usage: kill [-9|-15|-0] pid...\n");
}

int main(int argc, char **argv)
{
    int sig = 15;
    int argi = 1;
    int rc = 0;

    if (argc < 2) {
        usage();
        return 1;
    }

    if (argv[argi] && argv[argi][0] == '-') {
        if (parse_signal(argv[argi], &sig) != 0) {
            write_fd_str(STDERR_FILENO, "kill: unsupported signal\n");
            return 1;
        }
        argi++;
    }

    if (argi >= argc) {
        usage();
        return 1;
    }

    for (; argi < argc; argi++) {
        int pid = 0;
        if (parse_positive_int(argv[argi], &pid) != 0) {
            write_fd_str(STDERR_FILENO, "kill: invalid pid: ");
            write_fd_str(STDERR_FILENO, argv[argi]);
            write_fd_str(STDERR_FILENO, "\n");
            rc = 1;
            continue;
        }
        if (openos_kill(pid, sig) != 0) {
            write_fd_str(STDERR_FILENO, "kill: failed to kill pid ");
            write_fd_int(STDERR_FILENO, pid);
            write_fd_str(STDERR_FILENO, "\n");
            rc = 1;
        }
    }

    return rc;
}
