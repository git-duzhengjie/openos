#include "openos64.h"

/*
 * H.3 demo: hello64_v2 -- the program that /bin/launcher execve's into.
 *
 * Deliberately a *different* binary from /bin/hello64 so we can prove:
 *   (a) the kernel really swapped the image -- output text is unique
 *   (b) the second-round ring3 entry works (no stale state from round 1)
 *   (c) SYS_EXIT after execve still tears down the proc cleanly
 *
 * We keep it minimal: print a banner, read pid/tid (should match what
 * /bin/launcher had since execve preserves pid), and exit(42).
 */

static void write_str(int fd, const char *s) {
    openos64_size_t n = openos64_strlen(s);
    (void)openos64_write(fd, s, n);
}

int openos64_main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write_str(OPENOS64_STDOUT_FILENO,
              "[hello64_v2] H.3: I am the post-execve image, pid preserved\n");

    long pid  = openos64_getpid();
    long tid  = openos64_gettid();
    long ppid = openos64_getppid();

    char line[80];
    openos64_size_t i = 0;
    const char *pfx = "[hello64_v2] pid=";
    for (const char *p = pfx; *p; ++p) line[i++] = *p;
    line[i++] = (char)('0' + (pid  & 0x7F));
    line[i++] = ' '; line[i++] = 't'; line[i++] = 'i'; line[i++] = 'd'; line[i++] = '=';
    line[i++] = (char)('0' + (tid  & 0x7F));
    line[i++] = ' '; line[i++] = 'p'; line[i++] = 'p'; line[i++] = 'i'; line[i++] = 'd'; line[i++] = '=';
    line[i++] = (char)('0' + (ppid & 0x7F));
    line[i++] = '\n';
    (void)openos64_write(OPENOS64_STDOUT_FILENO, line, i);

    write_str(OPENOS64_STDOUT_FILENO,
              "[hello64_v2] H.3: exiting with code 42\n");

    return 42;
}
