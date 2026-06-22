# Blink/content_shell 单进程软件渲染启动链路

本文档记录 OpenOS 真实 Chromium 路线的 P6 阶段：接入官方 Blink/content_shell 的最小启动链路。

## 边界

当前仓库中的 `/bin/chromium` 仍是 OpenOS demo，不等同于 Chromium 内核浏览器。P6 新增的是官方 Chromium checkout 上的 `content_shell` 引入入口：

```text
Chromium Content -> Blink -> V8 -> Skia
```

目标是先形成可重复的检查、GN 生成、构建和 smoke 命令，再逐步补齐 OpenOS 原生平台能力。

## 文件

- `scripts/chromium-content-shell.sh`
  - `--check`：检查 Chromium checkout、depot_tools、GN/Ninja、GN args 和 pin 文件。
  - `--gn-gen`：使用 OpenOS content_shell GN 参数生成构建目录。
  - `--build`：构建 `//content/shell:content_shell`。
  - `--smoke`：用 `--single-process --disable-gpu` 启动最小 data URL。
  - `--pin`：从真实 Chromium checkout 写入官方 Blink/content_shell pin。
- `ports/chromium-openos/args.content-shell-openos-i386.gn`
  - 固定 `target_os = "openos"`、`target_cpu = "x86"`。
  - 选择单进程、headless ozone、软件渲染优先的最小 bring-up 配置。
- `ports/chromium-openos/blink.official.pin`
  - 由 `--pin` 在真实 Chromium checkout 存在时生成。
- `ports/chromium-openos/content_shell.official.pin`
  - 由 `--pin` 在真实 Chromium checkout 存在时生成。

## build.sh 入口

```bash
./build.sh chromium-content-shell-check
./build.sh chromium-content-shell-gn-gen
./build.sh chromium-content-shell-build
./build.sh chromium-content-shell-smoke
```

## 前置条件

P6 不提交 Chromium 巨型源码。源码仍由 `docs/chromium-upstream-pin.md` 和 `scripts/chromium-source.sh` 管理：

```bash
./build.sh chromium-source-check
scripts/chromium-source.sh --fetch-depot-tools
scripts/chromium-source.sh --fetch
```

默认目录：

```text
.openos-deps/chromium/src
```

可用环境变量覆盖：

```bash
OPENOS_CHROMIUM_SRC=/path/to/chromium/src ./build.sh chromium-content-shell-check
OPENOS_CONTENT_SHELL_OUT=/path/to/out ./build.sh chromium-content-shell-build
```

## 当前验证状态

本阶段已建立官方 Blink/content_shell 的最小启动链路入口与 GN 参数文件。若本机还没有完整 Chromium checkout，`chromium-content-shell-check` 会诚实失败并提示执行 `scripts/chromium-source.sh --fetch`；不会把 OpenOS demo 伪装成 Blink/content_shell 构建产物。

后续真正通过 `--build` 后，需要补充：

1. `ports/chromium-openos/blink.official.pin`
2. `ports/chromium-openos/content_shell.official.pin`
3. `docs/chromium-engine-reality-gate.md` 中严格门禁通过记录
4. OpenOS 运行时平台能力缺口列表
