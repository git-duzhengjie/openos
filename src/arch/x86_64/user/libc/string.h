/*
 * libc/string.h - M5.3a
 *
 * Standard C string / memory routines (freestanding subset).
 * Exports standard symbol names (memcpy/memset/strlen/...) so that
 * third-party sources can link against them without openos64_* shims.
 *
 * No libc dependency; pure computation.
 */
#ifndef OPENOS_LIBC_STRING_H
#define OPENOS_LIBC_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* memory */
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);

/* string length / compare */
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);

/* string copy / concat */
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);

/* string search */
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_LIBC_STRING_H */
