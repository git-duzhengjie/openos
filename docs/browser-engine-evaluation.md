# OpenOS 浏览器引擎评估结论

结论：OpenOS 不再选择 NetSurf、Dillo、Links/Lynx、LibWeb 或自研轻量 HTML/JS demo 作为最终浏览器路线。

唯一目标是移植真实 Chromium/Chrome 引擎栈：

```text
Chromium Content + Blink + V8 + Skia
```

## 当前限制

Chromium 不是一个可以直接复制进 OpenOS 的单一库。它需要逐步补齐：

- C/C++ runtime、libc/libstdc++ 能力
- pthread/futex/condition variable/thread-local storage
- mmap/VM/address space/protection flags
- file/socket/select/poll/epoll 等 POSIX 兼容层
- shared memory、IPC、process model 或单进程降级路径
- timer、monotonic clock、random、entropy
- font、image codec、GPU/软件 raster surface
- TLS/HTTPS 依赖
- Chromium GN/toolchain/sysroot 适配

## 执行原则

任何 OpenOS 自研 demo 只能作为系统能力 smoke test，不能标记为真实浏览器完成。真实浏览器完成标准必须至少包含官方 Skia、官方 V8、Blink/content shell 的可验证构建与运行路径。
