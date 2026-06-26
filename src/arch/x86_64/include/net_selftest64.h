#ifndef OPENOS_ARCH_X86_64_NET_SELFTEST64_H
#define OPENOS_ARCH_X86_64_NET_SELFTEST64_H

/*
 * Step E.3 — in-kernel self-test for the loopback datagram socket layer.
 *
 * Lives in the kernel so it can be invoked from kernel64.c before ring3 work
 * begins. The point is to prove the API surface (socket/bind/sendto/recvfrom/
 * close) is end-to-end correct independent of any user-space scaffolding.
 *
 * Returns 1 on PASS, 0 on FAIL. Prints a single-line PASS/FAIL summary plus
 * the counters from arch_x86_64_net_print_status() either way.
 */

int  arch_x86_64_net_selftest_run(void);

#endif /* OPENOS_ARCH_X86_64_NET_SELFTEST64_H */
