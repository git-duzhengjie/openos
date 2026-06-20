/* ============================================================
 * openos - Socket syscall fd layer
 *
 * This module provides POSIX-like socket() and bind() entry
 * points as real file descriptors. Data-plane syscalls are layered
 * on top in later tasks.
 * ============================================================ */

#include "socket.h"
#include "net.h"
#include "../fs/vfs.h"
#include "../include/fd.h"
#include "../include/heap.h"
#include "../include/pmm.h"
#include "../include/string.h"

#define SOCKET_MAGIC 0x534F434Bu /* 'SOCK' */
#define SOCKET_MAX_BINDS 64
#define SOCKET_EPHEMERAL_FIRST 49152u
#define SOCKET_EPHEMERAL_LAST  65535u
#define SOCKET_RECV_QUEUE_LEN 8
#define SOCKET_RECV_PACKET_MAX (NET_ETH_MTU - 8u)
#define SOCKET_FLAG_PAIR 0x1u

typedef struct socket_packet {
    uint8_t used;
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t len;
    uint8_t data[SOCKET_RECV_PACKET_MAX];
} socket_packet_t;

typedef struct socket_file {
    uint32_t magic;
    openos_socket_info_t info;
    socket_packet_t recv_queue[SOCKET_RECV_QUEUE_LEN];
    uint32_t recv_head;
    uint32_t recv_count;
    uint32_t flags;
    struct socket_file *peer;
} socket_file_t;

typedef struct socket_bind_slot {
    uint8_t used;
    uint32_t socket_id;
    socket_file_t *sock;
    int domain;
    int type;
    uint32_t ip;
    uint16_t port;
} socket_bind_slot_t;

static uint32_t next_socket_id = 1;
static uint16_t next_ephemeral_port = SOCKET_EPHEMERAL_FIRST;
static socket_bind_slot_t bind_slots[SOCKET_MAX_BINDS];

static socket_file_t *socket_alloc_file(void) {
    socket_file_t *sock = (socket_file_t *)kmalloc(sizeof(socket_file_t));
    if (!sock) {
        return NULL;
    }
    memset(sock, 0, sizeof(socket_file_t));
    return sock;
}

static void socket_free_file(socket_file_t *sock) {
    if (sock) {
        kfree(sock);
    }
}

static uint16_t socket_bswap16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

static int socket_type_base(int type) {
    return type & 0xF;
}

static int socket_validate_args(int domain, int type, int protocol) {
    int base_type = socket_type_base(type);
    if (domain != OPENOS_AF_UNSPEC && domain != OPENOS_AF_INET) {
        return -1;
    }
    if (base_type != OPENOS_SOCK_STREAM &&
        base_type != OPENOS_SOCK_DGRAM &&
        base_type != OPENOS_SOCK_RAW) {
        return -1;
    }
    if (protocol < 0) {
        return -1;
    }
    return 0;
}

static socket_file_t *socket_from_file(file_t *f) {
    socket_file_t *sock;
    if (!f || !f->fs_data) {
        return NULL;
    }
    sock = (socket_file_t *)f->fs_data;
    if (sock->magic != SOCKET_MAGIC) {
        return NULL;
    }
    return sock;
}

static int socket_port_conflicts(int domain, int type, uint32_t ip, uint16_t port) {
    int base_type = socket_type_base(type);
    for (uint32_t i = 0; i < SOCKET_MAX_BINDS; i++) {
        socket_bind_slot_t *slot = &bind_slots[i];
        if (!slot->used) {
            continue;
        }
        if (slot->domain != domain || socket_type_base(slot->type) != base_type) {
            continue;
        }
        if (slot->port != port) {
            continue;
        }
        if (slot->ip == OPENOS_INADDR_ANY || ip == OPENOS_INADDR_ANY || slot->ip == ip) {
            return 1;
        }
    }
    return 0;
}

