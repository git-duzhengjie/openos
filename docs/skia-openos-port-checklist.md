# Skia OpenOS Port 前置清单

> 目标：为方案 B 的真实 Skia 接入建立可验证的缺口清单。当前 `/bin/skia_demo` 是 OpenOS 自研 GUI/raster smoke，用于验证窗口、RGBA blit、文本和字体查询路径；它尚未链接官方 Skia，也不能作为 Skia 已移植完成的证据。

## 接入边界

### 当前已有

- i386 freestanding 用户态 ELF 构建链。
- `target/openos-sdk/` SDK/sysroot 导出入口。
- `./build.sh sdk-smoke` 可验证 SDK 能链接最小用户程序。
- OpenOS GUI 基础能力：window、fill rect、RGBA32 blit、draw text、present。
- 字体测量 smoke：`openos_font_query()`。
- C/C++ runtime 路线文档和初步 archive 占位。

### Skia 首阶段目标

- 只启用 CPU software raster。
- 禁用 GPU、Ganesh/Graphite 后端、font manager 复杂平台后端、图片 codec 可选项。
- 先输出单个静态 OpenOS 用户态 ELF。
- 绘制结果通过 OpenOS GUI RGBA32 surface present。
- smoke 内容：清屏、矩形、路径/圆角可选、文字、bitmap blit、dirty rect。

## OpenOS Surface Glue

需要新增或确认的适配层：

- `OpenOSSkiaSurface`：持有 RGBA32 像素缓冲、宽高、stride、dirty rect。
- `present()`：把 Skia raster buffer 转为 `openos_gui_blit_rgba32()` + `openos_gui_present()`。
- 像素格式固定为 32-bit RGBA/BGRA 之一，并在文档和测试中锁定。
- 失败路径返回明确错误码，smoke 输出 PASS/FAIL。

## 字体与文本

首阶段策略：

- 先使用内置 bitmap/basic font fallback 或 OpenOS 简化字体接口。
- `openos_font_query()` 只能作为文字测量烟测，不等价于完整 Skia font manager。
- 后续需要补齐：
  - 字体文件枚举。
  - 字体 fallback。
  - glyph cache。
  - UTF-8/CJK shaping 路线。

## 图片与资源

首阶段建议：

- PNG/JPEG/WebP codec 暂不作为首个阻塞项。
- bitmap smoke 使用内置 RGBA 数组。
- 文件资源读取通过 OpenOS VFS smoke 单独验证。

后续缺口：

- 文件 mmap/read 性能。
- 图片 codec 所需 zlib/libpng/libjpeg/webp 子集。
- 资源路径和 `/usr/share` 或应用 bundle 约定。

## 内存、线程与同步依赖

Skia 最小 raster 仍可能触发：

- `malloc/free/realloc` 稳定性。
- C++ `new/delete`、static local guard、`__cxa_atexit`。
- 原子 builtins。
- 线程局部存储 TLS。
- mutex/condition variable 或禁用多线程路径。
- 大块内存分配、对齐分配。

首阶段应尽量关闭 Skia 多线程和复杂 cache，缺口通过 `/bin/chromiumcaptest` 或新增 smoke 补测。

## 构建系统任务

- 固定 Skia 源码版本或子模块/外部缓存策略。
- 增加 OpenOS GN args/toolchain 草案，不伪装为 Linux。
- 使用 `target/openos-sdk/` 作为 OpenOS ABI 输入。
- 禁用不支持的平台 backend。
- 产出 `target/skia-openos-smoke.elf` 或同等可嵌入用户程序。

## 验收标准

真实 Skia 接入的第一阶段完成必须同时满足：

1. 构建日志明确显示链接的是官方 Skia 源码产物。
2. 输出 OpenOS i386 ELF，不依赖 Linux libc/sysroot。
3. smoke 在 OpenOS 中绘制 raster 内容并 present 到 GUI 窗口。
4. smoke 控制台输出明确 PASS/FAIL。
5. 文档不再把当前 `/bin/skia_demo` 描述为真实 Skia，只称为 Skia 接入前的 OpenOS 图形能力 smoke。
