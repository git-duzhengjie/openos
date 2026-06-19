/* ============================================================
 * openos - minimal user runtime helpers
 * ============================================================ */

#ifndef OPENOS_USER_OPENOS_H
#define OPENOS_USER_OPENOS_H

#define SYS_EXIT        1
#define SYS_GETPID      20
#define SYS_GETTID      21
#define SYS_WRITE       64
#define SYS_READ        63
#define SYS_MALLOC      73
#define SYS_FREE        74
#define SYS_SLEEP       200
#define SYS_YIELD       201
#define SYS_FORK        220
#define SYS_EXEC        221
#define SYS_WAIT        222
#define SYS_WAITPID     223
#define SYS_GETPPID     224
#define SYS_OPEN        225
#define SYS_CLOSE       226
#define SYS_READ_FD     227
#define SYS_WRITE_FD    228
#define SYS_SEEK        229
#define SYS_MKDIR       230
#define SYS_UNLINK      231
#define SYS_RMDIR       232
#define SYS_SPAWN       233
#define SYS_EXEC_ENV    234
#define SYS_SPAWN_ENV   235
#define SYS_STAT        236
#define SYS_GETCWD      237
#define SYS_CHDIR       238
#define SYS_READDIR     239
#define SYS_FSTAT       240
#define SYS_LSTAT       241
#define SYS_DUP         242
#define SYS_DUP2        243
#define SYS_PIPE        244
#define SYS_KILL        245
#define SYS_ALARM       246
#define SYS_LINK        247
#define SYS_SYMLINK     248
#define SYS_READLINK    249
#define SYS_MMAP        250
#define SYS_MUNMAP      251
#define SYS_BRK         252
#define SYS_SBRK        253
#define SYS_THREAD_CREATE 254
#define SYS_THREAD_EXIT   255
#define SYS_MUTEX_CREATE  256
#define SYS_MUTEX_LOCK    257
#define SYS_MUTEX_UNLOCK  258
#define SYS_MUTEX_DESTROY 259
#define SYS_SEM_CREATE    260
#define SYS_SEM_WAIT      261
#define SYS_SEM_POST      262
#define SYS_SEM_DESTROY   263
#define SYS_COND_CREATE   264
#define SYS_COND_WAIT     265
#define SYS_COND_SIGNAL   266
#define SYS_COND_BROADCAST 267
#define SYS_COND_DESTROY  268
#define SYS_FUTEX_WAIT    269
#define SYS_FUTEX_WAKE    270
#define SYS_GETPRIORITY   271
#define SYS_SETPRIORITY   272
#define SYS_NICE          273
#define SYS_CHMOD         274
#define SYS_CHOWN         275
#define SYS_GETUID        276
#define SYS_SETUID        277
#define SYS_GETGID        278
#define SYS_SETGID        279
#define SYS_POLL          280
#define SYS_SELECT        281
#define SYS_FSYNC         282
#define SYS_SOCKET        283
#define SYS_BIND          284
#define SYS_LISTEN        285
#define SYS_ACCEPT        286
#define SYS_CONNECT       287
#define SYS_SEND          288
#define SYS_RECV          289
#define SYS_SENDTO        290
#define SYS_RECVFROM      291
#define SYS_NETINFO       292
#define SYS_PING          293
#define SYS_NETCONFIG     294
#define SYS_FIREWALL      295
#define SYS_MQ_CREATE     296
#define SYS_MQ_SEND       297
#define SYS_MQ_RECV       298
#define SYS_MQ_DESTROY    299
#define SYS_SHM_CREATE    300
#define SYS_SHM_MAP       301
#define SYS_SHM_DESTROY   302
#define SYS_EVENTFD_CREATE 303
#define SYS_EVENTFD_WRITE  304
#define SYS_EVENTFD_READ   305
#define SYS_EVENTFD_DESTROY 306
#define SYS_SOCKETPAIR    307
#define SYS_GETPWUID      308
#define SYS_GETGRGID      309
#define SYS_CAPGET       310
#define SYS_CAPSET       311
#define SYS_SANDBOX_GET  312
#define SYS_SANDBOX_SET  313
#define SYS_AI_REQUEST   314
#define SYS_NETDEVCTL    315
#define SYS_DNSLOOKUP    316

#define OPENOS_CAP_SETUID    (1u << 0)
#define OPENOS_CAP_SETGID    (1u << 1)
#define OPENOS_CAP_NET_ADMIN (1u << 2)
#define OPENOS_CAP_SYS_ADMIN (1u << 3)
#define OPENOS_CAP_KILL      (1u << 4)
#define OPENOS_CAP_ALL       0xffffffffu
#define OPENOS_CAP_BASIC     0u

#define OPENOS_AF_UNSPEC  0
#define OPENOS_AF_INET    2
#define OPENOS_SOCK_STREAM 1
#define OPENOS_SOCK_DGRAM  2
#define OPENOS_SOCK_RAW    3
#define OPENOS_INADDR_ANY  0u

typedef struct openos_sockaddr {
    unsigned short sa_family;
    char sa_data[14];
} openos_sockaddr_t;

typedef struct openos_sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    unsigned int sin_addr;
    unsigned char sin_zero[8];
} openos_sockaddr_in_t;

#define NET_DEVICE_FLAG_PRESENT 0x00000001u
#define NET_DEVICE_FLAG_UP      0x00000002u
#define NET_DEVICE_FLAG_LINK_UP 0x00000004u
#define NET_DEVICE_FLAG_DHCP    0x00000008u
#define NET_DEVICE_FLAG_DEFAULT 0x00000010u
#define NET_DEVICE_FLAG_STATIC  0x00000020u

#define NET_CONFIG_MODE_NONE   0u
#define NET_CONFIG_MODE_STATIC 1u
#define NET_CONFIG_MODE_DHCP   2u

#define NETDEV_CTL_SET_DOWN     0u
#define NETDEV_CTL_SET_UP       1u
#define NETDEV_CTL_DHCP_START   2u
#define NETDEV_CTL_DHCP_RENEW   3u
#define NETDEV_CTL_DHCP_RELEASE 4u
#define NETDEV_CTL_REFRESH      5u

typedef struct openos_netinfo {
    char name[16];
    unsigned char mac[6];
    unsigned int ip;
    unsigned int netmask;
    unsigned int gateway;
    unsigned int dns;
    unsigned int flags;
    unsigned int config_mode;
    unsigned int rx_packets;
    unsigned int tx_packets;
    unsigned int rx_dropped;
    unsigned int tx_dropped;
    unsigned int arp_entries;
    unsigned int udp_bindings;
    unsigned int tcp_listeners;
    unsigned int tcp_connections;
    unsigned int icmp_echo_requests;
    unsigned int icmp_echo_replies;
} openos_netinfo_t;
typedef struct openos_user {
    unsigned int uid;
    unsigned int gid;
    char name[32];
    char home[64];
    char shell[64];
} openos_user_t;

typedef struct openos_group {
    unsigned int gid;
    char name[32];
} openos_group_t;

typedef struct openos_ai_request {
    const char *prompt;
    char *response;
    unsigned int response_len;
    unsigned int flags;
} openos_ai_request_t;

#define OPENOS_FW_OP_GET    0u
#define OPENOS_FW_OP_ADD    1u
#define OPENOS_FW_OP_DELETE 2u
#define OPENOS_FW_OP_CLEAR  3u

#define OPENOS_FW_ACTION_ALLOW 0u
#define OPENOS_FW_ACTION_DENY  1u

#define OPENOS_FW_PROTO_ANY  0u
#define OPENOS_FW_PROTO_ICMP 1u
#define OPENOS_FW_PROTO_TCP  6u
#define OPENOS_FW_PROTO_UDP  17u

typedef struct openos_firewall_rule {
    unsigned int used;
    unsigned int action;
    unsigned int protocol;
    unsigned int port;
    unsigned int hits;
} openos_firewall_rule_t;

static inline unsigned short openos_htons(unsigned short v)
{
    return (unsigned short)((v >> 8) | (v << 8));
}

#define OPENOS_POLLIN     0x0001
#define OPENOS_POLLOUT    0x0004
#define OPENOS_POLLERR    0x0008
#define OPENOS_POLLHUP    0x0010

#define WNOHANG         1
#define SIGKILL         9
#define SIGALRM         14
#define SIGTERM         15
#define WIFEXITED(status)      (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)    (((status) >> 8) & 0xff)

#define FS_FILE         0x1000
#define FS_DIR          0x2000
#define FS_SYMLINK      0xA000
#define O_RDONLY        0
#define O_WRONLY        1
#define O_RDWR          2
#define O_CREAT         0x100
#define O_TRUNC         0x200
#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2
#define EOF             (-1)
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2
#define OPENOS_PATH_MAX 128

