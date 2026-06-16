#include "openos.h"

static void print_line(const char *s)
{
    openos_write(1, s, openos_strlen(s));
}

int main(void)
{
    openos_mq_t mq;
    char buf[32];
    int ret;

    print_line("mqtest: start\n");

    if (openos_mq_create(&mq) < 0) {
        print_line("mqtest: create failed\n");
        return 1;
    }

    ret = openos_mq_send(&mq, "hello", 6);
    if (ret != 6) {
        print_line("mqtest: send failed\n");
        openos_mq_destroy(&mq);
        return 1;
    }

    ret = openos_mq_recv(&mq, buf, sizeof(buf));
    if (ret != 6 || buf[0] != 'h' || buf[1] != 'e' || buf[2] != 'l' ||
        buf[3] != 'l' || buf[4] != 'o' || buf[5] != 0) {
        print_line("mqtest: recv mismatch\n");
        openos_mq_destroy(&mq);
        return 1;
    }

    ret = openos_mq_send(&mq, "second", 7);
    if (ret != 7) {
        print_line("mqtest: second send failed\n");
        openos_mq_destroy(&mq);
        return 1;
    }

    ret = openos_mq_recv(&mq, buf, 4);
    if (ret != 4 || buf[0] != 's' || buf[1] != 'e' || buf[2] != 'c' || buf[3] != 'o') {
        print_line("mqtest: truncate recv failed\n");
        openos_mq_destroy(&mq);
        return 1;
    }

    if (openos_mq_destroy(&mq) != 0 || mq != 0) {
        print_line("mqtest: destroy failed\n");
        return 1;
    }

    print_line("mqtest: ok\n");
    return 0;
}
