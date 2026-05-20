#include "string.h"

size_t strlen(const char* str)
{
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

void* memcpy(void* dest, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*) dest;
    const unsigned char* s = (const unsigned char*) src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}

void* memset(void* dest, int c, size_t n)
{
    unsigned char* d = (unsigned char*) dest;
    for (size_t i = 0; i < n; i++)
        d[i] = (unsigned char) c;
    return dest;
}

int strcmp(const char* a, const char* b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}
