#include "openos.h"

static openos_sem_t slots = 0;
static volatile int produced = 0;
static volatile int consumed = 0;

static void producer(void *arg)
{
    (void)arg;

    for (int i = 0; i < 5; i++) {
        produced++;
        openos_write_str("producer item=");
        openos_print_int(produced);
        openos_write_str("\n");

        if (openos_sem_post(&slots) < 0) {
            openos_write_fd(2, "semtest: post failed\n", 21);
            openos_thread_exit(1);
        }
        openos_yield();
    }

    openos_thread_exit(0);
}

static void consumer(void *arg)
{
    (void)arg;

    for (int i = 0; i < 5; i++) {
        if (openos_sem_wait(&slots) < 0) {
            openos_write_fd(2, "semtest: wait failed\n", 21);
            openos_thread_exit(1);
        }

        consumed++;
        openos_write_str("consumer item=");
        openos_print_int(consumed);
        openos_write_str("\n");
        openos_yield();
    }

    openos_thread_exit(0);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    openos_thread_t prod;
    openos_thread_t cons;

    if (openos_sem_init(&slots, 0) < 0) {
        openos_write_fd(2, "semtest: sem init failed\n", 26);
        return 1;
    }

    openos_write_str("semtest: start\n");

    if (openos_thread_create(&cons, consumer, 0) < 0) {
        openos_write_fd(2, "semtest: consumer thread failed\n", 32);
        return 1;
    }
    if (openos_thread_create(&prod, producer, 0) < 0) {
        openos_write_fd(2, "semtest: producer thread failed\n", 32);
        return 1;
    }

    for (int i = 0; i < 100 && consumed < 5; i++)
        openos_yield();

    if (openos_sem_destroy(&slots) < 0) {
        openos_write_fd(2, "semtest: destroy failed\n", 24);
        return 1;
    }

    openos_write_str("semtest: produced=");
    openos_print_int(produced);
    openos_write_str(" consumed=");
    openos_print_int(consumed);
    openos_write_str("\n");

    return produced == 5 && consumed == 5 ? 0 : 1;
}