static int socket_reserve_port(socket_file_t *sock, uint32_t ip, uint16_t port) {
    socket_bind_slot_t *free_slot = NULL;
    for (uint32_t i = 0; i < SOCKET_MAX_BINDS; i++) {
        if (bind_slots[i].used && bind_slots[i].socket_id == sock->info.id) {
            return -1;
        }
        if (!bind_slots[i].used && !free_slot) {
            free_slot = &bind_slots[i];
        }
    }
    if (!free_slot) {
        return -1;
    }
    if (port == 0) {
        uint32_t attempts = SOCKET_EPHEMERAL_LAST - SOCKET_EPHEMERAL_FIRST + 1u;
        while (attempts--) {
            uint16_t candidate = next_ephemeral_port++;
            if (next_ephemeral_port == 0) {
                next_ephemeral_port = SOCKET_EPHEMERAL_FIRST;
            }
            if (!socket_port_conflicts(sock->info.domain, sock->info.type, ip, candidate)) {
                port = candidate;
                break;
            }
        }
        if (port == 0) {
            return -1;
        }
    } else if (socket_port_conflicts(sock->info.domain, sock->info.type, ip, port)) {
        return -1;
    }

    free_slot->used = 1;
    free_slot->socket_id = sock->info.id;
    free_slot->sock = sock;
    free_slot->domain = sock->info.domain;
    free_slot->type = sock->info.type;
    free_slot->ip = ip;
    free_slot->port = port;
    sock->info.local_ip = ip;
    sock->info.local_port = port;
    sock->info.state = OPENOS_SOCKET_STATE_BOUND;
    return 0;
}

static void socket_release_bind(uint32_t socket_id) {
    for (uint32_t i = 0; i < SOCKET_MAX_BINDS; i++) {
        if (bind_slots[i].used && bind_slots[i].socket_id == socket_id) {
            memset(&bind_slots[i], 0, sizeof(bind_slots[i]));
            return;
        }
    }
}

static int socket_close(file_t *f) {
    socket_file_t *sock = socket_from_file(f);
    if (sock) {
        if (sock->info.tcp_conn_id >= 0) {
            net_tcp_close(sock->info.tcp_conn_id);
            sock->info.tcp_conn_id = -1;
        }
        socket_release_bind(sock->info.id);
        if ((sock->flags & SOCKET_FLAG_PAIR) && sock->peer) {
            sock->peer->peer = NULL;
        }
        sock->info.state = OPENOS_SOCKET_STATE_CLOSED;
        sock->magic = 0;
        f->fs_data = NULL;
        socket_free_file(sock);
    }
    return 0;
}

static int socket_dequeue_from(socket_file_t *sock, uint8_t *buf, uint32_t count,
                               uint32_t *src_ip, uint16_t *src_port) {
    socket_packet_t *pkt;
    uint32_t copy_len;
    if (!sock || !buf || count == 0 || sock->recv_count == 0) {
        return -1;
    }
    pkt = &sock->recv_queue[sock->recv_head];
    if (!pkt->used) {
        sock->recv_count = 0;
        return -1;
    }
    copy_len = pkt->len;
    if (copy_len > count) {
        copy_len = count;
    }
    memcpy(buf, pkt->data, copy_len);
    if (src_ip) {
        *src_ip = pkt->src_ip;
    }
    if (src_port) {
        *src_port = pkt->src_port;
    }
    memset(pkt, 0, sizeof(*pkt));
    sock->recv_head = (sock->recv_head + 1u) % SOCKET_RECV_QUEUE_LEN;
    sock->recv_count--;
    return (int)copy_len;
}

static int socket_dequeue(socket_file_t *sock, uint8_t *buf, uint32_t count) {
    return socket_dequeue_from(sock, buf, count, NULL, NULL);
}

static int socket_enqueue_packet(socket_file_t *sock, uint32_t src_ip, uint16_t src_port,
                                const uint8_t *data, uint16_t len) {
    socket_packet_t *pkt;
    uint32_t tail;

    if (!sock || !data || len == 0 || len > SOCKET_RECV_PACKET_MAX ||
        sock->recv_count >= SOCKET_RECV_QUEUE_LEN || sock->info.read_shutdown) {
        return -1;
    }
    tail = (sock->recv_head + sock->recv_count) % SOCKET_RECV_QUEUE_LEN;
    pkt = &sock->recv_queue[tail];
    memset(pkt, 0, sizeof(*pkt));
    pkt->used = 1;
    pkt->src_ip = src_ip;
    pkt->src_port = src_port;
    pkt->len = len;
    memcpy(pkt->data, data, len);
    sock->recv_count++;
    return 0;
}

