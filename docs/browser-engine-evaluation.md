# OpenOS 开源浏览器内核评估与裁剪清单

## 评估结论

OpenOS 第一代开源浏览器内核优先评估 NetSurf。原因：

- 体量相对 Chromium / WebKit / Gecko 小得多。
- 有 framebuffer frontend，可绕开大型桌面 GUI toolkit。
- 组件化程度较高，HTML、CSS、DOM、网络、平台层可以分阶段适配。
- 更适合当前 OpenOS 的自研内核、轻量 GUI 和逐步补齐 POSIX/socket/libc 的路线。

近期不建议直接移植 Chromium / WebKit / Gecko。这些项目要求完整 POSIX、线程、进程沙箱、动态链接、图形栈、字体栈、TLS、证书、媒体、JavaScript JIT/解释器等大量基础设施，当前阶段成本过高。

## NetSurf 组件理解

NetSurf 常见核心组件：

| 组件 | 作用 | OpenOS 迁移关注点 |
| --- | --- | --- |
| NetSurf core | 浏览器主逻辑、窗口、内容管理 | 需要平台 glue、调度、fetch、绘制回调 |
| framebuffer frontend | 无桌面环境的 framebuffer 前端 | 最适合 OpenOS 初版 GUI/裸 framebuffer 适配 |
| libdom | DOM 实现 | 需要内存、字符串、构建系统适配 |
| libcss | CSS 解析与样式计算 | 需要字体度量、颜色、布局输入 |
| hubbub | HTML5 parser | 依赖 string/charset/内存接口 |
| libparserutils | parser 基础工具 | charset、buffer、错误处理适配 |
| libwapcaplet | 字符串 intern | 内存分配与哈希性能关注 |
| libnsbmp/libnsgif | BMP/GIF 图像支持 | 可选，初版可裁剪图片支持 |
| libnsutils | 工具库 | 需要构建系统适配 |
| fetch/curl 路径 | HTTP/HTTPS 获取 | OpenOS 初期不直接使用 libcurl，优先自写 fetch glue 到 socket API |

## NetSurf 最小移植切片

### 切片 0：宿主机验证

目标是在 Linux/WSL 宿主机上完成最小 framebuffer frontend 构建，确认依赖和构建顺序。

产出：

- 依赖版本清单。
- 构建命令记录。
- 可运行的 framebuffer demo 或静态页面加载验证。

### 切片 1：OpenOS 平台层骨架

目标只编译平台层，不要求完整网页显示。

需要提供：

- 内存分配：malloc/free/realloc/calloc。
- 时间：monotonic clock / timer。
- 文件：open/read/write/close/stat/opendir/readdir。
- 日志：printf/syslog。
- 基础字符串和 errno。

### 切片 2：绘制适配

目标显示纯文本/简单 HTML。

需要提供：

- framebuffer 或窗口 surface。
- fill rectangle。
- plot line / clip rectangle。
- bitmap blit。
- text measure / text draw。
- scroll / dirty rect present。

### 切片 3：输入与事件循环

需要提供：

- mouse move/down/up/wheel。
- key down/up。
- text input。
- timer tick。
- quit / close window。

### 切片 4：网络 fetch

先支持 HTTP：

- getaddrinfo 或等价 DNS。
- nonblocking connect/send/recv。
- poll/select 或事件驱动。
- redirect、Content-Length、chunked 可分阶段实现。

后支持 HTTPS：

- 优先用 BearSSL / mbedTLS / wolfSSL 之一在用户态实现。
- 证书链校验可以先做可选警告模式，最终必须补齐。

### 切片 5：资源能力

分阶段接入：

- PNG。
- JPEG。
- GIF。
- Cookie/cache/download。
- 表单与输入。

## NetSurf 依赖裁剪清单

初期保留：

- HTML parser / DOM / CSS 基础依赖。
- framebuffer frontend 思路。
- 基础 layout 和 text render 路径。

初期裁剪或延后：

- JavaScript。
- 复杂图片格式。
- PDF、视频、音频。
- 插件体系。
- 大型 TLS/curl 依赖路径。
- 平台特定 GUI toolkit frontend。

OpenOS 需要自行替换/实现：

- frontend 绘制接口。
- frontend 输入接口。
- fetch/network glue。
- font/text measure glue。
- file/cache/config path glue。
- timer/event loop glue。

## 备选轻量浏览器方案

### Dillo

优点：

- 轻量。
- GUI 浏览器形态更接近最终目标。
- HTML/CSS 支持比纯文本浏览器更好。

风险：

- 依赖 FLTK，OpenOS 需要移植 FLTK 或重写平台层。
- 中文字体、编码和输入支持需要额外验证。
- HTTPS 能力依赖外部 TLS/证书基础设施。

结论：作为备选 GUI 浏览器调研对象，优先级低于 NetSurf。

### Links2 / Lynx

优点：

- 更容易在 POSIX-like 环境中跑起来。
- 适合验证用户态 libc、终端、socket、DNS、HTTP。

风险：

- 主要是文本/半图形浏览器，不是最终 GUI 浏览器目标。
- 现代网页兼容有限。

结论：适合作为阶段性 smoke test，不作为最终 GUI Browser 目标。

### SerenityOS LibWeb / Ladybird

优点：

- 现代浏览器架构，Web 标准支持方向清晰。
- 对自研 OS 项目有较强参考价值。

风险：

- 依赖 C++ 运行时、线程、事件循环、图形、字体、TLS、JS 等完整环境。
- 当前 OpenOS 基础设施尚未成熟。

结论：远期参考，待 C++ runtime、用户态 GUI、线程和字体服务成熟后再评估。

### Chromium / WebKit / Gecko

优点：

- 现代网页兼容性强。
- 生态成熟。

风险：

- 体量巨大，构建系统复杂。
- 依赖完整 OS API、图形栈、字体栈、线程/进程、TLS、证书、安全沙箱。
- 对当前 OpenOS 阶段不现实。

结论：近期不作为目标，仅长期参考。

## JavaScript 路线

短期：

- 内核内置 Browser 不实现 JavaScript。
- 避免把 JS 引擎、GC、对象模型和 Web API 放进内核。

中期：

- 评估 QuickJS 作为轻量 JS 引擎候选。
- 前置条件：用户态 Browser、malloc/GC 友好环境、文件/模块 API、定时器、异常日志。

长期：

- 若走 NetSurf 路线，根据 NetSurf JS 支持和平台能力决定是否接 QuickJS 或其他引擎。
- 若远期走 LibWeb/Ladybird，JS/DOM/CSSOM 深度随其架构演进。

## 下一步阻塞项

要真正从调研进入移植，OpenOS 需要优先完成：

1. 用户态 GUI ABI 的内核实现和 libc 封装。
2. 用户态 socket API 与非阻塞 poll/select。
3. 可复用 TLS 用户态库。
4. 字体测量与 fallback API。
5. 文件/配置/缓存目录约定。
