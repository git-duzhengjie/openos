#ifndef OPENOS_ARCH_X86_64_USER_OPENOS64_H
#define OPENOS_ARCH_X86_64_USER_OPENOS64_H

#include <stdint.h>

/* 系统调用号与内核 src/kernel/include/syscall.h 保持一致。
 * x86_64 与 i386 共享同一套 SYS_* ABI，仅入口指令不同（int 0x80 / syscall）。 */
#define OPENOS64_SYS_EXIT    1ULL
#define OPENOS64_SYS_GETPID  20ULL
#define OPENOS64_SYS_GETTID  21ULL
#define OPENOS64_SYS_READ    63ULL
#define OPENOS64_SYS_WRITE   64ULL
#define OPENOS64_SYS_MALLOC  73ULL
#define OPENOS64_SYS_FREE    74ULL
#define OPENOS64_SYS_YIELD   201ULL
#define OPENOS64_SYS_OPEN    225ULL
#define OPENOS64_SYS_CLOSE   226ULL
/* M5.4a: writable-VFS file operations (numbers mirror src/kernel/include/syscall.h) */
#define OPENOS64_SYS_SEEK    229ULL
#define OPENOS64_SYS_MKDIR   230ULL
#define OPENOS64_SYS_UNLINK  231ULL
#define OPENOS64_SYS_RMDIR   232ULL
#define OPENOS64_SYS_STAT    236ULL
#define OPENOS64_SYS_READDIR 239ULL
#define OPENOS64_SYS_GETPPID 224ULL
#define OPENOS64_SYS_EXEC    221ULL
#define OPENOS64_SYS_FORK    220ULL
#define OPENOS64_SYS_WAIT    222ULL
#define OPENOS64_SYS_WAITPID 223ULL

/* Step E.3: loopback datagram sockets. Numbers mirror the kernel
 * SYS_SOCKET/BIND/SENDTO/RECVFROM in src/kernel/include/syscall.h. */
#define OPENOS64_SYS_SOCKET   283ULL
#define OPENOS64_SYS_BIND     284ULL
#define OPENOS64_SYS_SENDTO   290ULL
#define OPENOS64_SYS_RECVFROM 291ULL
#define OPENOS64_SYS_NETINFO   292ULL
#define OPENOS64_SYS_PING      293ULL
#define OPENOS64_SYS_NETCONFIG 294ULL
#define OPENOS64_SYS_DNSLOOKUP 316ULL
#define OPENOS64_SYS_UPTIME_MS 317ULL
#define OPENOS64_SYS_SLEEP         200ULL
#define OPENOS64_SYS_CLOCK_GETTIME 332ULL
#define OPENOS64_SYS_NANOSLEEP     348ULL
/* M1.7 ring3 用户态 TCP */
#define OPENOS64_SYS_TCP_CONNECT 460ULL
#define OPENOS64_SYS_TCP_SEND    461ULL
#define OPENOS64_SYS_TCP_RECV    462ULL
#define OPENOS64_SYS_TCP_CLOSE   463ULL
#define OPENOS64_SYS_HTTP_GET    464ULL
#define OPENOS64_SYS_DL_RESOLVE  477ULL   /* M5.1d 惰性绑定：a0=link_map a1=reloc_index -> 目标地址 */

/* M5.2 threads: clone + futex.
 * CLONE:  a0=flags a1=child_stack a2=entry a3=arg a4=tls  -> new tid
 * FUTEX_WAIT:         a0=uaddr a1=expected            -> 0 / -EAGAIN
 * FUTEX_WAKE:         a0=uaddr a1=n                    -> #woken
 * FUTEX_WAIT_TIMEOUT: a0=uaddr a1=expected a2=timeout_ms -> 0/-EAGAIN/-ETIMEDOUT */
