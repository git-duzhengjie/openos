/*
 * M5.3b libc/libc_sbrk.c — target-only libc backend (SYS_SBRK / SYS_EXIT).
 *
 * Split out from stdlib.c so the allocator core stays syscall-agnostic and can
 * be exercised by the host unit test (which supplies its own __libc_sbrk).
 */
#include "../openos64.h"
#include "stdlib.h"

#ifndef SYS_SBRK
#define SYS_SBRK 253
#endif

/* Extend the program break by `increment` bytes; returns previous break. */
void *__libc_sbrk(long increment)
{
    long r = openos64_syscall1(SYS_SBRK, (uint64_t)increment);
    return (void *)r;
}

void _Exit(int status)
{
    openos64_exit(status);   /* OPENOS64_SYS_EXIT = 1 */
    for (;;) { }
}

void exit(int status) { _Exit(status); }

void abort(void)
{
    _Exit(134);   /* 128 + SIGABRT(6), matching the conventional shell status */
    for (;;) { }
}
