#include "openos64.h"

/*
 * A2.P5 demo: hello_fork -- the standalone fork/wait smoke test.
 *
 * Historically the [wait] block lived at the tail of /bin/hello64_v2, which
 * meant we could only exercise fork/wait AFTER an execve. That coupled two
 * subsystems (execve + fork/wait) into one regression, which made bisecting
 * A2.P2 painful.
 *
 * A2.P5 splits it out: /bin/launcher -> execve /bin/hello64_v2
 *                     /bin/hello64_v2 -> execve /bin/hello_fork
 *                     /bin/hello_fork -> fork + wait (+ [wait] PASS/FAIL)
 *
 * Success criteria mirror the historical inline test:
 *   - Parent's wait() must return the same pid fork() handed it.
 *   - Exit status high byte must equal the child's exit code (7).
 *   - On success we print "[wait] PASS", on failure "[wait] FAIL".
 *
 * Nothing else runs after this program -- returning any non-zero code from
 * openos64_main just tells crt0 to SYS_EXIT with that code.
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
    (void)argv;
    (void)envp;

    write_str(OPENOS64_STDOUT_FILENO,
              "[hello_fork] A2.P5: standalone fork/wait test image\n");

    /* Echo argc so we can eyeball SysV startup frame end-to-end after the
     * second execve too. hello64_v2 forwards argv/envp verbatim. */
    write_str(OPENOS64_STDOUT_FILENO, "[hello_fork] argc=");
    write_dec(OPENOS64_STDOUT_FILENO, (long)argc);
    write_str(OPENOS64_STDOUT_FILENO, "\n");

    long ppid = openos64_getppid();
    long pid  = openos64_getpid();
    write_str(OPENOS64_STDOUT_FILENO, "[hello_fork] pid=");
    write_dec(OPENOS64_STDOUT_FILENO, pid);
    write_str(OPENOS64_STDOUT_FILENO, " ppid=");
    write_dec(OPENOS64_STDOUT_FILENO, ppid);
    write_str(OPENOS64_STDOUT_FILENO, "\n");

    write_str(OPENOS64_STDOUT_FILENO, "[wait] pre\n");
    long rv = openos64_fork();
    if (rv == 0) {
        write_str(OPENOS64_STDOUT_FILENO, "[wait] child exit=7\n");
        openos64_exit(7);
    } else if (rv > 0) {
        int st = -1;
        long wp = openos64_wait(&st);
        write_str(OPENOS64_STDOUT_FILENO, "[wait] parent pid=");
        write_dec(OPENOS64_STDOUT_FILENO, wp);
        write_str(OPENOS64_STDOUT_FILENO, " status=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)st);
        write_str(OPENOS64_STDOUT_FILENO, " exit=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)((st >> 8) & 0xFF));
        write_str(OPENOS64_STDOUT_FILENO, "\n");
        if (wp == rv && ((st >> 8) & 0xFF) == 7) {
            write_str(OPENOS64_STDOUT_FILENO, "[wait] PASS\n");
        } else {
            write_str(OPENOS64_STDOUT_FILENO, "[wait] FAIL\n");
        }
    } else {
        write_str(OPENOS64_STDOUT_FILENO, "[wait] fork err\n");
        return 1;
    }

    return 0;
}
