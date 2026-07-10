/* ======================================================================
 * clone64.h  --  M5.2a: thread creation (clone) flags & argument struct.
 *
 * openos threads are PCBs that SHARE their creator's address space rather
 * than deep-cloning it (the fork path). SYS_CLONE with CLONE_VM|CLONE_THREAD
 * yields a POSIX-thread-like sibling inside the same thread group (tgid).
 *
 * The flag bit values below intentionally mirror the Linux clone(2) ABI so
 * that a musl/glibc-style pthread port needs no translation layer. Only the
 * subset relevant to M5.2 is defined; unknown flags are rejected by do_clone.
 * ==================================================================== */
#ifndef OPENOS_ARCH_X86_64_CLONE64_H
#define OPENOS_ARCH_X86_64_CLONE64_H

#include <stdint.h>
#include <stdbool.h>

/* --- clone flags (Linux-compatible bit values) --------------------- */
#define OPENOS_CLONE_VM             0x00000100u  /* share address space      */
#define OPENOS_CLONE_FS             0x00000200u  /* share filesystem info    */
#define OPENOS_CLONE_FILES          0x00000400u  /* share open file table    */
#define OPENOS_CLONE_SIGHAND        0x00000800u  /* share signal handlers    */
#define OPENOS_CLONE_THREAD         0x00010000u  /* same thread group (tgid) */
#define OPENOS_CLONE_SETTLS         0x00080000u  /* set %fs.base from tls arg */
#define OPENOS_CLONE_PARENT_SETTID  0x00100000u  /* store child tid @ ptid    */
#define OPENOS_CLONE_CHILD_CLEARTID 0x00200000u  /* clear+wake tid @ ctid on exit */
#define OPENOS_CLONE_CHILD_SETTID   0x01000000u  /* store child tid @ ctid    */

/* Flags this implementation understands. A do_clone request carrying any
 * bit outside this mask is rejected with -EINVAL, so future kernels can
 * grow the set without silently ignoring semantics userland relies on. */
#define OPENOS_CLONE_SUPPORTED_MASK  \
    (OPENOS_CLONE_VM | OPENOS_CLONE_FS | OPENOS_CLONE_FILES |     \
     OPENOS_CLONE_SIGHAND | OPENOS_CLONE_THREAD | OPENOS_CLONE_SETTLS | \
     OPENOS_CLONE_PARENT_SETTID | OPENOS_CLONE_CHILD_CLEARTID |   \
     OPENOS_CLONE_CHILD_SETTID)

/* The canonical "make a pthread" flag combination. A request must include
 * (at minimum) CLONE_VM|CLONE_THREAD for openos to treat it as a thread
 * that shares the parent AS; otherwise it is not a thread and is rejected
 * in M5.2a (full fork-style clone is out of scope for this sub-step). */
#define OPENOS_CLONE_THREAD_MIN  (OPENOS_CLONE_VM | OPENOS_CLONE_THREAD)

/* --- clone argument bundle ----------------------------------------- *
 * Gathered from the SYS_CLONE register arguments by do_clone and passed
 * to arch_x86_64_proc_clone_thread(). Keeping it in a struct avoids a
 * 6-argument C function and documents each field's role. */
typedef struct openos_clone_args {
    uint32_t flags;         /* OPENOS_CLONE_* bitmask                    */
    uint64_t child_stack;   /* user %rsp for the new thread (top-of-stack,
                             * 16-byte aligned; must be non-zero)        */
    uint64_t entry;         /* user instruction pointer (thread start fn) */
    uint64_t arg;           /* single argument passed to entry (in %rdi)  */
    uint64_t tls;           /* CLONE_SETTLS: %fs.base value for the thread */
    uint64_t parent_tid;    /* CLONE_PARENT_SETTID: user addr for child tid */
    uint64_t child_tid;     /* CLONE_CHILD_*TID: user addr for child tid   */
} openos_clone_args_t;

#endif /* OPENOS_ARCH_X86_64_CLONE64_H */
