/*
 * syscall_net64.c — x86_64 network syscall backends.
 *
 * Split out of syscall_dispatch64.c (Step E.4) so the core dispatcher stays
 * focused on process/file/memory/time syscalls. These thin wrappers translate
 * the user-mode uint64_t arg vector into the net64 / kernel TCP-IP stack APIs.
 *
 * Shared helpers (validate_user_buf / k_strlen) are exported by the dispatcher
 * as arch_x86_64_validate_user_buf / arch_x86_64_k_strlen; see syscall_net64.h.
 */
#include <stddef.h>
#include <stdint.h>

#include "../include/net64.h"
#include "../include/syscall_net64.h"
#include "../../../kernel/net/net.h" /* real TCP/IP stack: ping/dns/netinfo/dev-ctl */
#include "../include/arch64_types.h" /* x86_64_size_t */

/* -------------------------------------------------------------------------
 * Network syscall backends (moved verbatim from syscall_dispatch64.c).
 * ------------------------------------------------------------------------- */
uint64_t arch_x86_64_sys_socket(uint64_t domain, uint64_t type, uint64_t protocol) {
    int fd = arch_x86_64_net_socket((int)domain, (int)type, (int)protocol);
    if (fd < 0) return (uint64_t)-1;
    return (uint64_t)fd;
}

uint64_t arch_x86_64_sys_bind(uint64_t fd, uint64_t port) {
    int r = arch_x86_64_net_bind((int)fd, (uint16_t)port);
    return (r < 0) ? (uint64_t)-1 : 0;
}

uint64_t arch_x86_64_sys_sendto(uint64_t fd,
                          uint64_t buf_ptr,
                          uint64_t len,
                          uint64_t flags,
                          uint64_t dst_port) {
    (void)flags;
    if (!arch_x86_64_validate_user_buf(buf_ptr, len)) return (uint64_t)-1;
    int n = arch_x86_64_net_sendto((int)fd,
                                   (const void *)(uintptr_t)buf_ptr,
                                   (x86_64_size_t)len,
                                   (uint16_t)dst_port);
    if (n < 0) return (uint64_t)-1;
    return (uint64_t)n;
}

uint64_t arch_x86_64_sys_recvfrom(uint64_t fd,
                            uint64_t buf_ptr,
                            uint64_t len,
                            uint64_t flags,
                            uint64_t src_port_out_ptr) {
    (void)flags;
    if (!arch_x86_64_validate_user_buf(buf_ptr, len)) return (uint64_t)-1;
    /*
     * src_port_out is an optional uint16_t* in user memory. We accept NULL
     * by passing through to net64_recvfrom which already handles it.
     */
    int n = arch_x86_64_net_recvfrom((int)fd,
                                     (void *)(uintptr_t)buf_ptr,
                                     (x86_64_size_t)len,
                                     (uint16_t *)(uintptr_t)src_port_out_ptr);
    if (n < 0) return (uint64_t)-1;
    return (uint64_t)n;
}

/* ---------------------------------------------------------------------------
 * M1.5.3 real-stack syscalls: expose netstack.c capabilities to ring3.
 * These back the userland /bin/ifconfig, /bin/ping, /bin/nslookup tools
 * (declared inline in src/user/openos.h). Unlike the loopback socket
 * helpers above, they talk to the live virtio-net TCP/IP stack.
 * ------------------------------------------------------------------------- */

/*
 * SYS_NETINFO (292): fill a user openos_netinfo_t.
 *
 * The userland struct (src/user/openos.h) equals net_diag_stats_t's
 * name/mac/ip/netmask/gateway header, PLUS three fields (dns/flags/config_mode)
 * that only net_device_info_t carries, PLUS net_diag_stats_t's entire counter
 * block (rx_packets .. last_ping_send_result) which is byte-identical. So we
 * pull counters+addr from net_get_diag_stats() and dns/flags/mode from
 * net_get_device_info(0).
 */