static int socket_read(file_t *f, void *buf, uint32_t count) {
    socket_file_t *sock = socket_from_file(f);
    if (!sock || sock->info.read_shutdown) {
        return -1;
    }
    if (sock->flags & SOCKET_FLAG_PAIR) {
        return socket_dequeue(sock, (uint8_t *)buf, count);
    }
    if (socket_type_base(sock->info.type) == OPENOS_SOCK_STREAM) {
        if (sock->info.tcp_conn_id < 0) return -1;
        return net_tcp_recv(sock->info.tcp_conn_id, (uint8_t *)buf, (uint16_t)count);
    }
    return socket_dequeue(sock, (uint8_t *)buf, count);
}

static int socket_write(file_t *f, const void *buf, uint32_t count) {
    socket_file_t *sock = socket_from_file(f);
    if (!sock || sock->info.write_shutdown) {
        return -1;
    }
    if (sock->flags & SOCKET_FLAG_PAIR) {
        if (!buf || count == 0 || count > SOCKET_RECV_PACKET_MAX || !sock->peer ||
            sock->peer->magic != SOCKET_MAGIC || sock->peer->info.read_shutdown ||
            socket_enqueue_packet(sock->peer, OPENOS_INADDR_ANY, 0,
                                  (const uint8_t *)buf, (uint16_t)count) < 0) {
            return -1;
        }
        return (int)count;
    }
    if (socket_type_base(sock->info.type) == OPENOS_SOCK_STREAM) {
        if (sock->info.tcp_conn_id < 0 || !buf || count > NET_ETH_MTU) return -1;
        return net_tcp_send(sock->info.tcp_conn_id, (const uint8_t *)buf, (uint16_t)count) == 0 ? (int)count : -1;
    }
    (void)buf;
    (void)count;
    return -1;
}

static int socket_seek(file_t *f, int offset, int whence) {
    (void)f;
    (void)offset;
    (void)whence;
    return -1;
}

static int socket_poll(file_t *f, uint32_t events) {
    socket_file_t *sock = socket_from_file(f);
    uint32_t ready = 0;
    if (!sock || sock->info.state == OPENOS_SOCKET_STATE_CLOSED) {
        return VFS_POLLERR | VFS_POLLHUP;
    }
    if (sock->flags & SOCKET_FLAG_PAIR) {
        if ((events & VFS_POLLIN) && !sock->info.read_shutdown && sock->recv_count > 0) ready |= VFS_POLLIN;
        if ((events & VFS_POLLOUT) && !sock->info.write_shutdown && sock->peer &&
            sock->peer->magic == SOCKET_MAGIC && !sock->peer->info.read_shutdown) ready |= VFS_POLLOUT;
        if (!sock->peer || sock->peer->magic != SOCKET_MAGIC || sock->info.read_shutdown ||
            (sock->peer && sock->peer->magic == SOCKET_MAGIC && sock->peer->info.write_shutdown)) ready |= VFS_POLLHUP;
    } else if (socket_type_base(sock->info.type) == OPENOS_SOCK_STREAM) {
        int state = sock->info.tcp_conn_id >= 0 ? net_tcp_state(sock->info.tcp_conn_id) : -1;
        if (state == NET_TCP_STATE_CLOSED || state < 0) ready |= VFS_POLLHUP;
        if ((events & VFS_POLLIN) && sock->info.tcp_conn_id >= 0 && net_tcp_available(sock->info.tcp_conn_id) > 0) {
            ready |= VFS_POLLIN;
        }
        if ((events & VFS_POLLOUT) && state == NET_TCP_STATE_ESTABLISHED) ready |= VFS_POLLOUT;
    } else {
        if ((events & VFS_POLLIN) && sock->recv_count > 0) {
            ready |= VFS_POLLIN;
        }
        if (events & VFS_POLLOUT) {
            ready |= VFS_POLLOUT;
        }
    }
    return (int)ready;
}

static file_ops_t socket_file_ops = {
    .open = NULL,
    .close = socket_close,
    .read = socket_read,
    .write = socket_write,
    .seek = socket_seek,
    .truncate = NULL,
    .readdir = NULL,
    .poll = socket_poll,
};

static int socket_init_file_fd(int fd, socket_file_t *sock) {
    file_t *file = (file_t *)pmm_alloc_page();
    if (!file) {
        return -1;
    }
    memset(file, 0, sizeof(file_t));
    file->flags = O_RDWR;
    file->offset = 0;
    file->ref_count = 1;
    file->fs_data = sock;
    file->ops = &socket_file_ops;
    if (vfs_put_file(fd, file) < 0) {
        return -1;
    }
    return 0;
}

