/*
 * M5.3b libc/stdlib.c — standard C stdlib subset for OpenOS ring3 userland.
 *
 * Heap allocator design:
 *   - Blocks carry an 8-byte-aligned header {size, free-flag, next-free}.
 *   - malloc: first-fit over an explicit free list; splits oversized blocks.
 *   - free: pushes back to the free list and coalesces physically-adjacent
 *           neighbours to fight fragmentation.
 *   - Growth: __libc_sbrk() extends the break in >=64 KiB chunks.
 *
 * The sbrk backend is provided by libc_sbrk.c on the target (SYS_SBRK=253) and
 * overridden by the host unit test, so this file stays syscall-agnostic.
 */
#include "stdlib.h"

/* Provided by the target crt (libc_sbrk.c) or the host test harness. */
extern void *__libc_sbrk(long increment);

/* ---------- heap allocator ---------- */

#define ALIGN_UP(n, a)   (((n) + ((a) - 1)) & ~((size_t)(a) - 1))
#define HEAP_ALIGN       16u
#define HEAP_CHUNK       (64u * 1024u)   /* sbrk growth granularity */

typedef struct block_header {
    size_t               size;   /* usable payload bytes (excl. header) */
    int                  free;   /* 1 = on free list */
    struct block_header *next;   /* physical next block (address order) */
    struct block_header *fnext;  /* free-list link (valid when free==1) */
} block_header;

#define HDR_SZ  ALIGN_UP(sizeof(block_header), HEAP_ALIGN)

static block_header *g_heap_head;   /* first block, address order */
static block_header *g_heap_tail;   /* last block, for growth append */
static block_header *g_free_list;   /* explicit free list head */

static void freelist_push(block_header *b)
{
    b->free  = 1;
    b->fnext = g_free_list;
    g_free_list = b;
}

static void freelist_remove(block_header *b)
{
    block_header **pp = &g_free_list;
    while (*pp) {
        if (*pp == b) { *pp = b->fnext; b->fnext = NULL; break; }
        pp = &(*pp)->fnext;
    }
    b->free = 0;
}

/* Grow the heap by at least `need` payload bytes; returns a fresh block. */
static block_header *heap_grow(size_t need)
{
    size_t want = ALIGN_UP(need + HDR_SZ, HEAP_CHUNK);
    void  *base = __libc_sbrk((long)want);
    if (base == (void *)-1 || base == NULL)
        return NULL;

    block_header *b = (block_header *)base;
    b->size  = want - HDR_SZ;
    b->free  = 0;
    b->next  = NULL;
    b->fnext = NULL;

    if (!g_heap_head) {
        g_heap_head = b;
        g_heap_tail = b;
    } else {
        g_heap_tail->next = b;
        g_heap_tail = b;
    }
    return b;
}

/* Split `b` so it holds exactly `size` payload; remainder becomes a free block. */
static void block_split(block_header *b, size_t size)
{
    size_t remain = b->size - size;
    if (remain < HDR_SZ + HEAP_ALIGN)
        return;   /* not worth splitting */

    block_header *n = (block_header *)((char *)b + HDR_SZ + size);
    n->size  = remain - HDR_SZ;
    n->free  = 0;
    n->next  = b->next;
    n->fnext = NULL;
    b->next  = n;
    if (g_heap_tail == b)
        g_heap_tail = n;
    b->size = size;
    freelist_push(n);
}

void *malloc(size_t size)
{
    if (size == 0)
        return NULL;
    size = ALIGN_UP(size, HEAP_ALIGN);

    block_header *b = g_free_list;
    while (b) {
        if (b->size >= size) {
            freelist_remove(b);
            block_split(b, size);
            return (char *)b + HDR_SZ;
        }
        b = b->fnext;
    }

    b = heap_grow(size);
    if (!b)
        return NULL;
    block_split(b, size);
    return (char *)b + HDR_SZ;
}

