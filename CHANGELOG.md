# Changelog

本文件记录 OpenOS 的重要变更。

## [Unreleased]

### Added — ring3 网络工具全链路 + GUI 终端交互 (2026-07-06)

继内核网络栈（virtio-net + 以太网/ARP/IPv4/ICMP/UDP/DHCP/DNS/TCP + NAT 出口）打通后，
本轮完成 **ring3 用户态网络工具 wget，并把 ifconfig/ping/nslookup/wget 接入 GUI 终端
实现交互式使用，最终 GUI 实测三工具全部可用且不卡界面**。

- **M1.7 ring3 用户态 TCP**（`f41d8ca`）：4 个阻塞式 TCP 导出 + `SYS_TCP_*`(460-463)
  + `SYS_HTTP_GET`(464) + `wget64.c`。
- **M1.8 GUI 终端交互式**（`3501fbe`）：终端 run/exec 分支改为空格分词传 argv（最多 8 参）。
- **M1.9 HTTP 写回缓冲**（`7e41fdb`）：`net_http_get_buf` 把响应真写回用户缓冲（上限 1MiB）
  + wget `-1` one-shot 模式。
- **M2.0 GUI 终端网络工具内建别名**（`593078c`）：`gui_net_alias_match()` 识别
  `wget`/`ping`/`nslookup`/`ifconfig`，免 `run /bin/` 前缀。
- **M2.1/M2.2 卡死初步修复**（`b0f4f53`/`90ea58a`）：`http_pump` + `net_ping_ipv4_impl`
  + `net_dns_resolve` 的 busy-poll 循环加 yield；TCP/HTTP 系统调用号 400-404→460-464
  避开 GUI 控件撞号。
- **M2.3 网络工具异步化重构**（`d20f18e`）：**根治卡死**。根因是 GUI 终端走
  `launch_path` 同步阻塞跑 ring3 程序，for(;;) 死等进程退出堵死 GUI 主线程，
  此时无其他 kthread 可切，yield 空转救不了。方案 A：新增非阻塞 `net_ping_start/poll`
  + `gui_nettool` 状态机（RESOLVING/PING_WAIT/CONNECTING/SENDING/RECV/DONE），
  仿 `browser_load_tick` 挂进 GUI 主循环；网络别名不再走 launch_path，启动状态机
  后立即返回，界面持续响应。
- **M2.4 补真实非阻塞 DNS**：发现 `gui64_stubs.c` 里 `dns_query_a`/`dns_get_state`/
  `dns_get_last_result` 是空桩直返 -1，从未接网络栈（headless 走内核内直调
  `net_dns_resolve` 绕过桩，故一直假 PASS）。新增 `net_dns_query_start/state/result`
  非阻塞 DNS 状态机，三桩改为转发。
- **验收**：GUI 终端实测 `nslookup example.com`（出 IP）/ `ping 10.0.2.2` / `wget example.com`
  全部可用，敲命令后界面不卡、结果逐行刷出。

### Removed — i386 (32-bit) 分支归档 (2026-07-03)

经依赖差集分析确认 x86_64 主线不再引用后，物理删除了 **150 个 i386 专属源文件**，
使 **x86_64 成为唯一受支持的构建目标**。

- **删除范围（150 文件）**：
  - 32 位内核主体：`kernel.c` / `idt.c` / `gdt.c` / `i386_arch_ops.c`
  - BIOS 引导：`src/boot/boot.asm`、`entry.asm`、`isr.asm` 等全部 `.asm`
  - i386 驱动：`src/kernel/drivers/*`（ata/ahci/virtio/e1000/pci/keyboard/mouse…）
  - i386 子系统：`src/kernel/core/*`（mm/sched/fs/proc/ipc）、`net/*`、`ai/`、`smp.c`
  - i386 用户程序：`src/user/*.c`（sh/ls/cat/browser/stickynote…）
  - BIOS 平台层：`pc_bios_platform_ops.c`

- **保留的 11 个共享源码（x86_64 主线复用，未删除）**：
  `gui.c`、`gui_user.c`、`i18n.c`、`font.c`、`window_manager.c`、
  `generated/cjk_font.c`、`arch_ops.c`、`platform_ops.c`、`device.c`、
  `driver.c`、`basic_devices.c`
  > 这些文件是 GUI/字体子系统的母本，被 x86_64 段以 64 位目标重新编译。

- **build.sh 变更**：i386 构建段入口加入 deprecation 拦截，
  执行 `ARCH=i386 bash build.sh` 时立即报错退出并提示已归档，
  避免误触发一连串文件缺失错误。

- **备份**：删除的文件已打包存档至
  `legacy_backup/i386_files_20260703_220506.tar.gz`（约 331 KB）。
  恢复方式：`tar xzf legacy_backup/i386_files_*.tar.gz` 并回退 build.sh 拦截块。

- **验证**：
  - ✅ 删除后 `ARCH=x86_64 bash build.sh` 构建成功（EXIT=0，零误伤）
  - ✅ QEMU 运行时冒烟测试通过（串口出现 `poll loop reached`，桌面正常渲染）
  - ✅ `ARCH=i386 bash build.sh` 正确被拦截退出
