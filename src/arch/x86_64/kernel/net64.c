#include "../include/net64.h"

#include "../include/early_console64.h"

/*
 * Step E.3 — loopback datagram socket layer.
 *
 * Implementation notes:
 *   - Socket table is dense: index 0..NET64_MAX_SOCKETS-1, fd = index + NET64_FD_BASE.
 *   - Each socket has a ring buffer of NET64_PACKET_QUEUE_DEPTH datagrams.
 *   - Drops happen at the destination's inbox when the ring is full. Counters
 *     bump so tests can observe pressure deterministically.
 *   - Bind requires port != 0 and the port must be unique across all open sockets.
 *
 * No locking: we run cooperatively on a single CPU before SMP / preemption is
 * introduced. When that changes, every socket_t becomes a per-CPU object or
 * gains a spinlock — at which point the API stays the same.
 */

typedef struct {
    uint16_t      length;
    uint16_t      src_port;
    uint8_t       data[NET64_PACKET_MAX_LEN];
} net64_packet_t;

typedef struct {
    uint8_t         in_use;     /* socket slot allocated? */
    uint8_t         bound;      /* port assigned via bind()? */
    uint16_t        port;
    uint32_t        head;       /* next slot to write */
    uint32_t        tail;       /* next slot to read */
    uint32_t        count;      /* packets currently queued */
    net64_packet_t  queue[NET64_PACKET_QUEUE_DEPTH];
} net64_socket_t;

static net64_socket_t s_sockets[NET64_MAX_SOCKETS];
static uint64_t       s_sendto_total;
static uint64_t       s_recvfrom_total;
static uint64_t       s_drop_total;

