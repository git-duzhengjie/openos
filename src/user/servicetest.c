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
    openos_service_channel_t channel;
    char buf[16];
    int pid;
    int status;
    int n;

    print_line("servicetest: start\n");

    if (openos_service_channel_create(&channel) != 0) {
        print_line("servicetest: channel failed\n");
        return 1;
    }

    pid = openos_fork();
    if (pid < 0) {
        print_line("servicetest: fork failed\n");
        openos_service_channel_close(&channel);
        return 1;
    }

    if (pid == 0) {
        openos_service_client_close(&channel);
        n = openos_service_recv(&channel, buf, sizeof(buf));
        if (n != 4 || !same_bytes(buf, "ping", 4)) {
            print_line("servicetest: server recv failed\n");
            openos_exit(2);
        }
        if (openos_service_reply(&channel, "pong", 4) != 4) {
            print_line("servicetest: server reply failed\n");
            openos_exit(3);
        }
        openos_service_server_close(&channel);
        openos_exit(0);
    }

    openos_service_server_close(&channel);
    n = openos_service_call(&channel, "ping", 4, buf, sizeof(buf));
    if (n != 4 || !same_bytes(buf, "pong", 4)) {
        print_line("servicetest: client call failed\n");
        openos_service_client_close(&channel);
        return 1;
    }

    status = 0;
    openos_wait(&status);
    openos_service_client_close(&channel);

    print_line("servicetest: ok\n");
    return 0;
}
