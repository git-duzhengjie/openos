# Changelog

本文件记录 OpenOS 的重要变更。

## [Unreleased]

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
