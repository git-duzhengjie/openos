#ifndef STRING_H
#define STRING_H

#include "types.h"

size_t strlen(const char* str);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
int strcmp(const char* a, const char* b);

#endif