#define S_IRUSR         0400
#define S_IWUSR         0200
#define S_IXUSR         0100
#define S_IRGRP         0040
#define S_IWGRP         0020
#define S_IXGRP         0010
#define S_IROTH         0004
#define S_IWOTH         0002
#define S_IXOTH         0001
#define S_IRWXU         0700
#define S_IRWXG         0070
#define S_IRWXO         0007
#define S_IRWXUGO       0777

#define OPENOS_EPERM       1
#define OPENOS_ENOENT      2
#define OPENOS_EIO         5
#define OPENOS_EBADF       9
#define OPENOS_ENOMEM      12
#define OPENOS_EACCES      13
#define OPENOS_EFAULT      14
#define OPENOS_EBUSY       16
#define OPENOS_EEXIST      17
#define OPENOS_ENODEV      19
#define OPENOS_ENOTDIR     20
#define OPENOS_EISDIR      21
#define OPENOS_EINVAL      22
#define OPENOS_ENFILE      23
#define OPENOS_EMFILE      24
#define OPENOS_ENOSPC      28
#define OPENOS_EPIPE       32
#define OPENOS_ENOSYS      38
#define OPENOS_ENOTEMPTY   39
#define OPENOS_ESPIPE      29

static int openos_errno = 0;

static inline int *openos_errno_location(void)
{
    return &openos_errno;
}

#ifndef errno
#define errno (*openos_errno_location())
#endif

static inline int openos_get_errno(void)
{
    return openos_errno;
}

static inline void openos_set_errno(int err)
{
    openos_errno = err < 0 ? -err : err;
}

static inline void openos_clear_errno(void)
{
    openos_errno = 0;
}

static inline int openos_syscall_result(int ret)
{
    if (ret < 0) {
        openos_set_errno(ret);
        return -1;
    }
    openos_clear_errno();
    return ret;
}

typedef unsigned int openos_uint32_t;
typedef int openos_thread_t;
typedef int openos_mutex_t;
typedef int openos_sem_t;
typedef int openos_cond_t;
typedef int openos_mq_t;
typedef int openos_shm_t;
typedef int openos_eventfd_t;
typedef struct openos_service_channel {
    int client_fd;
    int server_fd;
} openos_service_channel_t;

#define OPENOS_SERVICE_PAYLOAD_MAX 64
#define OPENOS_SERVICE_STATUS_OK 0
#define OPENOS_SERVICE_STATUS_ERROR 1

typedef struct openos_service_message {
    unsigned int service;
    unsigned int opcode;
    unsigned int seq;
    unsigned int status;
    unsigned int length;
    unsigned char payload[OPENOS_SERVICE_PAYLOAD_MAX];
} openos_service_message_t;
typedef void (*openos_thread_start_t)(void *);

typedef struct openos_stat {
    openos_uint32_t ino;
    openos_uint32_t mode;
    openos_uint32_t size;
    openos_uint32_t nlinks;
    openos_uint32_t fs_type;
    openos_uint32_t uid;
    openos_uint32_t gid;
} openos_stat_t;

typedef struct openos_dirent {
    openos_uint32_t ino;
    openos_uint32_t mode;
    openos_uint32_t size;
    char name[32];
} openos_dirent_t;

typedef struct openos_pollfd {
    int fd;
    short events;
    short revents;
} openos_pollfd_t;

static inline int openos_syscall3(int num, int a, int b, int c)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c)
        : "memory"
    );
    return ret;
}

static inline int openos_syscall0(int num)
{
    return openos_syscall3(num, 0, 0, 0);
}

static inline int openos_syscall1(int num, int a)
{
    return openos_syscall3(num, a, 0, 0);
}

static inline int openos_syscall2(int num, int a, int b)
{
    return openos_syscall3(num, a, b, 0);
}

static inline int openos_syscall4(int num, int a, int b, int c, int d)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d)
        : "memory"
    );
    return ret;
}

static inline int openos_socket(int domain, int type, int protocol)
{
    return openos_syscall_result(openos_syscall3(SYS_SOCKET, domain, type, protocol));
}

static inline int openos_socketpair(int domain, int type, int protocol, int sv[2])
{
    if (!sv) return -1;
    return openos_syscall_result(openos_syscall4(SYS_SOCKETPAIR, domain, type, protocol, (int)sv));
}

static inline int openos_bind(int fd, const openos_sockaddr_t *addr, unsigned int addrlen)
{
    return openos_syscall_result(openos_syscall3(SYS_BIND, fd, (int)addr, (int)addrlen));
}

static inline int openos_listen(int fd, int backlog)
{
    return openos_syscall_result(openos_syscall2(SYS_LISTEN, fd, backlog));
}

static inline int openos_accept(int fd, openos_sockaddr_t *addr, unsigned int *addrlen)
{
    return openos_syscall_result(openos_syscall3(SYS_ACCEPT, fd, (int)addr, (int)addrlen));
}

static inline int openos_connect(int fd, const openos_sockaddr_t *addr, unsigned int addrlen)
{
    return openos_syscall_result(openos_syscall3(SYS_CONNECT, fd, (int)addr, (int)addrlen));
}

static inline int openos_send(int fd, const void *buf, unsigned int len, int flags)
{
    return openos_syscall_result(openos_syscall4(SYS_SEND, fd, (int)buf, (int)len, flags));
}

static inline int openos_recv(int fd, void *buf, unsigned int len, int flags)
{
    return openos_syscall_result(openos_syscall4(SYS_RECV, fd, (int)buf, (int)len, flags));
}


static inline int openos_syscall5(int num, int a, int b, int c, int d, int e)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
        : "memory"
    );
    return ret;
}

static inline int openos_sendto(int fd, const void *buf, unsigned int len, int flags,
                                const openos_sockaddr_t *addr, unsigned int addrlen)
{
    (void)addrlen;
    return openos_syscall_result(openos_syscall5(SYS_SENDTO, fd, (int)buf, (int)len, flags, (int)addr));
}

static inline int openos_recvfrom(int fd, void *buf, unsigned int len, int flags,
                                  openos_sockaddr_t *addr, unsigned int *addrlen)
{
    int ret = openos_syscall_result(openos_syscall5(SYS_RECVFROM, fd, (int)buf, (int)len, flags, (int)addr));
    if (ret >= 0 && addr && addrlen)
        *addrlen = sizeof(openos_sockaddr_in_t);
    return ret;
}

static inline int openos_netinfo(openos_netinfo_t *info)
{
    return openos_syscall_result(openos_syscall1(SYS_NETINFO, (int)info));
}

static inline int openos_ping(unsigned int ip)
{
    return openos_syscall_result(openos_syscall1(SYS_PING, (int)ip));
}

static inline int openos_dnslookup(const char *name, unsigned int *ip)
{
    return openos_syscall_result(openos_syscall2(SYS_DNSLOOKUP, (int)name, (int)ip));
}

static inline int openos_netconfig(unsigned int ip, unsigned int netmask, unsigned int gateway, unsigned int dns)
{
    return openos_syscall_result(openos_syscall4(SYS_NETCONFIG, (int)ip, (int)netmask, (int)gateway, (int)dns));
}

static inline int openos_netdevctl(const char *name, unsigned int op)
{
    return openos_syscall_result(openos_syscall2(SYS_NETDEVCTL, (int)name, (int)op));
}

static inline int openos_firewall(unsigned int op, unsigned int index, openos_firewall_rule_t *rule)
{
    return openos_syscall_result(openos_syscall3(SYS_FIREWALL, (int)op, (int)index, (int)rule));
}

static inline void openos_thread_exit(int code);

static void __attribute__((unused)) openos_thread_return_trampoline(void)
{
    openos_thread_exit(0);
}

static inline int openos_thread_create(openos_thread_t *thread,
                                       openos_thread_start_t start,
                                       void *arg)
{
    int tid;

    if (!start) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }

    tid = openos_syscall_result(openos_syscall3(SYS_THREAD_CREATE,
                                               (int)start,
                                               (int)arg,
                                               (int)openos_thread_return_trampoline));
    if (tid < 0)
        return -1;
    if (thread)
        *thread = tid;
    return 0;
}

static inline void openos_thread_exit(int code)
{
    openos_syscall1(SYS_THREAD_EXIT, code);
    for (;;) {
        __asm__ volatile("pause");
    }
}

static inline int openos_mutex_init(openos_mutex_t *mutex)
{
    int handle;

    if (!mutex) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }

    handle = openos_syscall_result(openos_syscall0(SYS_MUTEX_CREATE));
    if (handle < 0)
        return -1;
    *mutex = handle;
    return 0;
}

