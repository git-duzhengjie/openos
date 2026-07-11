/*
 * M5.3e libc_demo64.c — end-to-end integration test for the standard C
 * library subset (M5.3a..d) exercised from a real ring3 process.
 *
 * This program deliberately calls the STANDARD symbol names exported by
 * libc/ (memcpy, strcmp, malloc, qsort, printf, snprintf, ...) rather than
 * the private openos64_* API. If it links and runs to "[libc] PASS", it
 * proves third-party C sources can build against openos' libc subset.
 *
 * Launch chain tail: /bin/thread_demo execve's into /bin/libc_demo.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "libc/string.h"
#include "libc/stdlib.h"
#include "libc/stdio.h"
#include "openos64.h"    /* M5.4a: openos64_execve for launch-chain tail */

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *name, int cond)
{
    if (cond) {
        g_pass++;
        printf("  PASS  %s\n", name);
    } else {
        g_fail++;
        printf("  FAIL  %s\n", name);
    }
}

/* qsort comparator: ascending int */
static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

int openos64_main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    printf("\n[libc_demo] M5.3e standard C library subset end-to-end test\n");

    /* ---- string.h ---- */
    {
        char buf[32];
        memset(buf, 'A', sizeof(buf));
        check("memset fills", buf[0] == 'A' && buf[31] == 'A');

        strcpy(buf, "hello");
        check("strcpy/strlen", strlen(buf) == 5);
        check("strcmp equal", strcmp(buf, "hello") == 0);
        check("strcmp order", strcmp("abc", "abd") < 0);
        check("strncmp", strncmp("hello", "help", 3) == 0);

        strcat(buf, " world");
        check("strcat", strcmp(buf, "hello world") == 0);

        check("strchr", strchr(buf, 'w') == buf + 6);
        check("strrchr", strrchr(buf, 'o') == buf + 7);
        check("strstr", strstr(buf, "world") == buf + 6);

        char dst[16];
        memcpy(dst, "12345", 6);
        check("memcpy", memcmp(dst, "12345", 6) == 0);

        char ov[16];
        memcpy(ov, "abcdefgh", 9);
        memmove(ov + 2, ov, 6); /* overlapping */
        check("memmove overlap", memcmp(ov, "ababcdef", 8) == 0);
    }

    /* ---- stdlib.h: numeric conversion ---- */
    {
        check("atoi", atoi("  -42abc") == -42);
        check("atol", atol("100000") == 100000L);
        check("strtol base10", strtol("789", NULL, 10) == 789);
        check("strtol hex", strtol("0x1F", NULL, 16) == 31);
        check("strtol auto-hex", strtol("0xff", NULL, 0) == 255);
        check("strtoul", strtoul("4000000000", NULL, 10) == 4000000000UL);
        check("abs", abs(-7) == 7);
        check("labs", labs(-123456L) == 123456L);
    }

    /* ---- stdlib.h: malloc/free/calloc/realloc ---- */
    {
        void *p = malloc(64);
        check("malloc non-null", p != NULL);
        memset(p, 0x5A, 64);
        check("malloc writable", ((unsigned char *)p)[63] == 0x5A);

        void *q = realloc(p, 256);
        check("realloc grow", q != NULL);
        check("realloc preserves", ((unsigned char *)q)[63] == 0x5A);
        free(q);

        int *arr = (int *)calloc(16, sizeof(int));
        check("calloc non-null", arr != NULL);
        int zero = 1;
        for (int i = 0; i < 16; i++) if (arr[i] != 0) zero = 0;
        check("calloc zeroed", zero == 1);
        free(arr);

        /* stress: many small allocs then free */
        void *ptrs[32];
        int all_ok = 1;
        for (int i = 0; i < 32; i++) {
            ptrs[i] = malloc((size_t)(i + 1) * 8);
            if (!ptrs[i]) all_ok = 0;
        }
        for (int i = 0; i < 32; i++) free(ptrs[i]);
        check("malloc stress 32x", all_ok == 1);
    }

    /* ---- stdlib.h: qsort / bsearch ---- */
    {
        int a[8] = { 5, 3, 8, 1, 9, 2, 7, 4 };
        qsort(a, 8, sizeof(int), cmp_int);
        int sorted = 1;
        for (int i = 1; i < 8; i++) if (a[i - 1] > a[i]) sorted = 0;
        check("qsort ascending", sorted == 1);

        int key = 7;
        int *found = (int *)bsearch(&key, a, 8, sizeof(int), cmp_int);
        check("bsearch found", found != NULL && *found == 7);

        int missing = 6;
        int *nf = (int *)bsearch(&missing, a, 8, sizeof(int), cmp_int);
        check("bsearch missing", nf == NULL);
    }

    /* ---- stdio.h: snprintf formatting engine ---- */
    {
        char sb[64];
        int n;

        n = snprintf(sb, sizeof(sb), "%d %u %x", -5, 42u, 255u);
        check("snprintf %d/%u/%x", strcmp(sb, "-5 42 ff") == 0 && n == 8);

        snprintf(sb, sizeof(sb), "[%5d][%-5d][%05d]", 42, 42, 42);
        check("snprintf width/flags", strcmp(sb, "[   42][42   ][00042]") == 0);

        snprintf(sb, sizeof(sb), "%s=%c pct%%", "key", 'X');
        check("snprintf %s/%c/%%", strcmp(sb, "key=X pct%") == 0);

        snprintf(sb, sizeof(sb), "%ld %#x", 100000L, 255u);
        check("snprintf %ld/%#x", strcmp(sb, "100000 0xff") == 0);

        /* truncation: returns full length even when clipped */
        char small[4];
        n = snprintf(small, sizeof(small), "abcdef");
        check("snprintf truncation", n == 6 && strcmp(small, "abc") == 0);
    }

    /* ---- summary ---- */
    printf("[libc_demo] results: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("[libc] PASS\n");
        /* M5.4a launch-chain tail: hand off to /bin/fs_demo to exercise the
         * writable VFS (mkdir/open/write/read/lseek/stat/unlink/rmdir). */
        {
            char *const argv[] = { "/bin/fs_demo", 0 };
            char *const envp[] = { 0 };
            printf("[libc_demo] execve /bin/fs_demo (M5.4a)...\n");
            openos64_execve("/bin/fs_demo", argv, envp);
            /* execve only returns on failure */
            printf("[libc_demo] execve /bin/fs_demo FAILED\n");
        }
        return 0;
    }
    printf("[libc] FAIL\n");
    return 1;
}
