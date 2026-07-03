/* ============================================================
 * openos x86_64 GUI 移植 —— 外部依赖桩层 (gui64_stubs.c)
 *
 * i386 版 gui.c/gui_user.c 深度依赖 net/dns/dhcp/tls/vfs/mouse
 * 等子系统。范围 B 的移植目标是"先让桌面能显示、能动"，因此：
 *   - 网络 / DNS / DHCP / TLS（浏览器功能）→ 全部安全空桩
 *   - VFS（文件管理器）              → 暂时空桩（返回空/失败）
 *   - mouse / input / usb_tablet     → 桩 + 后续接 PS/2
 *   - proc / sched / spawn           → 转发到 x86_64 内核等价物
 *
 * 所有签名通过包含真实头文件自动对齐，避免手写不一致。
 * 后续逐子系统替换为真实实现即可（删掉对应桩）。
 * ============================================================ */

#include "types.h"
#include "core/fs/vfs.h"
#include "net/net.h"
#include "net/dns.h"
#include "net/dhcp.h"
#include "tls_parser.h"
#include "mouse.h"
#include "input.h"

/* ---- x86_64 内核堆（真实实现在 heap64） ---- */
extern void *arch_x86_64_kmalloc(size_t size);
extern void  arch_x86_64_kfree(void *ptr);

/* ============================================================
 * 1. 网络 / DNS / DHCP / TLS —— 全部空桩（浏览器功能已砍）
 * ============================================================ */
int  dhcp_start(void) { return -1; }

int      dns_query_a(const char *name) { (void)name; return -1; }
uint32_t dns_get_last_result(void) { return 0; }
dns_state_t dns_get_state(void) { return (dns_state_t)0; }

net_device_t *net_get_default_device(void) { return 0; }
int net_get_device_info(uint32_t index, net_device_info_t *out) { (void)index; (void)out; return -1; }
int net_get_device_info_by_name(const char *name, net_device_info_t *out) { (void)name; (void)out; return -1; }
uint32_t net_scan_wifi(net_wifi_network_info_t *out_list, uint32_t max_results) { (void)out_list; (void)max_results; return 0; }
int net_set_device_admin_up(const char *name, int up) { (void)name; (void)up; return -1; }
int net_refresh_device_status(const char *name) { (void)name; return -1; }
void net_poll(void) {}
int net_config_ipv4(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns) { (void)ip; (void)netmask; (void)gateway; (void)dns; return -1; }
int net_parse_ipv4(const char *text, uint32_t *out) { (void)text; if (out) *out = 0; return -1; }

int net_tcp_open(uint32_t local_ip, uint16_t local_port, uint32_t remote_ip, uint16_t remote_port, int active) { (void)local_ip; (void)local_port; (void)remote_ip; (void)remote_port; (void)active; return -1; }
int net_tcp_send(int conn_id, const uint8_t *data, uint16_t len) { (void)conn_id; (void)data; (void)len; return -1; }
int net_tcp_recv(int conn_id, uint8_t *data, uint16_t len) { (void)conn_id; (void)data; (void)len; return -1; }
int net_tcp_close(int conn_id) { (void)conn_id; return -1; }
int net_tcp_state(int conn_id) { (void)conn_id; return -1; }
int net_tcp_send_syn(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port) { (void)dst_ip; (void)src_port; (void)dst_port; return -1; }

int net_config_save_dhcp(void) { return -1; }
int net_config_save_static(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns) { (void)ip; (void)netmask; (void)gateway; (void)dns; return -1; }

int tls_parse_records(const uint8_t* data, size_t len, tls_parser_summary_t* summary) { (void)data; (void)len; (void)summary; return -1; }
const char* tls_handshake_type_name(uint8_t type) { (void)type; return ""; }
const char* tls_record_type_name(uint8_t type) { (void)type; return ""; }

/* ============================================================
 * 2. VFS（文件管理器）—— 暂时空桩（后续接 vfs64）
 * ============================================================ */
int vfs_open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
int vfs_close(int fd) { (void)fd; return -1; }
int vfs_read(int fd, void *buf, uint32_t count) { (void)fd; (void)buf; (void)count; return -1; }
int vfs_write(int fd, const void *buf, uint32_t count) { (void)fd; (void)buf; (void)count; return -1; }
int vfs_stat(const char *path, inode_t *st) { (void)path; (void)st; return -1; }
int vfs_mkdir(const char *path, int mode) { (void)path; (void)mode; return -1; }
int vfs_rmdir(const char *path) { (void)path; return -1; }
int vfs_unlink(const char *path) { (void)path; return -1; }
int vfs_rename(const char *oldpath, const char *newpath) { (void)oldpath; (void)newpath; return -1; }
dentry_t *vfs_readdir(const char *path, int index) { (void)path; (void)index; return 0; }

/* ============================================================
 * 3. 鼠标 / 输入 / USB tablet —— PS/2 鼠标驱动见 mouse64.c
 *    mouse_snapshot_and_clear_delta / mouse_set_bounds /
 *    mouse_set_position 现由 mouse64.c 提供真实实现。
 *    这里仅保留 GUI 仍会调用、但 x86_64 尚无对应硬件的桩。
 * ============================================================ */
void input_flush_events(void) {}
int  usb_tablet_poll(mouse_state_t *out) { (void)out; return 0; }
void input_flush(void) {}

/* ============================================================
 * 4. proc / sched / spawn —— 转发到 x86_64 内核等价物
 * ============================================================ */
extern uint64_t arch_x86_64_tsc_uptime_ms(void);
extern uint32_t arch_x86_64_proc_current_pid(void);
extern uint32_t arch_x86_64_proc_spawn_user(const char *name);

uint32_t sched_time_ms(void) { return (uint32_t)arch_x86_64_tsc_uptime_ms(); }
uint32_t proc_current_pid(void) { return arch_x86_64_proc_current_pid(); }

int spawn_user_process(const char *path, char *const argv[]) {
    (void)argv;
    return (int)arch_x86_64_proc_spawn_user(path);
}

/* GUI 内启动 shell 线程：x86_64 主线已有独立 shell 流程，此处暂空 */
void kernel_start_shell_thread(void) {}
