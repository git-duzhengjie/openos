# openos

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
qemu-system-i386 -drive format=raw,file=target/openos.img -m 512M -serial stdio -display gtk,show-cursor=on -device piix3-usb-uhci,id=uhci -device usb-tablet,bus=uhci.0
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

## 中文字库资源

默认构建会把内置 UI 字库导出为压缩 `.ofntz` 容器，并以 `/fonts/cjk.ofnt` 路径安装到 ramfs；加载失败时会回退到内核内置小字库。

如需生成真正的大覆盖中文字库，需要安装 Pillow 并提供可用中文 TTF / OTF / TTC 字体。大覆盖资源默认只生成到 `target/cjk-large.ofntz`，不嵌入 BIOS 低端加载的内核镜像，避免再次触发 VGA 保留内存重叠：

```bash
# GB2312 覆盖，默认生成压缩外置资源 target/cjk-large.ofntz
OPENOS_CJK_COVERAGE=gb2312 OPENOS_CJK_FONT=/path/to/chinese-font.ttf bash build.sh

# CJK Unified Ideographs 基本区覆盖
OPENOS_CJK_COVERAGE=cjk-basic OPENOS_CJK_FONT=/path/to/chinese-font.ttf bash build.sh

# 仅在确认镜像体积可接受时，才显式嵌入大字库
OPENOS_CJK_COVERAGE=gb2312 OPENOS_CJK_EMBED=1 OPENOS_CJK_FONT=/path/to/chinese-font.ttf bash build.sh
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