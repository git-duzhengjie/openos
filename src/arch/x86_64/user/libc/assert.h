/*
 * M5.3d libc/assert.h — diagnostics for OpenOS ring3 userland.
 *
 * Follows the standard: when NDEBUG is defined assert() expands to a no-op;
 * otherwise a failed predicate reports file/line/function and aborts. The
 * header is intentionally re-includable so NDEBUG can be toggled per TU.
 */
#undef assert

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else

void __assert_fail(const char *expr, const char *file,
                   unsigned int line, const char *func);

#define assert(expr) \
    ((expr) ? (void)0 \
            : __assert_fail(#expr, __FILE__, __LINE__, __func__))

#endif /* NDEBUG */
