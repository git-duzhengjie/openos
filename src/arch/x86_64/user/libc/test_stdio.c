/*
 * M5.3c host unit test for libc/stdio.c
 *
 * Provides a capturing __libc_write so printf/puts/fwrite output can be
 * verified, and exercises the snprintf formatting engine against the host
 * libc's snprintf for cross-checking.
 *
 * Build & run:
 *   gcc -Wall -Wextra -o /tmp/t_stdio test_stdio.c stdio.c && /tmp/t_stdio
 */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>   /* host write() for the test report, unaffected by our printf */

/* Tiny report printer that bypasses our overriding printf(): builds a line and
 * writes it straight to fd 2 via the host write() syscall wrapper. */
static void report(const char *msg)
{
    size_t n = 0; while (msg[n]) n++;
    ssize_t rc = write(2, msg, n); (void)rc;
}
static void report_int(int v)
{
    char b[16]; int i = 0, neg = v < 0; unsigned u = neg ? -v : v;
    if (u == 0) b[i++] = '0';
    while (u) { b[i++] = '0' + u % 10; u /= 10; }
    if (neg) b[i++] = '-';
    char o[16]; int j = 0;
    while (i) o[j++] = b[--i];
    o[j] = 0; report(o);
}

/* Our stdio.h clashes on names with host's; include ours in a namespace by
 * declaring the symbols we test directly (they live in stdio.c). */
typedef struct _openos_FILE { int fd; int error; int eof; } OFILE;
extern OFILE *o_stdout __asm__("stdout");   /* stdio.c's stdout, aliased to avoid host clash */
int   o_snprintf(char *, unsigned long, const char *, ...) __asm__("snprintf");
int   o_printf(const char *, ...)                          __asm__("printf");
int   o_puts(const char *)                                 __asm__("puts");
int   o_putchar(int)                                       __asm__("putchar");
unsigned long o_fwrite(const void *, unsigned long, unsigned long, void *) __asm__("fwrite");

/* --- capturing backend --- */
static char capbuf[8192];
static unsigned long caplen;
long __libc_write(int fd, const void *buf, unsigned long len)
{
    (void)fd;
    if (caplen + len < sizeof(capbuf)) {
        memcpy(capbuf + caplen, buf, len);
        caplen += len;
    }
    return (long)len;
}
static void capreset(void) { caplen = 0; capbuf[0] = 0; }
static const char *capstr(void) { capbuf[caplen] = 0; return capbuf; }

static int fails = 0;
#define CHK_STR(expr, want) do { \
    const char *g = (expr); \
    if (strcmp(g, want) != 0) { \
        report("FAIL CHK_STR got=["); report(g); report("] want=["); report(want); report("]\n"); \
        fails++; \
    } \
} while (0)

static char sb[512];
#define SNP(want, ...) do { \
    o_snprintf(sb, sizeof(sb), __VA_ARGS__); \
    if (strcmp(sb, want) != 0) { \
        report("FAIL SNP got=["); report(sb); report("] want=["); report(want); report("]\n"); \
        fails++; \
    } \
} while (0)

static void test_basic(void)
{
    SNP("hello", "hello");
    SNP("a%b", "a%%b");
    SNP("42", "%d", 42);
    SNP("-42", "%d", -42);
    SNP("+42", "%+d", 42);
    SNP(" 42", "% d", 42);
    SNP("0", "%d", 0);
    SNP("4294967295", "%u", 4294967295U);
    SNP("ff", "%x", 255);
    SNP("FF", "%X", 255);
    SNP("0xff", "%#x", 255);
    SNP("777", "%o", 511);
    SNP("c", "%c", 'c');
    SNP("str", "%s", "str");
    SNP("(null)", "%s", (char *)0);
}

static void test_width_prec(void)
{
    SNP("   42", "%5d", 42);
    SNP("42   ", "%-5d", 42);
    SNP("00042", "%05d", 42);
    SNP("  abc", "%5s", "abc");
    SNP("abc  ", "%-5s", "abc");
    SNP("ab", "%.2s", "abcdef");
    SNP("00042", "%.5d", 42);
    SNP("  00042", "%7.5d", 42);
    SNP("3.14", "%s", "3.14");
    SNP("   42", "%*d", 5, 42);
    SNP("42   ", "%-*d", 5, 42);
    SNP("ab", "%.*s", 2, "abcdef");
}

static void test_long_ptr(void)
{
    SNP("9999999999", "%ld", 9999999999L);
    SNP("18446744073709551615", "%llu", (unsigned long long)-1);
    SNP("deadbeef", "%lx", 0xdeadbeefUL);
    char pb[64];
    o_snprintf(pb, sizeof(pb), "%p", (void *)0x1234);
    if (strcmp(pb, "0x1234") != 0) { report("FAIL ptr got=["); report(pb); report("]\n"); fails++; }
}

static void test_snprintf_truncate(void)
{
    char b[6];
    int r = o_snprintf(b, sizeof(b), "%d", 1234567);   /* "1234567" len 7 */
    if (strcmp(b, "12345") != 0) { report("FAIL trunc buf=["); report(b); report("]\n"); fails++; }
    if (r != 7) { report("FAIL trunc ret="); report_int(r); report(" want 7\n"); fails++; }

    r = o_snprintf(b, 0, "%d", 42);   /* size 0: no write, still count */
    if (r != 2) { report("FAIL size0 ret="); report_int(r); report(" want 2\n"); fails++; }
}

static void test_output(void)
{
    capreset();
    o_printf("x=%d y=%s", 7, "hi");
    CHK_STR(capstr(), "x=7 y=hi");

    capreset();
    o_puts("line");
    CHK_STR(capstr(), "line\n");

    capreset();
    o_putchar('Z');
    CHK_STR(capstr(), "Z");

    capreset();
    o_fwrite("abcdef", 1, 6, o_stdout);
    CHK_STR(capstr(), "abcdef");

    capreset();
    o_fwrite("abcd", 2, 2, o_stdout);   /* 4 bytes, 2 elems */
    CHK_STR(capstr(), "abcd");
}

int main(void)
{
    test_basic();
    test_width_prec();
    test_long_ptr();
    test_snprintf_truncate();
    test_output();
    if (fails == 0) report("M5.3c stdio.c: ALL PASS\n");
    else          { report("M5.3c stdio.c: "); report_int(fails); report(" FAIL\n"); }
    return fails ? 1 : 0;
}
