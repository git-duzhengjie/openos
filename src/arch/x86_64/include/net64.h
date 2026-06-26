#ifndef OPENOS_ARCH_X86_64_NET64_H
#define OPENOS_ARCH_X86_64_NET64_H

/*
 * Step E.3 — Minimal loopback-only socket layer for x86_64.
 *
 * Goal: unblock the BSD-style socket ABI (SOCKET / BIND / SENDTO / RECVFROM)
 * without dragging the entire i386 net.c stack onto x86_64. All traffic stays
 * inside the kernel as an in-memory UDP-like queue, so user programs can
 * exercise the syscalls end-to-end before a real NIC driver lands.
 *
 * Design constraints:
 *   - Fully static, zero heap allocation. Everything lives in .bss.
 *   - net fd numbering starts at NET64_FD_BASE (32) so it never overlaps with
 *     fdtable64 (which owns 0..15). Mixing the two namespaces in the dispatcher
 *     is therefore unnecessary.
 *   - Datagram semantics: send copies, recv copies, no fragmentation.
 *   - Only AF_OPENOS / SOCK_DGRAM / PROTO_DEFAULT is honored. Anything else
 *     returns -1 so callers fail loudly rather than silently doing nothing.
 *
 * When a real net stack is ported, this header is the single seam to replace.
 */

#include <stdint.h>

#include "arch64_types.h"

#define NET64_MAX_SOCKETS         4
#define NET64_PACKET_QUEUE_DEPTH  4
#define NET64_PACKET_MAX_LEN      256
#define NET64_FD_BASE             32

/* Single supported (domain, type, protocol) triple in Step E.3. */
#define NET64_DOMAIN_OPENOS       1
#define NET64_TYPE_DGRAM          2
#define NET64_PROTO_DEFAULT       0

void arch_x86_64_net_init(void);

/* socket()/bind() return >=0 on success, -1 on failure. */
int  arch_x86_64_net_socket(int domain, int type, int protocol);
int  arch_x86_64_net_bind(int fd, uint16_t port);

/*
 * sendto: enqueue exactly `len` bytes into the inbox of whichever socket is
 * currently bound to dst_port. Returns the number of bytes copied, or -1 if
 * the queue is full or dst_port has no listener.
 */
int  arch_x86_64_net_sendto(int fd,
                            const void *buf,
                            x86_64_size_t len,
                            uint16_t dst_port);

/*
 * recvfrom: pop one datagram from `fd`'s inbox. Returns bytes copied (which
 * may be less than the datagram length if the user buffer is too small — the
 * remainder is discarded, matching UDP semantics). src_port_out may be NULL.
 * Returns -1 if `fd` is not a bound socket or the inbox is empty.
 */
int  arch_x86_64_net_recvfrom(int fd,
                              void *buf,
                              x86_64_size_t len,
                              uint16_t *src_port_out);

/* Close a socket fd; idempotent on already-closed fds (returns -1). */
int  arch_x86_64_net_close(int fd);

/* Diagnostics. */
void     arch_x86_64_net_print_status(void);
uint64_t arch_x86_64_net_sendto_count(void);
uint64_t arch_x86_64_net_recvfrom_count(void);
uint64_t arch_x86_64_net_drop_count(void);
void     arch_x86_64_net_reset_counters(void);

#endif /* OPENOS_ARCH_X86_64_NET64_H */
