#include "openos.h"

static openos_mutex_t lock = 0;
static openos_cond_t cond = 0;
static volatile int ready = 0;
static volatile int observed = 0;

static void waiter(void *arg)
{
    (void)arg;

    if (openos_mutex_lock(&lock) < 0) {
        openos_write_fd(2, "condtest: waiter lock failed\n", 29);
        openos_thread_exit(1);
    }

    while (!ready) {
        if (openos_cond_wait(&cond, &lock) < 0) {
            openos_write_fd(2, "condtest: cond wait failed\n", 27);
            openos_thread_exit(1);
        }
    }

    observed = ready;
    openos_write_str("condtest: waiter observed ready=1\n");

    if (openos_mutex_unlock(&lock) < 0) {
        openos_write_fd(2, "condtest: waiter unlock failed\n", 31);
        openos_thread_exit(1);
    }

    openos_thread_exit(0);
}

static void signaler(void *arg)
{
    (void)arg;

    for (int i = 0; i < 5; i++)
        openos_yield();

    if (openos_mutex_lock(&lock) < 0) {
        openos_write_fd(2, "condtest: signaler lock failed\n", 31);
        openos_thread_exit(1);
    }

    ready = 1;
    openos_write_str("condtest: signal ready=1\n");

    if (openos_cond_signal(&cond) < 0) {
        openos_write_fd(2, "condtest: cond signal failed\n", 29);
        openos_thread_exit(1);
    }

    if (openos_mutex_unlock(&lock) < 0) {
        openos_write_fd(2, "condtest: signaler unlock failed\n", 33);
        openos_thread_exit(1);
    }

    openos_thread_exit(0);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    openos_thread_t wait_tid;
    openos_thread_t signal_tid;

    if (openos_mutex_init(&lock) < 0) {
        openos_write_fd(2, "condtest: mutex init failed\n", 28);
        return 1;
    }
    if (openos_cond_init(&cond) < 0) {
        openos_write_fd(2, "condtest: cond init failed\n", 27);
        return 1;
    }

    openos_write_str("condtest: start\n");

    if (openos_thread_create(&wait_tid, waiter, 0) < 0) {
        openos_write_fd(2, "condtest: waiter thread failed\n", 31);
        return 1;
    }
    if (openos_thread_create(&signal_tid, signaler, 0) < 0) {
        openos_write_fd(2, "condtest: signaler thread failed\n", 33);
        return 1;
    }

    for (int i = 0; i < 100 && !observed; i++)
        openos_yield();

    if (openos_cond_destroy(&cond) < 0) {
        openos_write_fd(2, "condtest: cond destroy failed\n", 30);
        return 1;
    }
    if (openos_mutex_destroy(&lock) < 0) {
        openos_write_fd(2, "condtest: mutex destroy failed\n", 31);
        return 1;
    }

    openos_write_str("condtest: observed=");
    openos_print_int(observed);
    openos_write_str("\n");

    return observed == 1 ? 0 : 1;
}
