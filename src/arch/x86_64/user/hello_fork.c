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

    /* γ.4 S2b — fan-out fork test. Parent forks N children, each exits
     * with a distinct code (100..100+N-1). Parent then wait()s N times
     * and validates the *set* of returned (pid,exit) pairs matches the
     * set of pids/codes fork() handed us. Order is intentionally not
     * asserted: the kernel currently drains zombies LIFO but any
     * ordering must be tolerated. */
#define OPENOS64_FORK_N 3
    long child_pids[OPENOS64_FORK_N];
    int  child_idx = -1;

    for (int i = 0; i < OPENOS64_FORK_N; i++) {
        long rv = openos64_fork();
        if (rv == 0) {
            /* Child i: distinct exit code so parent can identify us. */
            child_idx = i;
            break;
        } else if (rv > 0) {
            child_pids[i] = rv;
            write_str(OPENOS64_STDOUT_FILENO, "[fork-multi] spawned child ");
            write_dec(OPENOS64_STDOUT_FILENO, (long)i);
            write_str(OPENOS64_STDOUT_FILENO, " pid=");
            write_dec(OPENOS64_STDOUT_FILENO, rv);
            write_str(OPENOS64_STDOUT_FILENO, "\n");
        } else {
            write_str(OPENOS64_STDOUT_FILENO, "[fork-multi] fork err at i=");
            write_dec(OPENOS64_STDOUT_FILENO, (long)i);
            write_str(OPENOS64_STDOUT_FILENO, "\n");
            return 1;
        }
    }

    if (child_idx >= 0) {
        /* We're a child. Emit a distinctive line then exit(100+i). */
        int exit_code = 100 + child_idx;
        write_str(OPENOS64_STDOUT_FILENO, "[fork-multi] child idx=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)child_idx);
        write_str(OPENOS64_STDOUT_FILENO, " exit=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)exit_code);
        write_str(OPENOS64_STDOUT_FILENO, "\n");
        openos64_exit(exit_code);
    }

    /* Parent: reap N children. Track which pids/codes we've seen. */
    int seen_pid[OPENOS64_FORK_N] = {0};
    int seen_code[OPENOS64_FORK_N] = {0};
    int reaped = 0;
    int mismatch = 0;
    for (int w = 0; w < OPENOS64_FORK_N; w++) {
        int st = -1;
        long wp = openos64_wait(&st);
        int code = (int)((st >> 8) & 0xFF);
        write_str(OPENOS64_STDOUT_FILENO, "[wait-multi] got pid=");
        write_dec(OPENOS64_STDOUT_FILENO, wp);
        write_str(OPENOS64_STDOUT_FILENO, " exit=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)code);
        write_str(OPENOS64_STDOUT_FILENO, "\n");
        if (wp <= 0) { mismatch = 1; continue; }

        /* Find matching child by pid, cross-check exit code == 100+idx. */
        int found = -1;
        for (int j = 0; j < OPENOS64_FORK_N; j++) {
            if (child_pids[j] == wp && !seen_pid[j]) { found = j; break; }
        }
        if (found < 0) { mismatch = 1; continue; }
        seen_pid[found]  = 1;
        seen_code[found] = 1;
        if (code != 100 + found) mismatch = 1;
        reaped++;
    }

    if (reaped == OPENOS64_FORK_N && !mismatch) {
        write_str(OPENOS64_STDOUT_FILENO, "[wait-multi] PASS\n");
    } else {
        write_str(OPENOS64_STDOUT_FILENO, "[wait-multi] FAIL reaped=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)reaped);
        write_str(OPENOS64_STDOUT_FILENO, " mismatch=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)mismatch);
        write_str(OPENOS64_STDOUT_FILENO, "\n");
        return 1;
    }

    /* γ.4 S2c — waitpid(specific_pid) test. Same fan-out shape as
     * wait-multi above, but this time the parent reaps in an explicit
     * order that is *not* LIFO, exercising the interior-splice path in
     * try_reap_zombie_by_pid. Children exit with 200..200+N-1 so log
     * lines don't collide with the wait-multi block above. */
#define OPENOS64_WAITPID_N 3
    long wp_pids[OPENOS64_WAITPID_N];
    int  wp_child_idx = -1;

    write_str(OPENOS64_STDOUT_FILENO, "[waitpid] pre\n");

    for (int i = 0; i < OPENOS64_WAITPID_N; i++) {
        long rv = openos64_fork();
        if (rv == 0) {
            wp_child_idx = i;
            break;
        } else if (rv > 0) {
            wp_pids[i] = rv;
            write_str(OPENOS64_STDOUT_FILENO, "[waitpid] spawned child ");
            write_dec(OPENOS64_STDOUT_FILENO, (long)i);
            write_str(OPENOS64_STDOUT_FILENO, " pid=");
            write_dec(OPENOS64_STDOUT_FILENO, rv);
            write_str(OPENOS64_STDOUT_FILENO, "\n");
        } else {
            write_str(OPENOS64_STDOUT_FILENO, "[waitpid] fork err at i=");
            write_dec(OPENOS64_STDOUT_FILENO, (long)i);
            write_str(OPENOS64_STDOUT_FILENO, "\n");
            return 1;
        }
    }

    if (wp_child_idx >= 0) {
        int exit_code = 200 + wp_child_idx;
        write_str(OPENOS64_STDOUT_FILENO, "[waitpid] child idx=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)wp_child_idx);
        write_str(OPENOS64_STDOUT_FILENO, " exit=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)exit_code);
        write_str(OPENOS64_STDOUT_FILENO, "\n");
        openos64_exit(exit_code);
    }

    /* Parent — reap in a deliberately-scrambled order: middle, first,
     * last. Each waitpid must return exactly the requested pid, and
     * status byte must equal 200+idx. */
    static const int reap_order[OPENOS64_WAITPID_N] = { 1, 0, 2 };
    int wp_mismatch = 0;
    int wp_reaped   = 0;
    for (int k = 0; k < OPENOS64_WAITPID_N; k++) {
        int  idx  = reap_order[k];
        long want = wp_pids[idx];
        int  st   = -1;
        long got  = openos64_waitpid(want, &st);
        int  code = (int)((st >> 8) & 0xFF);
        write_str(OPENOS64_STDOUT_FILENO, "[waitpid] want=");
        write_dec(OPENOS64_STDOUT_FILENO, want);
        write_str(OPENOS64_STDOUT_FILENO, " got=");
        write_dec(OPENOS64_STDOUT_FILENO, got);
        write_str(OPENOS64_STDOUT_FILENO, " exit=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)code);
        write_str(OPENOS64_STDOUT_FILENO, "\n");
        if (got != want)          wp_mismatch = 1;
        if (code != 200 + idx)    wp_mismatch = 1;
        if (got == want)          wp_reaped++;
    }

    /* Negative case: waitpid on a bogus pid should return -1 (ECHILD)
     * because zombie_head is drained and no live child has that pid. */
    {
        int  st  = -1;
        long got = openos64_waitpid(999999, &st);
        write_str(OPENOS64_STDOUT_FILENO, "[waitpid] bogus want=999999 got=");
        write_dec(OPENOS64_STDOUT_FILENO, got);
        write_str(OPENOS64_STDOUT_FILENO, "\n");
        if (got != -1) wp_mismatch = 1;
    }

    if (wp_reaped == OPENOS64_WAITPID_N && !wp_mismatch) {
        write_str(OPENOS64_STDOUT_FILENO, "[waitpid] PASS\n");
    } else {
        write_str(OPENOS64_STDOUT_FILENO, "[waitpid] FAIL reaped=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)wp_reaped);
        write_str(OPENOS64_STDOUT_FILENO, " mismatch=");
        write_dec(OPENOS64_STDOUT_FILENO, (long)wp_mismatch);
        write_str(OPENOS64_STDOUT_FILENO, "\n");
    }

    return 0;
}
