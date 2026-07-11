/*
 * M5.3b libc/stdlib.h — standard C stdlib subset for OpenOS ring3 userland.
 *
 * Provides the malloc family (heap allocator backed by SYS_SBRK=253) plus the
 * common numeric/util helpers third-party software expects. Freestanding: no
 * host libc dependency; size_t is defined locally to avoid pulling <stddef.h>.
 */
#ifndef OPENOS_LIBC_STDLIB_H
#define OPENOS_LIBC_STDLIB_H

#ifndef OPENOS_LIBC_SIZE_T_DEFINED
#define OPENOS_LIBC_SIZE_T_DEFINED
typedef unsigned long  size_t;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

/* --- heap allocator (freelist + coalescing, sbrk-backed) --- */
void  *malloc(size_t size);
void   free(void *ptr);
void  *calloc(size_t nmemb, size_t size);
void  *realloc(void *ptr, size_t size);

/* --- numeric conversion --- */
int         atoi(const char *nptr);
long        atol(const char *nptr);
long        strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);

/* --- integer math --- */
int  abs(int j);
long labs(long j);

/* --- search / sort --- */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
void  qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));

/* --- pseudo-random --- */
#define RAND_MAX 2147483647
int  rand(void);
void srand(unsigned int seed);

/* --- process termination --- */
void _Exit(int status);
void exit(int status);
void abort(void);

#endif /* OPENOS_LIBC_STDLIB_H */
