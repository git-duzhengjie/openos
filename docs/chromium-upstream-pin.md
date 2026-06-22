# Chromium 上游源码固定入口（冻结归档）

> 状态：已冻结。OpenOS 当前放弃迁移 Chrome/Chromium 内核，改用自研轻量浏览器内核方案。

本文件仅保留历史研究记录，避免后续误把 Chromium 迁移任务重新当作当前 P0/P1 主线。

## 当前浏览器主线

当前主线是 OpenOS 自研轻量浏览器：

```text
/bin/browser
src/user/browser.c
src/user/browser_engine.h
```

目标能力：

```text
网络加载 + HTML tokenizer/parser + 最小 DOM + 默认 CSS + GUI 文本/块布局渲染
```

## 冻结的 Chromium 迁移路线

以下工作不再作为当前默认任务执行：

- 获取官方 Chromium `src`。
- 同步 Chromium `third_party`，无论全量还是最小闭包。
- 配置 OpenOS Chromium GN/toolchain。
- 构建官方 `content_shell`。
- 用官方 `content_shell` 替换 `/bin/chromium`。
- 将 `/bin/chromium` 作为当前浏览器主入口。

## 历史结论

Chromium/Chrome 官方引擎栈曾被评估为：

```text
Chromium Content + Blink + V8 + Skia
```

但这条路线依赖大量 OpenOS 尚需长期补齐的系统能力、工具链适配和外部源码同步条件。当前阶段继续推进会阻塞浏览器可用性，因此迁移路线冻结归档。

## 历史阻塞记录

记录时间：2026-06-22

当时尝试最小 Chromium 源码入口时遇到：

```text
Chromium src 最小入口：优先 https://github.com/chromium/chromium.git
GitHub 最小 src 浅克隆结果：fetch-pack unexpected disconnect while reading sideband packet
Gitiles clone fallback：WSL 访问 chromium.googlesource.com 连接超时
host tool: unzip 缺失
```

这些记录只用于历史追踪，不再要求当前任务继续解决。

## 禁止事项

- 禁止把当前 OpenOS 自研 `/bin/browser` 宣称为 Chrome/Chromium 内核。
- 禁止把 `/bin/chromium` demo 作为当前浏览器主线。
- 禁止在默认构建中重新引入 Chromium 源码或 `third_party` 同步。
- 禁止把 Chromium checkout、`content_shell` 构建或真实 Chrome 迁移作为当前 P0 阻塞项。

## 如未来恢复本路线

必须先由任务清单显式恢复，并重新确认：

1. 目标、资源预算和磁盘空间。
2. 源码获取方式和可复现 pin。
3. 最小依赖闭包或完整 checkout 的边界。
4. OpenOS GN/toolchain/sysroot 适配计划。
5. 不影响 `/bin/browser` 自研主线的回归策略。
