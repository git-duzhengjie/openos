#include "pthread64.h"

/*
 * M5.2d pthread runtime implementation. See pthread64.h for the design
 * contract and the deliberate limitations of this minimal library.
 */

/* Per-thread control block. Lives in the BSS pool so it is visible to
 * both the creating thread and the new thread (shared address space). */
typedef struct {
    volatile int       used;      /* 0=free slot, 1=allocated */
    volatile int       done;      /* join futex word: 0=running, 1=exited */
    openos64_thread_fn fn;
    void              *arg;
    void              *retval;
    long               tid;       /* kernel tid returned by clone */
} openos64_tcb_t;

/* Fixed thread table + stack pool (no dynamic allocator in this runtime). */
static openos64_tcb_t g_tcbs[OPENOS64_PTHREAD_MAX];

/* Each stack is 16-byte aligned; give the whole pool page-ish alignment. */
static __attribute__((aligned(64)))
unsigned char g_stacks[OPENOS64_PTHREAD_MAX][OPENOS64_PTHREAD_STACKSZ];

/* Table lock so two threads can create concurrently without racing on
 * slot allocation. Bootstrap uses the futex mutex itself. */
static openos64_mutex_t g_table_lock = OPENOS64_MUTEX_INIT;

/* ------------------------------------------------------------------ */
/* mutex — 3-state futex fast mutex (Drepper's "Futexes Are Tricky").  */
/* ------------------------------------------------------------------ */

void openos64_mutex_init(openos64_mutex_t *m) {
    m->state = 0;
}

int openos64_mutex_trylock(openos64_mutex_t *m) {
    int expected = 0;
    if (__atomic_compare_exchange_n(&m->state, &expected, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return 0;   /* 0 -> 1, acquired uncontended */
    }
    return -1;      /* already held */
}

void openos64_mutex_lock(openos64_mutex_t *m) {
    int c = 0;
    /* Fast path: 0 -> 1. */
    if (__atomic_compare_exchange_n(&m->state, &c, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return;
    }
    /* Contended. Ensure state is 2 (locked, maybe-waiters) and park. */
    if (c != 2) {
        c = __atomic_exchange_n(&m->state, 2, __ATOMIC_ACQUIRE);
    }
    while (c != 0) {
        /* Sleep until someone releases; spurious wakeups tolerated. */
        (void)openos64_futex_wait(&m->state, 2);
        c = __atomic_exchange_n(&m->state, 2, __ATOMIC_ACQUIRE);
    }
}

void openos64_mutex_unlock(openos64_mutex_t *m) {
    /* If state was 2 there may be waiters to wake. */
    if (__atomic_fetch_sub(&m->state, 1, __ATOMIC_RELEASE) != 1) {
        __atomic_store_n(&m->state, 0, __ATOMIC_RELEASE);
        (void)openos64_futex_wake(&m->state, 1);
    }
}

/* ------------------------------------------------------------------ */
/* thread lifecycle                                                    */
/* ------------------------------------------------------------------ */

/* clone() entry shim: runs on the new thread's stack, rdi = tcb pointer
 * (M5.2b trampoline delivers arg in rdi). Runs the user function, records
 * the return value, publishes done + wakes joiners, then SYS_EXIT.
 * Must never return to its (bogus) return address. */
static void openos64_thread_trampoline(void *raw) {
    openos64_tcb_t *tcb = (openos64_tcb_t *)raw;
    void *rv = tcb->fn(tcb->arg);
    tcb->retval = rv;
    /* Publish completion, then wake any joiner blocked on &tcb->done. */
    __atomic_store_n(&tcb->done, 1, __ATOMIC_RELEASE);
    (void)openos64_futex_wake(&tcb->done, 0x7fffffff);
    openos64_exit(0);
    /* unreachable */
    for (;;) { }
}

openos64_pthread_t openos64_pthread_self(void) {
    long me = openos64_getpid();   /* thread tid == its own pid in this model */
    for (unsigned i = 0; i < OPENOS64_PTHREAD_MAX; i++) {
        if (g_tcbs[i].used && g_tcbs[i].tid == me) {
            return (openos64_pthread_t)(i + 1);
        }
    }
    return 0;   /* main thread / not created via pthread_create */
}

int openos64_pthread_create(openos64_pthread_t *out_tid,
                            openos64_thread_fn fn, void *arg) {
    if (!fn) return -1;

    /* Allocate a free slot under the table lock. */
    openos64_mutex_lock(&g_table_lock);
    int slot = -1;
    for (unsigned i = 0; i < OPENOS64_PTHREAD_MAX; i++) {
        if (!g_tcbs[i].used) { slot = (int)i; break; }
    }
    if (slot < 0) {
        openos64_mutex_unlock(&g_table_lock);
        return -1;   /* table full */
    }
    openos64_tcb_t *tcb = &g_tcbs[slot];
    tcb->used   = 1;
    tcb->done   = 0;
    tcb->fn     = fn;
    tcb->arg    = arg;
    tcb->retval = 0;
    tcb->tid    = 0;
    openos64_mutex_unlock(&g_table_lock);

    /* Stack grows down: point child SP at the top of its slab, 16-aligned. */
    unsigned char *stack_top = g_stacks[slot] + OPENOS64_PTHREAD_STACKSZ;
    stack_top = (unsigned char *)((uintptr_t)stack_top & ~(uintptr_t)0xF);

    uint64_t flags = OPENOS64_CLONE_VM | OPENOS64_CLONE_THREAD |
                     OPENOS64_CLONE_FS | OPENOS64_CLONE_FILES |
                     OPENOS64_CLONE_SIGHAND;

    long tid = openos64_clone(flags, stack_top,
                              (void *)openos64_thread_trampoline,
                              (void *)tcb, (void *)0);
    if (tid < 0) {
        tcb->used = 0;   /* roll back the slot */
        return -1;
    }
    tcb->tid = tid;
    if (out_tid) *out_tid = (openos64_pthread_t)(slot + 1);
    return 0;
}

int openos64_pthread_join(openos64_pthread_t tid, void **retval) {
    if (tid == 0 || tid > OPENOS64_PTHREAD_MAX) return -1;
    openos64_tcb_t *tcb = &g_tcbs[tid - 1];
    if (!tcb->used) return -1;

    /* Block until the thread's exit shim publishes done==1. */
    while (__atomic_load_n(&tcb->done, __ATOMIC_ACQUIRE) == 0) {
        (void)openos64_futex_wait(&tcb->done, 0);
    }
    if (retval) *retval = tcb->retval;
    tcb->used = 0;   /* recycle the slot */
    return 0;
}

void openos64_pthread_exit(void *retval) {
    long me = openos64_getpid();
    for (unsigned i = 0; i < OPENOS64_PTHREAD_MAX; i++) {
        if (g_tcbs[i].used && g_tcbs[i].tid == me) {
            g_tcbs[i].retval = retval;
            __atomic_store_n(&g_tcbs[i].done, 1, __ATOMIC_RELEASE);
            (void)openos64_futex_wake(&g_tcbs[i].done, 0x7fffffff);
            break;
        }
    }
    openos64_exit(0);
    for (;;) { }
}
