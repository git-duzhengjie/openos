#ifndef STRING_H
#define STRING_H

#include "types.h"

size_t strlen(const char* str);
size_t strnlen(const char* str, size_t maxlen);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
int strcmp(const char* a, const char* b);

#endif