/* Coalesce physically adjacent free blocks starting from g_heap_head. */
static void heap_coalesce(void)
{
    block_header *b = g_heap_head;
    while (b && b->next) {
        block_header *n = b->next;
        int adjacent = ((char *)b + HDR_SZ + b->size == (char *)n);
        if (b->free && n->free && adjacent) {
            freelist_remove(n);
            b->size += HDR_SZ + n->size;
            b->next  = n->next;
            if (g_heap_tail == n)
                g_heap_tail = b;
            continue;   /* try merging the new neighbour too */
        }
        b = b->next;
    }
}

void free(void *ptr)
{
    if (!ptr)
        return;
    block_header *b = (block_header *)((char *)ptr - HDR_SZ);
    if (b->free)
        return;   /* double-free guard */
    freelist_push(b);
    heap_coalesce();
}

void *calloc(size_t nmemb, size_t size)
{
    if (nmemb && size > (size_t)-1 / nmemb)
        return NULL;   /* overflow */
    size_t total = nmemb * size;
    void  *p = malloc(total);
    if (p) {
        char *d = (char *)p;
        for (size_t i = 0; i < total; i++)
            d[i] = 0;
    }
    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    block_header *b = (block_header *)((char *)ptr - HDR_SZ);
    size_t want = ALIGN_UP(size, HEAP_ALIGN);
    if (b->size >= want) {
        block_split(b, want);
        return ptr;
    }
    void *n = malloc(size);
    if (!n)
        return NULL;
    char *s = (char *)ptr, *d = (char *)n;
    for (size_t i = 0; i < b->size; i++)
        d[i] = s[i];
    free(ptr);
    return n;
}

/* ---------- numeric conversion ---------- */

static int digit_val(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

long strtol(const char *nptr, char **endptr, int base)
{
    const char *p = nptr;
    while (is_space((unsigned char)*p)) p++;

    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }

    if ((base == 0 || base == 16) &&
        p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        digit_val((unsigned char)p[2]) >= 0 && digit_val((unsigned char)p[2]) < 16) {
        p += 2; base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }

    long acc = 0;
    int any = 0;
    for (;;) {
        int d = digit_val((unsigned char)*p);
        if (d < 0 || d >= base) break;
        acc = acc * base + d;
        any = 1;
        p++;
    }
    if (endptr)
        *endptr = (char *)(any ? p : nptr);
    return neg ? -acc : acc;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    return (unsigned long)strtol(nptr, endptr, base);
}

int atoi(const char *nptr)  { return (int)strtol(nptr, NULL, 10); }
long atol(const char *nptr) { return strtol(nptr, NULL, 10); }

/* ---------- integer math ---------- */

int  abs(int j)   { return j < 0 ? -j : j; }
long labs(long j) { return j < 0 ? -j : j; }

/* ---------- search / sort ---------- */

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void *p = (const char *)base + mid * size;
        int c = compar(key, p);
        if (c < 0)      hi = mid;
        else if (c > 0) lo = mid + 1;
        else            return (void *)p;
    }
    return NULL;
}

static void mem_swap(char *a, char *b, size_t size)
{
    while (size--) { char t = *a; *a++ = *b; *b++ = t; }
}

/* Simple insertion sort: stable enough for a freestanding subset. */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    char *arr = (char *)base;
    for (size_t i = 1; i < nmemb; i++) {
        for (size_t j = i; j > 0; j--) {
            char *cur  = arr + j * size;
            char *prev = arr + (j - 1) * size;
            if (compar(prev, cur) <= 0)
                break;
            mem_swap(prev, cur, size);
        }
    }
}

/* ---------- pseudo-random (glibc-style LCG) ---------- */

static unsigned long g_rand_state = 1;

int rand(void)
{
    g_rand_state = g_rand_state * 1103515245UL + 12345UL;
    return (int)((g_rand_state >> 16) & 0x7fffffffUL);
}

void srand(unsigned int seed) { g_rand_state = seed; }
