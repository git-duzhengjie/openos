# OpenOS C/C++ Runtime 与 Chromium 工具链路线

> 目标：为 Skia、V8、Blink、Chromium 逐步提供可维护的 C/C++ 用户态运行时与宿主机构建链。

## 当前状态

- 当前用户态程序以 C 为主，使用 `crt0.c`、`user.ld` 和 `openos.h` 直接链接到 OpenOS syscall ABI。
- 当前构建环境未检测到可用的 `g++` 或 `clang++`，因此不能宣称已经具备用户态 C++ 编译和链接能力。
- 已具备 Chromium 底座所需的一批 C ABI 验收入口：内存映射、线程/TLS syscall、futex、IPC、文件系统、DNS、GUI/font smoke。

## M8 最小可交付顺序

### 1. 宿主机工具链

- 安装或引入可固定版本的 i386 交叉编译工具链。
- 优先目标：`i686-elf-gcc/g++` 或 clang + lld，输出 freestanding i386 ELF。
- 构建脚本需要显式探测 C++ 编译器，失败时给出清晰错误而不是静默跳过。

### 2. C++ ABI 最小运行时

先支持 Chromium 依赖链前置 smoke，而不是一次性追求完整 libstdc++：

- `operator new/delete`
- 全局对象构造/析构数组：`.init_array` / `.fini_array`
- 禁用异常或提供最小异常策略桩：`-fno-exceptions` 起步
- 禁用 RTTI 或提供明确策略：`-fno-rtti` 起步
- `__cxa_atexit` / `__cxa_pure_virtual` / guard variable for local statics
- 原子 builtins 与内存序基础
- thread-local storage 与现有 `SYS_TLS_SET/SYS_TLS_GET` 对接

### 3. 用户态 libc 子集扩展

- `mem*` / `str*` / `printf` 已有基础，继续补 `errno`、时间、文件、目录、socket 错误码。
- 对 Chromium base 层需要的 POSIX 子集建立 OpenOS-native 映射，不做 Linux 兼容层伪装。

### 4. C++ smoke 程序

新增 `/bin/cppsmoke`，验证：

- 构造/析构顺序
- `new/delete`
- virtual dispatch 或禁用 RTTI 策略下的最小类模型
- local static guard
- atomic increment
- TLS 读写

通过后把 smoke 结果接入 `/bin/chromiumcaptest` 或作为嵌入式子程序由其 spawn 验收。

### 5. Chromium 构建链

- 建立 GN/Ninja/Clang 宿主机构建文档。
- 起步目标不是完整浏览器，而是：
  1. `/bin/skia_demo`
  2. `/bin/v8_shell` jitless
  3. `/bin/blink_smoke`
  4. `/bin/content_shell`
  5. `/bin/chromium`

## 验收原则

- 每个 runtime 能力必须有独立 smoke 和 `/bin/chromiumcaptest` 汇总验收。
- 工具链版本、编译 flags、链接脚本变更必须写入文档并固定在构建脚本中。
- 不把“文档路线”标记为“能力完成”；只有构建和 smoke 通过后才关闭对应 M8 子项。
