# Chromium 上游源码固定入口

OpenOS 真实浏览器路线只接受 Chromium/Chrome 引擎栈：

```text
Chromium Content + Blink + V8 + Skia
```

当前仓库不直接 vendoring Chromium 源码。Chromium 源码体量通常超过 100GB，必须放在外部依赖缓存目录，并通过脚本显式获取。

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
- Chromium checkout 是否存在
- 可用磁盘空间，默认至少 180GB

磁盘阈值可覆盖：

```bash
OPENOS_CHROMIUM_MIN_FREE_GB=250 ./build.sh chromium-source-check
```

## 获取 depot_tools

```bash
scripts/chromium-source.sh --fetch-depot-tools
```

## 获取 Chromium 源码

必须显式执行：

```bash
scripts/chromium-source.sh --fetch
```

该命令会执行 Chromium 官方流程：

```bash
fetch --nohooks --no-history chromium
gclient sync --no-history --with_branch_heads --with_tags
```

注意：这会消耗 100GB+ 磁盘和较长时间。不要在普通 `./build.sh` 中自动触发。

## 版本固定策略

P1 阶段先固定源码获取入口，不在 OpenOS 仓库中保存巨大源码。

后续 P2/P3 需要在本文件补充实际 pin：

```text
chromium_commit: <待真实 checkout 后填写>
depot_tools_commit: <待真实 checkout 后填写>
gclient_args: --no-history --with_branch_heads --with_tags
```

提交 pin 前必须能运行：

```bash
git -C "$OPENOS_CHROMIUM_ROOT/src" rev-parse HEAD
git -C "$OPENOS_DEPOT_TOOLS_DIR" rev-parse HEAD
```

## 当前 P0.2 尝试记录

记录时间：2026-06-22

当前机器尚未完成完整 Chromium checkout，原因是外部条件未满足：

```text
C: 可用约 9GB
D: 可用约 29GB
E: 可用约 80GB
F: 可用约 75GB
默认阈值：OPENOS_CHROMIUM_MIN_FREE_GB=180
```

`depot_tools` 官方源为：

```text
https://chromium.googlesource.com/chromium/tools/depot_tools.git
```

本轮通过 WSL 访问官方 Gitiles 时出现连接超时：

```text
Failed to connect to chromium.googlesource.com port 443
```

因此 P0.2 的完整验收需要先满足以下任一条件：

1. 提供可用空间不少于 180GB 的 `OPENOS_CHROMIUM_DEPS_DIR`；
2. 修复当前网络到 `chromium.googlesource.com` 的访问，或提供已同步的 `depot_tools`/Chromium 源码缓存；
3. 安装缺失的 host tool，例如 `unzip`。

在这些条件满足前，`./build.sh chromium-source-check` 必须继续失败，不能伪造 Chromium checkout/pin。

## 禁止事项

- 禁止把 NetSurf/Dillo/Links/QuickJS demo 作为真实浏览器路线。
- 禁止把当前 OpenOS 自研 `/bin/chromium` 文本/GUI demo 宣称为 Chrome 引擎。
- 禁止把 Chromium 上游源码直接提交到 OpenOS 仓库。
