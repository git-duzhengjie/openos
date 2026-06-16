/* ============================================================
 * openos - 系统调用接口
 * ============================================================ */

#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>

/* 系统调用号 */
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

#define OPENOS_CAP_SETUID    (1u << 0)
#define OPENOS_CAP_SETGID    (1u << 1)
#define OPENOS_CAP_NET_ADMIN (1u << 2)
#define OPENOS_CAP_SYS_ADMIN (1u << 3)
#define OPENOS_CAP_KILL      (1u << 4)
#define OPENOS_CAP_ALL       0xffffffffu
#define OPENOS_CAP_BASIC     0u

#define OPENOS_POLLIN     0x0001
#define OPENOS_POLLOUT    0x0004
#define OPENOS_POLLERR    0x0008
#define OPENOS_POLLHUP    0x0010

typedef struct openos_pollfd {
    int fd;
    short events;
    short revents;
} openos_pollfd_t;

typedef struct openos_stat {
    uint32_t ino;
    uint32_t mode;
    uint32_t size;
    uint32_t nlinks;
    uint32_t fs_type;
    uint32_t uid;
    uint32_t gid;
} openos_stat_t;

typedef struct openos_dirent {
    uint32_t ino;
    uint32_t mode;
    uint32_t size;
    char name[32];
} openos_dirent_t;

typedef struct openos_netinfo {
    char name[16];
    uint8_t mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
    uint32_t arp_entries;
    uint32_t udp_bindings;
    uint32_t tcp_listeners;
    uint32_t tcp_connections;
    uint32_t icmp_echo_requests;
    uint32_t icmp_echo_replies;
} openos_netinfo_t;

typedef struct openos_user {
    uint32_t uid;
    uint32_t gid;
    char name[32];
    char home[64];
    char shell[64];
} openos_user_t;

typedef struct openos_group {
    uint32_t gid;
    char name[32];
} openos_group_t;

/* 调用号通过 EAX 传递，参数通过 EBX/ECX/EDX/ESI/EDI */
uint32_t syscall_handler(uint32_t syscall_num,
                         uint32_t arg1, uint32_t arg2, uint32_t arg3,
                         uint32_t arg4, uint32_t arg5);

/* 注册系统调用处理函数 */
typedef uint32_t (*syscall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void syscall_register(int num, syscall_fn_t fn);

/* 初始化系统调用 */
void syscall_init(void);

/* C 实现的 syscall_dispatch */
uint32_t syscall_dispatch(uint32_t num,
                          uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e);

/* 用户程序入口 */
void user_entry(void);

#endif /* KERNEL_SYSCALL_H */
