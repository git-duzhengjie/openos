/*
 * M5.3b host unit test for libc/stdlib.c (allocator + numeric + sort/search).
 * Supplies its own __libc_sbrk backed by a static arena so the allocator core
 * can be exercised without any syscall. Build:
 *   gcc -I. -o /tmp/t_stdlib test_stdlib.c stdlib.c && /tmp/t_stdlib
 */
#include <stdio.h>
#include <string.h>
#include "stdlib.h"

/* ---- fake sbrk over a static arena ---- */
static char   g_arena[4 * 1024 * 1024];
static size_t g_used = 0;

void *__libc_sbrk(long increment)
{
    if (increment < 0) return (void *)-1;
    if (g_used + (size_t)increment > sizeof(g_arena))
        return (void *)-1;
    void *p = &g_arena[g_used];
    g_used += (size_t)increment;
    return p;
}

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    printf("FAIL: %s (line %d)\n", #cond, __LINE__); g_fail++; } } while (0)

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

int main(void)
{
    /* malloc / free basics */
    void *a = malloc(100);
    void *b = malloc(200);
    CHECK(a && b && a != b);
    memset(a, 0xAB, 100);
    memset(b, 0xCD, 200);
    CHECK(((unsigned char *)a)[99] == 0xAB);
    CHECK(((unsigned char *)b)[199] == 0xCD);
    free(a);
    free(b);

    /* reuse after free (freelist) */
    void *c = malloc(50);
    CHECK(c != NULL);
    free(c);

    /* calloc zeroing + overflow guard */
    int *arr = calloc(16, sizeof(int));
    CHECK(arr != NULL);
    for (int i = 0; i < 16; i++) CHECK(arr[i] == 0);
    CHECK(calloc((size_t)-1, 2) == NULL);
    free(arr);

    /* realloc grow preserves data */
    char *s = malloc(8);
    memcpy(s, "1234567", 8);
    s = realloc(s, 64);
    CHECK(s && strcmp(s, "1234567") == 0);
    free(s);
    CHECK(realloc(NULL, 32) != NULL);   /* == malloc */

    /* many allocs to force heap growth + coalescing */
    void *v[64];
    for (int i = 0; i < 64; i++) { v[i] = malloc(1024); CHECK(v[i]); }
    for (int i = 0; i < 64; i++) free(v[i]);
    void *big = malloc(60 * 1024);      /* should reuse coalesced space */
    CHECK(big != NULL);
    free(big);

    /* strtol / atoi */
    char *end;
    CHECK(strtol("  -42xyz", &end, 10) == -42 && *end == 'x');
    CHECK(strtol("0x1F", NULL, 16) == 31);
    CHECK(strtol("0x1F", NULL, 0)  == 31);
    CHECK(strtol("077", NULL, 0)   == 63);   /* octal */
    CHECK(strtol("101", NULL, 2)   == 5);
    CHECK(atoi("12345") == 12345);
    CHECK(atol("-99") == -99);

    /* abs / labs */
    CHECK(abs(-7) == 7 && abs(7) == 7);
    CHECK(labs(-100000L) == 100000L);

    /* qsort + bsearch */
    int data[] = { 9, 3, 7, 1, 8, 2, 5, 4, 6, 0 };
    qsort(data, 10, sizeof(int), cmp_int);
    for (int i = 0; i < 10; i++) CHECK(data[i] == i);
    int key = 7;
    int *found = bsearch(&key, data, 10, sizeof(int), cmp_int);
    CHECK(found && *found == 7);
    int miss = 42;
    CHECK(bsearch(&miss, data, 10, sizeof(int), cmp_int) == NULL);

    /* rand determinism */
    srand(1);
    int r1 = rand();
    srand(1);
    CHECK(rand() == r1);
    CHECK(r1 >= 0 && r1 <= RAND_MAX);

    if (g_fail == 0)
        printf("M5.3b stdlib.c: ALL PASS\n");
    else
        printf("M5.3b stdlib.c: %d FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
