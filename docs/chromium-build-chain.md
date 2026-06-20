# OpenOS Chromium GN/Ninja/Clang 构建链设计

> 目标：为后续 Skia、V8、Blink、content_shell、chromium 的分阶段移植，固定一条可重复、可诊断、OpenOS-native 的宿主机构建路线。

## 原则

- 不把 OpenOS 伪装成 Linux，也不通过 Linux 兼容层绕过底层能力缺口。
- Chromium 侧采用交叉编译目标配置，最终输出 OpenOS 用户态 ELF 或可由 OpenOS loader 装载的静态/半静态产物。
- 先构建小目标，再逐步扩大：`skia_demo` -> `v8_shell --jitless` -> `blink_smoke` -> `content_shell` -> `chromium`。
- 每个阶段必须有 OpenOS 侧 smoke 程序和 `/bin/chromiumcaptest` 关联验收。

## 宿主机工具版本固定

建议在 `toolchains/chromium/` 或外部缓存目录固定以下工具版本，并在文档/脚本中记录 checksum：

- Clang/LLVM：优先使用 Chromium 官方兼容版本，或固定一版可输出 i386 freestanding ELF 的 clang/lld。
- GN：使用 Chromium depot_tools 中的 GN，或固定独立 GN 二进制。
- Ninja：固定可重复版本。
- Python：跟随 depot_tools / Chromium 所需版本。
- sysroot：使用 OpenOS 自有用户态 headers、crt objects、linker script、最小 libc/libc++ runtime，不复用 Linux sysroot。

## 目标三元组与输出约定

初始目标：

```text
i386-openos-elf
```

建议 clang 参数基线：

```text
-target i386-openos-elf
-ffreestanding
-fno-pic -fno-pie -fno-PIE
-fno-stack-protector
-fno-exceptions
-fno-rtti
-nostdlib
-I src/user
-T src/user/user.ld
```

随着 C++ runtime 完成，再逐步打开：

- local static guard
- `.init_array` / `.fini_array`
- `operator new/delete`
- 原子 builtins
- TLS runtime glue
- 必要 libc++ 子集

## GN args 初始草案

第一阶段仅用于小型组件 smoke，不直接构建完整 Chrome：

```text
is_debug=false
is_component_build=false
is_clang=true
use_sysroot=false
target_os="openos"
target_cpu="x86"
use_custom_libcxx=false
use_allocator_shim=false
use_partition_alloc=false
v8_enable_i18n_support=false
v8_use_external_startup_data=false
v8_enable_pointer_compression=false
v8_enable_sandbox=false
v8_enable_webassembly=false
v8_jitless=true
is_official_build=false
symbol_level=1
```

后续应新增 Chromium GN toolchain 文件，例如：

```text
//build/toolchain/openos:clang_i386
```

该 toolchain 负责：

- 指向 OpenOS clang/lld wrapper。
- 注入 OpenOS headers/sysroot 路径。
- 使用 OpenOS 用户态 linker script。
- 链接 OpenOS crt0、libopenos、最小 libc/C++ runtime。

## 分阶段目标

### 1. `skia_demo`

- 仅启用软件 raster。
- 输出单个 OpenOS 用户态 ELF。
- 使用 OpenOS GUI/window surface ABI 做 present。
- 验收矩形、文本、位图 blit、dirty rect。

### 2. `v8_shell --jitless`

- 禁用 JIT、Wasm、sandbox、pointer compression。
- 先跑基础表达式、字符串、数组、对象。
- 依赖 OpenOS TLS、线程、mmap、clock、atomic、new/delete。

### 3. `blink_smoke`

- 最小 HTML/CSS 解析、layout smoke。
- 不接完整网络栈，先使用内置字符串或本地文件。
- 依赖字体测量、资源读取、Skia software surface。

### 4. `content_shell`

- 单进程。
- 禁用 GPU。
- 禁用 sandbox。
- 打开 `http://example.com`。
- 依赖 socket、DNS、poll/select、文件缓存、证书时间基础。

### 5. `/bin/chromium`

- 单窗口单标签起步。
- 地址栏、导航、刷新、错误页。
- 下载和 profile/cache 后置。

## OpenOS 侧需要提供的构建产物

```text
target/openos-user-crt0.o
target/libopenos.a
target/libopenos_c.a
target/libopenos_cxx.a
src/user/openos.h
src/user/user.ld
```

其中 C++ runtime 初期只提供最小 ABI：

- `operator new/delete`
- `__cxa_pure_virtual`
- `__cxa_atexit`
- guard variables
- atomic fallback
- TLS glue

## 验收门槛

每个阶段都必须满足：

1. 宿主机构建脚本能明确探测工具缺失，并给出下一步提示。
2. OpenOS 镜像构建通过。
3. 用户态 ELF 可嵌入 `/bin` 并启动。
4. smoke 输出明确 PASS/FAIL。
5. `/bin/chromiumcaptest` 至少覆盖该阶段新增依赖的底层 OS 能力。

## 当前状态

- 已有 `build.sh cppsmoke` 作为 C++ 工具链探测入口。
- 已有 `docs/chromium-cpp-runtime-roadmap.md` 记录 M8 runtime 顺序。
- 本文完成 GN/Ninja/Clang 构建链设计闭环；真正的 Skia/V8/Blink/Chromium 构建与运行仍属于 M9 长期任务。
