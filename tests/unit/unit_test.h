#ifndef OPENOS_UNIT_TEST_H
#define OPENOS_UNIT_TEST_H

#include <stddef.h>

#define UNIT_TEST_CASE(name) static void name(void)
#define UNIT_TEST_RUN(name) unit_test_run(#name, name)

#define ASSERT_TRUE(expr) \
    unit_test_assert_true((expr) != 0, #expr, __FILE__, __LINE__)

#define ASSERT_FALSE(expr) \
    unit_test_assert_true((expr) == 0, "!(" #expr ")", __FILE__, __LINE__)

#define ASSERT_EQ_INT(expected, actual) \
    unit_test_assert_int_eq((long long)(expected), (long long)(actual), \
                            #expected, #actual, __FILE__, __LINE__)

#define ASSERT_EQ_SIZE(expected, actual) \
    unit_test_assert_size_eq((size_t)(expected), (size_t)(actual), \
                             #expected, #actual, __FILE__, __LINE__)

#define ASSERT_STREQ(expected, actual) \
    unit_test_assert_str_eq((expected), (actual), \
                            #expected, #actual, __FILE__, __LINE__)

typedef void (*unit_test_fn_t)(void);

void unit_test_run(const char *name, unit_test_fn_t fn);
void unit_test_assert_true(int ok, const char *expr, const char *file, int line);
void unit_test_assert_int_eq(long long expected, long long actual,
                             const char *expected_expr,
                             const char *actual_expr,
                             const char *file, int line);
void unit_test_assert_size_eq(size_t expected, size_t actual,
                              const char *expected_expr,
                              const char *actual_expr,
                              const char *file, int line);
void unit_test_assert_str_eq(const char *expected, const char *actual,
                             const char *expected_expr,
                             const char *actual_expr,
                             const char *file, int line);
int unit_test_finish(void);

#endif
