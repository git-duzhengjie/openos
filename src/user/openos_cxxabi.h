/* ============================================================
 * openos - minimal C++ ABI support hooks for userland smoke
 * ============================================================ */

#ifndef OPENOS_USER_OPENOS_CXXABI_H
#define OPENOS_USER_OPENOS_CXXABI_H

#include "openos.h"

typedef void (*openos_cxxabi_func_t)(void);

typedef struct openos_cxxabi_guard {
    volatile int state;
} openos_cxxabi_guard_t;

static inline void *openos_cxx_operator_new(unsigned int size)
{
    if (size == 0)
        size = 1;
    return openos_malloc((int)size);
}

static inline void *openos_cxx_operator_new_array(unsigned int size)
{
    return openos_cxx_operator_new(size);
}

static inline void openos_cxx_operator_delete(void *ptr)
{
    if (ptr)
        openos_free(ptr);
}

static inline void openos_cxx_operator_delete_array(void *ptr)
{
    openos_cxx_operator_delete(ptr);
}

static inline int openos_cxx_guard_acquire(openos_cxxabi_guard_t *guard)
{
    int old;
    if (!guard)
        return 0;
    old = __sync_val_compare_and_swap(&guard->state, 0, 1);
    return old == 0;
}

static inline void openos_cxx_guard_release(openos_cxxabi_guard_t *guard)
{
    if (guard)
        __sync_synchronize();
}

static inline void openos_cxx_guard_abort(openos_cxxabi_guard_t *guard)
{
    if (guard)
        __sync_lock_test_and_set(&guard->state, 0);
}

static inline int openos_cxx_atomic_fetch_add_int(volatile int *ptr, int value)
{
    return __sync_fetch_and_add(ptr, value);
}

static inline int openos_cxx_atomic_load_int(volatile int *ptr)
{
    int value;
    __sync_synchronize();
    value = *ptr;
    __sync_synchronize();
    return value;
}

static inline void openos_cxx_run_array(openos_cxxabi_func_t *begin,
                                        openos_cxxabi_func_t *end)
{
    while (begin && begin < end) {
        if (*begin)
            (*begin)();
        begin++;
    }
}

#endif /* OPENOS_USER_OPENOS_CXXABI_H */
