/* ============================================================
 * openos - x86_64 GUI porting shim layer
 * ------------------------------------------------------------
 * Lets i386-side gui.c / gui_user.c / font.c / i18n.c compile
 * and link UNCHANGED under the x86_64 kernel.
 *
 * gui code uses i386 kernel symbol names (kmalloc/kfree/
 * serial_write/serial_putc/memcpy/memset/...), while x86_64
 * kernel provides equivalents under different names
 * (arch_x86_64_kmalloc/early_serial64_write/...). This file
 * implements the DIFFERING symbols and forwards them.
 *
 * TYPE ISOLATION RULE:
 *   gui64 units use ONLY i386 types.h (size_t = uint32_t).
 *   Memory funcs (memcpy/memset/...) use __SIZE_TYPE__ so they
 *   match GCC's auto-generated calls (64-bit size on x86_64),
 *   independent of any header's size_t.
 * ============================================================ */

/* Basic fixed-width types (avoid pulling <stdint.h> which
 * conflicts with i386 types.h size_t/int64_t definitions). */
typedef unsigned char       u8;
typedef unsigned int        u32;
typedef unsigned long long  u64;
#define SZ __SIZE_TYPE__   /* GCC's size_t, ABI-correct on x86_64 */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* ---- x86_64 kernel implementations (external symbols) ---- */
extern void  *arch_x86_64_kmalloc(u64 size);
extern void   arch_x86_64_kfree(void *ptr);
extern void   early_serial64_putc(char c);
extern void   early_serial64_write(const char *text);

/* ============================================================
 * 1. Heap: kmalloc / kfree  (i386 heap.h: void *kmalloc(uint32_t))
 * ============================================================ */
void *kmalloc(u32 size)
{
    return arch_x86_64_kmalloc((u64)size);
}

void kfree(void *ptr)
{
    arch_x86_64_kfree(ptr);
}

u32 heap_get_current(void)
{
    return 0;
}

void heap_init(void)
{
    /* x86_64 heap already initialized elsewhere */
}

/* ============================================================
 * 2. Serial output
 * ============================================================ */
void serial_putc(char c)
{
    early_serial64_putc(c);
}

void serial_write(const char *s)
{
    early_serial64_write(s);
}

void serial_init(void)
{
    /* already initialized */
}

void serial_write_hex(u32 val)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(val >> ((7 - i) * 4)) & 0xF];
    }
    buf[10] = '\0';
    early_serial64_write(buf);
}

/* ============================================================
 * 3. Memory functions
 *    Use __SIZE_TYPE__ (SZ) so signatures match GCC-emitted calls.
 * ============================================================ */
void *memcpy(void *dst, const void *src, SZ n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, SZ n)
{
    u8 *d = (u8 *)dst;
    while (n--) {
        *d++ = (u8)c;
    }
    return dst;
}

void *memmove(void *dst, const void *src, SZ n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
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

int memcmp(const void *a, const void *b, SZ n)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    while (n--) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
    }
    return 0;
}

/* ============================================================
 * 4. String functions (use SZ for count params for ABI safety)
 * ============================================================ */
SZ strlen(const char *s)
{
    SZ n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, SZ n)
{
    while (n && *a && (*a == *b)) {
        a++;
        b++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0') {
    }
    return dst;
}

char *strncpy(char *dst, const char *src, SZ n)
{
    char *d = dst;
    while (n && (*d = *src) != '\0') {
        d++;
        src++;
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) {
        d++;
    }
    while ((*d++ = *src++) != '\0') {
    }
    return dst;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}