uint64_t arch_x86_64_sys_netinfo(uint64_t out_ptr) {
    /* full struct size is validated below once the layout struct is known */
    if (!arch_x86_64_validate_user_buf(out_ptr, 4)) return (uint64_t)-1;

    net_diag_stats_t st;
    if (net_get_diag_stats(&st) != 0) return (uint64_t)-1;

    net_device_info_t info;
    uint32_t ip = st.ip, netmask = st.netmask, gateway = st.gateway;
    uint32_t dns = 0, flags = 0, config_mode = 0;
    if (net_get_device_info(0, &info) == 0) {
        /* device_info is the authoritative source for L3 config; diag_stats
         * does not track ip/netmask/gateway, so prefer info here. */
        ip = info.ip; netmask = info.netmask; gateway = info.gateway;
        dns = info.dns; flags = info.flags; config_mode = info.config_mode;
    }

    /* Mirror the exact userland struct so offsets line up 1:1. */
    struct u_netinfo {
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
    } *u = (struct u_netinfo *)(uintptr_t)out_ptr;

    /* now validate the full struct span before writing into it */
    if (!arch_x86_64_validate_user_buf(out_ptr, sizeof(struct u_netinfo))) return (uint64_t)-1;

    for (int i = 0; i < 16; ++i) u->name[i] = st.name[i];
    for (int i = 0; i < 6; ++i)  u->mac[i]  = st.mac[i];
    u->ip = ip; u->netmask = netmask; u->gateway = gateway;
    u->dns = dns; u->flags = flags; u->config_mode = config_mode;
    u->rx_packets = st.rx_packets; u->tx_packets = st.tx_packets;
    u->rx_dropped = st.rx_dropped; u->tx_dropped = st.tx_dropped;
    u->arp_entries = st.arp_entries; u->udp_bindings = st.udp_bindings;
    u->tcp_listeners = st.tcp_listeners; u->tcp_connections = st.tcp_connections;
    u->icmp_echo_requests = st.icmp_echo_requests;
    u->icmp_echo_replies = st.icmp_echo_replies;
    u->last_ipv4_src = st.last_ipv4_src; u->last_ipv4_dst = st.last_ipv4_dst;
    u->last_ipv4_protocol = st.last_ipv4_protocol;
    u->last_icmp_src = st.last_icmp_src; u->last_icmp_type = st.last_icmp_type;
    u->last_icmp_code = st.last_icmp_code;
    u->ipv4_drop_short = st.ipv4_drop_short;
    u->ipv4_drop_version = st.ipv4_drop_version;
    u->ipv4_drop_ihl = st.ipv4_drop_ihl; u->ipv4_drop_len = st.ipv4_drop_len;
    u->ipv4_drop_checksum = st.ipv4_drop_checksum;
    u->ipv4_drop_dst = st.ipv4_drop_dst;
    u->last_ipv4_tx_src = st.last_ipv4_tx_src;
    u->last_ipv4_tx_dst = st.last_ipv4_tx_dst;
    u->last_ipv4_tx_next_hop = st.last_ipv4_tx_next_hop;
    u->last_ipv4_tx_protocol = st.last_ipv4_tx_protocol;
    u->last_ipv4_tx_len = st.last_ipv4_tx_len;
    u->last_ipv4_tx_result = st.last_ipv4_tx_result;
    u->last_ping_dst = st.last_ping_dst; u->last_ping_id = st.last_ping_id;
    u->last_ping_seq = st.last_ping_seq;
    u->last_ping_send_result = st.last_ping_send_result;
    return 0;
}

/*
 * SYS_PING (293): a0 = destination IPv4 (network byte order), a1 = timeout ms
 * (currently advisory; the kernel ping uses a fixed poll budget).
 * The kernel net_ping_ipv4() returns 0 on reply / -1 on timeout and does not
 * measure RTT, so we return 0 on success and -1 on failure.
 */
uint64_t arch_x86_64_sys_ping(uint64_t ip_be, uint64_t timeout_ms) {
    (void)timeout_ms;
    int rc = net_ping_ipv4((uint32_t)ip_be);
    return (rc == 0) ? 0 : (uint64_t)-1;
}

/*
 * SYS_DNSLOOKUP (316): a0 = user hostname string, a1 = out uint32_t* (IPv4,
 * network byte order). Returns 0 on success, -1 on failure.
 */
uint64_t arch_x86_64_sys_dnslookup(uint64_t name_ptr, uint64_t out_ip_ptr) {
    if (!arch_x86_64_validate_user_buf(name_ptr, 1)) return (uint64_t)-1;
    if (!arch_x86_64_validate_user_buf(out_ip_ptr, 4)) return (uint64_t)-1;
    const char *host = (const char *)(uintptr_t)name_ptr;
    if (arch_x86_64_k_strlen(host) == 0 || arch_x86_64_k_strlen(host) > 253) return (uint64_t)-1;
    uint32_t ip = 0;
    if (net_dns_resolve(host, &ip) != 0) return (uint64_t)-1;
    *(uint32_t *)(uintptr_t)out_ip_ptr = ip;
    return 0;
}

/*
 * SYS_NETCONFIG (294): reconfigure the interface.
 *   a0 = mode: 0 = DHCP (async), 1 = static
 *   a1 = ip, a2 = netmask, a3 = gateway, a4 = dns   (static mode, network order)
 * Returns 0 on success, -1 on error.
 */
