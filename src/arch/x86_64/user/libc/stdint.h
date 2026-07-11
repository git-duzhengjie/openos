/*
 * M5.3d libc/stdint.h — fixed-width integer types for OpenOS ring3 userland.
 *
 * x86_64 LP64 model: int=32, long=64, pointer=64. Provides exact-width,
 * least/fast aliases (mapped to the exact widths), pointer-sized ints and
 * the accompanying limit macros. Freestanding: no host libc dependency.
 */
#ifndef OPENOS_LIBC_STDINT_H
#define OPENOS_LIBC_STDINT_H

/* --- exact-width integer types --- */
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long               int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long      uint64_t;

/* --- least-width (mapped to exact widths on this target) --- */
typedef int8_t   int_least8_t;
typedef int16_t  int_least16_t;
typedef int32_t  int_least32_t;
typedef int64_t  int_least64_t;
typedef uint8_t  uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

/* --- fast-width (widest native = 64-bit for the small ones) --- */
typedef int8_t   int_fast8_t;
typedef int64_t  int_fast16_t;
typedef int64_t  int_fast32_t;
typedef int64_t  int_fast64_t;
typedef uint8_t  uint_fast8_t;
typedef uint64_t uint_fast16_t;
typedef uint64_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

/* --- pointer-sized & max-width --- */
typedef long           intptr_t;
typedef unsigned long  uintptr_t;
typedef long           intmax_t;
typedef unsigned long  uintmax_t;

/* --- limits of exact-width types --- */
#define INT8_MIN    (-128)
#define INT8_MAX    127
#define UINT8_MAX   255
#define INT16_MIN   (-32768)
#define INT16_MAX   32767
#define UINT16_MAX  65535
#define INT32_MIN   (-2147483647 - 1)
#define INT32_MAX   2147483647
#define UINT32_MAX  4294967295U
#define INT64_MIN   (-9223372036854775807L - 1)
#define INT64_MAX   9223372036854775807L
#define UINT64_MAX  18446744073709551615UL

/* --- limits of pointer/max types --- */
#define INTPTR_MIN  INT64_MIN
#define INTPTR_MAX  INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#define INTMAX_MIN  INT64_MIN
#define INTMAX_MAX  INT64_MAX
#define UINTMAX_MAX UINT64_MAX
#define SIZE_MAX    UINT64_MAX

/* --- integer constant macros --- */
#define INT8_C(x)   (x)
#define INT16_C(x)  (x)
#define INT32_C(x)  (x)
#define INT64_C(x)  (x ## L)
#define UINT8_C(x)  (x)
#define UINT16_C(x) (x)
#define UINT32_C(x) (x ## U)
#define UINT64_C(x) (x ## UL)
#define INTMAX_C(x)  (x ## L)
#define UINTMAX_C(x) (x ## UL)

#endif /* OPENOS_LIBC_STDINT_H */
