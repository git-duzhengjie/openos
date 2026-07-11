/*
 * M5.3d libc/assert.c — assertion failure handler for OpenOS ring3 userland.
 *
 * Reports the failed expression with file/line/function to stderr and aborts.
 * Kept in its own TU (never compiled with NDEBUG) so the reporting path is
 * always available to translation units that enable assertions.
 */
#include "stdio.h"
#include "stdlib.h"

void __assert_fail(const char *expr, const char *file,
                   unsigned int line, const char *func)
{
    fprintf(stderr, "%s:%u: %s: Assertion `%s' failed.\n",
            file, line, func, expr);
    abort();
}
