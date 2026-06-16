/* ============================================================
 * openos - futex smoke test
 * ============================================================ */

#include "openos.h"

static volatile unsigned int futex_word = 0;

static void worker(void *arg)
{
    (void)arg;
    openos_sleep(20);
    futex_word = 1;
    openos_futex_wake(&futex_word, 1);
    openos_thread_exit(0);
}

int main(int argc, char **argv)
{
    openos_thread_t tid = 0;

    (void)argc;
    (void)argv;

    openos_write_str("futextest: start\n");
    if (openos_thread_create(&tid, worker, 0) < 0) {
        openos_write_fd(2, "futextest: thread_create failed\n", 34);
        return 1;
    }

    while (futex_word == 0) {
        int ret = openos_futex_wait(&futex_word, 0);
        if (ret < 0) {
            openos_write_fd(2, "futextest: futex_wait failed\n", 30);
            return 1;
        }
    }

    openos_write_str("futextest: ok\n");
    return 0;
}
