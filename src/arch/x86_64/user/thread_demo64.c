#include "openos64.h"
#include "pthread64.h"

/*
 * M5.2d end-to-end demo: /bin/thread_demo
 *
 * Proves three things about the M5.2 thread stack from ring3:
 *   1. Shared address space  — worker threads mutate a global counter that
 *      the main thread reads back after join.
 *   2. Mutual exclusion      — each worker does N unlocked-looking ++ under
 *      a futex mutex; the final total must be exactly WORKERS*ITERS with no
 *      lost updates (races would drop some).
 *   3. join                  — main blocks in pthread_join until every
 *      worker has published its result, then recycles the slots.
 *
 * Launch chain tail: /bin/hello_fork -> execve /bin/thread_demo.
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

#define WORKERS 4
#define ITERS   10000

static volatile long   g_counter;      /* shared, mutex-protected */
static openos64_mutex_t g_lock = OPENOS64_MUTEX_INIT;
static volatile long   g_worker_hits[WORKERS];  /* per-thread progress proof */

static void *worker(void *arg) {
    long id = (long)(uintptr_t)arg;
    for (int i = 0; i < ITERS; i++) {
        openos64_mutex_lock(&g_lock);
        g_counter++;               /* critical section */
        openos64_mutex_unlock(&g_lock);
    }
    g_worker_hits[id] = ITERS;
    return (void *)(uintptr_t)(id + 1);   /* distinct non-zero retval */
}

int openos64_main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    write_str(OPENOS64_STDOUT_FILENO,
              "[thread_demo] M5.2d: clone+futex+mutex+join end-to-end\n");

    openos64_pthread_t tids[WORKERS];
    int spawned = 0;
    for (long i = 0; i < WORKERS; i++) {
        if (openos64_pthread_create(&tids[i], worker,
                                    (void *)(uintptr_t)i) == 0) {
            spawned++;
            write_str(OPENOS64_STDOUT_FILENO, "[thread_demo] spawned worker ");
            write_dec(OPENOS64_STDOUT_FILENO, i);
            write_str(OPENOS64_STDOUT_FILENO, "\n");
        } else {
            write_str(OPENOS64_STDOUT_FILENO,
                      "[thread_demo] FAIL: pthread_create\n");
        }
    }

    long retsum = 0;
    for (int i = 0; i < spawned; i++) {
        void *rv = 0;
        if (openos64_pthread_join(tids[i], &rv) == 0) {
            retsum += (long)(uintptr_t)rv;
        }
    }

    long expect_counter = (long)spawned * ITERS;
    long expect_retsum  = (long)(spawned * (spawned + 1)) / 2;  /* 1+2+...+spawned */

    write_str(OPENOS64_STDOUT_FILENO, "[thread_demo] counter=");
    write_dec(OPENOS64_STDOUT_FILENO, g_counter);
    write_str(OPENOS64_STDOUT_FILENO, " expect=");
    write_dec(OPENOS64_STDOUT_FILENO, expect_counter);
    write_str(OPENOS64_STDOUT_FILENO, "\n");

    write_str(OPENOS64_STDOUT_FILENO, "[thread_demo] retsum=");
    write_dec(OPENOS64_STDOUT_FILENO, retsum);
    write_str(OPENOS64_STDOUT_FILENO, " expect=");
    write_dec(OPENOS64_STDOUT_FILENO, expect_retsum);
    write_str(OPENOS64_STDOUT_FILENO, "\n");

    int ok = (g_counter == expect_counter) && (retsum == expect_retsum);
    write_str(OPENOS64_STDOUT_FILENO,
              ok ? "[thread] PASS\n" : "[thread] FAIL\n");

    /* M5.3e launch-chain tail: hand off to /bin/libc_demo to exercise the
     * standard C library subset (memcpy/malloc/printf/qsort/...) in ring3. */
    if (ok) {
        write_str(OPENOS64_STDOUT_FILENO,
                  "[thread_demo] execve /bin/libc_demo (M5.3e)...\n");
        char *const argv[] = { "/bin/libc_demo", 0 };
        char *const envp[] = { 0 };
        openos64_execve("/bin/libc_demo", argv, envp);
        /* execve only returns on failure */
        write_str(OPENOS64_STDOUT_FILENO,
                  "[thread_demo] execve /bin/libc_demo FAILED\n");
        return 1;
    }
    return ok ? 0 : 1;
}