static void socket_init_info(socket_file_t *sock, int domain, int type, int protocol) {
    memset(sock, 0, sizeof(*sock));
    sock->magic = SOCKET_MAGIC;
    sock->info.id = next_socket_id++;
    sock->info.domain = domain;
    sock->info.type = type;
    sock->info.protocol = protocol;
    sock->info.state = OPENOS_SOCKET_STATE_CREATED;
    sock->info.local_ip = OPENOS_INADDR_ANY;
    sock->info.remote_ip = OPENOS_INADDR_ANY;
    sock->info.tcp_conn_id = -1;
}

int socket_create_fd(int domain, int type, int protocol) {
    int fd;
    file_t *file;
    socket_file_t *sock;

    if (socket_validate_args(domain, type, protocol) < 0) {
        return -1;
    }

    fd = vfs_alloc_fd();
    if (fd < 0) {
        return -1;
    }

    file = (file_t *)pmm_alloc_page();
    if (!file) {
        return -1;
    }
    memset(file, 0, sizeof(file_t));

    sock = socket_alloc_file();
    if (!sock) {
        pmm_free_page(file);
        return -1;
    }

    sock->magic = SOCKET_MAGIC;
    sock->info.id = next_socket_id++;
    sock->info.domain = domain;
    sock->info.type = type;
    sock->info.protocol = protocol;
    sock->info.state = OPENOS_SOCKET_STATE_CREATED;
    sock->info.local_ip = OPENOS_INADDR_ANY;
    sock->info.local_port = 0;
    sock->info.remote_ip = OPENOS_INADDR_ANY;
    sock->info.remote_port = 0;
    sock->info.tcp_conn_id = -1;
    sock->info.listen_backlog = 0;

    file->flags = O_RDWR;
    file->offset = 0;
    file->ref_count = 1;
    file->fs_data = sock;
    file->ops = &socket_file_ops;

    if (vfs_put_file(fd, file) < 0) {
        socket_close(file);
        return -1;
    }
    return fd;
}

int socketpair_create_fds(int domain, int type, int protocol, int sv[2]) {
    int fd0;
    int fd1;
    socket_file_t *a;
    socket_file_t *b;
    int base_type = socket_type_base(type);

    if (!sv || socket_validate_args(domain, type, protocol) < 0) {
        return -1;
    }
    if (domain != OPENOS_AF_UNSPEC && domain != OPENOS_AF_INET) {
        return -1;
    }
    if (base_type != OPENOS_SOCK_STREAM && base_type != OPENOS_SOCK_DGRAM) {
        return -1;
    }

    fd0 = vfs_alloc_fd();
    fd1 = vfs_alloc_fd();
    if (fd0 < 0 || fd1 < 0 || fd0 == fd1) {
        if (fd0 >= 0) vfs_put_file(fd0, NULL);
        if (fd1 >= 0) vfs_put_file(fd1, NULL);
        return -1;
    }

    a = socket_alloc_file();
    b = socket_alloc_file();
    if (!a || !b) {
        if (a) socket_free_file(a);
        if (b) socket_free_file(b);
        if (fd0 >= 0) vfs_put_file(fd0, NULL);
        if (fd1 >= 0) vfs_put_file(fd1, NULL);
        return -1;
    }
    socket_init_info(a, domain, type, protocol);
    socket_init_info(b, domain, type, protocol);
    a->flags = SOCKET_FLAG_PAIR;
    b->flags = SOCKET_FLAG_PAIR;
    a->peer = b;
    b->peer = a;
    a->info.state = OPENOS_SOCKET_STATE_CONNECTED;
    b->info.state = OPENOS_SOCKET_STATE_CONNECTED;

    if (socket_init_file_fd(fd0, a) < 0 || socket_init_file_fd(fd1, b) < 0) {
        vfs_put_file(fd0, NULL);
        vfs_put_file(fd1, NULL);
        return -1;
    }

    sv[0] = fd0;
    sv[1] = fd1;
    return 0;
}

