# 真实 Chrome/Chromium 引擎门槛

## 当前事实

当前 `/bin/chromium` 不是真实 Chrome/Chromium 引擎。

它是 OpenOS 用户态 demo，用于验证：

- GUI 窗口、按钮、绘制和事件循环
- HTTP 加载和后台线程
- 下载到 VFS
- OpenOS SDK/sysroot 和用户态 ELF 嵌入流程

它没有链接官方 Chromium Content、Blink、V8 或 Skia，因此不能宣称为真实浏览器完成。

## 真实 Chrome 引擎最低门槛

只有满足以下条件，才允许把 OpenOS 浏览器称为真实 Chrome/Chromium 引擎：

1. 存在官方 Chromium checkout 的固定提交：`docs/chromium-upstream-pin.md` 记录实际 commit。
2. 存在官方 Skia 构建产物或 pin：`ports/chromium-openos/skia.official.pin`。
3. 存在官方 V8 构建产物或 pin：`ports/chromium-openos/v8.official.pin`。
4. 存在 Blink/content_shell 构建产物或真实 pin：
   - `ports/chromium-openos/blink.official.pin`
   - `ports/chromium-openos/content_shell.official.pin`
   - 占位 pin（`status=pending_*` 或 `<pending>`）只能表示路线入口存在，不能让严格门禁通过。
5. OpenOS 运行路径使用 Chromium Content/Blink/V8/Skia，而不是自研 HTML 文本折叠 demo。
6. 构建日志能证明目标为：

```gn
target_os = "openos"
target_cpu = "x86"
```

## 门禁脚本

普通真实性检查：

```bash
./build.sh chromium-engine-gate
```

严格检查官方引擎 pin：

```bash
scripts/chromium-engine-gate.sh --strict
```

最终真实切换检查：

```bash
./build.sh chromium-real-switch-gate
```

`chromium-real-switch-gate` 必须在以下条件全部满足后才通过：

- Skia/V8/Blink/content_shell pin 均记录真实 40 位 commit，不能是 `status=pending_*` 或 `<pending>`。
- 官方 `content_shell`/Chromium 产物存在、非空，并可记录 size/sha256。
- `build.sh` 不再把 `src/user/chromium.c` 编译/嵌入为 `chromium_elf`/`chromium.elf`。
- `src/kernel/kernel.c` 不再把 demo `chromium_elf` 安装到 `/bin/chromium`。
- OpenOS content_shell GN 参数仍保持 `target_os = "openos"`、`target_cpu = "x86"` 和单进程软件渲染路线。

当前阶段只要求普通检查通过；严格检查和最终真实切换检查会在 P0.2-P0.7 逐步补齐官方 Chromium checkout、GN、`content_shell` 构建、安装切换和 smoke 验证后通过。P6 已建立 `scripts/chromium-content-shell.sh` 和 `args.content-shell-openos-i386.gn`，但在真实 Chromium checkout、足够磁盘空间和成功 `content_shell` 构建前，Blink/content_shell pin 仍必须被视为 pending。

## 禁止说法

在官方引擎 pin 和运行链路完成前，禁止使用以下说法：

- “OpenOS 已经有真实 Chrome 浏览器”
- “/bin/chromium 已接入 Blink/V8/Skia”
- “当前 chromium demo 是 Chromium 内核”

允许说法：

- “当前 `/bin/chromium` 是 OpenOS Chromium Demo”
- “当前 demo 用于验证 OpenOS GUI/HTTP/线程/SDK 能力”
- “真实 Chromium 引擎路线正在按 Skia → V8 → Blink/content_shell 推进”
