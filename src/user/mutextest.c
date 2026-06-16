#include "openos.h"

static volatile int shared_counter = 0;
static openos_mutex_t counter_lock = 0;

static void worker(void *arg)
{
    int id = (int)(unsigned long)arg;

    for (int i = 0; i < 5; i++) {
        if (openos_mutex_lock(&counter_lock) < 0) {
            openos_write_fd(2, "mutextest: lock failed\n", 23);
            openos_thread_exit(1);
        }

        int value = shared_counter;
        openos_yield();
        shared_counter = value + 1;

        openos_write_str("worker ");
        openos_print_int(id);
        openos_write_str(" counter=");
        openos_print_int(shared_counter);
        openos_write_str("\n");

        if (openos_mutex_unlock(&counter_lock) < 0) {
            openos_write_fd(2, "mutextest: unlock failed\n", 25);
            openos_thread_exit(1);
        }
        openos_yield();
    }

    openos_thread_exit(0);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (openos_mutex_init(&counter_lock) < 0) {
        openos_write_fd(2, "mutextest: mutex init failed\n", 29);
        return 1;
    }

    openos_write_str("mutextest: start\n");

    openos_thread_t t1;
    openos_thread_t t2;

    if (openos_thread_create(&t1, worker, (void *)1) < 0) {
        openos_write_fd(2, "mutextest: thread 1 failed\n", 28);
        return 1;
    }
    if (openos_thread_create(&t2, worker, (void *)2) < 0) {
        openos_write_fd(2, "mutextest: thread 2 failed\n", 28);
        return 1;
    }

    for (int i = 0; i < 80 && shared_counter < 10; i++)
        openos_yield();

    if (openos_mutex_destroy(&counter_lock) < 0) {
        openos_write_fd(2, "mutextest: destroy failed\n", 26);
        return 1;
    }

    openos_write_str("mutextest: final counter=");
    openos_print_int(shared_counter);
    openos_write_str("\n");

    return shared_counter == 10 ? 0 : 1;
}
