# OpenOS 浏览器引擎路线：自研轻量内核主线

OpenOS 浏览器当前不再迁移 Chrome/Chromium 内核，近期唯一主线改为自研轻量浏览器内核：

```text
OpenOS Browser Engine = 网络加载 + HTML tokenizer/parser + 最小 DOM + 默认 CSS + GUI 文本/块布局渲染
```

## 当前决策

- `/bin/browser` 是当前浏览器主入口。
- 源码入口为 `src/user/browser.c`，自研内核接口为 `src/user/browser_engine.h`。
- `/bin/chromium` 只保留为历史兼容/demo 名称，不能再作为当前迁移目标。
- Chromium/Chrome 官方内核迁移任务冻结为历史备选，不再作为 P0/P1 阻塞项。

## 明确不做

当前阶段不追求：

- Chrome/Chromium/Blink 兼容性。
- 官方 `content_shell` 构建和替换 `/bin/chromium`。
- 全量或最小 Chromium `third_party` 依赖闭包同步。
- JavaScript/V8、GPU 合成、复杂 CSS、媒体播放、现代 Web App 兼容。

## 当前事实

当前 `/bin/browser` 是 OpenOS 自研轻量浏览器，用于验证 GUI、HTTP/文件加载、HTML 文本化解析、最小 DOM/CSS 分层、历史导航和滚动阅读能力。

它不是 Chrome 引擎，也不需要伪装成 Chrome 引擎。

## 近期主线

1. 继续完善 `src/user/browser_engine.h` 的 tokenizer/parser/DOM/style 分层。
2. 将 `src/user/browser.c` 的文本输出逐步切换为 DOM 遍历渲染。
3. 扩展默认 CSS display、基础选择器和简单块布局。
4. 完善 `file://`、HTTP 错误页、本地 smoke 测试与 GUI 回归。
5. 在 OpenOS GUI 支持输入控件后补真正地址栏。
6. Chromium 相关文档和脚本仅保留为历史资料/长期备选，不参与当前默认构建路线。
