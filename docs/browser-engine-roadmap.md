# OpenOS 浏览器内核路线

## 当前状态

OpenOS 目前的 Browser 是内核 GUI 中的轻量过渡实现，主要用于验证网络栈、GUI 控件、HTTP 请求和基础 HTML 文本化显示能力。它不是最终形态的完整浏览器内核，也不等同于 Chromium、WebKit、Gecko 或 LibWeb 级别的现代浏览器。

当前已具备：

- 基础 URL 输入、前进/后退占位按钮、刷新/前往按钮。
- HTTP 访问链路：DNS、TCP connect、HTTP GET、响应接收。
- 非阻塞加载状态机：DNS / TCP / HTTP / TLS probe 分阶段推进，避免 GUI 主线程卡死。
- 基础超时与失败提示：DNS 失败、TCP 连接失败、HTTP 接收超时、TLS 无响应等。
- HTML 转可读文本：跳过 head/style/script/svg/noscript，去除标签，显示正文文本。
- 基础链接提取与点击导航：从 `<a href="...">` 中提取文本与 URL，并解析相对链接。
- HTTPS 探测：可发送 TLS ClientHello，并展示 ServerHello / Certificate / ECDHE 摘要；尚不支持 HTTPS 页面解密加载。

## 当前 Browser 的边界

短期 Browser 只作为系统联调工具和轻量文档查看器，不承担完整网页兼容目标。

当前不支持：

- DOM 树与标准 HTML 解析模型。
- CSS 选择器、层叠、盒模型和布局。
- JavaScript、DOM API、事件循环和 Web API。
- 图片、表格、表单、复杂排版和媒体资源。
- HTTPS 完整加密通道、证书链校验和 TLS 应用数据解密。
- 多标签页、缓存、Cookie、下载管理和浏览器安全模型。

## 阶段 1：保留并增强轻量 Browser

目标：继续把内核内置 Browser 作为网络栈/GUI 的调试入口，但避免它膨胀为完整浏览器内核。

计划：

1. 保持 HTTP 访问、DNS/TCP/HTTP 非阻塞加载和基础 HTML 文本化显示可用。
2. 改善 HTML 文本化渲染：
   - 更多 HTML entity 解码。
   - 更稳定的空白压缩。
   - 标题、段落、列表、pre/code 的基础显示规则。
3. 改善加载状态机：
   - DNS、TCP、HTTP、TLS probe 分阶段超时。
   - 连续 Go / Refresh 时取消旧连接。
   - 浏览器关闭时取消加载上下文。
   - 所有失败路径必须退出 Loading 状态。
4. 增加小型回归测试或 fixture，覆盖 example.com 一类静态页面。

## 阶段 2：浏览器用户态化

目标：将 Browser 从 `src/kernel/gui.c` 中拆出，迁移为用户态 `/bin/browser`，避免浏览器错误拖垮内核。

需要先补齐用户态 GUI 应用 ABI：

- 窗口创建、关闭、移动、尺寸调整。
- 文本绘制、矩形绘制、位图 blit、裁剪区域。
- 鼠标、键盘、文本输入、焦点和定时器事件。
- 控件或轻量 UI toolkit 的用户态 API。
- 剪贴板、输入法/Unicode 文本输入的后续扩展。

用户态 Browser 应统一通过 libc / socket API 访问网络，而不是直接调用内核内部网络函数。

## 阶段 3：补齐开源浏览器内核运行环境

移植开源浏览器内核前，需要具备以下基础：

- libc/POSIX 子集：malloc/free/realloc、stdio、string、time、errno、文件和目录 API。
- socket API：getaddrinfo/gethostbyname、connect/send/recv/close、select/poll、非阻塞 socket。
- TLS/HTTPS 用户态库：优先评估 mbedTLS、BearSSL、wolfSSL 等轻量方案。
- 字体接口：字体枚举、字形查询、UTF-8/Unicode 文本测量、fallback。
- 图形接口：framebuffer/窗口绘制、矩形裁剪、双缓冲、滚动。
- 图片解码：PNG、JPEG、GIF、WebP 分阶段接入。
- 文件与配置目录：缓存、Cookie、证书、字体资源、下载目录。

## 阶段 4：优先评估 NetSurf

NetSurf 是 OpenOS 第一代开源浏览器内核的优先候选，因为它比 Chromium/WebKit/Gecko 更轻量，且已有 framebuffer frontend。

迁移步骤：

1. 阅读 NetSurf framebuffer frontend、libdom、libcss、hubbub、utils 等依赖结构。
2. 在宿主机完成最小 framebuffer frontend 构建验证。
3. 记录依赖裁剪清单，明确必须库、可选库和替代层。
4. 编写 OpenOS 平台层：framebuffer 绘制、输入事件、定时器、文件、socket、字体。
5. 先支持 HTTP 静态页面显示，再逐步接入 HTTPS、图片、表单、下载等能力。

## 备选方案

- Dillo：轻量 GUI 浏览器，但需要评估 FLTK 依赖替换成本、HTTPS 和中文支持工作量。
- Links2 / Lynx：适合作为文本/半图形浏览器验证方案，不作为最终 GUI 浏览器目标。
- SerenityOS LibWeb / Ladybird：现代架构参考，适合远期；需要 C++ 运行时、线程、图形和 JS 环境成熟后再评估。
- Chromium / WebKit / Gecko：近期不作为目标，依赖体量和平台适配成本过高，仅保留长期参考价值。

## JavaScript 路线

- 短期：不在内核内置 Browser 中实现 JavaScript。
- 中期：评估 QuickJS 作为轻量 JS 引擎。
- 长期：随 NetSurf / LibWeb 等浏览器内核路线决定 JS、DOM、CSSOM 支持深度。

## 决策原则

1. 浏览器应优先用户态化，不能让复杂网页解析逻辑长期留在内核。
2. 内核 Browser 只保留调试和过渡能力，不追求完整 Web 兼容。
3. 开源内核移植必须分阶段验证，每一步都要可构建、可回退。
4. HTTPS、字体、图片、缓存、Cookie 等能力应尽量复用用户态库或平台服务。
