/* ============================================================
 * openos - Socket syscall fd layer
 *
 * This module provides the POSIX-like socket() entry point as a
 * real file descriptor. Protocol binding (bind/listen/connect and
 * send/recv) is intentionally layered on top in later tasks.
 * ============================================================ */

#include "socket.h"
#include "../fs/vfs.h"
#include "../include/fd.h"
#include "../include/pmm.h"
#include "../include/string.h"

#define SOCKET_MAGIC 0x534F434Bu /* 'SOCK' */

typedef struct socket_file {
    uint32_t magic;
    openos_socket_info_t info;
} socket_file_t;

static uint32_t next_socket_id = 1;

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

static int socket_close(file_t *f) {
    socket_file_t *sock = socket_from_file(f);
    if (sock) {
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
    if (!sock || sock->info.state != OPENOS_SOCKET_STATE_CREATED) {
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

const openos_socket_info_t *socket_get_info(int fd) {
    file_t *file = vfs_get_file(fd);
    socket_file_t *sock = socket_from_file(file);
    if (!sock) {
        return NULL;
    }
    return &sock->info;
}
