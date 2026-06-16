/* ============================================================
 * openos - Socket syscall fd layer
 *
 * This module provides POSIX-like socket() and bind() entry
 * points as real file descriptors. Data-plane syscalls are layered
 * on top in later tasks.
 * ============================================================ */

#include "socket.h"
#include "../fs/vfs.h"
#include "../include/fd.h"
#include "../include/pmm.h"
#include "../include/string.h"

#define SOCKET_MAGIC 0x534F434Bu /* 'SOCK' */
#define SOCKET_MAX_BINDS 64
#define SOCKET_EPHEMERAL_FIRST 49152u
#define SOCKET_EPHEMERAL_LAST  65535u

typedef struct socket_file {
    uint32_t magic;
    openos_socket_info_t info;
} socket_file_t;

typedef struct socket_bind_slot {
    uint8_t used;
    uint32_t socket_id;
    int domain;
    int type;
    uint32_t ip;
    uint16_t port;
} socket_bind_slot_t;

static uint32_t next_socket_id = 1;
static uint16_t next_ephemeral_port = SOCKET_EPHEMERAL_FIRST;
static socket_bind_slot_t bind_slots[SOCKET_MAX_BINDS];

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
            if (next_ephemeral_port == 0 || next_ephemeral_port > SOCKET_EPHEMERAL_LAST) {
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
        socket_release_bind(sock->info.id);
        sock->info.state = OPENOS_SOCKET_STATE_CLOSED;
        sock->magic = 0;
    }
    return 0;
}

static int socket_read(file_t *f, void *buf, uint32_t count) {
    (void)f;
    (void)buf;
    (void)count;
    return -1;
}

static int socket_write(file_t *f, const void *buf, uint32_t count) {
    (void)f;
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
    if (events & VFS_POLLOUT) {
        ready |= VFS_POLLOUT;
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

    sock = (socket_file_t *)pmm_alloc_page();
    if (!sock) {
        return -1;
    }
    memset(sock, 0, sizeof(socket_file_t));

    sock->magic = SOCKET_MAGIC;
    sock->info.id = next_socket_id++;
    sock->info.domain = domain;
    sock->info.type = type;
    sock->info.protocol = protocol;
    sock->info.state = OPENOS_SOCKET_STATE_CREATED;
    sock->info.local_ip = OPENOS_INADDR_ANY;
    sock->info.local_port = 0;
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
    sock->info.listen_backlog = backlog;
    sock->info.state = OPENOS_SOCKET_STATE_LISTENING;
    return 0;
}

int socket_accept_fd(int fd, openos_sockaddr_t *addr, uint32_t *addrlen) {
    file_t *file;
    socket_file_t *sock;

    file = vfs_get_file(fd);
    sock = socket_from_file(file);
    if (!sock || sock->info.state != OPENOS_SOCKET_STATE_LISTENING) {
        return -1;
    }
    if (socket_type_base(sock->info.type) != OPENOS_SOCK_STREAM) {
        return -1;
    }
    if (addr && addrlen) {
        if (*addrlen < sizeof(openos_sockaddr_in_t)) {
            return -1;
        }
        *addrlen = sizeof(openos_sockaddr_in_t);
        memset(addr, 0, sizeof(openos_sockaddr_in_t));
        ((openos_sockaddr_in_t *)addr)->sin_family = OPENOS_AF_INET;
    }
    return -1;
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
