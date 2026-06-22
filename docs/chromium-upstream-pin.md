# Chromium 上游源码固定入口

OpenOS 真实浏览器路线只接受 Chromium/Chrome 引擎栈：

```text
Chromium Content + Blink + V8 + Skia
```

当前仓库不直接 vendoring Chromium 源码，也不要求默认同步海量无关 `third_party`。P0 路线采用“官方 Chromium 源码入口 + `content_shell` 所需最小依赖闭包”：先获取 Chromium `src` 最小历史，再只补齐 GN 生成和 `//content/shell:content_shell` 构建实际需要的依赖。

## 固定目录

默认目录：

```text
.openos-deps/
├── depot_tools/
└── chromium/
    └── src/
```

可通过环境变量覆盖：

```bash
OPENOS_CHROMIUM_DEPS_DIR=/data/openos-deps
OPENOS_DEPOT_TOOLS_DIR=/data/openos-deps/depot_tools
OPENOS_CHROMIUM_ROOT=/data/openos-deps/chromium
```

`.openos-deps/` 已加入 `.gitignore`，不能提交 Chromium 上游源码到 OpenOS 仓库。

## 前置检查

默认只检查环境，不下载源码：

```bash
./build.sh chromium-source-check
```

等价于：

```bash
scripts/chromium-source.sh --check
```

检查内容：

- git / python3 / curl / tar / unzip / xz
- depot_tools 是否存在
- Chromium `src` 是否存在
- 可用磁盘空间，默认至少 40GB，用于最小源码/依赖闭包起步

磁盘阈值可覆盖：

```bash
OPENOS_CHROMIUM_MIN_FREE_GB=80 ./build.sh chromium-source-check
```

## 获取 depot_tools

```bash
scripts/chromium-source.sh --fetch-depot-tools
```

## 获取 Chromium 最小源码入口

必须显式执行：

```bash
scripts/chromium-source.sh --fetch
```

该命令只做最小起步：

```bash
git clone --depth=1 https://chromium.googlesource.com/chromium/src.git .openos-deps/chromium/src
```

它不会默认执行完整：

```bash
gclient sync --no-history --with_branch_heads --with_tags
```

原因：完整 `gclient sync` 会同步大量与 OpenOS `content_shell` 最小路线无关的 `third_party`、测试资源、移动端/平台专用依赖和工具链缓存。P0.2 的正确目标是形成可记录、可复现的最小依赖闭包，而不是把“完整 Chromium checkout”当成默认前提。

如后续确实需要完整官方工作树，可显式使用 `scripts/chromium-source.sh --fetch-full` 或手动执行官方 gclient 流程，并在本文件记录原因、磁盘占用和 commit。

## 最小依赖闭包原则

P0.2/P0.3 同步依赖时遵循：

1. 必须保留 `content_shell` 构建必需依赖，例如 V8、Skia、Blink 相关生成工具、build/buildtools、ICU、基础压缩/图片库等。
2. 默认跳过测试数据、大型 fuzz/corpus、Android/iOS/ChromeOS 专用包、未启用媒体/GPU/远程服务依赖。
3. 优先使用 `--no-history`、浅克隆、可复用缓存和已存在目录，避免重复下载。
4. 每个新增依赖必须能解释其被 GN/Ninja 引用的原因，不能为了省事全量拉取 `third_party`。

## 版本固定策略

P1/P0.2 阶段先固定源码获取入口，不在 OpenOS 仓库中保存巨大源码。

后续需要在本文件补充实际 pin：

```text
chromium_commit: <待真实最小源码入口获取后填写>
depot_tools_commit: <待真实获取后填写>
minimal_deps: <待 GN/content_shell 闭包确认后填写>
gclient_args: <仅在显式完整同步时填写>
```

提交 pin 前必须能运行：

```bash
git -C "$OPENOS_CHROMIUM_ROOT/src" rev-parse HEAD
git -C "$OPENOS_DEPOT_TOOLS_DIR" rev-parse HEAD
```

## 当前 P0.2 尝试记录

记录时间：2026-06-22

当前机器不应再以“180GB 完整 checkout”作为默认阻塞项。当前真实阻塞是网络和 host tool：

```text
WSL 访问 chromium.googlesource.com: 连接超时
host tool: unzip 缺失
默认阈值：OPENOS_CHROMIUM_MIN_FREE_GB=40
```

`depot_tools` 官方源为：

```text
https://chromium.googlesource.com/chromium/tools/depot_tools.git
```

本轮通过 WSL 访问官方 Gitiles 时出现连接超时：

```text
Failed to connect to chromium.googlesource.com port 443
```

因此 P0.2 的最小验收需要先满足以下任一条件：

1. 修复当前网络到 `chromium.googlesource.com` 的访问，或提供已同步的 `depot_tools`/Chromium `src` 缓存；
2. 安装缺失的 host tool，例如 `unzip`；
3. 提供足够最小闭包起步的依赖目录，默认 40GB，可按实际闭包增长调整。

在这些条件满足前，`./build.sh chromium-source-check` 必须继续失败，不能伪造 Chromium checkout/pin。

## 禁止事项

- 禁止把 NetSurf/Dillo/Links/QuickJS demo 作为真实浏览器路线。
- 禁止把当前 OpenOS 自研 `/bin/chromium` 文本/GUI demo 宣称为 Chrome 引擎。
- 禁止把 Chromium 上游源码直接提交到 OpenOS 仓库。
- 禁止把完整 `third_party` 全量同步作为默认路线；只能按 `content_shell` 最小闭包逐项引入。
