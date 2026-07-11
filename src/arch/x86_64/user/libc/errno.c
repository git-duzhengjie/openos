/*
 * M5.3d libc/errno.c — errno storage for OpenOS ring3 userland.
 *
 * Single-process backing cell exposed through __errno_location(). When the
 * pthread subset later needs per-thread errno this is the one spot to swap in
 * a TLS lookup; the public contract (an int* to the current errno) stays put.
 */
#include "errno.h"

static int __errno_storage = 0;

int *__errno_location(void)
{
    return &__errno_storage;
}
