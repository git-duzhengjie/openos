# OpenOS Chromium 核心能力路线

## 目标声明

OpenOS 的浏览器长期目标调整为 Chromium 级实现。这里的重点不是给 Chromium 做一层薄兼容壳，而是把 OpenOS 缺失的底层核心能力补齐，使其具备运行 Chromium content / Blink / V8 / Skia 的真实系统基础。

当前内置 `/bin/browser` 继续作为网络、GUI、HTTP 调试工具保留，但不再作为最终浏览器内核方向。

## 第一版 Chromium 目标

第一版目标不是完整 Chrome 产品，而是 OpenOS 原生的 Chromium 内容壳：

```text
/bin/chromium
    ↓
Chromium content/Blink/V8/Skia
    ↓
OpenOS process/thread/mm/ipc/fs/net/gui/font core
```

初期运行约束：

- 单窗口。
- 软件渲染优先。
- GPU 进程、硬件加速、沙箱策略后置。
- 先跑通 `http://example.com`，再补 HTTPS、证书、缓存、Cookie、复杂网页。
- 不把复杂浏览器逻辑放入内核；内核只提供可通用复用的核心能力。

## 核心能力分层

### 1. 内存与地址空间

Chromium/V8/Skia 需要强内存能力：

- `mmap/munmap`：已具备匿名映射、基础 `MAP_FIXED` 和文件私有快照映射雏形，后续需扩展 shared/file page cache/COW 语义。
- `mprotect`：已具备页级权限切换基础能力，V8 JIT、只读页、W^X 仍需策略化增强。
- `brk/sbrk`：已具备堆增长雏形，需要大分配压力测试。
- 共享内存：已有 `shm_create/shm_map/shm_destroy` 雏形，需要跨进程引用计数、大小参数、权限和生命周期。
- demand paging：已有匿名 mmap 预留思路，需要稳定 page fault 分配和 OOM 处理。
- 可执行内存策略：V8 初期可先走 jitless，长期需要受控 JIT 映射。

验收程序：`/bin/chromiumcaptest` 覆盖 mmap、brk、shm 基础行为。

### 2. 线程、同步与调度

Chromium base、V8、网络栈和渲染流水线依赖稳定线程模型：

- 用户线程：已有 `openos_thread_create/thread_exit/gettid`。
- futex：已有测试入口，需要覆盖 wait/wake/timeout/多等待者。
- mutex/semaphore/condition：已有基础测试入口。
- 线程局部存储 TLS：需要实现，C++ runtime 和 Chromium base 必需。
- 调度公平性和优先级：需要补足 timer、yield、sleep、nice/priority 行为。
- 栈管理：线程栈 guard page、栈溢出诊断。

验收程序：`/bin/chromiumcaptest` 覆盖线程创建、共享地址空间和 eventfd 唤醒语义。

### 3. 进程、加载器与 IPC

Chromium 默认多进程，OpenOS 必须具备可靠的用户态进程模型：

- ELF 加载：已支持用户态 ELF，需要扩展动态链接或确定静态链接策略。
- fork/exec/wait：已有测试入口，需要完善 fd 继承、COW、信号/退出状态。
- 进程间 IPC：已有 mq/shm/eventfd/socketpair 雏形，需要形成稳定 IPC 基础。
- 句柄/FD 生命周期：需要 close-on-exec、dup/dup2、poll 语义。
- 崩溃隔离：渲染进程崩溃不能拖垮浏览器主进程。

第一阶段可允许单进程 content shell；第二阶段恢复 Chromium 多进程架构。

### 4. 文件系统与资源管理

Chromium 需要真实文件系统能力：

- `open/read/write/close/lseek/stat/fstat`。
- `mkdir/unlink/rename/fsync/readdir`。
- 路径规范化、当前目录、用户目录、缓存目录。
- 大文件和稀疏文件后置。
- mmap file 后续接入资源、缓存、字体文件。

建议约定：

```text
/home/chromium/cache
/home/chromium/cookies
/home/chromium/certs
/home/chromium/downloads
/home/chromium/profile
```

### 5. 网络与安全传输

Chromium 需要完整网络基础：

- TCP socket：connect/send/recv/poll 已有，需加强非阻塞和错误码。
- DNS：已有基础能力，需要 getaddrinfo 级语义、IPv4/IPv6 后置。
- socketpair：已有用户态 API，用于本地 IPC smoke test。
- TLS/HTTPS：需要用户态 TLS 库或原生 TLS 服务；证书链、时间、根证书存储必须补。
- poll/select/epoll：已有 poll，需要扩展可扩展事件通知。Chromium 级别建议最终实现 epoll/kqueue 类事件核心。

### 6. 图形、字体与输入

Chromium 渲染链路需要：

- 软件 framebuffer surface。
- 脏矩形刷新、双缓冲。
- 鼠标、键盘、文本输入、焦点、剪贴板。
- 字体文件加载、字形 raster、字体 fallback、文本测量。
- Skia software backend 到 OpenOS window surface。

现有 GUI/font 查询接口可作为最早 smoke test，但长期需要字体服务和更完整的图形 surface API。

### 7. C/C++ 运行时与工具链

Chromium 主体是 C++，OpenOS 需要：

- 可交叉编译的 C++ runtime 策略。
- new/delete、异常策略、RTTI 策略。
- TLS、静态初始化、析构器。
- 原子操作、内存序。
- libc 字符串、stdio、errno、time、locale 子集。

建议先建立 `chromium_base_smoke`，只验证 base 风格能力，不直接拉完整 Chromium。

## 里程碑

### M0：保存当前稳定状态

- 提交当前 `/bin/browser` 网络修复。
- 保留旧浏览器作为网络调试工具。

### M1：Chromium Core Capability Test

新增 `/bin/chromiumcaptest`，作为 Chromium 底层能力的持续验收入口。

初版覆盖：

- mmap/munmap。
- mprotect。
- file-backed private snapshot mmap。
- brk/sbrk。
- thread create/gettid/yield/sleep。
- shm 双映射一致性。
- eventfd read/write。
- socketpair + poll。
- monotonic uptime。

### M2：内存系统增强

- `mprotect`。
- 固定地址 mmap。
- 文件 mmap：已完成 fd 内容到用户地址空间的基础私有快照映射，后续接入 page cache、shared/COW 和只读资源映射。
- shm 大小参数和跨进程映射。
- COW fork。

### M3：线程与 IPC 增强

- TLS。
- futex timeout 和多等待者。
- fd poll 统一。
- event loop 可承载 Chromium base message pump。

### M4：C++ Runtime Smoke

- 编译并运行 OpenOS C++ 用户程序。
- 静态构造、析构、new/delete、虚函数、原子操作测试。

### M5：Skia Software Smoke

- 在 OpenOS 窗口 surface 上完成矩形、路径、文字、图片绘制。

### M6：V8 Jitless Smoke

- 先不启用 JIT，运行简单 JavaScript。
- 后续再补可执行内存和 W^X 策略。

### M7：Chromium content shell

- 单进程、禁用 GPU、禁用 sandbox 的最小 content shell。
- 打开 `http://example.com` 并完成 Blink layout + Skia paint。

## 当前第一步

本路线的第一步是把底层能力变成可运行的系统验收，而不是只写规划：

```text
/bin/chromiumcaptest
```

后续每补一个内核核心能力，都必须加入该测试或对应专项测试，保证 Chromium 工程不会建立在不稳定基础上。
