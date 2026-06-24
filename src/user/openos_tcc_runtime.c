/* OPENOS TinyCC generated-program runtime support.
 * This file is compiled by the host build into /usr/lib/tcc/crt0.o and is also
 * installed as source documentation in the in-memory sysroot.  It intentionally
 * includes openos.h directly so programs built inside OPENOS can use the same
 * syscall ABI as built-in user commands.
 */

#define OPENOS_NO_LIBC_ALIASES
#include "openos.h"

int main(int argc, char **argv, char **envp);

void *memset(void *dst, int value, unsigned int len)
{
    return openos_memset(dst, value, (int)len);
}

void *memcpy(void *dst, const void *src, unsigned int len)
{
    return openos_memcpy(dst, src, (int)len);
}

void *memmove(void *dst, const void *src, unsigned int len)
{
    return openos_memmove(dst, src, (int)len);
}

int memcmp(const void *a, const void *b, unsigned int len)
{
    return openos_memcmp(a, b, (int)len);
}

int open(const char *path, int flags, int mode)
{
    return openos_open(path, flags, mode);
}

int close(int fd)
{
    return openos_close(fd);
}

int read(int fd, void *buf, unsigned int len)
{
    return openos_read(fd, buf, (int)len);
}

int write(int fd, const void *buf, unsigned int len)
{
    return openos_write_fd(fd, buf, (int)len);
}

void _start(int argc, char **argv, char **envp)
{
    int code = main(argc, argv, envp);
    openos_exit(code);
}
