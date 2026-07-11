/*
 * libc/string.c - M5.3a
 *
 * Freestanding implementation of the standard C string/memory subset.
 * Compiled with -ffreestanding -fno-builtin so that the compiler does
 * not turn these into recursive builtin calls.
 */
#include "string.h"

/* ---- memory ---- */

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    unsigned char v = (unsigned char)c;
    while (n--) {
        *d++ = v;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char v = (unsigned char)c;
    while (n--) {
        if (*p == v) {
            return (void *)p;
        }
        p++;
    }
    return (void *)0;
}

/* ---- length / compare ---- */

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) {
        p++;
    }
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && (*a == *b)) {
        a++;
        b++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* ---- copy / concat ---- */

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {
        /* copy including terminator */
    }
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && *src) {
        *d++ = *src++;
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) {
        d++;
    }
    while ((*d++ = *src++)) {
        /* append including terminator */
    }
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (*d) {
        d++;
    }
    while (n && *src) {
        *d++ = *src++;
        n--;
    }
    *d = '\0';
    return dst;
}

/* ---- search ---- */

char *strchr(const char *s, int c) {
    char ch = (char)c;
    for (;;) {
        if (*s == ch) {
            return (char *)s;
        }
        if (*s == '\0') {
            return (char *)0;
        }
        s++;
    }
}

char *strrchr(const char *s, int c) {
    char ch = (char)c;
    const char *last = (const char *)0;
    for (;;) {
        if (*s == ch) {
            last = s;
        }
        if (*s == '\0') {
            return (char *)last;
        }
        s++;
    }
}

char *strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') {
        return (char *)haystack;
    }
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (*n == '\0') {
            return (char *)haystack;
        }
    }
    return (char *)0;
}
