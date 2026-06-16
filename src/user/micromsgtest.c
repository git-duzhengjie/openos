#include "openos.h"

static void print_line(const char *s)
{
    openos_write(1, s, openos_strlen(s));
}

static int same_bytes(const unsigned char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != (unsigned char)b[i]) return 0;
    }
    return 1;
}

int main(void)
{
    openos_service_channel_t channel;
    openos_service_message_t req;
    openos_service_message_t rep;
    int pid;
    int status;

    print_line("micromsgtest: start\n");

    if (openos_service_channel_create(&channel) != 0) {
        print_line("micromsgtest: channel failed\n");
        return 1;
    }

    pid = openos_fork();
    if (pid < 0) {
        print_line("micromsgtest: fork failed\n");
        openos_service_channel_close(&channel);
        return 1;
    }

    if (pid == 0) {
        openos_service_client_close(&channel);
        if (openos_service_receive_request(&channel, &req) != 0 || req.service != 7 ||
            req.opcode != 11 || req.seq != 42 || req.length != 4 ||
            !same_bytes(req.payload, "ping", 4)) {
            print_line("micromsgtest: server request failed\n");
            openos_exit(2);
        }
        openos_service_message_init(&rep, req.service, req.opcode, req.seq, "pong", 4);
        rep.status = OPENOS_SERVICE_STATUS_OK;
        if (openos_service_send_reply(&channel, &rep) != 0) {
            print_line("micromsgtest: server reply failed\n");
            openos_exit(3);
        }
        openos_service_server_close(&channel);
        openos_exit(0);
    }

    openos_service_server_close(&channel);
    openos_service_message_init(&req, 7, 11, 42, "ping", 4);
    if (openos_service_request(&channel, &req, &rep) != 0 || rep.length != 4 ||
        !same_bytes(rep.payload, "pong", 4)) {
        print_line("micromsgtest: client request failed\n");
        openos_service_client_close(&channel);
        return 1;
    }

    status = 0;
    openos_wait(&status);
    openos_service_client_close(&channel);
    print_line("micromsgtest: ok\n");
    return 0;
}
