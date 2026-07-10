#ifndef OPENOS_ARCH_X86_64_USER_PTHREAD64_H
#define OPENOS_ARCH_X86_64_USER_PTHREAD64_H

/*
 * M5.2d — minimal pthread-style userspace threading runtime.
 *
 * Built directly on the M5.2 kernel primitives:
 *   - clone(CLONE_VM|CLONE_THREAD|...) to spawn a thread that shares the
 *     caller's address space and starts at entry(arg) on its own stack.
 *   - futex_wait/wake for blocking synchronisation.
 *
 * Design notes / deliberate limitations (kept small on purpose):
 *   - Thread stacks come from a fixed BSS pool (no malloc/mmap in this
 *     runtime yet). OPENOS64_PTHREAD_MAX threads, each with a fixed stack.
 *   - join() blocks on a per-thread futex word that the thread-exit shim
 *     clears + wakes. We do NOT rely on the kernel CLONE_CHILD_CLEARTID
 *     path here because the 5-arg clone ABI does not carry a ctid pointer;
 *     the shim performs the clear+wake explicitly in userspace before it
 *     issues SYS_EXIT.
 *   - No detach / cancellation / TLS-key API. Just create/join/self/exit
 *     and a futex-backed mutex — enough to prove shared-AS + mutual
 *     exclusion + join end-to-end from ring3.
 */

#include "openos64.h"

#ifndef OPENOS64_PTHREAD_MAX
#define OPENOS64_PTHREAD_MAX      8u
#endif
#ifndef OPENOS64_PTHREAD_STACKSZ
#define OPENOS64_PTHREAD_STACKSZ  (64u * 1024u)   /* 64 KiB per thread */
#endif

typedef unsigned long openos64_pthread_t;   /* index into the thread table + 1 */

typedef void *(*openos64_thread_fn)(void *arg);

/* Futex-backed mutex. state: 0=unlocked, 1=locked-no-waiters,
 * 2=locked-maybe-waiters (classic 3-state fast mutex). */
typedef struct {
    volatile int state;
} openos64_mutex_t;

#define OPENOS64_MUTEX_INIT  { 0 }

/* ---- thread lifecycle ---- */
int  openos64_pthread_create(openos64_pthread_t *out_tid,
                             openos64_thread_fn fn, void *arg);
int  openos64_pthread_join(openos64_pthread_t tid, void **retval);
openos64_pthread_t openos64_pthread_self(void);
void openos64_pthread_exit(void *retval);   /* never returns */

/* ---- mutex ---- */
void openos64_mutex_init(openos64_mutex_t *m);
void openos64_mutex_lock(openos64_mutex_t *m);
int  openos64_mutex_trylock(openos64_mutex_t *m);   /* 0=got it, -1=busy */
void openos64_mutex_unlock(openos64_mutex_t *m);

#endif /* OPENOS_ARCH_X86_64_USER_PTHREAD64_H */
