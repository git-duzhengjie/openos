#include "openos64.h"

/*
 * H.3 demo: a tiny launcher that exercises the SYS_EXEC trampoline.
 *
 * The kernel boots into /bin/launcher (instead of /bin/hello64 like in
 * H.2). launcher prints a banner so we can prove it really ran in ring3,
 * then calls execve("/bin/hello64_v2"). On a working execve the kernel
 * longjmps back to ring0, reloads the image, and re-enters ring3 on
 * hello64_v2's entry point. hello64_v2 then runs the full Step C demo
 * and exits via SYS_EXIT as usual.
 *
 * If execve returns at all, it failed -- we print a diagnostic and exit
 * with a unique code so regressions are grep-able from the serial log.
 */

static void write_str(int fd, const char *s) {
    openos64_size_t n = openos64_strlen(s);
    (void)openos64_write(fd, s, n);
}

int openos64_main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write_str(OPENOS64_STDOUT_FILENO,
              "[launcher] H.3: from launcher, about to execve /bin/hello64_v2\n");

    long rc = openos64_execve("/bin/hello64_v2", (char *const *)0, (char *const *)0);

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
