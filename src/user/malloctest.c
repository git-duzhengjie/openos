/* ============================================================
 * openos - userspace malloc/free regression test
 * ============================================================ */

#include "openos.h"

int main(int argc, char **argv, char **envp)
{
    char *a;
    char *b;
    char *c;
    char *d;
    int *nums;
    int i;

    (void)argc;
    (void)argv;
    (void)envp;

    openos_puts("[malloctest] checking userspace heap...");

    a = (char *)openos_malloc(16);
    b = (char *)openos_malloc(24);
    if (!a || !b)
        openos_fail(1, "[malloctest] malloc failed");

    openos_str_copy(a, "hello", 16);
    openos_str_copy(b, "openos", 24);
    if (openos_strcmp(a, "hello") != 0 || openos_strcmp(b, "openos") != 0)
        openos_fail(2, "[malloctest] malloc content failed");

    if (openos_free(a) < 0)
        openos_fail(3, "[malloctest] free a failed");

    c = (char *)openos_malloc(8);
    if (!c)
        openos_fail(4, "[malloctest] reuse allocation failed");
    openos_str_copy(c, "reuse", 8);
    if (openos_strcmp(c, "reuse") != 0)
        openos_fail(5, "[malloctest] reuse content failed");

    nums = (int *)openos_calloc(8, sizeof(int));
    if (!nums)
        openos_fail(6, "[malloctest] calloc failed");
    for (i = 0; i < 8; i++) {
        if (nums[i] != 0)
            openos_fail(7, "[malloctest] calloc zero failed");
        nums[i] = i + 1;
    }

    d = (char *)openos_realloc(c, 64);
    if (!d)
        openos_fail(8, "[malloctest] realloc failed");
    if (openos_strcmp(d, "reuse") != 0)
        openos_fail(9, "[malloctest] realloc preserve failed");

    if (openos_free(b) < 0 || openos_free(nums) < 0 || openos_free(d) < 0)
        openos_fail(10, "[malloctest] final free failed");

    if (openos_free(d) == 0)
        openos_fail(11, "[malloctest] double free was not detected");

    a = (char *)openos_malloc(4000);
    if (!a)
        openos_fail(12, "[malloctest] large allocation failed");
    a[0] = 'A';
    a[3999] = 'Z';
    if (a[0] != 'A' || a[3999] != 'Z')
        openos_fail(13, "[malloctest] large content failed");
    openos_free(a);

    openos_puts("[malloctest] userspace heap ok");
    return 0;
}
