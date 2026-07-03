#ifndef TYPES_H
#define TYPES_H

#ifdef __x86_64__
/* ------------------------------------------------------------
 * x86_64 long-mode：复用 GCC 内置 <stdint.h> 的固定宽度类型，
 * 保证与 x86_64 ABI 一致（int64_t = long, size_t = 64 位）。
 * 仅在 x86_64 内核 / gui64 移植代码中生效，不影响 i386 构建。
 * ------------------------------------------------------------ */
#include <stdint.h>

typedef unsigned long size_t;
typedef long          ssize_t;

#else
/* ------------------------------------------------------------
 * i386 32 位（原始定义，保持不变）
 * ------------------------------------------------------------ */
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long long uint64_t;

typedef signed char    int8_t;
typedef signed short   int16_t;
typedef signed int     int32_t;
typedef signed long long   int64_t;

typedef uint32_t size_t;
typedef int32_t  ssize_t;
#endif

#define bool uint32_t

#define true  1
#define false 0
#ifndef NULL
#define NULL  ((void*)0)
#endif

#endif
