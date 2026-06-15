#include "openos.h"

static int expect_errno(const char *name, int condition)
{
    if (!condition) {
        openos_printf("errnotest: FAIL %s errno=%d\n", name, openos_get_errno());
        return 1;
    }
    openos_printf("errnotest: PASS %s errno=%d\n", name, openos_get_errno());
    return 0;
}

int main(int argc, char **argv)
{
    int failures = 0;
    int fd;
    void *ptr;

    (void)argc;
    (void)argv;

    openos_set_errno(123);
    fd = openos_open("/no_such_errno_file", O_RDONLY, 0);
    failures += expect_errno("open missing", fd == -1 && openos_get_errno() != 0);

    openos_set_errno(123);
    fd = openos_open("/bin/hello", O_RDONLY, 0);
    failures += expect_errno("open clears", fd >= 0 && openos_get_errno() == 0);
    if (fd >= 0)
        openos_close(fd);

    openos_set_errno(123);
    failures += expect_errno("close bad fd", openos_close(-1) == -1 && openos_get_errno() != 0);

    openos_set_errno(123);
    ptr = openos_malloc(0);
    failures += expect_errno("malloc zero", ptr == 0 && openos_get_errno() == OPENOS_EINVAL);

    openos_set_errno(123);
    ptr = openos_malloc(16);
    failures += expect_errno("malloc clears", ptr != 0 && openos_get_errno() == 0);

    openos_set_errno(123);
    failures += expect_errno("free clears", openos_free(ptr) == 0 && openos_get_errno() == 0);

    openos_set_errno(123);
    failures += expect_errno("double free", openos_free(ptr) == -1 && openos_get_errno() == OPENOS_EINVAL);

    if (failures) {
        openos_printf("errnotest: %d failure(s)\n", failures);
        return 1;
    }

    openos_printf("errnotest: all tests passed\n");
    return 0;
}