uint64_t arch_x86_64_sys_netconfig(uint64_t mode, uint64_t ip, uint64_t netmask,
                             uint64_t gateway, uint64_t dns) {
    if (mode == 0) {
        /* DHCP: kick off async discover; result lands via net_poll. */
        return (net_dhcp_start() == 0) ? 0 : (uint64_t)-1;
    }
    int rc = net_config_ipv4((uint32_t)ip, (uint32_t)netmask,
                             (uint32_t)gateway, (uint32_t)dns);
    return (rc == 0) ? 0 : (uint64_t)-1;
}

/* ---------------------------------------------------------------------------
 * M1.7 ring3 用户态 TCP（直通真 netstack 的阻塞式封装）
 * ------------------------------------------------------------------------- */

/* SYS_TCP_CONNECT (460): a0 = dst_ip (host 序), a1 = dst_port. 返回 conn_id>=0 / (uint64_t)-1 */
uint64_t arch_x86_64_sys_tcp_connect(uint64_t dst_ip, uint64_t dst_port) {
    if (dst_ip == 0 || dst_port == 0 || dst_port > 0xFFFF) return (uint64_t)-1;
    int conn = net_tcp_connect_blocking((uint32_t)dst_ip, (uint16_t)dst_port);
    if (conn < 0) return (uint64_t)-1;
    return (uint64_t)conn;
}

/* SYS_TCP_SEND (461): a0 = conn_id, a1 = user buf, a2 = len. 返回已发字节数 / -1 */
uint64_t arch_x86_64_sys_tcp_send(uint64_t conn_id, uint64_t buf, uint64_t len) {
    if (len == 0 || len > 8192) return (uint64_t)-1;
    if (!arch_x86_64_validate_user_buf(buf, len)) return (uint64_t)-1;
    int r = net_tcp_send_blocking((int)conn_id, (const uint8_t *)(uintptr_t)buf, (uint16_t)len);
    if (r < 0) return (uint64_t)-1;
    return (uint64_t)r;
}

/* SYS_TCP_RECV (462): a0 = conn_id, a1 = user buf, a2 = len, a3 = poll_loops. 返回收字节数/0/-1 */
uint64_t arch_x86_64_sys_tcp_recv(uint64_t conn_id, uint64_t buf, uint64_t len, uint64_t poll_loops) {
    if (len == 0 || len > 8192) return (uint64_t)-1;
    if (!arch_x86_64_validate_user_buf(buf, len)) return (uint64_t)-1;
    int r = net_tcp_recv_blocking((int)conn_id, (uint8_t *)(uintptr_t)buf, (uint16_t)len, (uint32_t)poll_loops);
    if (r < 0) return (uint64_t)-1;
    return (uint64_t)r;
}

/* SYS_TCP_CLOSE (463): a0 = conn_id */
uint64_t arch_x86_64_sys_tcp_close(uint64_t conn_id) {
    int r = net_tcp_close_blocking((int)conn_id);
    return (r == 0) ? 0 : (uint64_t)-1;
}

/* SYS_HTTP_GET (464): a0 = host str, a1 = path str, a2 = user buf, a3 = buflen.
 * M1.9: 完整 HTTP GET，将响应正文写回用户缓冲 buf[0..buflen)，
 * 返回实际写入字节数 (>=0)；参数非法/失败返回 (uint64_t)-1。
 * buf 可为 0（仅触发下载、返回响应总长）。 */
uint64_t arch_x86_64_sys_http_get(uint64_t host_ptr, uint64_t path_ptr, uint64_t buf, uint64_t buflen) {
    if (!arch_x86_64_validate_user_buf(host_ptr, 1)) return (uint64_t)-1;
    if (!arch_x86_64_validate_user_buf(path_ptr, 1)) return (uint64_t)-1;
    const char *host = (const char *)(uintptr_t)host_ptr;
    const char *path = (const char *)(uintptr_t)path_ptr;
    if (arch_x86_64_k_strlen(host) == 0 || arch_x86_64_k_strlen(host) > 253) return (uint64_t)-1;

    uint8_t *out = 0;
    int cap = 0;
    if (buf) {
        if (buflen == 0 || buflen > (1u << 20)) return (uint64_t)-1; /* 上限 1MiB */
        if (!arch_x86_64_validate_user_buf(buf, buflen)) return (uint64_t)-1;
        out = (uint8_t *)(uintptr_t)buf;
        cap = (int)buflen;
    }
    int r = net_http_get_buf(host, path, out, cap);
    return (r < 0) ? (uint64_t)-1 : (uint64_t)r;
}
