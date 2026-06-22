# OpenOS 自研轻量浏览器

当前浏览器主线改为 OpenOS 自研轻量浏览器，入口为 `/bin/browser`，源码为 `src/user/browser.c`。

## 当前能力

- 已新增 `src/user/browser_engine.h`，定义轻量浏览器引擎接口/基类：
  - `ob_html_tokenizer_i_t`：HTML tokenizer 接口。
  - `ob_html_parser_i_t`：HTML parser 接口。
  - `ob_dom_document_t` / `ob_dom_node_t`：最小 DOM 文档和节点结构。
  - `ob_style_resolver_i_t`：默认 CSS/样式解析接口，先支持块级、行内、隐藏三类 display。
- 支持命令行传入 `http://host/path` 或 `host path`。
- 使用 HTTP/1.0 GET 进行页面加载。
- 保留 DNS、TCP 连接、发送、接收阶段的错误诊断。
- 解析 HTTP 状态行，并在 GUI 状态栏显示。
- 从 HTML 中提取 `<title>`。
- 将常见块级标签转换为换行，输出可读纯文本。
- 支持基础 HTML 实体解码：`&amp;`、`&lt;`、`&gt;`、`&quot;`、`&#39;`、`&apos;`、`&nbsp;`。
- GUI 提供 `Refresh`、`Back`、`Forward`、`Up`、`Down`、`Close`，支持刷新、最小历史导航和文本分页滚动。
- 支持 `file://path` 和绝对本地路径加载 HTML。
- 新增 `tests/unit/test_browser_engine_smoke.c`，在 host-side 单元测试中验证 tokenizer/parser/DOM 最小链路。

## 非目标

- 当前不追求 Chromium/Chrome 兼容性；Chromium 官方内核迁移路线已冻结为历史备选。
- 当前不实现 JavaScript、复杂 CSS、媒体播放、GPU 合成和现代 Web App 兼容。
- Chromium 官方内核迁移路线已冻结为历史备选，不再作为当前 P0/P1 阻塞项。

## 后续路线

1. 将文本渲染逐步切换到 DOM 遍历输出。
2. 扩展 CSS 默认样式和基础选择器。
3. 后续在 GUI 框架支持输入框后，补真正的地址栏。
