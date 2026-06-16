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

#define OPENOS_SOCKET_STATE_CREATED 1u
#define OPENOS_SOCKET_STATE_CLOSED  2u

typedef struct openos_socket_info {
    uint32_t id;
    int domain;
    int type;
    int protocol;
    uint32_t state;
} openos_socket_info_t;

int socket_create_fd(int domain, int type, int protocol);
const openos_socket_info_t *socket_get_info(int fd);

#endif /* OPENOS_NET_SOCKET_H */
