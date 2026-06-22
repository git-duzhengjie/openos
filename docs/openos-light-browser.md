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
- 基于自研 DOM 遍历渲染正文，不再使用纯字符串折叠输出。
- 将常见块级标签转换为换行，输出可读纯文本。
- 默认块级标签已覆盖 `article`、`section`、`nav`、`header`、`footer`、`main` 等语义标签。
- 支持基础 HTML 实体解码：`&amp;`、`&lt;`、`&gt;`、`&quot;`、`&#39;`、`&apos;`、`&nbsp;`。
- HTML tokenizer 支持跳过带引号属性，parser 支持 `<br>`、`<img>`、`<meta>` 等 void 标签不入栈。
- GUI 提供 `Refresh`、`Back`、`Forward`、`Up`、`Down`、`Close`，支持刷新、最小历史导航和文本分页滚动。
- 支持 `file://path` 和绝对本地路径加载 HTML。
- 新增 `tests/unit/test_browser_engine_smoke.c`，在 host-side 单元测试中验证 tokenizer/parser/DOM/render 最小链路。

## 非目标

- 当前不追求 Chromium/Chrome 兼容性；Chromium 官方内核迁移路线已冻结为历史备选。
- 当前不实现 JavaScript、复杂 CSS、媒体播放、GPU 合成和现代 Web App 兼容。
- Chromium 官方内核迁移路线已冻结为历史备选，不再作为当前 P0/P1 阻塞项。

## OpenOS/QEMU 手动回归：GUI 文档视图

当前 GUI 框架还没有浏览器专用自动点击/截图断言，因此 GUI 文档视图使用手动回归步骤记录。

### 1. 准备本地 HTML

在 OpenOS shell 中创建测试页：

```sh
cat > /tmp/browser-smoke.html <<'HTML'
<html>
<head><title>Local Browser Smoke</title></head>
<body>
<header>Top &amp; Header</header>
<nav>Nav</nav>
<main data-x="1>2">
  <article><section>Body A<br>Body B<img src="x>y.png"></section></article>
  <p>Line C</p>
  <p>Line D</p>
  <p>Line E</p>
  <p>Line F</p>
  <p>Line G</p>
</main>
<footer>Bottom</footer>
</body>
</html>
HTML
```

### 2. 启动浏览器

```sh
/bin/browser file:///tmp/browser-smoke.html
```

也可以使用绝对路径：

```sh
/bin/browser /tmp/browser-smoke.html
```

### 3. 期望结果

- 状态栏先显示 `Loading file:///tmp/browser-smoke.html ...`，加载完成后显示 `FILE loaded`。
- 正文顶部显示标题 `Local Browser Smoke`。
- 正文按块级 DOM 输出，能看到：
  - `Top & Header`
  - `Nav`
  - `Body A`
  - `Body BLine C` 或相邻 void 标签后的等价文本布局
  - `Bottom`
- `&amp;` 被解码为 `&`。
- 属性值中的 `>` 不应截断标签解析。
- `<br>` 产生换行；`<img>` 输出 `[Image: ...]` 占位且不吞掉后续正文。
- 点击 `Down` 后正文窗口向后滚动，点击 `Up` 后回滚。
- 点击 `Refresh` 后强制重新加载当前页面。
- 点击 `Back`/`Forward`：有历史时优先使用最近页面缓存恢复正文与滚动位置；无历史时状态栏显示 `Back: no history` / `Forward: no history`。
- 点击链接后加载目标页面；网络错误、404/非 2xx、重定向失败、非 HTML 内容会显示可诊断错误页。
- HTTP 状态栏会展示 `Content-Type`、`Content-Length` 和 `Location` 等关键 header。

### 4. 本地兼容性样例

样例页面位于：

```text
tests/resources/browser/index.html
tests/resources/browser/form.html
tests/resources/browser/error.html
```

`./build.sh test` 会自动编译并运行浏览器样例回归，覆盖基础排版、链接、表单、图片占位和错误页文本解析。

### 5. QEMU 启动级 smoke

```bash
scripts/qemu-smoke.sh --timeout 12
```

该脚本验证 OpenOS 能启动到可交互环境；GUI 文档视图仍按上面的手动步骤确认。

## 后续路线

1. 扩展 CSS 默认样式和基础选择器。
2. 后续在 GUI 框架支持输入框后，补真正的地址栏。
3. 增加 GUI 自动化事件/截图断言后，把上述手动回归转为自动化测试。
4. Chromium 路线保持冻结；只有在构建体积、依赖闭包、许可证与长期维护成本重新评估通过后才恢复。
