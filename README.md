# openos

> 注意：当前 `/bin/chromium` 是 OpenOS Chromium Demo，用于验证 GUI/HTTP/线程/SDK；它还没有链接官方 Chromium Content、Blink、V8 或 Skia，不是真实 Chrome 引擎。


> **开源 · 智能 · 跨端 · 优雅**

openos 是一个从零开始构建的开源操作系统，旨在打造新一代智能、跨平台的操作系统体验。

## 快速开始

```bash
# 克隆仓库
git clone https://github.com/git-duzhengjie/openos.git
cd openos

# 构建
./build.sh

# 运行（需要 QEMU）
qemu-system-i386 -m 512M -drive file=target/openos.img,format=raw -serial stdio -netdev tap,id=net0,ifname='OpenOS-TAP',script=no,downscript=no -device e1000,netdev=net0,mac=52:54:00:12:34:56 -display gtk
# 启动后在 OpenOS Shell 中执行：guitest
```

## 架构状态与构建门禁

当前默认构建目标是 **i386**，它是 OpenOS 目前最完整、最稳定的主线，覆盖启动、基础内核、用户态程序、shell、GUI、网络与浏览器 smoke 测试。后续 i386 的长期定位是 **legacy / regression / 调试目标**：用于保持现有功能不回退，并作为跨架构迁移过程中的稳定参照。

新的 PC 产品主线将逐步迁移到 **x86_64 + UEFI**；Mobile 基础主线将新增 **aarch64**，并优先从 QEMU virt 开始，而不是直接适配真实手机硬件。

当前真实架构状态：

| 架构 | 当前状态 | 后续定位 |
| --- | --- | --- |
| i386 | 当前最完整主线，可默认构建 `target/openos.img`，覆盖用户态程序、shell、GUI、网络与浏览器 smoke 测试 | legacy / regression / 调试目标 |
| x86_64 | 已有 GDT、TSS、IDT、异常入口、syscall/sysret、PMM、VMM、heap、ELF64 loader、UEFI `BOOTX64.EFI` 骨架和 `hello64.elf` 回归程序 | PC 产品主线，逐步补齐 initrd、VFS、init、shell |
| ARM32 | `src/arch/arm` 当前是 ARM 32 位移植骨架，包含 QEMU virt 常量、PL011 UART、`_start` 和异常向量占位；它不是 Mobile 所需的 ARM64 主线 | 保留为实验/参考骨架 |
| RISC-V RV64 | `src/arch/riscv` 当前是早期 RV64 骨架，包含 QEMU virt 常量、UART、`_start` 和 kernel main；trap、MMU、PLIC、CLINT、用户态仍待推进 | 长期多架构探索，不阻塞 PC/Mobile 主线 |
| aarch64 | 已建立 QEMU virt 最小主线，支持 PL011 串口、EL1 启动、异常向量、ELF64 loader、hello64 staged、initrd -> VFS -> /bin/init -> /bin/sh 冒烟 | Mobile 基础主线，继续补齐真实 EL0 运行、系统服务和移动平台能力 |

跨架构基础门禁命令：

```bash
# 单元测试与 smoke 测试
bash build.sh test

# 当前稳定 i386 镜像构建，输出 target/openos.img
bash build.sh

# x86_64 骨架构建，输出 kernel64.elf / BOOTX64.EFI 等产物
ARCH=x86_64 bash build.sh

# aarch64 QEMU virt 最小主线构建，输出 target/aarch64/openos-aarch64.elf / .bin
ARCH=aarch64 bash build.sh
```

## 路线文档

- [GUI ABI v1 与用户态 GUI 边界](docs/user-gui-abi.md)
- [Shell 用户态化路线](docs/shell-userspace-roadmap.md)
- [系统服务用户态化路线](docs/system-services-userspace-roadmap.md)

## 调试

```bash
# 构建调试符号和镜像
bash build.sh

# 终端 1：启动暂停在复位入口、开启 GDB stub
bash scripts/debug-qemu-gdb.sh

# 终端 2：加载符号并连接 QEMU
OPENOS_GDB_PORT=1234 gdb -q -x scripts/gdb-openos.gdb
(gdb) openos-connect
(gdb) openos-break-boot
(gdb) continue
```

