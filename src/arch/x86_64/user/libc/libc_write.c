/*
 * M5.3c libc/libc_write.c — target backend for stdio output.
 *
 * Binds the weak __libc_write hook used by stdio.c to the OpenOS SYS_WRITE
 * syscall. Kept in a separate translation unit so host unit tests can supply
 * their own __libc_write without pulling in the syscall stubs.
 */
#include "../openos64.h"

long __libc_write(int fd, const void *buf, unsigned long len)
{
    return openos64_write(fd, buf, (openos64_size_t)len);
}
