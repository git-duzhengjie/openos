#include "openos.h"

static void print_line(const char *s)
{
    openos_write(1, s, openos_strlen(s));
}

int main(void)
{
    openos_shm_t shm;
    char *a;
    char *b;

    print_line("shmtest: start\n");

    if (openos_shm_create(&shm) < 0) {
        print_line("shmtest: create failed\n");
        return 1;
    }

    a = (char *)openos_shm_map(&shm);
    b = (char *)openos_shm_map(&shm);
    if ((int)a == -1 || (int)b == -1 || a == b) {
        print_line("shmtest: map failed\n");
        openos_shm_destroy(&shm);
        return 1;
    }

    a[0] = 'S';
    a[1] = 'H';
    a[2] = 'M';
    a[3] = 0;
    if (b[0] != 'S' || b[1] != 'H' || b[2] != 'M' || b[3] != 0) {
        print_line("shmtest: mirror failed\n");
        openos_shm_destroy(&shm);
        return 1;
    }

    b[1] = 'A';
    if (a[0] != 'S' || a[1] != 'A' || a[2] != 'M') {
        print_line("shmtest: reverse mirror failed\n");
        openos_shm_destroy(&shm);
        return 1;
    }

    if (openos_munmap(a, 4096) != 0 || openos_munmap(b, 4096) != 0) {
        print_line("shmtest: munmap failed\n");
        openos_shm_destroy(&shm);
        return 1;
    }

    if (openos_shm_destroy(&shm) != 0 || shm != 0) {
        print_line("shmtest: destroy failed\n");
        return 1;
    }

    print_line("shmtest: ok\n");
    return 0;
}
