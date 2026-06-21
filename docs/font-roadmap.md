# OpenOS 字体路线

## 当前状态

OpenOS 当前构建会生成并嵌入 CJK 字库资源，启动后以 `/fonts/cjk.ofnt` 安装到 ramfs，GUI 字体层加载成功后用于中文显示；加载失败时回退到内核内置小字库。

已具备：

- 构建阶段生成 CJK 字体资源。
- 支持 `.ofnt` 与压缩 `.ofntz` 资源。
- 资源随镜像嵌入并在启动时安装到 ramfs。
- GUI 层可加载外部 CJK 字库。
- `scripts/check_cjk_coverage.py` 可检查项目文本与字库资源覆盖率。

## 当前问题

- 默认资源主要覆盖 GB2312 级常用汉字，对文档和未来 UI 字符仍可能缺字。
- 缺字 fallback 还需要更明确的占位策略和调试提示。
- glyph cache 尚未系统化，长文本或复杂界面可能反复解码字形。
- 还没有字体枚举、字体族、字号、粗细和样式选择能力。

## 短期计划

1. 扩大 CJK 覆盖：从 GB2312 扩展到 GBK 常用区，并保留构建参数控制资源体积。
2. 缺字检测：构建时默认输出覆盖率提示，严格模式下可通过 `OPENOS_CJK_COVERAGE_STRICT=1` 让缺字变为构建失败。
3. fallback 策略：缺字时显示统一方框或问号，并在调试日志中记录 codepoint。
4. glyph cache：缓存最近使用字形，避免频繁查表和解压。

## 中期计划

- 支持多个字体资源并按 Unicode 范围选择 fallback。
- 支持基础字体度量：advance、baseline、line height。
- 支持 UTF-8 文本测量 API，为用户态 GUI ABI 和浏览器移植提供基础。
- 支持按需加载字体资源，减少内核常驻内存。

## 长期计划

- 支持完整 Unicode CJK 扩展区的外部字体包。
- 支持矢量字体或离线栅格化字体缓存。
- 用户态字体服务：字体枚举、fallback、文本 shaping 的平台统一入口。
- 为 Chromium/Blink/Skia 提供字体平台层。
