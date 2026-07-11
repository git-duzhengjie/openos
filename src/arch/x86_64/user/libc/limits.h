/*
 * M5.3d libc/limits.h — implementation limits for OpenOS ring3 userland.
 *
 * x86_64 LP64: char=8, short=16, int=32, long=64, long long=64. char is
 * signed by default on this target.
 */
#ifndef OPENOS_LIBC_LIMITS_H
#define OPENOS_LIBC_LIMITS_H

#define CHAR_BIT    8

#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255

/* char is signed on x86_64 */
#define CHAR_MIN    SCHAR_MIN
#define CHAR_MAX    SCHAR_MAX

#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535

#define INT_MIN     (-2147483647 - 1)
#define INT_MAX     2147483647
#define UINT_MAX    4294967295U

#define LONG_MIN    (-9223372036854775807L - 1)
#define LONG_MAX    9223372036854775807L
#define ULONG_MAX   18446744073709551615UL

#define LLONG_MIN   (-9223372036854775807LL - 1)
#define LLONG_MAX   9223372036854775807LL
#define ULLONG_MAX  18446744073709551615ULL

#endif /* OPENOS_LIBC_LIMITS_H */
