#include "openos64.h"

/*
 * H.4 demo: launcher exercises the SYS_EXEC trampoline AND the new
 * argv plumbing.
 *
 * The kernel boots into /bin/launcher (H.3) and now spawns it with
 * argv = {"/bin/launcher"} (kernel-side, see kernel64.c). We forward
 * an extended argv vector through execve so /bin/hello64_v2 will see
 * argc>=2 and print the strings back to confirm the round-trip.
 *
 * If execve returns at all, it failed -- we print a diagnostic and exit
 * with a unique code so regressions are grep-able from the serial log.
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
              "[launcher] H.4: hello from launcher\n");

    /*
     * Print our own argv first to prove kernel-side initial-spawn argv
     * seeding works (kernel64.c calls set_args({"/bin/launcher"})).
     */
    write_str(OPENOS64_STDOUT_FILENO, "[launcher] argc=");
    write_dec(OPENOS64_STDOUT_FILENO, (long)argc);
    write_str(OPENOS64_STDOUT_FILENO, "\n");
    for (int i = 0; i < argc; ++i) {
        write_str(OPENOS64_STDOUT_FILENO, "[launcher] argv[");
        write_dec(OPENOS64_STDOUT_FILENO, (long)i);
        write_str(OPENOS64_STDOUT_FILENO, "]=");
        write_str(OPENOS64_STDOUT_FILENO, argv && argv[i] ? argv[i] : "(null)");
        write_str(OPENOS64_STDOUT_FILENO, "\n");
    }

    /* H.5a: prove kernel-side initial-spawn envp seeding works too. */
    int envc = 0;
    if (envp) while (envp[envc]) ++envc;
    write_str(OPENOS64_STDOUT_FILENO, "[launcher] envc=");
    write_dec(OPENOS64_STDOUT_FILENO, (long)envc);
    write_str(OPENOS64_STDOUT_FILENO, "\n");
    for (int i = 0; i < envc; ++i) {
        write_str(OPENOS64_STDOUT_FILENO, "[launcher] envp[");
        write_dec(OPENOS64_STDOUT_FILENO, (long)i);
        write_str(OPENOS64_STDOUT_FILENO, "]=");
        write_str(OPENOS64_STDOUT_FILENO, envp[i]);
        write_str(OPENOS64_STDOUT_FILENO, "\n");
    }

    write_str(OPENOS64_STDOUT_FILENO,
              "[launcher] H.4: about to execve /bin/hello64_v2 with argv\n");

    /*
     * Hand-rolled argv: must be a NULL-terminated array of char*.
     * Strings live in launcher's .rodata; the kernel snapshots them
     * before elf64_load_image clobbers the VA.
     */
    static const char *child_argv[] = {
        "hello64_v2",
        "from",
        "launcher",
        "H.4",
        (const char *)0,
    };

    /* H.5a: stage envp for the child so SYS_EXEC plumbs the full ABI
     * frame. We pass three string slots that hello64_v2 will echo back
     * to stdout; the kernel snapshots them just like argv. */
    static const char *child_envp[] = {
        "PATH=/bin",
        "HOME=/",
        "OPENOS_STAGE=H.5a",
        (const char *)0,
    };

    long rc = openos64_execve("/bin/hello64_v2",
                              (char *const *)child_argv,
                              (char *const *)child_envp);

    /* Reaching here means execve failed. Print rc in hex for diagnosis. */
    static const char hexd[] = "0123456789abcdef";
    char buf[64];
    openos64_size_t i = 0;
    const char *pfx = "[launcher] ERR: execve returned rc=0x";
    for (const char *p = pfx; *p; ++p) buf[i++] = *p;
    uint64_t v = (uint64_t)rc;
    for (int s = 60; s >= 0; s -= 4) buf[i++] = hexd[(v >> s) & 0xF];
    buf[i++] = '\n';
    (void)openos64_write(OPENOS64_STDERR_FILENO, buf, i);

    return 99;
}
