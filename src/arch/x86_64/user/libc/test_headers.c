/*
 * M5.3d test_headers.c — host-side sanity test for the standard header subset.
 *
 * Compiled with the freestanding libc headers (stddef/stdint/stdbool/limits/
 * ctype/errno). Verifies type widths, limit macros, ctype classification and
 * the errno lvalue. Reports via write(2) to dodge any printf self-reference.
 */
#include "stddef.h"
#include "stdint.h"
#include "stdbool.h"
#include "limits.h"
#include "ctype.h"
#include "errno.h"

#include <unistd.h>   /* host write() for reporting only */

static int failures = 0;

static size_t s_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void say(const char *s) { (void)write(1, s, s_len(s)); }

#define CHECK(cond) do {                              \
    if (cond) { say("PASS  " #cond "\n"); }           \
    else      { say("FAIL  " #cond "\n"); failures++; } \
} while (0)

int main(void)
{
    /* --- stdint: exact widths --- */
    CHECK(sizeof(int8_t)   == 1);
    CHECK(sizeof(int16_t)  == 2);
    CHECK(sizeof(int32_t)  == 4);
    CHECK(sizeof(int64_t)  == 8);
    CHECK(sizeof(uint64_t) == 8);
    CHECK(sizeof(intptr_t) == 8);
    CHECK(sizeof(size_t)   == 8);

    /* --- stdint: limit macros --- */
    CHECK(INT32_MAX == 2147483647);
    CHECK(UINT8_MAX == 255);
    CHECK(INT64_MIN < 0);
    CHECK(SIZE_MAX == UINT64_MAX);

    /* --- limits.h --- */
    CHECK(CHAR_BIT == 8);
    CHECK(INT_MAX == 2147483647);
    CHECK(UCHAR_MAX == 255);

    /* --- stdbool --- */
    CHECK(true == 1);
    CHECK(false == 0);
    CHECK(sizeof(bool) == 1);

    /* --- stddef --- */
    CHECK(NULL == (void *)0);

    /* --- ctype: classification --- */
    CHECK(isdigit('7') && !isdigit('a'));
    CHECK(isalpha('Q') && isalpha('q') && !isalpha('9'));
    CHECK(isalnum('z') && isalnum('0') && !isalnum('#'));
    CHECK(isspace(' ') && isspace('\t') && isspace('\n') && !isspace('x'));
    CHECK(isupper('A') && !isupper('a'));
    CHECK(islower('a') && !islower('A'));
    CHECK(isxdigit('f') && isxdigit('F') && isxdigit('9') && !isxdigit('g'));
    CHECK(isprint(' ') && !isprint('\n'));
    CHECK(isgraph('!') && !isgraph(' '));
    CHECK(iscntrl('\n') && !iscntrl('A'));
    CHECK(ispunct('!') && !ispunct('a') && !ispunct(' '));
    CHECK(isblank(' ') && isblank('\t') && !isblank('\n'));

    /* --- ctype: conversion --- */
    CHECK(toupper('a') == 'A' && toupper('Z') == 'Z' && toupper('5') == '5');
    CHECK(tolower('A') == 'a' && tolower('z') == 'z' && tolower('5') == '5');

    /* --- errno lvalue --- */
    errno = 0;
    CHECK(errno == 0);
    errno = EINVAL;
    CHECK(errno == EINVAL && EINVAL == 22);
    CHECK(EAGAIN == 11 && ETIMEDOUT == 110 && EWOULDBLOCK == EAGAIN);

    if (failures == 0) say("\nM5.3d headers: ALL PASS\n");
    else               say("\nM5.3d headers: SOME FAILED\n");
    return failures ? 1 : 0;
}
