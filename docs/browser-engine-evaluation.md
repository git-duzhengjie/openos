# OpenOS 浏览器引擎评估结论

结论：OpenOS 当前放弃迁移 Chrome/Chromium 内核，改用自研轻量浏览器内核方案。

当前目标是围绕 `/bin/browser` 落地一条可维护、可验证、适合 OpenOS 当前系统能力的浏览器路线：

```text
OpenOS Browser Engine = 网络加载 + HTML tokenizer/parser + 最小 DOM + 默认 CSS + GUI 文本/块布局渲染
```

## 为什么冻结 Chromium 迁移

Chromium 不是一个可以直接复制进 OpenOS 的单一库。它需要长期补齐：

- C/C++ runtime、libc/libstdc++ 能力。
- pthread/futex/condition variable/thread-local storage。
- mmap/VM/address space/protection flags。
- file/socket/select/poll/epoll 等 POSIX 兼容层。
- shared memory、IPC、process model 或单进程降级路径。
- timer、monotonic clock、random、entropy。
- font、image codec、GPU/软件 raster surface。
- TLS/HTTPS 依赖。
- Chromium GN/toolchain/sysroot 适配。

这些能力仍可作为 OpenOS 系统能力长期演进方向，但不再阻塞当前浏览器可用性。

## 当前执行原则

1. `/bin/browser` 是当前浏览器主入口。
2. 自研内核先支持 HTML tokenizer/parser、最小 DOM、默认 CSS display 和 GUI 可读渲染。
3. HTTP、DNS、TCP、文件加载、错误诊断、历史导航和滚动阅读必须保持可回归。
4. `/bin/chromium` 和 Chromium 相关脚本/文档只能作为历史兼容或长期备选资料，不能再被描述为当前主线。
5. 禁止把自研轻量浏览器宣称为 Chrome/Chromium 内核。
