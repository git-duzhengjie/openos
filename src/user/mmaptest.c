#include "openos.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *p = (char *)openos_mmap(0, 8192, 0);
    if ((int)p == -1) {
        openos_write_fd(2, "mmaptest: mmap failed\n", 23);
        return 1;
    }

    p[0] = 'O';
    p[1] = 'K';
    p[4096] = '!';

    if (p[0] != 'O' || p[1] != 'K' || p[4096] != '!') {
        openos_write_fd(2, "mmaptest: memory check failed\n", 31);
        return 2;
    }

    if (openos_munmap(p, 8192) != 0) {
        openos_write_fd(2, "mmaptest: munmap failed\n", 25);
        return 3;
    }

    openos_write_fd(1, "mmaptest: ok\n", 13);
    return 0;
}
