# OpenOS Chromium 进程模型边界

## 目标

`/bin/chromium` 第一版先采用单进程内容壳，目标是尽快验证 Blink/V8/Skia 到 OpenOS 内核能力的端到端链路；随后再恢复 Chromium 多进程结构。

这不是放弃多进程，而是把风险拆开：

1. 先验证用户态 C++ runtime、文件、网络、图形、字体、时间、线程和内存映射等基础能力。
2. 再验证浏览器进程、渲染进程、网络/服务进程之间的隔离与 IPC。

## 第一版：单进程模式

建议目标程序：

```text
/bin/content_shell
/bin/chromium
```

启动约束：

```text
--single-process
--disable-gpu
--disable-sandbox
--disable-dev-shm-usage 或使用 OpenOS shm backend
--js-flags=--jitless
```

第一版边界：

- 只支持单窗口、单标签页。
- 软件渲染优先，不启用 GPU 进程。
- V8 默认 jitless，不依赖可执行匿名映射。
- 网络先支持 HTTP，HTTPS 在 TLS/证书体系稳定后启用。
- 文件资源优先静态链接或固定路径 pak/resource 读取。
- 不在内核中实现 Chromium 逻辑，内核只提供通用 syscall/IPC/FS/NET/GUI 能力。

## 多进程恢复路径

多进程阶段逐步恢复以下角色：

```text
browser/main process
renderer process
network/service process
utility process
```

恢复顺序建议：

1. renderer 进程拆分：验证 `spawn/exec/waitpid`、argv/envp、fd 继承、崩溃回收。
2. IPC 通道稳定：以 socketpair/service channel 为基础，承载 request/reply 和事件流。
3. 共享内存位图/资源传递：通过 shm handle + 生命周期管理传递大块数据。
4. network/service 进程拆分：验证 socket、poll、DNS、TLS 与服务进程崩溃隔离。
5. sandbox/权限收敛：在 OpenOS 原生能力上定义最小权限边界。

## 需要保持稳定的系统语义

M4 阶段已经把以下语义纳入 `/bin/chromiumcaptest`：

- spawn + argv/envp + waitpid。
- fork 后 pipe fd 继承读写。
- close-on-exec / dup / pipe 基础 fd 生命周期能力。
- 共享内存 refcount/info/destroy 生命周期保护。
- message queue create/send/recv/truncate/destroy。
- service channel request/reply 元数据往返。
- socketpair + poll。

这些是后续多进程 Chromium 的最低验收线。任何进程、加载器、IPC 修改都必须保证 `/bin/chromiumcaptest` 继续通过。

## `/bin/chromium` 第一版接口边界

第一版 OpenOS 侧只承诺以下系统能力：

- 内存：anonymous mmap、file private mmap、mprotect、sbrk、shm、memory policy。
- 线程：thread create/gettid、TLS base、futex、pthread-like mutex/cond。
- 时间：monotonic uptime、clock_gettime monotonic timespec。
- 进程：spawn/exec/env/argv/waitpid，fork 作为兼容能力逐步增强。
- IPC：socketpair、poll、eventfd、message queue、service channel、shm。
- 文件：open/read/write/lseek/close，后续补 stat/readdir/cache/profile 目录。
- 网络：TCP connect/send/recv/poll，后续补 DNS/TLS/非阻塞边界。
- GUI：先提供软件 framebuffer/window surface，后续补 dirty rect、输入、字体 fallback。

## 后续验收

新增 Chromium 底层能力时必须同步：

1. 更新 `/bin/chromiumcaptest` 或新增专项 smoke test。
2. 更新 `TODOLIST.md`。
3. 必要时更新本文和 `docs/chromium-core-roadmap.md`。
4. 执行 `./build.sh`，确认生成 `target/openos.img`。
