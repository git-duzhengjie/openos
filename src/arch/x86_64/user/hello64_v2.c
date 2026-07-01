#include "openos64.h"

/*
 * H.4 demo: hello64_v2 -- the program that /bin/launcher execve's into.
 *
 * Compared to H.3 we now print the full argc/argv to the serial log so we
 * can eyeball the SysV startup frame end-to-end:
 *   kernel scratch -> seed_user_stack -> crt0.S -> openos64_main
 *
 * We keep it minimal: print banner, pid/tid/ppid (must match what
 * /bin/launcher had since execve preserves pid), dump argv, exit(42).
 */

static void write_str(int fd, const char *s) {
    openos64_size_t n = openos64_strlen(s);
    (void)openos64_write(fd, s, n);
}

static void write_dec(int fd, long v) {
    char buf[24];
    openos64_size_t n = 0;
    if (v < 0) { buf[n++] = '-'; v = -v; }
    char tmp[24];
    openos64_size_t t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    while (t > 0) buf[n++] = tmp[--t];
    (void)openos64_write(fd, buf, n);
}

int openos64_main(int argc, char **argv, char **envp) {
    write_str(OPENOS64_STDOUT_FILENO,
              "[hello64_v2] H.4: I am the post-execve image, pid preserved\n");

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

    /* H.4: print argv vector to prove SysV startup frame is intact. */
    write_str(OPENOS64_STDOUT_FILENO, "[hello64_v2] argc=");
    write_dec(OPENOS64_STDOUT_FILENO, (long)argc);
    write_str(OPENOS64_STDOUT_FILENO, "\n");
    for (int k = 0; k < argc; ++k) {
        write_str(OPENOS64_STDOUT_FILENO, "[hello64_v2] argv[");
        write_dec(OPENOS64_STDOUT_FILENO, (long)k);
        write_str(OPENOS64_STDOUT_FILENO, "]=");
        write_str(OPENOS64_STDOUT_FILENO, argv && argv[k] ? argv[k] : "(null)");
        write_str(OPENOS64_STDOUT_FILENO, "\n");
    }

    /* H.5a: print envp vector to prove SysV envp frame is intact. */
    int envc = 0;
    if (envp) {
        while (envp[envc]) ++envc;
    }
    write_str(OPENOS64_STDOUT_FILENO, "[hello64_v2] envc=");
    write_dec(OPENOS64_STDOUT_FILENO, (long)envc);
    write_str(OPENOS64_STDOUT_FILENO, "\n");
    for (int k = 0; k < envc; ++k) {
        write_str(OPENOS64_STDOUT_FILENO, "[hello64_v2] envp[");
        write_dec(OPENOS64_STDOUT_FILENO, (long)k);
        write_str(OPENOS64_STDOUT_FILENO, "]=");
        write_str(OPENOS64_STDOUT_FILENO, envp[k]);
        write_str(OPENOS64_STDOUT_FILENO, "\n");
    }

    write_str(OPENOS64_STDOUT_FILENO,
              "[hello64_v2] H.4: about to execve /bin/hello_fork (A2.P5)\n");

    /* A2.P5: hand fork/wait off to a dedicated ELF so execve and fork/wait
     * are decoupled regressions. Forward our argv/envp verbatim so the
     * second execve also exercises SysV startup frame plumbing. */
    static const char *fork_argv[] = {
        "hello_fork",
        "a2.p5",
        (const char *)0,
    };
    static const char *fork_envp[] = {
        "PATH=/bin",
        "OPENOS_STAGE=A2.P5",
        (const char *)0,
    };
    long rc = openos64_execve("/bin/hello_fork",
                              (char *const *)fork_argv,
                              (char *const *)fork_envp);

    /* Falling through means execve failed. Print diagnostic + exit 98 so it
     * is grep-able alongside launcher's 99. */
    static const char hexd[] = "0123456789abcdef";
    char errbuf[64];
    openos64_size_t bi = 0;
    const char *epfx = "[hello64_v2] ERR: execve returned rc=0x";
    for (const char *p = epfx; *p; ++p) errbuf[bi++] = *p;
    uint64_t v = (uint64_t)rc;
    for (int s = 60; s >= 0; s -= 4) errbuf[bi++] = hexd[(v >> s) & 0xF];
    errbuf[bi++] = '\n';
    (void)openos64_write(OPENOS64_STDERR_FILENO, errbuf, bi);
    return 98;
}