#define OPENOS64_SYS_CLONE              478ULL
#define OPENOS64_SYS_FUTEX_WAIT         269ULL
#define OPENOS64_SYS_FUTEX_WAKE         270ULL
#define OPENOS64_SYS_FUTEX_WAIT_TIMEOUT 339ULL

/* clone() flag subset accepted by the kernel (must include VM|THREAD). */
#define OPENOS64_CLONE_VM      0x00000100ULL
#define OPENOS64_CLONE_FS      0x00000200ULL
#define OPENOS64_CLONE_FILES   0x00000400ULL
#define OPENOS64_CLONE_SIGHAND 0x00000800ULL
#define OPENOS64_CLONE_THREAD  0x00010000ULL
#define OPENOS64_CLONE_SETTLS  0x00080000ULL

/* The kernel currently only accepts AF_OPENOS / SOCK_DGRAM / PROTO_DEFAULT. */
#define OPENOS64_AF_OPENOS     1
#define OPENOS64_SOCK_DGRAM    2
#define OPENOS64_PROTO_DEFAULT 0

#define OPENOS64_STDIN_FILENO  0
#define OPENOS64_STDOUT_FILENO 1
#define OPENOS64_STDERR_FILENO 2

typedef uint64_t openos64_size_t;
typedef int64_t openos64_ssize_t;

typedef struct openos64_runtime_info {
    uint64_t abi_version;
    uint64_t syscall_instruction;
    uint64_t crt0_entry;
} openos64_runtime_info_t;

