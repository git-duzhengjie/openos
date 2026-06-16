#include "unit_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_unit_test_failed;
static int g_unit_test_count;

static void unit_test_fail_at(const char *file, int line, const char *message)
{
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, message);
    g_unit_test_failed = 1;
}

void unit_test_run(const char *name, unit_test_fn_t fn)
{
    printf("[ RUN      ] %s\n", name);
    fn();
    g_unit_test_count++;
    printf("[       OK ] %s\n", name);
}

void unit_test_assert_true(int ok, const char *expr, const char *file, int line)
{
    if (!ok) {
        unit_test_fail_at(file, line, expr);
        exit(1);
    }
}

void unit_test_assert_int_eq(long long expected, long long actual,
                             const char *expected_expr,
                             const char *actual_expr,
                             const char *file, int line)
{
    if (expected != actual) {
        fprintf(stderr,
                "%s:%d: assertion failed: %s == %s (expected=%lld actual=%lld)\n",
                file, line, expected_expr, actual_expr, expected, actual);
        g_unit_test_failed = 1;
        exit(1);
    }
}

void unit_test_assert_size_eq(size_t expected, size_t actual,
                              const char *expected_expr,
                              const char *actual_expr,
                              const char *file, int line)
{
    if (expected != actual) {
        fprintf(stderr,
                "%s:%d: assertion failed: %s == %s (expected=%zu actual=%zu)\n",
                file, line, expected_expr, actual_expr, expected, actual);
        g_unit_test_failed = 1;
        exit(1);
    }
}

void unit_test_assert_str_eq(const char *expected, const char *actual,
                             const char *expected_expr,
                             const char *actual_expr,
                             const char *file, int line)
{
    if (expected == NULL || actual == NULL || strcmp(expected, actual) != 0) {
        fprintf(stderr,
                "%s:%d: assertion failed: %s == %s (expected=%s actual=%s)\n",
                file, line, expected_expr, actual_expr,
                expected != NULL ? expected : "(null)",
                actual != NULL ? actual : "(null)");
        g_unit_test_failed = 1;
        exit(1);
    }
}

int unit_test_finish(void)
{
    if (g_unit_test_failed) {
        fprintf(stderr, "[  FAILED  ] %d test(s) executed\n", g_unit_test_count);
        return 1;
    }

    printf("[  PASSED  ] %d test(s)\n", g_unit_test_count);
    return 0;
}