int socket_listen_fd(int fd, int backlog) {
    file_t *file;
    socket_file_t *sock;

    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock || sock->info.state != OPENOS_SOCKET_STATE_BOUND) {
        return -1;
    }
    if (sock->info.domain != OPENOS_AF_INET) {
        return -1;
    }
    if (socket_type_base(sock->info.type) != OPENOS_SOCK_STREAM) {
        return -1;
    }
    if (backlog < 0) {
        return -1;
    }
    if (backlog == 0) {
        backlog = 1;
    }
    if (backlog > 32) {
        backlog = 32;
    }
    sock->info.tcp_conn_id = net_tcp_open(sock->info.local_ip, sock->info.local_port, 0, 0, 0);
    if (sock->info.tcp_conn_id < 0) {
        return -1;
    }
    sock->info.listen_backlog = backlog;
    sock->info.state = OPENOS_SOCKET_STATE_LISTENING;
    return 0;
}

int socket_accept_fd(int fd, openos_sockaddr_t *addr, uint32_t *addrlen) {
    file_t *file;
    socket_file_t *sock;
    socket_file_t *accepted;
    int new_fd;
    int old_conn;
    int new_listen_conn;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;

    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock || sock->info.state != OPENOS_SOCKET_STATE_LISTENING) {
        return -1;
    }
    if (sock->info.domain != OPENOS_AF_INET ||
        socket_type_base(sock->info.type) != OPENOS_SOCK_STREAM) {
        return -1;
    }
    if (net_tcp_state(sock->info.tcp_conn_id) != NET_TCP_STATE_ESTABLISHED) {
        return -1;
    }
    if (net_tcp_get_endpoint(sock->info.tcp_conn_id, &local_ip, &local_port,
                             &remote_ip, &remote_port) < 0) {
        return -1;
    }
    if (addr || addrlen) {
        if (!addr || !addrlen || *addrlen < sizeof(openos_sockaddr_in_t)) {
            return -1;
        }
    }

    new_fd = vfs_alloc_fd();
    if (new_fd < 0) {
        return -1;
    }
    accepted = socket_alloc_file();
    if (!accepted) {
        vfs_put_file(new_fd, NULL);
        return -1;
    }
    accepted->magic = SOCKET_MAGIC;
    accepted->info.id = next_socket_id++;

    accepted->info.domain = sock->info.domain;
    accepted->info.type = sock->info.type;
    accepted->info.protocol = sock->info.protocol;
    accepted->info.state = OPENOS_SOCKET_STATE_CONNECTED;
    accepted->info.local_ip = local_ip;
    accepted->info.local_port = local_port;
    accepted->info.remote_ip = remote_ip;
    accepted->info.remote_port = remote_port;
    accepted->info.tcp_conn_id = sock->info.tcp_conn_id;
    accepted->info.listen_backlog = 0;

    if (socket_init_file_fd(new_fd, accepted) < 0) {
        accepted->magic = 0;
        vfs_put_file(new_fd, NULL);
        return -1;
    }

    if (addr && addrlen) {
        memset(addr, 0, sizeof(openos_sockaddr_in_t));
        ((openos_sockaddr_in_t *)addr)->sin_family = OPENOS_AF_INET;
        ((openos_sockaddr_in_t *)addr)->sin_port = socket_bswap16(remote_port);
        ((openos_sockaddr_in_t *)addr)->sin_addr = remote_ip;
        *addrlen = sizeof(openos_sockaddr_in_t);
    }

    old_conn = sock->info.tcp_conn_id;
    new_listen_conn = net_tcp_open(sock->info.local_ip, sock->info.local_port, 0, 0, 0);
    if (new_listen_conn >= 0) {
        sock->info.tcp_conn_id = new_listen_conn;
    } else {
        sock->info.tcp_conn_id = old_conn;
        sock->info.state = OPENOS_SOCKET_STATE_CONNECTED;
    }
    return new_fd;
}