static void net64_memcpy(void *dst, const void *src, x86_64_size_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (x86_64_size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

static net64_socket_t *net64_resolve_fd(int fd) {
    if (fd < NET64_FD_BASE) {
        return 0;
    }
    int idx = fd - NET64_FD_BASE;
    if (idx < 0 || idx >= NET64_MAX_SOCKETS) {
        return 0;
    }
    net64_socket_t *s = &s_sockets[idx];
    if (!s->in_use) {
        return 0;
    }
    return s;
}

static net64_socket_t *net64_find_listener(uint16_t port) {
    if (port == 0) {
        return 0;
    }
    for (int i = 0; i < NET64_MAX_SOCKETS; ++i) {
        net64_socket_t *s = &s_sockets[i];
        if (s->in_use && s->bound && s->port == port) {
            return s;
        }
    }
    return 0;
}

void arch_x86_64_net_init(void) {
    for (int i = 0; i < NET64_MAX_SOCKETS; ++i) {
        s_sockets[i].in_use = 0;
        s_sockets[i].bound  = 0;
        s_sockets[i].port   = 0;
        s_sockets[i].head   = 0;
        s_sockets[i].tail   = 0;
        s_sockets[i].count  = 0;
    }
    s_sendto_total   = 0;
    s_recvfrom_total = 0;
    s_drop_total     = 0;
    early_console64_write("[x86_64] net64 loopback socket layer ready\n");
}

int arch_x86_64_net_socket(int domain, int type, int protocol) {
    if (domain != NET64_DOMAIN_OPENOS) return -1;
    if (type != NET64_TYPE_DGRAM)      return -1;
    if (protocol != NET64_PROTO_DEFAULT) return -1;

    for (int i = 0; i < NET64_MAX_SOCKETS; ++i) {
        if (!s_sockets[i].in_use) {
            s_sockets[i].in_use = 1;
            s_sockets[i].bound  = 0;
            s_sockets[i].port   = 0;
            s_sockets[i].head   = 0;
            s_sockets[i].tail   = 0;
            s_sockets[i].count  = 0;
            return NET64_FD_BASE + i;
        }
    }
    return -1;
}

int arch_x86_64_net_bind(int fd, uint16_t port) {
    if (port == 0) return -1;
    net64_socket_t *s = net64_resolve_fd(fd);
    if (!s) return -1;
    if (s->bound) return -1;
    /* Reject port collision across all open sockets. */
    if (net64_find_listener(port)) return -1;
    s->port  = port;
    s->bound = 1;
    return 0;
}

int arch_x86_64_net_sendto(int fd,
                           const void *buf,
                           x86_64_size_t len,
                           uint16_t dst_port) {
    net64_socket_t *src = net64_resolve_fd(fd);
    if (!src)      return -1;
    if (!buf)      return -1;
    if (len == 0 || len > NET64_PACKET_MAX_LEN) return -1;

    net64_socket_t *dst = net64_find_listener(dst_port);
    if (!dst) return -1;

    if (dst->count >= NET64_PACKET_QUEUE_DEPTH) {
        s_drop_total++;
        return -1;
    }

    net64_packet_t *pkt = &dst->queue[dst->head];
    pkt->length   = (uint16_t)len;
    /*
     * If the sender has not yet bind()'d a port, src_port stays 0. That mirrors
     * UDP "ephemeral but unannounced": recvfrom() still reports the literal
     * port value, including 0, so the test can distinguish bound vs ad-hoc.
     */
    pkt->src_port = src->port;
    net64_memcpy(pkt->data, buf, len);
    dst->head = (dst->head + 1u) % NET64_PACKET_QUEUE_DEPTH;
    dst->count++;
    s_sendto_total++;
    return (int)len;
}

int arch_x86_64_net_recvfrom(int fd,
                             void *buf,
                             x86_64_size_t len,
                             uint16_t *src_port_out) {
    net64_socket_t *s = net64_resolve_fd(fd);
    if (!s)   return -1;
    if (!buf) return -1;
    if (s->count == 0) return -1;

    net64_packet_t *pkt = &s->queue[s->tail];
    x86_64_size_t   copy_len = pkt->length;
    if (copy_len > len) {
        /* UDP truncation: copy what fits, drop the rest. */
        copy_len = len;
    }
    net64_memcpy(buf, pkt->data, copy_len);
    if (src_port_out) {
        *src_port_out = pkt->src_port;
    }
    s->tail = (s->tail + 1u) % NET64_PACKET_QUEUE_DEPTH;
    s->count--;
    s_recvfrom_total++;
    return (int)copy_len;
}

int arch_x86_64_net_close(int fd) {
    net64_socket_t *s = net64_resolve_fd(fd);
    if (!s) return -1;
    s->in_use = 0;
    s->bound  = 0;
    s->port   = 0;
    s->head   = 0;
    s->tail   = 0;
    s->count  = 0;
    return 0;
}

void arch_x86_64_net_print_status(void) {
    early_console64_write("[x86_64][net] sockets={");
    for (int i = 0; i < NET64_MAX_SOCKETS; ++i) {
        net64_socket_t *s = &s_sockets[i];
        early_console64_write(" fd=");
        early_console64_write_hex64((uint64_t)(NET64_FD_BASE + i));
        early_console64_write(":");
        if (!s->in_use) {
            early_console64_write("free");
        } else {
            early_console64_write("port=");
            early_console64_write_hex64((uint64_t)s->port);
            early_console64_write(",q=");
            early_console64_write_hex64((uint64_t)s->count);
        }
    }
    early_console64_write(" } sendto=");
    early_console64_write_hex64(s_sendto_total);
    early_console64_write(" recvfrom=");
    early_console64_write_hex64(s_recvfrom_total);
    early_console64_write(" drops=");
    early_console64_write_hex64(s_drop_total);
    early_console64_write("\n");
}

uint64_t arch_x86_64_net_sendto_count(void)   { return s_sendto_total; }
uint64_t arch_x86_64_net_recvfrom_count(void) { return s_recvfrom_total; }
uint64_t arch_x86_64_net_drop_count(void)     { return s_drop_total; }

void arch_x86_64_net_reset_counters(void) {
    s_sendto_total   = 0;
    s_recvfrom_total = 0;
    s_drop_total     = 0;
}
