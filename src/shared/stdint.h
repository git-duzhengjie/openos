#ifndef OPENOS_SHARED_STDINT_H
#define OPENOS_SHARED_STDINT_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

typedef signed char int_least8_t;
typedef unsigned char uint_least8_t;
typedef short int_least16_t;
typedef unsigned short uint_least16_t;
typedef int int_least32_t;
typedef unsigned int uint_least32_t;
typedef long long int_least64_t;
typedef unsigned long long uint_least64_t;

#define INT8_MIN (-128)
#define INT8_MAX 127
#define UINT8_MAX 255u
#define INT16_MIN (-32767-1)
#define INT16_MAX 32767
#define UINT16_MAX 65535u
#define INT32_MIN (-2147483647-1)
#define INT32_MAX 2147483647
#define UINT32_MAX 4294967295u
#define INT64_MIN (-9223372036854775807LL-1LL)
#define INT64_MAX 9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL

#endif /* OPENOS_SHARED_STDINT_H */