static inline long openos64_syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall1(uint64_t num, uint64_t a0) {
    uint64_t ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall2(uint64_t num, uint64_t a0, uint64_t a1) {
    uint64_t ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0), "S"(a1)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall3(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0), "S"(a1), "d"(a2)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall4(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = a3;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0), "S"(a1), "d"(a2), "r"(r10)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall5(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = a3;
    register uint64_t r8 __asm__("r8") = a4;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline openos64_ssize_t openos64_write(int fd, const void *buf, openos64_size_t len) {
    return (openos64_ssize_t)openos64_syscall3(OPENOS64_SYS_WRITE, (uint64_t)fd, (uint64_t)(uintptr_t)buf, len);
}

static inline openos64_ssize_t openos64_read(int fd, void *buf, openos64_size_t len) {
    return (openos64_ssize_t)openos64_syscall3(OPENOS64_SYS_READ, (uint64_t)fd, (uint64_t)(uintptr_t)buf, len);
}

static inline int openos64_open(const char *path, int flags, int mode) {
    return (int)openos64_syscall3(OPENOS64_SYS_OPEN, (uint64_t)(uintptr_t)path, (uint64_t)flags, (uint64_t)mode);
}

static inline int openos64_close(int fd) {
    return (int)openos64_syscall1(OPENOS64_SYS_CLOSE, (uint64_t)fd);
}

/* -------- M5.4a: writable-VFS file operations -------- */

/* lseek whence values (mirror kernel do_lseek). */
#define OPENOS64_SEEK_SET 0
#define OPENOS64_SEEK_CUR 1
#define OPENOS64_SEEK_END 2

/* open flags (mirror kernel VFS: src/kernel/core/fs/vfs.h). NOTE: these are
 * openos-native values, NOT Linux values. O_CREAT=0x100, O_TRUNC=0x200. */
#ifndef OPENOS64_O_RDONLY
#define OPENOS64_O_RDONLY 0x000
#define OPENOS64_O_WRONLY 0x001
#define OPENOS64_O_RDWR   0x002
#define OPENOS64_O_CREAT  0x100
#define OPENOS64_O_TRUNC  0x200
#endif

static inline long openos64_lseek(int fd, long offset, int whence) {
    return openos64_syscall3(OPENOS64_SYS_SEEK, (uint64_t)fd,
                             (uint64_t)offset, (uint64_t)whence);
}

static inline int openos64_mkdir(const char *path, int mode) {
    return (int)openos64_syscall2(OPENOS64_SYS_MKDIR,
                                  (uint64_t)(uintptr_t)path, (uint64_t)mode);
}

static inline int openos64_unlink(const char *path) {
    return (int)openos64_syscall1(OPENOS64_SYS_UNLINK,
                                  (uint64_t)(uintptr_t)path);
}

static inline int openos64_rmdir(const char *path) {
    return (int)openos64_syscall1(OPENOS64_SYS_RMDIR,
                                  (uint64_t)(uintptr_t)path);
}

/* stat buffer must match kernel openos_stat_t layout (src/kernel/include/syscall.h). */
typedef struct {
    unsigned int       ino;
    unsigned int       mode;
    unsigned int       size;
    unsigned int       nlinks;
    unsigned int       fs_type;
    unsigned int       uid;
    unsigned int       gid;
    unsigned long long ctime_utc;
    unsigned long long mtime_utc;
    unsigned long long atime_utc;
} openos64_stat_t;

static inline int openos64_stat(const char *path, openos64_stat_t *st) {
    return (int)openos64_syscall2(OPENOS64_SYS_STAT,
                                  (uint64_t)(uintptr_t)path,
                                  (uint64_t)(uintptr_t)st);
}

/* dirent buffer must match kernel openos_dirent_t layout (src/kernel/include/syscall.h). */
typedef struct {
    unsigned int  ino;
    unsigned int  mode;
    unsigned int  size;
    char          name[32];
} openos64_dirent_t;

/*
 * Read directory entry #index of `path` into *out.
 * Returns 1 when an entry was written, 0 at end-of-directory, -1 on error.
 */
static inline int openos64_readdir(const char *path, int index, openos64_dirent_t *out) {
    return (int)openos64_syscall3(OPENOS64_SYS_READDIR,
                                  (uint64_t)(uintptr_t)path,
                                  (uint64_t)(unsigned)index,
                                  (uint64_t)(uintptr_t)out);
}

/*
 * Install an in-memory .opk image (M5.4c).
 * a0=image ptr, a1=image size, a2=install root (e.g. "/pkg").
 * Returns OPK_OK(0) or a negative opk error code.
 */
#define OPENOS64_SYS_OPK_INSTALL 479ULL
static inline long openos64_opk_install(const void *image, unsigned long size,
                                        const char *root) {
    return openos64_syscall3(OPENOS64_SYS_OPK_INSTALL,
                             (uint64_t)(uintptr_t)image,
                             (uint64_t)size,
                             (uint64_t)(uintptr_t)root);
}

static inline long openos64_getpid(void) {
    return openos64_syscall0(OPENOS64_SYS_GETPID);
}

/*
 * M6.1 — power management. a0 selects the operation.
 *   OPENOS64_POWER_SHUTDOWN : ACPI S5 soft-off (does not return on success)
 *   OPENOS64_POWER_REBOOT   : warm reboot       (does not return on success)
 *   OPENOS64_POWER_QUERY    : return capability bitmap without acting
 * On QUERY, returns bit0=ACPI \_S5 available, bit1=FADT reset available.
 */
#define OPENOS64_SYS_POWER    480ULL
#define OPENOS64_POWER_SHUTDOWN 0ULL
#define OPENOS64_POWER_REBOOT   1ULL
#define OPENOS64_POWER_QUERY    2ULL
static inline long openos64_power(unsigned long op) {
    return openos64_syscall1(OPENOS64_SYS_POWER, (uint64_t)op);
}

/*
 * M6.2 — CPU frequency / thermal observation (read-only). The kernel copies
 * a snapshot of CPUID/MSR-derived CPU power-management state into the
 * caller-provided buffer. Strictly read-only: querying never changes the
 * P-state. a0 = pointer to openos64_cpuinfo_t, a1 = sizeof that struct.
 * Returns 0 on success, -1 on error (bad pointer / cpufreq uninitialised).
 * The kernel fills at most a1 bytes, so older binaries with a smaller struct
 * remain forward-compatible.
 */
#define OPENOS64_SYS_CPUINFO  481ULL

/* Capability bits (mirror of kernel CPUFREQ_CAP_*). */
#define OPENOS64_CPU_CAP_TSC            (1u << 0)
#define OPENOS64_CPU_CAP_MSR            (1u << 1)
#define OPENOS64_CPU_CAP_INVARIANT_TSC  (1u << 2)
#define OPENOS64_CPU_CAP_THERM_DTS      (1u << 3)
#define OPENOS64_CPU_CAP_TURBO          (1u << 4)
#define OPENOS64_CPU_CAP_ARAT           (1u << 5)
#define OPENOS64_CPU_CAP_HWP            (1u << 6)
#define OPENOS64_CPU_CAP_PKG_THERM      (1u << 7)
#define OPENOS64_CPU_CAP_FREQ_LEAF      (1u << 8)
#define OPENOS64_CPU_CAP_PLATFORM_INFO  (1u << 9)
#define OPENOS64_CPU_CAP_PERF_STATUS    (1u << 10)

typedef struct {
    uint32_t caps;              /* OPENOS64_CPU_CAP_* bitmask       */
    char     vendor[16];        /* CPUID vendor string, NUL-term    */
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t base_mhz;          /* CPUID leaf 0x16 (0 if absent)    */
    uint32_t max_mhz;
    uint32_t bus_mhz;
    uint32_t tsc_mhz;           /* PIT-calibrated TSC MHz           */
    uint32_t cur_ratio;         /* current P-state ratio            */
    uint32_t cur_mhz;
    uint32_t max_nonturbo_ratio;
    uint32_t min_ratio;
    uint32_t tjmax_c;           /* junction max temp (Celsius)      */
    uint32_t core_temp_c;
    uint32_t pkg_temp_c;
    uint8_t  core_temp_valid;
    uint8_t  pkg_temp_valid;
    uint8_t  thermal_alert;
    uint8_t  reserved0;
} openos64_cpuinfo_t;

static inline long openos64_cpuinfo(openos64_cpuinfo_t *out) {
    return openos64_syscall2(OPENOS64_SYS_CPUINFO,
                             (uint64_t)(uintptr_t)out,
                             (uint64_t)sizeof(openos64_cpuinfo_t));
}

static inline long openos64_gettid(void) {
    return openos64_syscall0(OPENOS64_SYS_GETTID);
}

static inline long openos64_getppid(void) {
    return openos64_syscall0(OPENOS64_SYS_GETPPID);
}

static inline void openos64_exit(int code) __attribute__((noreturn));
static inline void openos64_exit(int code) {
    (void)openos64_syscall1(OPENOS64_SYS_EXIT, (uint64_t)(uint32_t)code);
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

static inline void openos64_yield(void) {
    (void)openos64_syscall0(OPENOS64_SYS_YIELD);
}

/* H.3 execve: replace the current process image with the program loaded
 * from `path` in the initrd. On success this call DOES NOT return (the
 * kernel longjmps back to its outer driver and re-enters ring3 on the new
 * image). On failure it returns -1 and the caller keeps running. argv and
 * envp are accepted but currently ignored by the kernel; pass NULL for
 * forward-compatibility. */
static inline long openos64_execve(const char *path, char *const argv[], char *const envp[]) {
    return openos64_syscall3(OPENOS64_SYS_EXEC,
                             (uint64_t)(uintptr_t)path,
                             (uint64_t)(uintptr_t)argv,
                             (uint64_t)(uintptr_t)envp);
}

/* A2.P3-B-beta: minimal fork wrapper. Returns child pid in parent, 0 in
 * child. -1 on error. Semantics are vfork-ish (child shares parent stack;
 * caller must not unwind before exec/exit). */
static inline long openos64_fork(void) {
    return openos64_syscall0(OPENOS64_SYS_FORK);
}

static inline long openos64_wait(int *status) {
    return openos64_syscall1(OPENOS64_SYS_WAIT, (uint64_t)(uintptr_t)status);
}

static inline long openos64_waitpid(long pid, int *status) {
    return openos64_syscall2(OPENOS64_SYS_WAITPID,
                             (uint64_t)pid,
                             (uint64_t)(uintptr_t)status);
}

/* ------------------ Step E.4 monotonic clock helper ------------------
 * Returns milliseconds since boot, calibrated against the i8254 PIT during
 * kernel init.  If calibration failed, the kernel falls back to a legacy
 * rdtsc>>20 estimate; callers should treat the return value as monotonic
 * non-decreasing only — not wall clock. */
static inline uint64_t openos64_uptime_ms(void) {
    return (uint64_t)openos64_syscall0(OPENOS64_SYS_UPTIME_MS);
}

/* ------------------ M4.1 time / clock helpers ------------------
 * openos_timespec mirrors the kernel openos_timespec_t layout
 * (int64 tv_sec + int64 tv_nsec). Only CLOCK_MONOTONIC is meaningful. */
#define OPENOS64_CLOCK_MONOTONIC 1
typedef struct openos64_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
} openos64_timespec_t;

/* Fill *ts with a monotonic timestamp (seconds since boot). Returns 0 on
 * success, -1 on a bad pointer. */
static inline int openos64_clock_gettime(int clk_id, openos64_timespec_t *ts) {
    return (int)openos64_syscall2(OPENOS64_SYS_CLOCK_GETTIME,
                                  (uint64_t)clk_id,
                                  (uint64_t)(uintptr_t)ts);
}

/* Sleep for whole seconds (cooperative busy-yield). Returns 0. */
static inline int openos64_sleep(unsigned int seconds) {
    return (int)openos64_syscall1(OPENOS64_SYS_SLEEP, (uint64_t)seconds);
}

/* Sleep for the duration in *req (ms granularity). rem may be NULL.
 * Returns 0 on success, -1 on a bad/negative request. */
static inline int openos64_nanosleep(const openos64_timespec_t *req,
                                     openos64_timespec_t *rem) {
    return (int)openos64_syscall2(OPENOS64_SYS_NANOSLEEP,
                                  (uint64_t)(uintptr_t)req,
                                  (uint64_t)(uintptr_t)rem);
}

/* ------------------ Step E.3 socket helpers ------------------ */
static inline int openos64_socket(int domain, int type, int protocol) {
    return (int)openos64_syscall3(OPENOS64_SYS_SOCKET,
                                  (uint64_t)domain,
                                  (uint64_t)type,
                                  (uint64_t)protocol);
}

static inline int openos64_bind(int fd, uint16_t port) {
    return (int)openos64_syscall2(OPENOS64_SYS_BIND,
                                  (uint64_t)fd,
                                  (uint64_t)port);
}

static inline openos64_ssize_t openos64_sendto(int fd,
                                               const void *buf,
                                               openos64_size_t len,
                                               uint16_t dst_port) {
    return (openos64_ssize_t)openos64_syscall5(OPENOS64_SYS_SENDTO,
                                               (uint64_t)fd,
                                               (uint64_t)(uintptr_t)buf,
                                               len,
                                               0,
                                               (uint64_t)dst_port);
}

static inline openos64_ssize_t openos64_recvfrom(int fd,
                                                 void *buf,
                                                 openos64_size_t len,
                                                 uint16_t *src_port_out) {
    return (openos64_ssize_t)openos64_syscall5(OPENOS64_SYS_RECVFROM,
                                               (uint64_t)fd,
                                               (uint64_t)(uintptr_t)buf,
                                               len,
                                               0,
                                               (uint64_t)(uintptr_t)src_port_out);
}

/* ------------------ M1.5.3 live TCP/IP stack helpers ------------------
 * These wrap the real virtio-net stack syscalls (netinfo/ping/dns/netconfig)
 * so userland tools (ifconfig/ping/nslookup) can talk to it directly.
 * The struct below must stay byte-identical with the kernel assembler in
 * src/arch/x86_64/kernel/syscall_dispatch64.c do_netinfo(). */
typedef struct openos64_netinfo {
    char     name[16];
    uint8_t  mac[6];
    uint32_t ip, netmask, gateway, dns, flags, config_mode;
    uint32_t rx_packets, tx_packets, rx_dropped, tx_dropped;
    uint32_t arp_entries, udp_bindings, tcp_listeners, tcp_connections;
    uint32_t icmp_echo_requests, icmp_echo_replies;
    uint32_t last_ipv4_src, last_ipv4_dst, last_ipv4_protocol;
    uint32_t last_icmp_src, last_icmp_type, last_icmp_code;
    uint32_t ipv4_drop_short, ipv4_drop_version, ipv4_drop_ihl;
    uint32_t ipv4_drop_len, ipv4_drop_checksum, ipv4_drop_dst;
    uint32_t last_ipv4_tx_src, last_ipv4_tx_dst, last_ipv4_tx_next_hop;
    uint32_t last_ipv4_tx_protocol, last_ipv4_tx_len;
    int32_t  last_ipv4_tx_result;
    uint32_t last_ping_dst, last_ping_id, last_ping_seq;
    int32_t  last_ping_send_result;
} openos64_netinfo_t;

/* config_mode values reported in openos64_netinfo_t.config_mode
 * (must match kernel net_config_mode_t) */
#define OPENOS64_NETCFG_NONE   0u
#define OPENOS64_NETCFG_STATIC 1u
#define OPENOS64_NETCFG_DHCP   2u

/* flags bits reported in openos64_netinfo_t.flags
 * (must match kernel NET_DEVICE_FLAG_*) */
#define OPENOS64_NETFLAG_PRESENT 0x01u
#define OPENOS64_NETFLAG_UP      0x02u
#define OPENOS64_NETFLAG_LINK_UP 0x04u
#define OPENOS64_NETFLAG_DHCP    0x08u

/* Fill *out with the primary interface state. Returns 0 on success, -1 on error. */
static inline int openos64_netinfo(openos64_netinfo_t *out) {
    return (int)openos64_syscall1(OPENOS64_SYS_NETINFO,
                                  (uint64_t)(uintptr_t)out);
}

/* Send one ICMP echo to dst_ip (network byte order). timeout_ms is advisory.
 * Returns 0 on reply received, -1 on timeout/error. */
static inline int openos64_ping(uint32_t dst_ip_be, uint32_t timeout_ms) {
    return (int)openos64_syscall2(OPENOS64_SYS_PING,
                                  (uint64_t)dst_ip_be,
                                  (uint64_t)timeout_ms);
}

/* Resolve hostname -> IPv4 (network byte order) into *out_ip.
 * Returns 0 on success, -1 on failure. */
static inline int openos64_dnslookup(const char *hostname, uint32_t *out_ip) {
    return (int)openos64_syscall2(OPENOS64_SYS_DNSLOOKUP,
                                  (uint64_t)(uintptr_t)hostname,
                                  (uint64_t)(uintptr_t)out_ip);
}

/* Reconfigure the interface. mode 0 = DHCP (async), 1 = static.
 * For static mode pass ip/netmask/gateway/dns in network byte order.
 * Returns 0 on success, -1 on error. */
static inline int openos64_netconfig(uint32_t mode, uint32_t ip, uint32_t netmask,
                                     uint32_t gateway, uint32_t dns) {
    return (int)openos64_syscall5(OPENOS64_SYS_NETCONFIG,
                                  (uint64_t)mode,
                                  (uint64_t)ip,
                                  (uint64_t)netmask,
                                  (uint64_t)gateway,
                                  (uint64_t)dns);
}

/* ------- M1.7 ring3 用户态 TCP（阻塞式，直通真 netstack）------- */
/* 建立 TCP 连接。dst_ip 为 host 字节序（与 dnslookup 输出的 network 序相反，需自行转换）。
 * 返回 conn_id>=0，<0 失败。 */
static inline int openos64_tcp_connect(uint32_t dst_ip_host, uint16_t dst_port) {
    return (int)openos64_syscall2(OPENOS64_SYS_TCP_CONNECT,
                                  (uint64_t)dst_ip_host,
                                  (uint64_t)dst_port);
}
static inline int openos64_tcp_send(int conn_id, const void *buf, uint32_t len) {
    return (int)openos64_syscall3(OPENOS64_SYS_TCP_SEND,
                                  (uint64_t)conn_id,
                                  (uint64_t)(uintptr_t)buf,
                                  (uint64_t)len);
}
static inline int openos64_tcp_recv(int conn_id, void *buf, uint32_t len, uint32_t poll_loops) {
    return (int)openos64_syscall4(OPENOS64_SYS_TCP_RECV,
                                  (uint64_t)conn_id,
                                  (uint64_t)(uintptr_t)buf,
                                  (uint64_t)len,
                                  (uint64_t)poll_loops);
}
static inline int openos64_tcp_close(int conn_id) {
    return (int)openos64_syscall2(OPENOS64_SYS_TCP_CLOSE, (uint64_t)conn_id, 0);
}
/* 辇便：一次性 HTTP GET 自测（内部 DNS+握手+GET+收，日志写串口）。返回 0/-1。 */
static inline int openos64_http_get(const char *host, const char *path, void *buf, uint32_t buflen) {
    return (int)openos64_syscall4(OPENOS64_SYS_HTTP_GET,
                                  (uint64_t)(uintptr_t)host,
                                  (uint64_t)(uintptr_t)path,
                                  (uint64_t)(uintptr_t)buf,
                                  (uint64_t)buflen);
}

/* ---- M5.2 low-level thread primitives ----
 * openos64_clone: raw clone. child begins at entry(arg) on child_stack with
 * %fs.base=tls; returns new tid to caller (never returns in child). */
static inline long openos64_clone(uint64_t flags, void *child_stack,
                                  void *entry, void *arg, void *tls) {
    return openos64_syscall5(OPENOS64_SYS_CLONE, flags,
                             (uint64_t)(uintptr_t)child_stack,
                             (uint64_t)(uintptr_t)entry,
                             (uint64_t)(uintptr_t)arg,
                             (uint64_t)(uintptr_t)tls);
}
static inline long openos64_futex_wait(volatile int *uaddr, int expected) {
    return openos64_syscall2(OPENOS64_SYS_FUTEX_WAIT,
                             (uint64_t)(uintptr_t)uaddr, (uint64_t)(uint32_t)expected);
}
static inline long openos64_futex_wake(volatile int *uaddr, int n) {
    return openos64_syscall2(OPENOS64_SYS_FUTEX_WAKE,
                             (uint64_t)(uintptr_t)uaddr, (uint64_t)(uint32_t)n);
}
static inline long openos64_futex_wait_timeout(volatile int *uaddr, int expected,
                                               uint64_t timeout_ms) {
    return openos64_syscall3(OPENOS64_SYS_FUTEX_WAIT_TIMEOUT,
                             (uint64_t)(uintptr_t)uaddr,
                             (uint64_t)(uint32_t)expected, timeout_ms);
}

openos64_size_t openos64_strlen(const char *text);
int openos64_main(int argc, char **argv, char **envp);/* H.5a: receives argc/argv/envp from crt0.S, never returns (calls openos64_exit). */
void openos64_start(int argc, char **argv, char **envp) __attribute__((noreturn));
const openos64_runtime_info_t *openos64_runtime_get_info(void);

#endif /* OPENOS_ARCH_X86_64_USER_OPENOS64_H */
