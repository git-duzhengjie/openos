#include "openos.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *base = (char *)openos_sbrk(0);
    char *p = (char *)openos_sbrk(5000);
    if ((int)base == -1 || p != base) {
        openos_write_fd(2, "sbrktest: initial sbrk failed\n", 30);
        return 1;
    }

    p[0] = 'h';
    p[4096] = 'i';
    p[4999] = '!';
    if (p[0] != 'h' || p[4096] != 'i' || p[4999] != '!') {
        openos_write_fd(2, "sbrktest: memory check failed\n", 31);
        return 2;
    }

    if ((int)openos_sbrk(-904) == -1) {
        openos_write_fd(2, "sbrktest: shrink failed\n", 24);
        return 3;
    }

    if (openos_brk(base) != 0) {
        openos_write_fd(2, "sbrktest: brk reset failed\n", 27);
        return 4;
    }

    openos_write_fd(1, "sbrktest: ok\n", 13);
    return 0;
}
