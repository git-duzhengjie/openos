#include "openos.h"

static void print_line(const char *s)
{
    openos_write(1, s, openos_strlen(s));
}

int main(void)
{
    openos_eventfd_t efd;
    unsigned int value;

    print_line("eventfdtest: start\n");

    if (openos_eventfd_create(&efd, 2) != 0) {
        print_line("eventfdtest: create failed\n");
        return 1;
    }

    if (openos_eventfd_write(&efd, 3) != 0) {
        print_line("eventfdtest: write failed\n");
        openos_eventfd_destroy(&efd);
        return 1;
    }

    value = 0;
    if (openos_eventfd_read(&efd, &value) != 0 || value != 5) {
        print_line("eventfdtest: read sum failed\n");
        openos_eventfd_destroy(&efd);
        return 1;
    }

    value = 99;
    if (openos_eventfd_read(&efd, &value) != 0 || value != 0) {
        print_line("eventfdtest: read clear failed\n");
        openos_eventfd_destroy(&efd);
        return 1;
    }

    if (openos_eventfd_destroy(&efd) != 0 || efd != 0) {
        print_line("eventfdtest: destroy failed\n");
        return 1;
    }

    print_line("eventfdtest: ok\n");
    return 0;
}
