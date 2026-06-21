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

OpenOS 当前内置 Browser 是过渡性的轻量浏览器，用于验证 GUI、DNS、TCP、HTTP 和基础 HTML 文本化显示能力。

当前支持基础 HTTP 页面访问、非阻塞 DNS/TCP/HTTP 加载、简单 HTML 转可读文本，以及基础链接点击导航；HTTPS 目前仅支持 TLS 握手摘要探测，不支持完整 TLS 加密通道内的网页解密加载。

它暂不等同于 Chromium / WebKit / Gecko 级完整浏览器，不支持完整 DOM、CSS 布局、JavaScript、图片、复杂表单和现代网页兼容。后续路线见 [`docs/browser-engine-roadmap.md`](docs/browser-engine-roadmap.md)。

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