/*
 * futex64.c — Fast userspace mutex primitives (M5.2c)
 *
 * See futex64.h for the design rationale. In short: a small static wait table
 * keyed by user virtual address, PRIVATE semantics only, cooperative sti/hlt
 * spin-yield blocking consistent with the rest of this kernel (pipe/wait/sleep).
 *
 * Structure
 * ---------
 *  - The table-management core (claim/find/mark/release a waiter slot, plus the
 *    *uaddr == val compare) is written as small pure functions so the host-side
 *    unit test can exercise the exact same logic without linking the kernel.
 *  - The actual parking loop (which needs arch_x86_64_proc_yield / do_uptime_ms)
 *    lives behind #ifndef OPENOS_UNIT_TEST so host builds don't pull kernel deps.
 */

#include "../include/futex64.h"
#ifndef OPENOS_UNIT_TEST
#include "../include/tsc64.h"
#endif

/* ------------------------------------------------------------------------- */
/* Wait table                                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    int      in_use;   /* slot occupied by a parked waiter                    */
    int      woken;    /* futex_wake flipped this -> waiter should return 0   */
    uint64_t uaddr;    /* user virtual address this waiter is parked on       */
} futex_waiter_t;

static futex_waiter_t g_futex_table[OPENOS_FUTEX_MAX_WAITERS];

/*
 * Claim a free slot for `uaddr`. Returns the slot index or -1 if the table is
 * full. The slot starts un-woken.
 */
static int futex_claim_slot(uint64_t uaddr) {
    for (int i = 0; i < OPENOS_FUTEX_MAX_WAITERS; ++i) {
        if (!g_futex_table[i].in_use) {
            g_futex_table[i].in_use = 1;
            g_futex_table[i].woken  = 0;
            g_futex_table[i].uaddr  = uaddr;
            return i;
        }
    }
    return -1;
}

/* Release a previously claimed slot. */
static void futex_release_slot(int idx) {
    if (idx < 0 || idx >= OPENOS_FUTEX_MAX_WAITERS) return;
    g_futex_table[idx].in_use = 0;
    g_futex_table[idx].woken  = 0;
    g_futex_table[idx].uaddr  = 0;
}

/*
 * Mark up to `count` still-parked (not-yet-woken) waiters on `uaddr` as woken.
 * Returns the number newly flagged. count < 0 means "all".
 */
static int futex_mark_woken(uint64_t uaddr, int count) {
    int woke = 0;
    for (int i = 0; i < OPENOS_FUTEX_MAX_WAITERS; ++i) {
        if (count >= 0 && woke >= count) break;
        if (g_futex_table[i].in_use &&
            !g_futex_table[i].woken &&
            g_futex_table[i].uaddr == uaddr) {
            g_futex_table[i].woken = 1;
            ++woke;
        }
    }
    return woke;
}

/* ------------------------------------------------------------------------- */
/* Introspection / reset (unit test surface)                                 */
/* ------------------------------------------------------------------------- */

int arch_x86_64_futex_waiter_count(uint64_t uaddr) {
    int n = 0;
    for (int i = 0; i < OPENOS_FUTEX_MAX_WAITERS; ++i) {
        if (g_futex_table[i].in_use && g_futex_table[i].uaddr == uaddr) ++n;
    }
    return n;
}

void arch_x86_64_futex_reset(void) {
    for (int i = 0; i < OPENOS_FUTEX_MAX_WAITERS; ++i) {
        g_futex_table[i].in_use = 0;
        g_futex_table[i].woken  = 0;
        g_futex_table[i].uaddr  = 0;
    }
}

/* ------------------------------------------------------------------------- */
/* futex_wake                                                                */
/* ------------------------------------------------------------------------- */

int arch_x86_64_futex_wake(uint64_t uaddr, int count) {
    if (uaddr == 0) return -OPENOS_FUTEX_EINVAL;
    if (count == 0) return 0;
    return futex_mark_woken(uaddr, count);
}

/* ------------------------------------------------------------------------- */
/* futex_wait — parking loop (kernel-only)                                   */
/* ------------------------------------------------------------------------- */

#ifndef OPENOS_UNIT_TEST

/* Provided by proc64 (yield) and tsc64 (monotonic uptime). */
extern int arch_x86_64_proc_yield(void);

/*
 * Read the 32-bit futex word. The kernel identity/higher-half maps user pages
 * that are already resident for a running thread, so a direct load is valid
 * here (same assumption the rest of syscall_dispatch64 makes for user buffers).
 */
static inline uint32_t futex_load(uint64_t uaddr) {
    return *(volatile uint32_t *)uaddr;
}

int arch_x86_64_futex_wait(uint64_t uaddr, uint32_t val, uint64_t timeout_ms) {
    if (uaddr == 0 || (uaddr & 0x3u) != 0u) {
        /* NULL or misaligned: a 32-bit futex word must be 4-byte aligned. */
        return -OPENOS_FUTEX_EINVAL;
    }

    /*
     * Compare-then-park to close the lost-wakeup race: if the value already
     * differs, another thread changed it before we could sleep — bail with
     * -EAGAIN so the caller re-evaluates its condition.
     */
    if (futex_load(uaddr) != val) {
        return -OPENOS_FUTEX_EAGAIN;
    }

    int idx = futex_claim_slot(uaddr);
    if (idx < 0) {
        /* Table full: degrade to EAGAIN so the caller spins/retries rather
         * than silently sleeping forever. */
        return -OPENOS_FUTEX_EAGAIN;
    }

    uint64_t start = arch_x86_64_tsc_uptime_ms();
    int rc = 0;

    for (;;) {
        if (g_futex_table[idx].woken) {
            rc = 0;
            break;
        }
        if (timeout_ms != 0 &&
            (arch_x86_64_tsc_uptime_ms() - start) >= timeout_ms) {
            rc = -OPENOS_FUTEX_ETIMEDOUT;
            break;
        }
        (void)arch_x86_64_proc_yield();
    }

    futex_release_slot(idx);
    return rc;
}

#else  /* OPENOS_UNIT_TEST: expose the pure table logic for host tests */

/*
 * Host-testable variant of futex_wait's *front half* (validate + compare +
 * claim). The real parking loop needs a live scheduler, so the unit test
 * drives futex_claim_slot / futex_mark_woken / futex_release_slot directly via
 * the thin wrappers below to assert the state machine.
 */
int arch_x86_64_futex_test_claim(uint64_t uaddr)      { return futex_claim_slot(uaddr); }
int arch_x86_64_futex_test_mark(uint64_t uaddr, int c){ return futex_mark_woken(uaddr, c); }
void arch_x86_64_futex_test_release(int idx)          { futex_release_slot(idx); }
int arch_x86_64_futex_test_is_woken(int idx) {
    if (idx < 0 || idx >= OPENOS_FUTEX_MAX_WAITERS) return -1;
    return g_futex_table[idx].woken;
}

#endif /* OPENOS_UNIT_TEST */
