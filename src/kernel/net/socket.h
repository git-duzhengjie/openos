/* ============================================================
 * openos - Socket syscall fd layer
 * ============================================================ */

#ifndef OPENOS_NET_SOCKET_H
#define OPENOS_NET_SOCKET_H

#include <stdint.h>

#define OPENOS_AF_UNSPEC 0
#define OPENOS_AF_INET   2

#define OPENOS_SOCK_STREAM 1
#define OPENOS_SOCK_DGRAM  2
#define OPENOS_SOCK_RAW    3

#define OPENOS_INADDR_ANY  0u

#define OPENOS_SOCKET_STATE_CREATED   1u
#define OPENOS_SOCKET_STATE_BOUND     2u
#define OPENOS_SOCKET_STATE_LISTENING 3u
#define OPENOS_SOCKET_STATE_CONNECTED 4u
#define OPENOS_SOCKET_STATE_CLOSED    5u

typedef struct openos_sockaddr {
    uint16_t sa_family;
    char sa_data[14];
} openos_sockaddr_t;

typedef struct openos_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
} openos_sockaddr_in_t;

typedef struct openos_socket_info {
    uint32_t id;
    int domain;
    int type;
    int protocol;
    uint32_t state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    int tcp_conn_id;
    int listen_backlog;
} openos_socket_info_t;

int socket_create_fd(int domain, int type, int protocol);
int socket_bind_fd(int fd, const openos_sockaddr_t *addr, uint32_t addrlen);
int socket_listen_fd(int fd, int backlog);
int socket_accept_fd(int fd, openos_sockaddr_t *addr, uint32_t *addrlen);
int socket_connect_fd(int fd, const openos_sockaddr_t *addr, uint32_t addrlen);
int socket_send_fd(int fd, const uint8_t *data, uint32_t len, int flags);
int socket_sendto_fd(int fd, const uint8_t *data, uint32_t len, int flags,
                     const openos_sockaddr_t *addr, uint32_t addrlen);
int socket_recv_fd(int fd, uint8_t *data, uint32_t len, int flags);
int socket_recvfrom_fd(int fd, uint8_t *data, uint32_t len, int flags,
                       openos_sockaddr_t *addr, uint32_t *addrlen);
int socket_deliver_udp(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                       const uint8_t *data, uint16_t len);
const openos_socket_info_t *socket_get_info(int fd);

#endif /* OPENOS_NET_SOCKET_H */