常用 GDB 辅助命令：`openos-help`、`openos-regs`、`openos-panic-dump`。

## 发布打包

```bash
bash scripts/package-release.sh --version nightly
```

发布包输出到 `target/release/`，包含 `openos.img`、`kernel.elf`、`kernel.bin`、文档、调试脚本和 SHA256 校验文件。详见 `docs/release.md`。

## Browser 当前能力边界

OpenOS 当前浏览器主线已经切换为自研轻量浏览器，入口是 `/bin/browser`，源码位于 `src/user/browser.c`，引擎接口位于 `src/user/browser_engine.h`。

当前支持 HTTP/1.0 页面访问、DNS/TCP/HTTP 加载诊断、`file://`/绝对本地路径 HTML 加载、`<title>` 提取、基础 HTML 实体解码、常见块级标签换行、最小 tokenizer/parser/DOM/CSS display 分层，以及 GUI 中的 Refresh / Back / Forward / Up / Down 操作。

它暂不等同于 Chromium / WebKit / Gecko 级完整浏览器，不支持 JavaScript、复杂 CSS 布局、图片、媒体播放和现代网页兼容。Chromium 官方内核路线保留为长期备选，不再作为当前 P0 阻塞项。当前路线见 [`docs/openos-light-browser.md`](docs/openos-light-browser.md)。

## 中文字库资源

默认构建会生成并嵌入 GB2312 覆盖的压缩 CJK 字库资源，启动时以 `/fonts/cjk.ofnt` 路径安装到 ramfs 并由 GUI 加载；加载失败时会回退到内核内置小字库。

生成大覆盖字库优先使用 Python 生成器：需要安装 Pillow 并提供可用中文 TTF / OTF / TTC 字体；在 Windows + WSL 环境下，如果 WSL 缺少 Pillow，构建脚本会自动尝试调用 `scripts/generate_cjk_font.ps1`，使用 Windows GDI+ 和系统中文字体生成同格式资源。

常用配置：

```bash
# 默认：GB2312 覆盖，压缩并嵌入 target/cjk.ofnt
bash build.sh

# 指定字体
OPENOS_CJK_FONT=/path/to/chinese-font.ttf bash build.sh

# CJK Unified Ideographs 基本区覆盖
OPENOS_CJK_COVERAGE=cjk-basic OPENOS_CJK_FONT=/path/to/chinese-font.ttf bash build.sh

# 只使用内置 UI 子集，适合最小镜像或无字体生成环境
OPENOS_CJK_COVERAGE=ui bash build.sh

# CI 中要求大字库生成失败时直接失败，不允许静默降级
OPENOS_CJK_STRICT=1 bash build.sh
```

## 版本管理

基础版本记录在根目录 `VERSION`，完整版本可通过以下命令查询：

```bash
bash scripts/version.sh --full
```

构建时会自动生成 `src/kernel/include/version.h`，发布打包默认使用同一版本来源。详见 `docs/versioning.md`。

## 浏览器能力边界

当前 `/bin/browser` 走自研轻量内核路线，支持文件/HTTP 加载、HTML 文本化 DOM 渲染、基础 CSS、链接历史、表单提交雏形、图片占位、HTTP header 诊断、重定向和最近页面缓存。暂不支持 JavaScript、复杂 CSS、媒体、HTTPS 渲染和 Chromium/Blink 兼容；Chromium 路线仅保留为历史备选。

## 设计理念

- **开源** — 完全开放，社区驱动
- **智能** — AI 原生集成
- **跨端** — 一次开发，多平台运行
- **优雅** — 简洁、高效、美观

## 项目结构

```
openos/
├── docs/          # 文档
├── src/           # 源码
│   ├── kernel/    # 内核
│   ├── services/  # 系统服务
│   ├── middleware/ # 中间件
│   └── user/      # 用户程序
├── tools/         # 工具脚本
└── build/         # 构建输出
```

## 许可证

[MIT License](./LICENSE)