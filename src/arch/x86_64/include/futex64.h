/*
 * futex64.h — Fast userspace mutex primitives (M5.2c)
 *
 * A minimal, self-contained futex subsystem sized for pthread-style thread
 * synchronization inside a shared address space (CLONE_VM thread groups).
 *
 * Design notes
 * ------------
 *  - PRIVATE semantics only: the futex "key" is the user *virtual* address of
 *    a 32-bit word. Because a thread group shares one address space, the same
 *    virtual address in two sibling threads names the same futex — no physical
 *    translation is required for the private case, which is all pthread needs.
 *
 *  - The wait table is a small static array of slots. Each slot records the
 *    user address a thread is parked on plus a generation-style "woken" flag
 *    that futex_wake flips. There is no true BLOCKED scheduler state in this
 *    kernel; waiters use the same cooperative sti/hlt spin-yield model as the
 *    pipe and wait() paths (see pipe64.c / syscall_dispatch64.c). This keeps
 *    futex consistent with the rest of the kernel and trivially unit-testable.
 *
 *  - futex_wait compares *uaddr against the expected value BEFORE parking to
 *    close the classic lost-wakeup race: if the value already changed the
 *    caller returns -EAGAIN and must re-check its condition.
 *
 * Errno-style returns (negative): -EAGAIN (value mismatch), -EINVAL (bad
 * address / args), -ETIMEDOUT (timeout expired). Non-negative from wake is
 * the number of waiters actually woken.
 */
#ifndef OPENOS_X86_64_FUTEX64_H
#define OPENOS_X86_64_FUTEX64_H

#include <stdint.h>
#include <stdbool.h>

/* futex errno-style codes (kept local so the subsystem has no libc dep) */
#define OPENOS_FUTEX_EAGAIN     11   /* value did not match expected  */
#define OPENOS_FUTEX_EINVAL     22   /* bad address / arguments        */
#define OPENOS_FUTEX_ETIMEDOUT  110  /* timeout elapsed while waiting  */

/* Upper bound on concurrently parked waiters across the whole system. */
#define OPENOS_FUTEX_MAX_WAITERS 64

/*
 * futex_wait — park the caller on *uaddr until woken (or timeout).
 *
 *   uaddr        user virtual address of the 32-bit futex word
 *   val          expected current value; if *uaddr != val return -EAGAIN
 *   timeout_ms   0 = wait forever; >0 = give up after this many ms (-ETIMEDOUT)
 *
 * Returns 0 on a genuine wake, or a negative errno-style code.
 */
int arch_x86_64_futex_wait(uint64_t uaddr, uint32_t val, uint64_t timeout_ms);

/*
 * futex_wake — wake up to `count` waiters parked on *uaddr.
 * Returns the number of waiters actually flagged for wakeup (>= 0), or
 * -EINVAL for a bogus address.
 */
int arch_x86_64_futex_wake(uint64_t uaddr, int count);

/*
 * Test / introspection helper: number of waiters currently parked on uaddr.
 * Exposed for the host-side unit test; carries no locking guarantees.
 */
int arch_x86_64_futex_waiter_count(uint64_t uaddr);

/* Reset the wait table (host unit tests only). */
void arch_x86_64_futex_reset(void);

#endif /* OPENOS_X86_64_FUTEX64_H */