static inline int openos_mutex_lock(openos_mutex_t *mutex)
{
    if (!mutex || *mutex <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return openos_syscall_result(openos_syscall1(SYS_MUTEX_LOCK, *mutex));
}

static inline int openos_mutex_unlock(openos_mutex_t *mutex)
{
    if (!mutex || *mutex <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return openos_syscall_result(openos_syscall1(SYS_MUTEX_UNLOCK, *mutex));
}

static inline int openos_mutex_destroy(openos_mutex_t *mutex)
{
    int ret;

    if (!mutex || *mutex <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    ret = openos_syscall_result(openos_syscall1(SYS_MUTEX_DESTROY, *mutex));
    if (ret == 0)
        *mutex = 0;
    return ret;
}

static inline int openos_sem_init(openos_sem_t *sem, int initial)
{
    int handle;

    if (!sem || initial < 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }

    handle = openos_syscall_result(openos_syscall1(SYS_SEM_CREATE, initial));
    if (handle < 0)
        return -1;
    *sem = handle;
    return 0;
}

static inline int openos_sem_wait(openos_sem_t *sem)
{
    if (!sem || *sem <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return openos_syscall_result(openos_syscall1(SYS_SEM_WAIT, *sem));
}

static inline int openos_sem_post(openos_sem_t *sem)
{
    if (!sem || *sem <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return openos_syscall_result(openos_syscall1(SYS_SEM_POST, *sem));
}

static inline int openos_sem_destroy(openos_sem_t *sem)
{
    int ret;

    if (!sem || *sem <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    ret = openos_syscall_result(openos_syscall1(SYS_SEM_DESTROY, *sem));
    if (ret == 0)
        *sem = 0;
    return ret;
}

static inline int openos_cond_init(openos_cond_t *cond)
{
    int handle;

    if (!cond) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }

    handle = openos_syscall_result(openos_syscall0(SYS_COND_CREATE));
    if (handle < 0)
        return -1;
    *cond = handle;
    return 0;
}

static inline int openos_cond_wait(openos_cond_t *cond, openos_mutex_t *mutex)
{
    if (!cond || *cond <= 0 || !mutex || *mutex <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return openos_syscall_result(openos_syscall2(SYS_COND_WAIT, *cond, *mutex));
}

static inline int openos_cond_signal(openos_cond_t *cond)
{
    if (!cond || *cond <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return openos_syscall_result(openos_syscall1(SYS_COND_SIGNAL, *cond));
}

static inline int openos_cond_broadcast(openos_cond_t *cond)
{
    if (!cond || *cond <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return openos_syscall_result(openos_syscall1(SYS_COND_BROADCAST, *cond));
}

static inline int openos_cond_destroy(openos_cond_t *cond)
{
    int ret;

    if (!cond || *cond <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    ret = openos_syscall_result(openos_syscall1(SYS_COND_DESTROY, *cond));
    if (ret == 0)
        *cond = 0;
    return ret;
}

static inline int openos_futex_wait(volatile unsigned int *uaddr, unsigned int expected)
{
    return openos_syscall_result(openos_syscall2(SYS_FUTEX_WAIT, (int)uaddr, (int)expected));
}

static inline int openos_futex_wake(volatile unsigned int *uaddr, unsigned int max_wake)
{
    return openos_syscall_result(openos_syscall2(SYS_FUTEX_WAKE, (int)uaddr, (int)max_wake));
}

static inline int openos_mq_create(openos_mq_t *mq)
{
    int handle;
    if (!mq) return -1;
    handle = openos_syscall_result(openos_syscall0(SYS_MQ_CREATE));
    if (handle < 0) return handle;
    *mq = handle;
    return 0;
}

static inline int openos_mq_send(openos_mq_t *mq, const void *buf, unsigned int len)
{
    if (!mq || *mq <= 0 || !buf || len == 0) return -1;
    return openos_syscall_result(openos_syscall3(SYS_MQ_SEND, *mq, (int)buf, (int)len));
}

static inline int openos_mq_recv(openos_mq_t *mq, void *buf, unsigned int len)
{
    if (!mq || *mq <= 0 || !buf || len == 0) return -1;
    return openos_syscall_result(openos_syscall3(SYS_MQ_RECV, *mq, (int)buf, (int)len));
}

static inline int openos_mq_destroy(openos_mq_t *mq)
{
    int ret;
    if (!mq || *mq <= 0) return -1;
    ret = openos_syscall_result(openos_syscall1(SYS_MQ_DESTROY, *mq));
    if (ret == 0) *mq = 0;
    return ret;
}

static inline int openos_shm_create(openos_shm_t *shm)
{
    int handle;
    if (!shm) return -1;
    handle = openos_syscall_result(openos_syscall0(SYS_SHM_CREATE));
    if (handle < 0) return handle;
    *shm = handle;
    return 0;
}

static inline void *openos_shm_map(openos_shm_t *shm)
{
    int addr;
    if (!shm || *shm <= 0) return (void *)-1;
    addr = openos_syscall_result(openos_syscall1(SYS_SHM_MAP, *shm));
    if (addr < 0) return (void *)-1;
    return (void *)addr;
}

static inline int openos_shm_destroy(openos_shm_t *shm)
{
    int ret;
    if (!shm || *shm <= 0) return -1;
    ret = openos_syscall_result(openos_syscall1(SYS_SHM_DESTROY, *shm));
    if (ret == 0) *shm = 0;
    return ret;
}

static inline int openos_eventfd_create(openos_eventfd_t *efd, unsigned int initval)
{
    int handle;
    if (!efd) return -1;
    handle = openos_syscall_result(openos_syscall1(SYS_EVENTFD_CREATE, (int)initval));
    if (handle < 0) return -1;
    *efd = handle;
    return 0;
}

static inline int openos_eventfd_write(openos_eventfd_t *efd, unsigned int value)
{
    if (!efd || *efd <= 0) return -1;
    return openos_syscall_result(openos_syscall2(SYS_EVENTFD_WRITE, *efd, (int)value));
}

static inline int openos_eventfd_read(openos_eventfd_t *efd, unsigned int *value)
{
    if (!efd || *efd <= 0 || !value) return -1;
    return openos_syscall_result(openos_syscall2(SYS_EVENTFD_READ, *efd, (int)value));
}

static inline int openos_eventfd_destroy(openos_eventfd_t *efd)
{
    int ret;
    if (!efd || *efd <= 0) return -1;
    ret = openos_syscall_result(openos_syscall1(SYS_EVENTFD_DESTROY, *efd));
    if (ret == 0) *efd = 0;
    return ret;
}

static inline int openos_getpriority(int pid)
{
    int ret = openos_syscall1(SYS_GETPRIORITY, pid);
    if (ret == -1000)
        return openos_syscall_result(-1);
    openos_clear_errno();
    return ret;
}

static inline int openos_setpriority(int pid, int nice_value)
{
    return openos_syscall_result(openos_syscall2(SYS_SETPRIORITY, pid, nice_value));
}

static inline int openos_nice(int inc)
{
    int ret = openos_syscall1(SYS_NICE, inc);
    if (ret == -1000)
        return openos_syscall_result(-1);
    openos_clear_errno();
    return ret;
}

static inline void openos_exit(int code)
{
    openos_syscall1(SYS_EXIT, code);
    for (;;) {
        __asm__ volatile("pause");
    }
}

static inline int openos_strlen(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static inline int openos_strcmp(const char *a, const char *b)
{
    int i = 0;

    if (!a || !b)
        return a == b ? 0 : (a ? 1 : -1);

    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static inline int openos_str_copy(char *dst, const char *src, int size)
{
    int i;

    if (!dst || !src || size <= 0)
        return -1;

    for (i = 0; i < size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;

    if (src[i])
        return -1;
    return 0;
}

static inline void *openos_memset(void *dst, int value, int len)
{
    unsigned char *p = (unsigned char *)dst;
    int i;

    if (!dst || len <= 0)
        return dst;

    for (i = 0; i < len; i++)
        p[i] = (unsigned char)value;
    return dst;
}

static inline void *openos_memcpy(void *dst, const void *src, int len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    int i;

    if (!dst || !src || len <= 0)
        return dst;

    for (i = 0; i < len; i++)
        d[i] = s[i];
    return dst;
}

static inline void *openos_memmove(void *dst, const void *src, int len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    int i;

    if (!dst || !src || len <= 0)
        return dst;

    if (d < s) {
        for (i = 0; i < len; i++)
            d[i] = s[i];
    } else if (d > s) {
        for (i = len - 1; i >= 0; i--)
            d[i] = s[i];
    }
    return dst;
}

static inline int openos_memcmp(const void *a, const void *b, int len)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    int i;

    if (a == b || len <= 0)
        return 0;
    if (!a || !b)
        return a ? 1 : -1;

    for (i = 0; i < len; i++) {
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static inline int openos_strncmp(const char *a, const char *b, int n)
{
    int i;

    if (n <= 0)
        return 0;
    if (!a || !b)
        return a == b ? 0 : (a ? 1 : -1);

    for (i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb || ca == 0 || cb == 0)
            return (int)ca - (int)cb;
    }
    return 0;
}

static inline char *openos_strchr(const char *s, int ch)
{
    char c = (char)ch;

    if (!s)
        return 0;

    while (*s) {
        if (*s == c)
            return (char *)s;
        s++;
    }
    return c == 0 ? (char *)s : 0;
}

static inline char *openos_strrchr(const char *s, int ch)
{
    const char *last = 0;
    char c = (char)ch;

    if (!s)
        return 0;

    do {
        if (*s == c)
            last = s;
    } while (*s++);

    return (char *)last;
}

static inline char *openos_strstr(const char *haystack, const char *needle)
{
    int needle_len;
    int i;

    if (!haystack || !needle)
        return 0;
    if (!needle[0])
        return (char *)haystack;

    needle_len = openos_strlen(needle);
    for (i = 0; haystack[i]; i++) {
        if (openos_strncmp(&haystack[i], needle, needle_len) == 0)
            return (char *)&haystack[i];
    }
    return 0;
}

static inline char *openos_strcpy(char *dst, const char *src)
{
    char *out = dst;
    if (!dst || !src)
        return dst;
    while ((*dst++ = *src++) != 0) {
    }
    return out;
}

static inline char *openos_strncpy(char *dst, const char *src, int n)
{
    int i;
    if (!dst || !src || n <= 0)
        return dst;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

static inline char *openos_strcat(char *dst, const char *src)
{
    char *out = dst;
    if (!dst || !src)
        return dst;
    while (*dst)
        dst++;
    while ((*dst++ = *src++) != 0) {
    }
    return out;
}

static inline char *openos_strncat(char *dst, const char *src, int n)
{
    char *out = dst;
    int i;
    if (!dst || !src || n <= 0)
        return dst;
    while (*dst)
        dst++;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return out;
}


static inline int openos_isdigit(int ch)
{
    return ch >= '0' && ch <= '9';
}

static inline int openos_islower(int ch)
{
    return ch >= 'a' && ch <= 'z';
}

static inline int openos_isupper(int ch)
{
    return ch >= 'A' && ch <= 'Z';
}

static inline int openos_isalpha(int ch)
{
    return openos_islower(ch) || openos_isupper(ch);
}

static inline int openos_isalnum(int ch)
{
    return openos_isalpha(ch) || openos_isdigit(ch);
}

static inline int openos_isxdigit(int ch)
{
    return openos_isdigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

static inline int openos_isprint(int ch)
{
    return ch >= 0x20 && ch <= 0x7e;
}

static inline int openos_iscntrl(int ch)
{
    return (ch >= 0 && ch < 0x20) || ch == 0x7f;
}

static inline int openos_tolower(int ch)
{
    return openos_isupper(ch) ? ch - 'A' + 'a' : ch;
}

static inline int openos_toupper(int ch)
{
    return openos_islower(ch) ? ch - 'a' + 'A' : ch;
}


static inline int openos_isspace(int ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f';
}

static inline int openos_atoi(const char *s)
{
    int sign = 1;
    int value = 0;

    if (!s)
        return 0;

    while (openos_isspace(*s))
        s++;
    if (*s == '-' || *s == '+') {
        if (*s == '-')
            sign = -1;
        s++;
    }
    while (openos_isdigit(*s)) {
        value = value * 10 + (*s - '0');
        s++;
    }
    return sign * value;
}

static inline char *openos_itoa(int value, char *buf, int base)
{
    char tmp[33];
    unsigned int v;
    int i = 0;
    int j = 0;
    int neg = 0;

    if (!buf || base < 2 || base > 16)
        return 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return buf;
    }

    if (value < 0 && base == 10) {
        neg = 1;
        v = (unsigned int)(-value);
    } else {
        v = (unsigned int)value;
    }

    while (v > 0 && i < (int)sizeof(tmp)) {
        int digit = (int)(v % (unsigned int)base);
        tmp[i++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        v /= (unsigned int)base;
    }

    if (neg)
        buf[j++] = '-';
    while (i > 0)
        buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

static inline int openos_write(int fd, const char *s, int len)
{
    return openos_syscall_result(openos_syscall3(SYS_WRITE, fd, (int)s, len));
}

static inline int openos_open(const char *path, int flags, int mode)
{
    return openos_syscall_result(openos_syscall3(SYS_OPEN, (int)path, flags, mode));
}

static inline int openos_close(int fd)
{
    return openos_syscall_result(openos_syscall1(SYS_CLOSE, fd));
}

static inline int openos_fsync(int fd)
{
    return openos_syscall_result(openos_syscall1(SYS_FSYNC, fd));
}

static inline int openos_read(int fd, void *buf, int len)
{
    return openos_syscall_result(openos_syscall3(SYS_READ_FD, fd, (int)buf, len));
}

static inline int openos_write_fd(int fd, const void *buf, int len)
{
    return openos_syscall_result(openos_syscall3(SYS_WRITE_FD, fd, (int)buf, len));
}

static inline int openos_service_channel_create(openos_service_channel_t *channel)
{
    int sv[2];
    if (!channel) return -1;
    channel->client_fd = -1;
    channel->server_fd = -1;
    if (openos_socketpair(OPENOS_AF_UNSPEC, OPENOS_SOCK_STREAM, 0, sv) != 0)
        return -1;
    channel->client_fd = sv[0];
    channel->server_fd = sv[1];
    return 0;
}

static inline int openos_service_client_close(openos_service_channel_t *channel)
{
    int ret = 0;
    if (!channel) return -1;
    if (channel->client_fd >= 0) ret = openos_close(channel->client_fd);
    channel->client_fd = -1;
    return ret;
}

static inline int openos_service_server_close(openos_service_channel_t *channel)
{
    int ret = 0;
    if (!channel) return -1;
    if (channel->server_fd >= 0) ret = openos_close(channel->server_fd);
    channel->server_fd = -1;
    return ret;
}

static inline int openos_service_channel_close(openos_service_channel_t *channel)
{
    int ret = 0;
    if (!channel) return -1;
    if (channel->client_fd >= 0 && openos_close(channel->client_fd) != 0) ret = -1;
    if (channel->server_fd >= 0 && openos_close(channel->server_fd) != 0) ret = -1;
    channel->client_fd = -1;
    channel->server_fd = -1;
    return ret;
}

static inline int openos_service_call(openos_service_channel_t *channel, const void *request, int request_len, void *reply, int reply_len)
{
    int n;
    if (!channel || channel->client_fd < 0 || !request || request_len <= 0 || !reply || reply_len <= 0)
        return -1;
    if (openos_write_fd(channel->client_fd, request, request_len) != request_len)
        return -1;
    n = openos_read(channel->client_fd, reply, reply_len);
    return n;
}

static inline int openos_service_recv(openos_service_channel_t *channel, void *request, int request_len)
{
    if (!channel || channel->server_fd < 0 || !request || request_len <= 0)
        return -1;
    return openos_read(channel->server_fd, request, request_len);
}

static inline int openos_service_reply(openos_service_channel_t *channel, const void *reply, int reply_len)
{
    if (!channel || channel->server_fd < 0 || !reply || reply_len <= 0)
        return -1;
    return openos_write_fd(channel->server_fd, reply, reply_len);
}

static inline void openos_service_message_init(openos_service_message_t *msg,
                                               unsigned int service,
                                               unsigned int opcode,
                                               unsigned int seq,
                                               const void *payload,
                                               unsigned int length)
{
    if (!msg) return;
    openos_memset(msg, 0, sizeof(*msg));
    msg->service = service;
    msg->opcode = opcode;
    msg->seq = seq;
    msg->status = OPENOS_SERVICE_STATUS_OK;
    if (payload && length > 0) {
        if (length > OPENOS_SERVICE_PAYLOAD_MAX)
            length = OPENOS_SERVICE_PAYLOAD_MAX;
        openos_memcpy(msg->payload, payload, (int)length);
        msg->length = length;
    }
}

static inline int openos_service_send_message(int fd, const openos_service_message_t *msg)
{
    if (fd < 0 || !msg) return -1;
    return openos_write_fd(fd, msg, sizeof(*msg)) == (int)sizeof(*msg) ? 0 : -1;
}

static inline int openos_service_recv_message(int fd, openos_service_message_t *msg)
{
    if (fd < 0 || !msg) return -1;
    return openos_read(fd, msg, sizeof(*msg)) == (int)sizeof(*msg) ? 0 : -1;
}

static inline int openos_service_request(openos_service_channel_t *channel,
                                         openos_service_message_t *request,
                                         openos_service_message_t *reply)
{
    if (!channel || !request || !reply || channel->client_fd < 0) return -1;
    if (openos_service_send_message(channel->client_fd, request) != 0)
        return -1;
    if (openos_service_recv_message(channel->client_fd, reply) != 0)
        return -1;
    if (reply->seq != request->seq || reply->service != request->service)
        return -1;
    return reply->status == OPENOS_SERVICE_STATUS_OK ? 0 : -1;
}

static inline int openos_service_receive_request(openos_service_channel_t *channel,
                                                 openos_service_message_t *request)
{
    if (!channel || !request || channel->server_fd < 0) return -1;
    return openos_service_recv_message(channel->server_fd, request);
}

static inline int openos_service_send_reply(openos_service_channel_t *channel,
                                            const openos_service_message_t *reply)
{
    if (!channel || !reply || channel->server_fd < 0) return -1;
    return openos_service_send_message(channel->server_fd, reply);
}

static inline int openos_dup(int oldfd)
{
    return openos_syscall_result(openos_syscall1(SYS_DUP, oldfd));
}

static inline int openos_dup2(int oldfd, int newfd)
{
    return openos_syscall_result(openos_syscall3(SYS_DUP2, oldfd, newfd, 0));
}

static inline int openos_pipe(int pipefd[2])
{
    return openos_syscall_result(openos_syscall1(SYS_PIPE, (int)pipefd));
}

static inline void openos_write_str(const char *s)
{
    openos_write(STDOUT_FILENO, s, openos_strlen(s));
}

static inline int openos_putchar(int ch)
{
    char c = (char)ch;
    return openos_write_fd(STDOUT_FILENO, &c, 1);
}

static inline int openos_puts(const char *s)
{
    if (s)
        openos_write_fd(STDOUT_FILENO, s, openos_strlen(s));
    openos_write_fd(STDOUT_FILENO, "\n", 1);
    return 0;
}

static inline int openos_print_int(int value)
{
    char buf[16];

    if (!openos_itoa(value, buf, 10))
        return -1;
    return openos_write_fd(STDOUT_FILENO, buf, openos_strlen(buf));
}

static inline void openos_format_emit_char(char *buf, int size, int *pos, int *total, int fd, int ch)
{
    char c = (char)ch;

    if (total)
        (*total)++;
    if (buf && size > 0 && pos && *pos < size - 1)
        buf[(*pos)++] = c;
    if (fd >= 0)
        openos_write_fd(fd, &c, 1);
}

static inline int openos_format_strlen(const char *s)
{
    return s ? openos_strlen(s) : 0;
}

static inline void openos_format_emit_str(char *buf, int size, int *pos, int *total, int fd, const char *s)
{
    if (!s)
        s = "(null)";
    while (*s) {
        openos_format_emit_char(buf, size, pos, total, fd, *s);
        s++;
    }
}

static inline void openos_format_emit_padded(char *buf, int size, int *pos, int *total, int fd,
                                             const char *s, int width, int left_align, char pad)
{
    int len;
    int pad_count;

    if (!s)
        s = "(null)";
    len = openos_format_strlen(s);
    pad_count = width > len ? width - len : 0;
    if (!left_align) {
        while (pad_count-- > 0)
            openos_format_emit_char(buf, size, pos, total, fd, pad);
    }
    openos_format_emit_str(buf, size, pos, total, fd, s);
    if (left_align) {
        while (pad_count-- > 0)
            openos_format_emit_char(buf, size, pos, total, fd, ' ');
    }
}

static inline void openos_utoa_base(unsigned int value, char *buf, int base)
{
    char tmp[33];
    int i = 0;
    int j = 0;

    if (!buf || base < 2 || base > 16)
        return;
    if (value == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    while (value > 0 && i < (int)sizeof(tmp)) {
        int digit = (int)(value % (unsigned int)base);
        tmp[i++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= (unsigned int)base;
    }
    while (i > 0)
        buf[j++] = tmp[--i];
    buf[j] = 0;
}

static inline int openos_vformat(char *buf, int size, int fd, const char *fmt, __builtin_va_list ap)
{
    int pos = 0;
    int total = 0;

    if (!fmt) {
        openos_set_errno(OPENOS_EINVAL);
        if (buf && size > 0)
            buf[0] = 0;
        return -1;
    }

    while (*fmt) {
        int left_align = 0;
        int zero_pad = 0;
        int width = 0;
        char spec;

        if (*fmt != '%') {
            openos_format_emit_char(buf, size, &pos, &total, fd, *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == 0)
            break;

        if (*fmt == '-') {
            left_align = 1;
            fmt++;
        }
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        spec = *fmt;
        if (spec == 0)
            break;

        if (spec == '%') {
            openos_format_emit_char(buf, size, &pos, &total, fd, '%');
        } else if (spec == 's') {
            const char *value = __builtin_va_arg(ap, const char *);
            openos_format_emit_padded(buf, size, &pos, &total, fd, value, width, left_align, ' ');
        } else if (spec == 'c') {
            char cbuf[2];
            cbuf[0] = (char)__builtin_va_arg(ap, int);
            cbuf[1] = 0;
            openos_format_emit_padded(buf, size, &pos, &total, fd, cbuf, width, left_align, ' ');
        } else if (spec == 'd' || spec == 'i') {
            char nbuf[16];
            openos_itoa(__builtin_va_arg(ap, int), nbuf, 10);
            openos_format_emit_padded(buf, size, &pos, &total, fd, nbuf, width, left_align, zero_pad ? '0' : ' ');
        } else if (spec == 'u') {
            char nbuf[16];
            openos_utoa_base(__builtin_va_arg(ap, unsigned int), nbuf, 10);
            openos_format_emit_padded(buf, size, &pos, &total, fd, nbuf, width, left_align, zero_pad ? '0' : ' ');
        } else if (spec == 'x') {
            char nbuf[16];
            openos_utoa_base(__builtin_va_arg(ap, unsigned int), nbuf, 16);
            openos_format_emit_padded(buf, size, &pos, &total, fd, nbuf, width, left_align, zero_pad ? '0' : ' ');
        } else {
            openos_format_emit_char(buf, size, &pos, &total, fd, '%');
            if (left_align)
                openos_format_emit_char(buf, size, &pos, &total, fd, '-');
            if (zero_pad)
                openos_format_emit_char(buf, size, &pos, &total, fd, '0');
            if (width > 0) {
                char wbuf[16];
                openos_itoa(width, wbuf, 10);
                openos_format_emit_str(buf, size, &pos, &total, fd, wbuf);
            }
            openos_format_emit_char(buf, size, &pos, &total, fd, spec);
        }
        fmt++;
    }

    if (buf && size > 0)
        buf[pos] = 0;
    openos_clear_errno();
    return total;
}

static inline int openos_vsnprintf(char *buf, int size, const char *fmt, __builtin_va_list ap)
{
    if (!buf || size <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return openos_vformat(buf, size, -1, fmt, ap);
}

static inline int openos_snprintf(char *buf, int size, const char *fmt, ...)
{
    __builtin_va_list ap;
    int ret;

    __builtin_va_start(ap, fmt);
    ret = openos_vsnprintf(buf, size, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

static inline int openos_vdprintf(int fd, const char *fmt, __builtin_va_list ap)
{
    if (fd < 0) {
        openos_set_errno(OPENOS_EBADF);
        return -1;
    }
    return openos_vformat(0, 0, fd, fmt, ap);
}

static inline int openos_dprintf(int fd, const char *fmt, ...)
{
    __builtin_va_list ap;
    int ret;

    __builtin_va_start(ap, fmt);
    ret = openos_vdprintf(fd, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

static inline int openos_printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    int ret;

    __builtin_va_start(ap, fmt);
    ret = openos_vdprintf(STDOUT_FILENO, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}


static inline int openos_getpid(void)
{
    return openos_syscall_result(openos_syscall0(SYS_GETPID));
}

static inline int openos_gettid(void)
{
    return openos_syscall_result(openos_syscall0(SYS_GETTID));
}

static inline int openos_getppid(void)
{
    return openos_syscall_result(openos_syscall0(SYS_GETPPID));
}

static inline int openos_yield(void)
{
    return openos_syscall_result(openos_syscall0(SYS_YIELD));
}

static inline int openos_sleep(int ticks)
{
    return openos_syscall_result(openos_syscall1(SYS_SLEEP, ticks));
}

static inline int openos_fork(void)
{
    return openos_syscall_result(openos_syscall0(SYS_FORK));
}

static inline int openos_wait(int *status)
{
    return openos_syscall_result(openos_syscall1(SYS_WAIT, (int)status));
}

static inline int openos_getcwd(char *buf, int size)
{
    return openos_syscall_result(openos_syscall2(SYS_GETCWD, (int)buf, size));
}

static inline int openos_chdir(const char *path)
{
    return openos_syscall_result(openos_syscall1(SYS_CHDIR, (int)path));
}

static inline int openos_mkdir(const char *path, int mode)
{
    return openos_syscall_result(openos_syscall2(SYS_MKDIR, (int)path, mode));
}

static inline int openos_unlink(const char *path)
{
    return openos_syscall_result(openos_syscall1(SYS_UNLINK, (int)path));
}

static inline int openos_link(const char *oldpath, const char *newpath)
{
    return openos_syscall_result(openos_syscall2(SYS_LINK, (int)oldpath, (int)newpath));
}

static inline int openos_symlink(const char *target, const char *linkpath)
{
    return openos_syscall_result(openos_syscall2(SYS_SYMLINK, (int)target, (int)linkpath));
}

static inline int openos_readlink(const char *path, char *buf, int size)
{
    return openos_syscall_result(openos_syscall3(SYS_READLINK, (int)path, (int)buf, size));
}

static inline void *openos_mmap(void *addr, int len, int flags)
{
    int result = openos_syscall3(SYS_MMAP, (int)addr, len, flags);
    if (result == -1)
        openos_errno = OPENOS_EINVAL;
    return (void *)result;
}

static inline int openos_munmap(void *addr, int len)
{
    return openos_syscall_result(openos_syscall2(SYS_MUNMAP, (int)addr, len));
}

static inline int openos_brk(void *addr)
{
    return openos_syscall_result(openos_syscall1(SYS_BRK, (int)addr));
}

static inline void *openos_sbrk(int increment)
{
    int result = openos_syscall1(SYS_SBRK, increment);
    if (result == -1)
        openos_errno = OPENOS_EINVAL;
    return (void *)result;
}

static inline int openos_rmdir(const char *path)
{
    return openos_syscall_result(openos_syscall1(SYS_RMDIR, (int)path));
}

static inline int openos_spawn(const char *path, char *const argv[])
{
    return openos_syscall_result(openos_syscall2(SYS_SPAWN, (int)path, (int)argv));
}

static inline int openos_spawn_env(const char *path, char *const argv[], char *const envp[])
{
    return openos_syscall_result(openos_syscall3(SYS_SPAWN_ENV, (int)path, (int)argv, (int)envp));
}

static inline int openos_waitpid(int pid, int *status, int options)
{
    return openos_syscall_result(openos_syscall3(SYS_WAITPID, pid, (int)status, options));
}

static inline int openos_kill(int pid, int sig)
{
    return openos_syscall_result(openos_syscall2(SYS_KILL, pid, sig));
}

static inline int openos_alarm(unsigned int seconds)
{
    return openos_syscall_result(openos_syscall1(SYS_ALARM, (int)seconds));
}

static inline int openos_exec(const char *path, char *const argv[])
{
    return openos_syscall_result(openos_syscall2(SYS_EXEC, (int)path, (int)argv));
}

static inline int openos_exec_env(const char *path, char *const argv[], char *const envp[])
{
    return openos_syscall_result(openos_syscall3(SYS_EXEC_ENV, (int)path, (int)argv, (int)envp));
}

#define OPENOS_HEAP_PAGE_SIZE 4096
#define OPENOS_HEAP_ALIGN     8
#define OPENOS_HEAP_MAGIC     0x0f0e0d0cU
#define OPENOS_HEAP_FREE      1U

typedef struct openos_heap_block {
    unsigned int magic;
    unsigned int size;
    unsigned int free;
    struct openos_heap_block *next;
} openos_heap_block_t;

static openos_heap_block_t *openos_heap_head = 0;

static inline void *openos_heap_alloc_page(void)
{
    void *ret = openos_sbrk(OPENOS_HEAP_PAGE_SIZE);
    if ((int)ret == -1)
        return 0;
    openos_clear_errno();
    return ret;
}

static inline int openos_heap_free_page(void *ptr)
{
    (void)ptr;
    return 0;
}

static inline int openos_heap_align_size(int size)
{
    if (size <= 0)
        return 0;
    return (size + OPENOS_HEAP_ALIGN - 1) & ~(OPENOS_HEAP_ALIGN - 1);
}

static inline void openos_heap_split_block(openos_heap_block_t *block, int size)
{
    openos_heap_block_t *next;
    int remain;

    if (!block)
        return;

    remain = (int)block->size - size - (int)sizeof(openos_heap_block_t);
    if (remain < (int)(OPENOS_HEAP_ALIGN + sizeof(openos_heap_block_t)))
        return;

    next = (openos_heap_block_t *)((char *)(block + 1) + size);
    next->magic = OPENOS_HEAP_MAGIC;
    next->size = (unsigned int)remain;
    next->free = OPENOS_HEAP_FREE;
    next->next = block->next;

    block->size = (unsigned int)size;
    block->next = next;
}

static inline void openos_heap_coalesce(void)
{
    openos_heap_block_t *cur = openos_heap_head;

    while (cur && cur->next) {
        char *cur_end = (char *)(cur + 1) + cur->size;
        if (cur->free && cur->next->free && cur_end == (char *)cur->next) {
            cur->size += (unsigned int)sizeof(openos_heap_block_t) + cur->next->size;
            cur->next = cur->next->next;
            continue;
        }
        cur = cur->next;
    }
}

static inline openos_heap_block_t *openos_heap_add_page(int min_size)
{
    char *page;
    int payload;
    int pages = 1;
    openos_heap_block_t *block;
    openos_heap_block_t *tail;

    while (pages * OPENOS_HEAP_PAGE_SIZE < min_size + (int)sizeof(openos_heap_block_t))
        pages++;

    if (pages != 1)
        return 0;

    page = (char *)openos_heap_alloc_page();
    if (!page)
        return 0;

    payload = OPENOS_HEAP_PAGE_SIZE - (int)sizeof(openos_heap_block_t);
    block = (openos_heap_block_t *)page;
    block->magic = OPENOS_HEAP_MAGIC;
    block->size = (unsigned int)payload;
    block->free = OPENOS_HEAP_FREE;
    block->next = 0;

    if (!openos_heap_head) {
        openos_heap_head = block;
    } else {
        tail = openos_heap_head;
        while (tail->next)
            tail = tail->next;
        tail->next = block;
    }
    return block;
}

static inline void *openos_malloc(int size)
{
    openos_heap_block_t *cur;
    int aligned = openos_heap_align_size(size);

    if (aligned <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return 0;
    }

    cur = openos_heap_head;
    while (cur) {
        if (cur->magic == OPENOS_HEAP_MAGIC && cur->free && (int)cur->size >= aligned) {
            openos_heap_split_block(cur, aligned);
            cur->free = 0;
            openos_clear_errno();
            return (void *)(cur + 1);
        }
        cur = cur->next;
    }

    cur = openos_heap_add_page(aligned);
    if (!cur) {
        openos_set_errno(OPENOS_ENOMEM);
        return 0;
    }
    openos_heap_split_block(cur, aligned);
    cur->free = 0;
    openos_clear_errno();
    return (void *)(cur + 1);
}

static inline int openos_free(void *ptr)
{
    openos_heap_block_t *block;

    if (!ptr) {
        openos_clear_errno();
        return 0;
    }

    block = ((openos_heap_block_t *)ptr) - 1;
    if (block->magic != OPENOS_HEAP_MAGIC) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    if (block->free) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }

    block->free = OPENOS_HEAP_FREE;
    openos_heap_coalesce();
    openos_clear_errno();
    return 0;
}

static inline char *openos_strdup(const char *s)
{
    char *copy;
    int len;
    if (!s) {
        openos_set_errno(OPENOS_EINVAL);
        return 0;
    }
    len = openos_strlen(s) + 1;
    copy = (char *)openos_malloc(len);
    if (!copy)
        return 0;
    openos_memcpy(copy, s, len);
    openos_clear_errno();
    return copy;
}

static inline void *openos_calloc(int count, int size)
{
    int total;
    void *ptr;

    if (count <= 0 || size <= 0) {
        openos_set_errno(OPENOS_EINVAL);
        return 0;
    }
    total = count * size;
    if (size != 0 && total / size != count) {
        openos_set_errno(OPENOS_ENOMEM);
        return 0;
    }
    ptr = openos_malloc(total);
    if (ptr)
        openos_memset(ptr, 0, total);
    return ptr;
}

static inline void *openos_realloc(void *ptr, int size)
{
    openos_heap_block_t *block;
    void *next;
    int copy;

    if (!ptr)
        return openos_malloc(size);
    if (size <= 0) {
        openos_free(ptr);
        return 0;
    }

    block = ((openos_heap_block_t *)ptr) - 1;
    if (block->magic != OPENOS_HEAP_MAGIC) {
        openos_set_errno(OPENOS_EINVAL);
        return 0;
    }
    if ((int)block->size >= size) {
        openos_clear_errno();
        return ptr;
    }

    next = openos_malloc(size);
    if (!next) {
        openos_set_errno(OPENOS_ENOMEM);
        return 0;
    }
    copy = (int)block->size;
    if (copy > size)
        copy = size;
    openos_memcpy(next, ptr, copy);
    openos_free(ptr);
    openos_clear_errno();
    return next;
}

static inline int openos_seek(int fd, int offset, int whence)
{
    return openos_syscall_result(openos_syscall3(SYS_SEEK, fd, offset, whence));
}



typedef struct openos_FILE {
    int fd;
    int flags;
    int error;
    int eof;
    int builtin;
} openos_FILE;

typedef openos_FILE FILE;

static openos_FILE openos_stdin_file = { STDIN_FILENO, O_RDONLY, 0, 0, 1 };
static openos_FILE openos_stdout_file = { STDOUT_FILENO, O_WRONLY, 0, 0, 1 };
static openos_FILE openos_stderr_file = { STDERR_FILENO, O_WRONLY, 0, 0, 1 };

#define stdin  (&openos_stdin_file)
#define stdout (&openos_stdout_file)
#define stderr (&openos_stderr_file)

static inline int openos_stdio_mode_flags(const char *mode, int *append)
{
    int flags;

    if (!mode || !mode[0]) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }

    *append = 0;
    if (mode[0] == 'r') {
        flags = O_RDONLY;
        if (openos_strchr(mode, '+'))
            flags = O_RDWR;
    } else if (mode[0] == 'w') {
        flags = O_CREAT | O_TRUNC | O_WRONLY;
        if (openos_strchr(mode, '+'))
            flags = O_CREAT | O_TRUNC | O_RDWR;
    } else if (mode[0] == 'a') {
        flags = O_CREAT | O_WRONLY;
        if (openos_strchr(mode, '+'))
            flags = O_CREAT | O_RDWR;
        *append = 1;
    } else {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return flags;
}

static inline openos_FILE *openos_fopen(const char *path, const char *mode)
{
    openos_FILE *file;
    int append = 0;
    int flags = openos_stdio_mode_flags(mode, &append);
    int fd;

    if (!path || flags < 0) {
        if (!path)
            openos_set_errno(OPENOS_EINVAL);
        return 0;
    }

    fd = openos_open(path, flags, 0644);
    if (fd < 0)
        return 0;
    if (append)
        openos_seek(fd, 0, SEEK_END);

    file = (openos_FILE *)openos_malloc(sizeof(openos_FILE));
    if (!file) {
        openos_close(fd);
        return 0;
    }
    file->fd = fd;
    file->flags = flags;
    file->error = 0;
    file->eof = 0;
    file->builtin = 0;
    openos_clear_errno();
    return file;
}

static inline int openos_fflush(openos_FILE *stream)
{
    if (!stream) {
        openos_set_errno(OPENOS_EINVAL);
        return EOF;
    }
    openos_clear_errno();
    return 0;
}

static inline int openos_fclose(openos_FILE *stream)
{
    int ret;

    if (!stream) {
        openos_set_errno(OPENOS_EINVAL);
        return EOF;
    }
    if (stream->builtin)
        return openos_fflush(stream);

    ret = openos_close(stream->fd);
    stream->fd = -1;
    openos_free(stream);
    return ret < 0 ? EOF : 0;
}

static inline int openos_fread(void *ptr, int size, int nmemb, openos_FILE *stream)
{
    int want;
    int got;

    if (!ptr || !stream || size < 0 || nmemb < 0) {
        openos_set_errno(OPENOS_EINVAL);
        if (stream)
            stream->error = 1;
        return 0;
    }
    if (size == 0 || nmemb == 0)
        return 0;
    want = size * nmemb;
    if (size != 0 && want / size != nmemb) {
        openos_set_errno(OPENOS_EINVAL);
        stream->error = 1;
        return 0;
    }
    got = openos_read(stream->fd, ptr, want);
    if (got < 0) {
        stream->error = 1;
        return 0;
    }
    if (got < want)
        stream->eof = 1;
    return got / size;
}

static inline int openos_fwrite(const void *ptr, int size, int nmemb, openos_FILE *stream)
{
    int want;
    int done;

    if (!ptr || !stream || size < 0 || nmemb < 0) {
        openos_set_errno(OPENOS_EINVAL);
        if (stream)
            stream->error = 1;
        return 0;
    }
    if (size == 0 || nmemb == 0)
        return 0;
    want = size * nmemb;
    if (size != 0 && want / size != nmemb) {
        openos_set_errno(OPENOS_EINVAL);
        stream->error = 1;
        return 0;
    }
    done = openos_write_fd(stream->fd, ptr, want);
    if (done < 0) {
        stream->error = 1;
        return 0;
    }
    return done / size;
}

static inline int openos_fgetc(openos_FILE *stream)
{
    unsigned char ch;
    int ret;

    if (!stream) {
        openos_set_errno(OPENOS_EINVAL);
        return EOF;
    }
    ret = openos_read(stream->fd, &ch, 1);
    if (ret == 1)
        return (int)ch;
    if (ret == 0)
        stream->eof = 1;
    else
        stream->error = 1;
    return EOF;
}

static inline int openos_fputc(int ch, openos_FILE *stream)
{
    unsigned char c = (unsigned char)ch;

    if (!stream) {
        openos_set_errno(OPENOS_EINVAL);
        return EOF;
    }
    if (openos_write_fd(stream->fd, &c, 1) != 1) {
        stream->error = 1;
        return EOF;
    }
    return ch;
}

static inline int openos_fputs(const char *s, openos_FILE *stream)
{
    int len;

    if (!s || !stream) {
        openos_set_errno(OPENOS_EINVAL);
        if (stream)
            stream->error = 1;
        return EOF;
    }
    len = openos_strlen(s);
    if (openos_write_fd(stream->fd, s, len) != len) {
        stream->error = 1;
        return EOF;
    }
    return 0;
}

static inline int openos_vfprintf(openos_FILE *stream, const char *fmt, __builtin_va_list ap)
{
    if (!stream) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    return openos_vdprintf(stream->fd, fmt, ap);
}

static inline int openos_fprintf(openos_FILE *stream, const char *fmt, ...)
{
    __builtin_va_list ap;
    int ret;

    __builtin_va_start(ap, fmt);
    ret = openos_vfprintf(stream, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

static inline int openos_feof(openos_FILE *stream)
{
    return stream ? stream->eof : 0;
}

static inline int openos_ferror(openos_FILE *stream)
{
    return stream ? stream->error : 1;
}

static inline void openos_clearerr(openos_FILE *stream)
{
    if (stream) {
        stream->error = 0;
        stream->eof = 0;
    }
}

#define fopen(path, mode)       openos_fopen((path), (mode))
#define fclose(stream)          openos_fclose((stream))
#define fread(ptr, size, nmemb, stream)  openos_fread((ptr), (size), (nmemb), (stream))
#define fwrite(ptr, size, nmemb, stream) openos_fwrite((ptr), (size), (nmemb), (stream))
#define fflush(stream)          openos_fflush((stream))
#define fgetc(stream)           openos_fgetc((stream))
#define fputc(ch, stream)       openos_fputc((ch), (stream))
#define fputs(s, stream)        openos_fputs((s), (stream))
#define fprintf(stream, fmt, ...) openos_fprintf((stream), (fmt), ##__VA_ARGS__)
#define printf(fmt, ...)        openos_printf((fmt), ##__VA_ARGS__)
#define snprintf(buf, size, fmt, ...) openos_snprintf((buf), (size), (fmt), ##__VA_ARGS__)
#define netinfo(info)          openos_netinfo((info))
#define ping(ip)               openos_ping((ip))
#define dnslookup(name, ip)    openos_dnslookup((name), (ip))
#define netconfig(ip, mask, gw, dns) openos_netconfig((ip), (mask), (gw), (dns))
#define netdevctl(name, op)    openos_netdevctl((name), (op))
#define firewall(op, index, rule) openos_firewall((op), (index), (rule))
#define socket(domain, type, protocol) openos_socket((domain), (type), (protocol))
#define socketpair(domain, type, protocol, sv) openos_socketpair((domain), (type), (protocol), (sv))
#define bind(fd, addr, len)    openos_bind((fd), (addr), (len))
#define listen(fd, backlog)    openos_listen((fd), (backlog))
#define accept(fd, addr, len)  openos_accept((fd), (addr), (len))
#define connect(fd, addr, len) openos_connect((fd), (addr), (len))
#define send(fd, buf, len, flags) openos_send((fd), (buf), (len), (flags))
#define recv(fd, buf, len, flags) openos_recv((fd), (buf), (len), (flags))
#define sendto(fd, buf, len, flags, addr, addrlen) openos_sendto((fd), (buf), (len), (flags), (addr), (addrlen))
#define recvfrom(fd, buf, len, flags, addr, addrlen) openos_recvfrom((fd), (buf), (len), (flags), (addr), (addrlen))
#define feof(stream)            openos_feof((stream))
#define ferror(stream)          openos_ferror((stream))
#define clearerr(stream)        openos_clearerr((stream))

typedef struct openos_DIR {
    char path[OPENOS_PATH_MAX];
    int index;
    int open;
    openos_dirent_t entry;
} openos_DIR;

static inline int openos_stat(const char *path, openos_stat_t *st)
{
    return openos_syscall_result(openos_syscall2(SYS_STAT, (int)path, (int)st));
}

static inline int openos_fstat(int fd, openos_stat_t *st)
{
    return openos_syscall_result(openos_syscall2(SYS_FSTAT, fd, (int)st));
}

static inline int openos_lstat(const char *path, openos_stat_t *st)
{
    return openos_syscall_result(openos_syscall2(SYS_LSTAT, (int)path, (int)st));
}

static inline int openos_chmod(const char *path, openos_uint32_t mode)
{
    return openos_syscall_result(openos_syscall2(SYS_CHMOD, (int)path, (int)mode));
}

static inline int openos_chown(const char *path, openos_uint32_t uid, openos_uint32_t gid)
{
    return openos_syscall_result(openos_syscall3(SYS_CHOWN, (int)path, (int)uid, (int)gid));
}

static inline int openos_getuid(void)
{
    return (int)openos_syscall0(SYS_GETUID);
}

static inline int openos_setuid(openos_uint32_t uid)
{
    return openos_syscall_result(openos_syscall1(SYS_SETUID, (int)uid));
}

static inline int openos_getgid(void)
{
    return (int)openos_syscall0(SYS_GETGID);
}

static inline int openos_setgid(openos_uint32_t gid)
{
    return openos_syscall_result(openos_syscall1(SYS_SETGID, (int)gid));
}

static inline int openos_getpwuid(openos_uint32_t uid, openos_user_t *user)
{
    return openos_syscall_result(openos_syscall2(SYS_GETPWUID, (int)uid, (int)user));
}

static inline int openos_getgrgid(openos_uint32_t gid, openos_group_t *group)
{
    return openos_syscall_result(openos_syscall2(SYS_GETGRGID, (int)gid, (int)group));
}

static inline openos_uint32_t openos_capget(void)
{
    return openos_syscall0(SYS_CAPGET);
}

static inline int openos_capset(openos_uint32_t caps)
{
    return openos_syscall_result(openos_syscall1(SYS_CAPSET, (int)caps));
}

static inline int openos_sandbox_get(void)
{
    return openos_syscall_result(openos_syscall0(SYS_SANDBOX_GET));
}

static inline int openos_sandbox_set(int enabled)
{
    return openos_syscall_result(openos_syscall1(SYS_SANDBOX_SET, enabled ? 1 : 0));
}

static inline int openos_poll(openos_pollfd_t *fds, openos_uint32_t nfds, openos_uint32_t timeout_ms)
{
    return openos_syscall_result(openos_syscall3(SYS_POLL, (int)fds, (int)nfds, (int)timeout_ms));
}

static inline int openos_select(openos_uint32_t nfds, openos_uint32_t *readfds, openos_uint32_t *writefds, openos_uint32_t *exceptfds, openos_uint32_t timeout_ms)
{
    return openos_syscall_result(openos_syscall5(SYS_SELECT, (int)nfds, (int)readfds, (int)writefds, (int)exceptfds, (int)timeout_ms));
}

static inline int openos_readdir_path(const char *path, int index, openos_dirent_t *entry)
{
    return openos_syscall_result(openos_syscall3(SYS_READDIR, (int)path, index, (int)entry));
}

static inline openos_DIR *openos_opendir(const char *path)
{
    static openos_DIR dir;
    openos_stat_t st;

    if (!path) {
        openos_set_errno(OPENOS_EINVAL);
        return 0;
    }
    if (openos_stat(path, &st) < 0)
        return 0;
    if ((st.mode & FS_DIR) != FS_DIR) {
        openos_set_errno(OPENOS_ENOTDIR);
        return 0;
    }
    if (openos_str_copy(dir.path, path, sizeof(dir.path)) < 0) {
        openos_set_errno(OPENOS_EINVAL);
        return 0;
    }

    dir.index = 0;
    dir.open = 1;
    openos_clear_errno();
    return &dir;
}

static inline openos_dirent_t *openos_readdir(openos_DIR *dir)
{
    int r;

    if (!dir || !dir->open) {
        openos_set_errno(OPENOS_EINVAL);
        return 0;
    }

    r = openos_readdir_path(dir->path, dir->index, &dir->entry);
    if (r <= 0)
        return 0;

    dir->index++;
    openos_clear_errno();
    return &dir->entry;
}

static inline int openos_closedir(openos_DIR *dir)
{
    if (!dir || !dir->open) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }
    dir->open = 0;
    openos_clear_errno();
    return 0;
}

static inline void openos_fail(int code, const char *msg)
{
    openos_write_str(msg);
    openos_exit(code);
}

static inline int openos_ai_request(const char *prompt, char *response, unsigned int response_len)
{
    openos_ai_request_t req;

    if (!prompt || !response || response_len == 0) {
        openos_set_errno(OPENOS_EINVAL);
        return -1;
    }

    req.prompt = prompt;
    req.response = response;
    req.response_len = response_len;
    req.flags = 0;
    return openos_syscall_result(openos_syscall1(SYS_AI_REQUEST, (int)&req));
}


#ifndef OPENOS_NO_LIBC_ALIASES
#define memset(dst, value, len)       openos_memset((dst), (value), (len))
#define memcpy(dst, src, len)         openos_memcpy((dst), (src), (len))
#define memmove(dst, src, len)        openos_memmove((dst), (src), (len))
#define memcmp(a, b, len)             openos_memcmp((a), (b), (len))
#define strlen(s)                     openos_strlen((s))
#define strcmp(a, b)                  openos_strcmp((a), (b))
#define strncmp(a, b, n)              openos_strncmp((a), (b), (n))
#define strcpy(dst, src)              openos_strcpy((dst), (src))
#define strncpy(dst, src, n)          openos_strncpy((dst), (src), (n))
#define strcat(dst, src)              openos_strcat((dst), (src))
#define strncat(dst, src, n)          openos_strncat((dst), (src), (n))
#define strchr(s, ch)                 openos_strchr((s), (ch))
#define strrchr(s, ch)                openos_strrchr((s), (ch))
#define strstr(h, n)                  openos_strstr((h), (n))
#define strdup(s)                     openos_strdup((s))
#define atoi(s)                       openos_atoi((s))
#define isdigit(ch)                   openos_isdigit((ch))
#define isspace(ch)                   openos_isspace((ch))
#define isalpha(ch)                   openos_isalpha((ch))
#define isalnum(ch)                   openos_isalnum((ch))
#define isxdigit(ch)                  openos_isxdigit((ch))
#define islower(ch)                   openos_islower((ch))
#define isupper(ch)                   openos_isupper((ch))
#define isprint(ch)                   openos_isprint((ch))
#define iscntrl(ch)                   openos_iscntrl((ch))
#define tolower(ch)                   openos_tolower((ch))
#define toupper(ch)                   openos_toupper((ch))
#define malloc(size)                  openos_malloc((size))
#define free(ptr)                     openos_free((ptr))
#define calloc(nmemb, size)           openos_calloc((nmemb), (size))
#define realloc(ptr, size)            openos_realloc((ptr), (size))
#define putchar(ch)                   openos_putchar((ch))
#define puts(s)                       openos_puts((s))
#endif

#endif /* OPENOS_USER_OPENOS_H */
