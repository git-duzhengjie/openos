#ifndef OPENOS_ARCH_X86_64_SYSCALL_NET64_H
#define OPENOS_ARCH_X86_64_SYSCALL_NET64_H

#include <stdint.h>

/*
 * Network syscall backends for the x86_64 kernel.
 *
 * These were split out of syscall_dispatch64.c (Step E.3+) to keep the core
 * dispatcher focused on process/file/memory/time syscalls. The dispatcher
 * forwards SYS_SOCKET/BIND/SENDTO/RECVFROM/NETINFO/PING/NETCONFIG/DNSLOOKUP
 * and the TCP and HTTP_GET numbers here.
 *
 * The thin wrappers translate the user-mode uint64_t arg vector into the
 * net64 / net.h stack APIs. They rely on two shared helpers exported by the
 * dispatcher (validate_user_buf / arch_x86_64_k_strlen) declared below.
 */

/* --- shared helpers (defined in syscall_dispatch64.c, exported here) --- */

/* Validate that [ptr, ptr+len) is a legal user-space buffer. */
int arch_x86_64_validate_user_buf(uint64_t ptr, uint64_t len);

/* strlen over a user string, bounded implicitly by the caller. */
uint64_t arch_x86_64_k_strlen(const char *s);

/* --- network syscall backends --- */

uint64_t arch_x86_64_sys_socket(uint64_t domain, uint64_t type, uint64_t protocol);
uint64_t arch_x86_64_sys_bind(uint64_t fd, uint64_t port);
uint64_t arch_x86_64_sys_sendto(uint64_t fd, uint64_t buf_ptr, uint64_t len,
                                uint64_t flags, uint64_t dst_port);
uint64_t arch_x86_64_sys_recvfrom(uint64_t fd, uint64_t buf_ptr, uint64_t len,
                                  uint64_t flags, uint64_t src_port_out_ptr);
uint64_t arch_x86_64_sys_netinfo(uint64_t out_ptr);
uint64_t arch_x86_64_sys_ping(uint64_t host_ptr, uint64_t out_ptr);
uint64_t arch_x86_64_sys_dnslookup(uint64_t host_ptr, uint64_t out_ptr);
uint64_t arch_x86_64_sys_netconfig(uint64_t a0, uint64_t a1, uint64_t a2,
                                   uint64_t a3, uint64_t a4);
uint64_t arch_x86_64_sys_tcp_connect(uint64_t host_ptr, uint64_t port);
uint64_t arch_x86_64_sys_tcp_send(uint64_t fd, uint64_t buf_ptr, uint64_t len);
uint64_t arch_x86_64_sys_tcp_recv(uint64_t fd, uint64_t buf_ptr, uint64_t len,
                                  uint64_t flags);
uint64_t arch_x86_64_sys_tcp_close(uint64_t fd);
uint64_t arch_x86_64_sys_http_get(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);

#endif /* OPENOS_ARCH_X86_64_SYSCALL_NET64_H */