int socket_connect_fd(int fd, const openos_sockaddr_t *addr, uint32_t addrlen) {
    file_t *file;
    socket_file_t *sock;
    const openos_sockaddr_in_t *in;
    uint16_t port;
    uint32_t ip;
    int base_type;

    if (!addr || addrlen < sizeof(openos_sockaddr_in_t)) {
        return -1;
    }

    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock || sock->info.state == OPENOS_SOCKET_STATE_CLOSED ||
        sock->info.state == OPENOS_SOCKET_STATE_LISTENING ||
        sock->info.state == OPENOS_SOCKET_STATE_CONNECTED) {
        return -1;
    }
    if (sock->info.domain != OPENOS_AF_INET || addr->sa_family != OPENOS_AF_INET) {
        return -1;
    }

    base_type = socket_type_base(sock->info.type);
    if (base_type != OPENOS_SOCK_STREAM && base_type != OPENOS_SOCK_DGRAM) {
        return -1;
    }

    in = (const openos_sockaddr_in_t *)addr;
    port = socket_bswap16(in->sin_port);
    ip = in->sin_addr;
    if (port == 0 || ip == OPENOS_INADDR_ANY) {
        return -1;
    }

    if (sock->info.state == OPENOS_SOCKET_STATE_CREATED) {
        if (socket_reserve_port(sock, OPENOS_INADDR_ANY, 0) < 0) {
            return -1;
        }
    }

    sock->info.remote_ip = ip;
    sock->info.remote_port = port;
    if (base_type == OPENOS_SOCK_STREAM) {
        sock->info.tcp_conn_id = net_tcp_open(sock->info.local_ip, sock->info.local_port,
                                             ip, port, 1);
        if (sock->info.tcp_conn_id < 0) {
            return -1;
        }
    }
    sock->info.state = OPENOS_SOCKET_STATE_CONNECTED;
    return 0;
}

int socket_recv_fd(int fd, uint8_t *data, uint32_t len, int flags) {
    file_t *file;
    socket_file_t *sock;

    (void)flags;
    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock) {
        return -1;
    }
    if (sock->flags & SOCKET_FLAG_PAIR) {
        return socket_dequeue(sock, data, len);
    }
    if (socket_type_base(sock->info.type) == OPENOS_SOCK_STREAM) {
        if (sock->info.tcp_conn_id < 0) {
            return -1;
        }
        return net_tcp_recv(sock->info.tcp_conn_id, data, (uint16_t)len);
    }
    if (socket_type_base(sock->info.type) != OPENOS_SOCK_DGRAM) {
        return -1;
    }
    return socket_dequeue(sock, data, len);
}

int socket_send_fd(int fd, const uint8_t *data, uint32_t len, int flags) {
    file_t *file;
    socket_file_t *sock;

    (void)flags;
    if (!data || len == 0 || len > (NET_ETH_MTU - 8u)) {
        return -1;
    }

    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock || sock->info.state != OPENOS_SOCKET_STATE_CONNECTED) {
        return -1;
    }
    if (sock->flags & SOCKET_FLAG_PAIR) {
        if (!sock->peer || sock->peer->magic != SOCKET_MAGIC ||
            socket_enqueue_packet(sock->peer, OPENOS_INADDR_ANY, 0, data, (uint16_t)len) < 0) {
            return -1;
        }
        return (int)len;
    }
    if (socket_type_base(sock->info.type) == OPENOS_SOCK_STREAM) {
        if (sock->info.tcp_conn_id < 0 || net_tcp_send(sock->info.tcp_conn_id, data, (uint16_t)len) < 0) {
            return -1;
        }
        return (int)len;
    }
    if (socket_type_base(sock->info.type) != OPENOS_SOCK_DGRAM) {
        return -1;
    }
    if (sock->info.local_port == 0 || sock->info.remote_port == 0) {
        return -1;
    }
    if (net_send_udp(sock->info.remote_ip, sock->info.local_port,
                     sock->info.remote_port, data, (uint16_t)len) < 0) {
        return -1;
    }
    return (int)len;
}

int socket_sendto_fd(int fd, const uint8_t *data, uint32_t len, int flags,
                     const openos_sockaddr_t *addr, uint32_t addrlen) {
    file_t *file;
    socket_file_t *sock;
    const openos_sockaddr_in_t *in;
    uint32_t remote_ip;
    uint16_t remote_port;

    (void)flags;
    if (!data || !addr || len == 0 || len > (NET_ETH_MTU - 8u) ||
        addrlen < sizeof(openos_sockaddr_in_t)) {
        return -1;
    }
    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock || sock->info.domain != OPENOS_AF_INET ||
        socket_type_base(sock->info.type) != OPENOS_SOCK_DGRAM ||
        sock->info.state == OPENOS_SOCKET_STATE_CLOSED ||
        addr->sa_family != OPENOS_AF_INET) {
        return -1;
    }
    in = (const openos_sockaddr_in_t *)addr;
    remote_ip = in->sin_addr;
    remote_port = socket_bswap16(in->sin_port);
    if (remote_ip == OPENOS_INADDR_ANY || remote_port == 0) {
        return -1;
    }
    if (sock->info.local_port == 0 && socket_reserve_port(sock, OPENOS_INADDR_ANY, 0) < 0) {
        return -1;
    }
    if (net_send_udp(remote_ip, sock->info.local_port, remote_port, data, (uint16_t)len) < 0) {
        return -1;
    }
    return (int)len;
}

