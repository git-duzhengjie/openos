#include "../include/net_selftest64.h"

#include "../include/early_console64.h"
#include "../include/net64.h"

/*
 * Coverage:
 *   1. socket() returns >= NET64_FD_BASE for the supported triple, -1 for any
 *      mismatched triple.
 *   2. bind(0) and bind(duplicate port) are rejected.
 *   3. Cross-socket sendto/recvfrom delivers the payload byte-for-byte and the
 *      src_port matches the sender's bound port.
 *   4. Queue overflow causes a drop (drop counter increments) and recv order
 *      stays FIFO.
 *   5. recv on empty queue returns -1.
 *   6. close() releases the slot so socket() can reuse it.
 *
 * The test resets counters at the start so the final PASS message reflects
 * exactly this run.
 */

static int net_selftest_fail(const char *tag) {
    early_console64_write("[net-selftest] FAIL: ");
    early_console64_write(tag);
    early_console64_write("\n");
    arch_x86_64_net_print_status();
    return 0;
}

int arch_x86_64_net_selftest_run(void) {
    arch_x86_64_net_reset_counters();
    early_console64_write("[net-selftest] begin\n");

    /* --- 1. socket triple validation --- */
    if (arch_x86_64_net_socket(999, NET64_TYPE_DGRAM, NET64_PROTO_DEFAULT) != -1)
        return net_selftest_fail("bad-domain-accepted");
    if (arch_x86_64_net_socket(NET64_DOMAIN_OPENOS, 999, NET64_PROTO_DEFAULT) != -1)
        return net_selftest_fail("bad-type-accepted");

    int sA = arch_x86_64_net_socket(NET64_DOMAIN_OPENOS,
                                    NET64_TYPE_DGRAM,
                                    NET64_PROTO_DEFAULT);
    int sB = arch_x86_64_net_socket(NET64_DOMAIN_OPENOS,
                                    NET64_TYPE_DGRAM,
                                    NET64_PROTO_DEFAULT);
    if (sA < NET64_FD_BASE || sB < NET64_FD_BASE || sA == sB)
        return net_selftest_fail("socket-alloc");

    /* --- 2. bind error paths --- */
    if (arch_x86_64_net_bind(sA, 0) != -1)
        return net_selftest_fail("bind-port-zero");
    if (arch_x86_64_net_bind(sA, 4242) != 0)
        return net_selftest_fail("bind-sA");
    if (arch_x86_64_net_bind(sB, 4242) != -1)
        return net_selftest_fail("bind-dup-port");
    if (arch_x86_64_net_bind(sB, 5353) != 0)
        return net_selftest_fail("bind-sB");

    /* --- 3. round trip A->B --- */
    const char payload[] = "ping64";
    int sent = arch_x86_64_net_sendto(sA, payload, sizeof(payload), 5353);
    if (sent != (int)sizeof(payload))
        return net_selftest_fail("sendto-len");

    char     rxbuf[32];
    uint16_t rx_src = 0;
    int got = arch_x86_64_net_recvfrom(sB, rxbuf, sizeof(rxbuf), &rx_src);
    if (got != (int)sizeof(payload))
        return net_selftest_fail("recvfrom-len");
    if (rx_src != 4242)
        return net_selftest_fail("recvfrom-src-port");
    for (int i = 0; i < got; ++i) {
        if (rxbuf[i] != payload[i])
            return net_selftest_fail("recvfrom-content");
    }

    /* --- 4. queue overflow --- */
    /* Fill sB's inbox: it's empty, depth = NET64_PACKET_QUEUE_DEPTH=4. */
    for (int i = 0; i < 4; ++i) {
        char buf[4] = { 'q', (char)('0' + i), 0, 0 };
        if (arch_x86_64_net_sendto(sA, buf, sizeof(buf), 5353) != 4)
            return net_selftest_fail("queue-fill");
    }
    /* One more should drop. */
    {
        char buf[4] = { 'x', 'x', 0, 0 };
        if (arch_x86_64_net_sendto(sA, buf, sizeof(buf), 5353) != -1)
            return net_selftest_fail("overflow-accepted");
    }
    if (arch_x86_64_net_drop_count() == 0)
        return net_selftest_fail("drop-counter");

    /* FIFO order check: q0..q3. */
    for (int i = 0; i < 4; ++i) {
        char buf[8] = { 0 };
        int  n = arch_x86_64_net_recvfrom(sB, buf, sizeof(buf), 0);
        if (n != 4)
            return net_selftest_fail("recv-fifo-len");
        if (buf[0] != 'q' || buf[1] != (char)('0' + i))
            return net_selftest_fail("recv-fifo-order");
    }

    /* --- 5. recv on empty inbox --- */
    if (arch_x86_64_net_recvfrom(sB, rxbuf, sizeof(rxbuf), 0) != -1)
        return net_selftest_fail("recv-empty");

    /* --- 6. close + reuse --- */
    if (arch_x86_64_net_close(sA) != 0)
        return net_selftest_fail("close-sA");
    if (arch_x86_64_net_close(sA) != -1)
        return net_selftest_fail("double-close");
    int sC = arch_x86_64_net_socket(NET64_DOMAIN_OPENOS,
                                    NET64_TYPE_DGRAM,
                                    NET64_PROTO_DEFAULT);
    if (sC != sA)
        return net_selftest_fail("slot-reuse");
    arch_x86_64_net_close(sB);
    arch_x86_64_net_close(sC);

    early_console64_write("[net-selftest] PASS sendto=");
    early_console64_write_hex64(arch_x86_64_net_sendto_count());
    early_console64_write(" recvfrom=");
    early_console64_write_hex64(arch_x86_64_net_recvfrom_count());
    early_console64_write(" drops=");
    early_console64_write_hex64(arch_x86_64_net_drop_count());
    early_console64_write("\n");
    return 1;
}
