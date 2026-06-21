/* ============================================================
 * openos - minimal C++ ABI support regression test
 * ============================================================ */

#include "openos.h"
#include "openos_cxxabi.h"

static int ctor_counter = 0;
static int dtor_counter = 0;

static void fake_ctor_a(void)
{
    ctor_counter += 1;
}

static void fake_ctor_b(void)
{
    ctor_counter += 2;
}

static void fake_dtor_a(void)
{
    dtor_counter += 4;
}

static void fake_dtor_b(void)
{
    dtor_counter += 8;
}

int main(int argc, char **argv, char **envp)
{
    void *single;
    void *array;
    unsigned char *bytes;
    openos_cxxabi_guard_t guard;
    volatile int atomic_counter;
    int old;
    openos_cxxabi_func_t init_array[2];
    openos_cxxabi_func_t fini_array[2];

    (void)argc;
    (void)argv;
    (void)envp;

    openos_puts("[cxxabitest] checking minimal C++ ABI hooks...");

    single = openos_cxx_operator_new(0);
    if (!single)
        openos_fail(1, "[cxxabitest] operator new(0) failed");
    bytes = (unsigned char *)single;
    bytes[0] = 0x5a;
    if (bytes[0] != 0x5a)
        openos_fail(2, "[cxxabitest] operator new storage failed");
    openos_cxx_operator_delete(single);

    array = openos_cxx_operator_new_array(64);
    if (!array)
        openos_fail(3, "[cxxabitest] operator new[] failed");
    bytes = (unsigned char *)array;
    bytes[0] = 0x11;
    bytes[63] = 0xee;
    if (bytes[0] != 0x11 || bytes[63] != 0xee)
        openos_fail(4, "[cxxabitest] operator new[] storage failed");
    openos_cxx_operator_delete_array(array);

    guard.state = 0;
    if (!openos_cxx_guard_acquire(&guard))
        openos_fail(5, "[cxxabitest] guard first acquire failed");
    openos_cxx_guard_release(&guard);
    if (openos_cxx_guard_acquire(&guard))
        openos_fail(6, "[cxxabitest] guard second acquire succeeded");
    openos_cxx_guard_abort(&guard);
    if (!openos_cxx_guard_acquire(&guard))
        openos_fail(7, "[cxxabitest] guard acquire after abort failed");

    atomic_counter = 7;
    old = openos_cxx_atomic_fetch_add_int(&atomic_counter, 5);
    if (old != 7 || openos_cxx_atomic_load_int(&atomic_counter) != 12)
        openos_fail(8, "[cxxabitest] atomic fetch_add/load failed");

    init_array[0] = fake_ctor_a;
    init_array[1] = fake_ctor_b;
    fini_array[0] = fake_dtor_a;
    fini_array[1] = fake_dtor_b;
    openos_cxx_run_array(init_array, init_array + 2);
    openos_cxx_run_array(fini_array, fini_array + 2);
    if (ctor_counter != 3 || dtor_counter != 12)
        openos_fail(9, "[cxxabitest] init/fini array dispatch failed");

    openos_puts("[cxxabitest] minimal C++ ABI hooks ok");
    return 0;
}