int socket_recvfrom_fd(int fd, uint8_t *data, uint32_t len, int flags,
                       openos_sockaddr_t *addr, uint32_t *addrlen) {
    file_t *file;
    socket_file_t *sock;
    uint32_t src_ip = OPENOS_INADDR_ANY;
    uint16_t src_port = 0;
    int ret;

    (void)flags;
    if (!data || len == 0 || (addr && !addrlen)) {
        return -1;
    }
    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock || sock->info.read_shutdown || socket_type_base(sock->info.type) != OPENOS_SOCK_DGRAM) {
        return -1;
    }
    ret = socket_dequeue_from(sock, data, len, &src_ip, &src_port);
    if (ret < 0) {
        return ret;
    }
    if (addr) {
        openos_sockaddr_in_t *in = (openos_sockaddr_in_t *)addr;
        if (*addrlen < sizeof(openos_sockaddr_in_t)) {
            return -1;
        }
        memset(in, 0, sizeof(*in));
        in->sin_family = OPENOS_AF_INET;
        in->sin_port = socket_bswap16(src_port);
        in->sin_addr = src_ip;
        *addrlen = sizeof(openos_sockaddr_in_t);
    }
    return ret;
}

int socket_shutdown_fd(int fd, int how) {
    file_t *file;
    socket_file_t *sock;

    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock || sock->info.state == OPENOS_SOCKET_STATE_CLOSED) {
        return -1;
    }
    if (how < 0 || how > 2) {
        return -1;
    }
    if (how == 0 || how == 2) {
        sock->info.read_shutdown = 1;
        memset(sock->recv_queue, 0, sizeof(sock->recv_queue));
        sock->recv_head = 0;
        sock->recv_count = 0;
    }
    if (how == 1 || how == 2) {
        sock->info.write_shutdown = 1;
    }
    return 0;
}

int socket_bind_fd(int fd, const openos_sockaddr_t *addr, uint32_t addrlen) {
    file_t *file;
    socket_file_t *sock;
    const openos_sockaddr_in_t *in;
    uint16_t port;
    uint32_t ip;

    if (!addr || addrlen < sizeof(openos_sockaddr_in_t)) {
        return -1;
    }

    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock || sock->info.state != OPENOS_SOCKET_STATE_CREATED) {
        return -1;
    }
    if (sock->info.domain != OPENOS_AF_INET) {
        return -1;
    }
    if (addr->sa_family != OPENOS_AF_INET) {
        return -1;
    }
    if (socket_type_base(sock->info.type) == OPENOS_SOCK_RAW) {
        return -1;
    }

    in = (const openos_sockaddr_in_t *)addr;
    port = socket_bswap16(in->sin_port);
    ip = in->sin_addr;
    return socket_reserve_port(sock, ip, port);
}

const openos_socket_info_t *socket_get_info(int fd) {
    file_t *file = vfs_get_file(fd);
    socket_file_t *sock = socket_from_file(file);
    if (!sock) {
        return NULL;
    }
    return &sock->info;
}


int socket_deliver_udp(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                       const uint8_t *data, uint16_t len) {
    socket_file_t *match = NULL;
    if (!data || len == 0 || len > SOCKET_RECV_PACKET_MAX) {
        return -1;
    }
    for (uint32_t i = 0; i < SOCKET_MAX_BINDS; i++) {
        socket_bind_slot_t *slot = &bind_slots[i];
        socket_file_t *sock;
        if (!slot->used || slot->port != dst_port || !slot->sock) {
            continue;
        }
        sock = slot->sock;
        if (sock->magic != SOCKET_MAGIC || sock->info.state == OPENOS_SOCKET_STATE_CLOSED) {
            continue;
        }
        if (socket_type_base(sock->info.type) != OPENOS_SOCK_DGRAM) {
            continue;
        }
        if (sock->info.remote_port && sock->info.remote_port != src_port) {
            continue;
        }
        if (sock->info.remote_ip != OPENOS_INADDR_ANY && sock->info.remote_ip != src_ip) {
            continue;
        }
        match = sock;
        break;
    }
    return socket_enqueue_packet(match, src_ip, src_port, data, len);
}
