/* ============================================================
 * openos - user thread API regression test
 * ============================================================ */

#include "openos.h"

static volatile int worker1_done = 0;
static volatile int worker2_done = 0;
static volatile int worker1_tid = 0;
static volatile int worker2_tid = 0;
static volatile int shared_counter = 0;

static void worker1(void *arg)
{
    int loops = (int)arg;
    worker1_tid = openos_gettid();
    for (int i = 0; i < loops; i++) {
        shared_counter++;
        openos_yield();
    }
    worker1_done = 1;
    openos_thread_exit(0);
}

static void worker2(void *arg)
{
    int loops = (int)arg;
    worker2_tid = openos_gettid();
    for (int i = 0; i < loops; i++) {
        shared_counter++;
        openos_yield();
    }
    worker2_done = 1;
    openos_thread_exit(0);
}

void _start(void)
{
    openos_thread_t t1 = 0;
    openos_thread_t t2 = 0;
    int spins = 0;

    openos_write_str("[threadtest] creating user threads...\n");

    if (openos_thread_create(&t1, worker1, (void *)3) != 0)
        openos_fail(1, "[threadtest] failed to create worker1\n");
    if (openos_thread_create(&t2, worker2, (void *)4) != 0)
        openos_fail(2, "[threadtest] failed to create worker2\n");

    while ((!worker1_done || !worker2_done) && spins < 1000) {
        openos_sleep(1);
        spins++;
    }

    if (!worker1_done || !worker2_done)
        openos_fail(3, "[threadtest] worker timeout\n");
    if (worker1_tid == 0 || worker2_tid == 0 || worker1_tid == worker2_tid)
        openos_fail(4, "[threadtest] invalid worker tid\n");
    if (t1 == 0 || t2 == 0 || t1 == t2)
        openos_fail(5, "[threadtest] invalid returned thread id\n");
    if (shared_counter != 7)
        openos_fail(6, "[threadtest] shared address space check failed\n");

    openos_write_str("[threadtest] user thread API ok\n");
    openos_exit(0);
}
