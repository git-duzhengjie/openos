#include "openos.h"

int main(int argc, char **argv)
{
    char buf[8];
    int fd;
    int n;

    if (argc < 2) {
        return 2;
    }

    fd = openos_atoi(argv[1]);
    n = openos_read(fd, buf, 6);
    if (n != 6) {
        return 3;
    }
    if (buf[0] != 's' || buf[1] != 'p' || buf[2] != 'a' ||
        buf[3] != 'w' || buf[4] != 'n' || buf[5] != '!') {
        return 4;
    }
    return 0;
}
