#include "openos.h"

static void print_line(const char *s)
{
    openos_write(1, s, openos_strlen(s));
}

static int same_bytes(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

int main(void)
{
    int sv[2];
    char buf[16];
    int n;

    print_line("socketpairtest: start\n");

    if (openos_socketpair(OPENOS_AF_UNSPEC, OPENOS_SOCK_STREAM, 0, sv) != 0) {
        print_line("socketpairtest: create failed\n");
        return 1;
    }

    if (openos_send(sv[0], "ping", 4, 0) != 4) {
        print_line("socketpairtest: send a failed\n");
        return 1;
    }
    n = openos_recv(sv[1], buf, sizeof(buf), 0);
    if (n != 4 || !same_bytes(buf, "ping", 4)) {
        print_line("socketpairtest: recv b failed\n");
        return 1;
    }

    if (openos_write(sv[1], "pong", 4) != 4) {
        print_line("socketpairtest: write b failed\n");
        return 1;
    }
    n = openos_read(sv[0], buf, sizeof(buf));
    if (n != 4 || !same_bytes(buf, "pong", 4)) {
        print_line("socketpairtest: read a failed\n");
        return 1;
    }

    print_line("socketpairtest: ok\n");
    return 0;
}
