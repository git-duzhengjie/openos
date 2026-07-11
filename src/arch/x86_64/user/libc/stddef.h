/*
 * M5.3d libc/stddef.h — standard C stddef subset for OpenOS ring3 userland.
 *
 * Authoritative home for size_t / ptrdiff_t / NULL / offsetof. Other libc
 * headers share the OPENOS_LIBC_SIZE_T_DEFINED guard so pulling several of
 * them together never produces a conflicting typedef.
 */
#ifndef OPENOS_LIBC_STDDEF_H
#define OPENOS_LIBC_STDDEF_H

#ifndef OPENOS_LIBC_SIZE_T_DEFINED
#define OPENOS_LIBC_SIZE_T_DEFINED
typedef unsigned long  size_t;
#endif

#ifndef OPENOS_LIBC_PTRDIFF_T_DEFINED
#define OPENOS_LIBC_PTRDIFF_T_DEFINED
typedef long           ptrdiff_t;
#endif

#ifndef OPENOS_LIBC_WCHAR_T_DEFINED
#define OPENOS_LIBC_WCHAR_T_DEFINED
typedef int            wchar_t;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#endif /* OPENOS_LIBC_STDDEF_H */
