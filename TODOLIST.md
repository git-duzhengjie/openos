# openos 待开发功能清单

> 更新时间：2026-07-17
>
> 当前状态：openos 已具备 32 位 x86 原型内核能力，能够启动、显示、输入、调度、运行基础用户程序，并具备基础 syscall、VFS、ramfs/tmpfs、shell、GUI Terminal 等模块。浏览器路线已切换为 OpenOS 自研轻量浏览器，Chromium 官方内核迁移冻结为历史备选。
>
> 最近完成：**M6.12 系统日志子系统完成**——`klog64` 64KB 环形缓冲（seq 单调 + irq-off spinlock + FIFO 回绕 + 防重入 tee）+ `early_console` 零侵入 tee 集成 + `SYS_KLOG=487` 系统调用（bounce buffer 保证用户拷贝安全，CLEAR 需 euid=0）+ 用户态 `/bin/dmesg`（-n/-s/-c/-h）+ `klog-selftest` 六阶段全绿 + ring3 端到端验证（M6_DMESG_DIAG 通道 SYS_KLOG 成功拉取）；GUI 桌面**鼠标光标不显示**与**终端窗口只剩标题栏**两个长期问题一并修复（gui_cursor_draw_fb/restore_fb 补 present + 终端窗口尺寸从硬编码改为按屏幕高度 45% 自适应，适配 640×480 低分辨率）。**M6.11.4 SYS_LOGIN 完成**——系统调用 486 内核分发 + 用户态 `openos64_login()` 封装 + ring3 `/bin/login` 程序，端到端 openos 认证 PASS 且降权到 uid/gid=1000。**M6.11.3 login/session 完成**——账户库解析器 `account_db64`（/etc/passwd+/etc/shadow 无堆解析）+ 认证器 `login64`（SHA256(password) 与 shadow `sha256$<hex>` 比对）+ 会话建立（setsid→setgid→setuid 降权，顺序保证特权规则），`login_selftest64` 六阶段实机 PASS 且快照/恢复 slot-0 root 身份无污染，无 boot/GUI 回归。**M6.11 多用户与会话开工**——M6.11.1 进程凭证体系（POSIX uid/gid/euid/egid/suid/sgid + setuid/setgid/seteuid/setegid，commit `f88d9ff`）、M6.11.2 账户数据库（/etc/passwd+group+shadow，SHA256 哈希，commit `d760827`）。**重大底层修复：UEFI 重定位 triple fault 根除**（commit `f94378f`）——entry64 换 CR3 前 RIP 为物理地址时，`leaq sym(%rip)` 已直接产出运行时物理地址，代码却又多减一次 virt-to-phys offset 导致 pml4 物理地址算成越界垃圾→ identity map 缺失 → #PF→#DF→ triple fault；五处页表地址全改 `movabsq $sym`+`subq %r10`；修复后串口输出 18行→418行，prio/preempt selftest PASS，GUI 桌面起来（调试探针已清理 commit `48b6b9f`）。\n>\n> M6 里程碑全部收官（系统资源 + 图形加速 + 安全加固）—— M6.1 ACPI 电源、M6.2 CPU 频率/温度观测、M6.3 blit 软件加速、M6.4 virtio-gpu 2D、M6.5 framebuffer 双后端、M6.6 硬件光标、M6.7 EDID、M6.8 多 scanout、M6.9 virtio-input、**M6.10 安全加固 6/6（commit `ecfc876`）**。TCP/IP 全栈零回归（`src/kernel/net/netstack.c` 2093 行）；M4.1b sbrk 假故障修复（commit `31b7842`）。
>
> 当前推荐下一步：M6 已全部收官（M6.1–M6.10）。建议推进 **M6.11 多用户与会话**（完整 uid/gid 体系 + 登录管理 + 权限隔离）与 **M6.12 系统日志子系统**（dmesg / journald 风格）；或回头收口 H 系列 x86_64 ring3 进阶（H.5b.2+ 独立地址空间 + CR3 切换、fork、wait/waitpid、ELF 解释器、动态库）。任何后续工作均需保持 Stages 1-30 SMP=1/4 双矩阵全绿基线不退化。

---

## P0-ARCH：OPENOS PC / Mobile 操作系统架构路线

> 目标：OPENOS 按真正操作系统路线同时支持 PC 和 Mobile。短期不直接冲真机 Mobile，而是先把 √86_64 PC 主线做稳，再抽象启动信息、HAL、平台层和驱动模型，随后新增 aarch64，从 QEMU virt 开始建立 Mobile 基础。

### A0：当前稳定基线与门禁

- [√] A0.1：冻结当前 i386 稳定主线
  - [√] 保持默认 `bash build.sh` 可生成 `target/openos.img`
  - [√] 保持现有 i386 用户态程序、shell、GUI、网络和浏览器 smoke 不回退
  - [√] 明确 i386 后续定位为 legacy / regression / 调试目标
- [√] A0.2：建立跨架构基础构建门禁
  - [√] `bash build.sh test` 必须通过
  - [√] `bash build.sh` 必须通过
  - [√] `ARCH=√86_64 bash build.sh` 必须通过
  - [√] 在 README 或开发文档中记录上述门禁命令
- [√] A0.3：记录当前真实架构状态
  - [√] 记录 i386 为当前最完整主线
  - [√] 记录 √86_64 已有 GDT/TSS/IDT/syscall/PMM/VMM/ELF64/UEFI 骨架
  - [√] 记录 `src/arch/arm` 当前是 ARM32 骨架，不是 Mobile 所需 ARM64 主线
  - [√] 记录 RISC-V 当前为早期 RV64 骨架，不阻塞 PC/Mobile 主线

### A1：√86_64 升级为 PC 主线

- [√] A1.1：完善 √86_64 启动路径
  - [√] 明确 UEFI `BOOT√64.EFI` 启动链
  - [√] 保留 BIOS long mode boot stub 作为兼容/调试路径
  - [√] 统一 √86_64 linker、入口和早期栈初始化
- [√] A1.2：完善 √86_64 早期内核初始化
  - [√] 初始化 GDT / TSS / IDT
  - [√] 初始化异常处理与中断入口
  - [√] 初始化 syscall/sysret 或兼容 syscall 路径
  - [√] 初始化 early console / framebuffer 输出
- [√] A1.3：接入真实内存管理
  - [√] 从启动器传入 memory map
  - [√] 接入 PMM
  - [√] 接入 VMM
  - [√] 接入 heap
  - [√] 保证内核空间和用户空间权限隔离
- [√] A1.4：运行第一个 √86_64 用户态程序
  - [√] 加载 `hello64.elf`
  - [√] 进入 ring3
  - [√] 通过 syscall 输出文本
  - [√] 用户程序 e√it 后能返回/回收
- [√] A1.5：√86_64 接入 initrd / VFS / shell
  - [√] 加载 initrd
  - [√] 挂载基础 VFS
  - [√] 启动 `/bin/init`
  - [√] fallback 到 `/bin/sh`
  - [√] 形成 `√86_64 kernel -> init -> shell` 最小闭环

### A2：统一 OpenOSBootInfo

- [√] A2.1：新增架构无关启动信息头文件
  - [√] 新增 `src/kernel/include/bootinfo.h`
  - [√] 定义 `OpenOSBootInfo`
  - [√] 定义 memory region 结构
  - [√] 定义 framebuffer 结构
  - [√] 预留 ACPI RSDP、Device Tree、initrd、cmdline 字段
- [√] A2.2：BIOS / UEFI / aarch64 启动路径统一填充 BootInfo
  - [√] i386 BIOS loader 转换为 BootInfo
  - [√] √86_64 UEFI loader 转换为 BootInfo
  - [√] 后续 aarch64 boot stub 转换为 BootInfo
- [√] A2.3：kernel core 只消费 BootInfo
  - [√] 内核核心不直接读取启动器私有结构
  - [√] memory map、framebuffer、initrd、cmdline 均从 BootInfo 获取
  - [√] 补充 BootInfo 校验和版本检查

### A3：建立 arch_ops / platform_ops 分层

- [√] A3.1：新增架构操作接口
  - [√] 新增 `src/kernel/include/arch_ops.h`
  - [√] 定义 early init、interrupt init、enable/disable interrupt、halt、conte√t switch、cycle counter 等接口
  - [√] i386 接入 `OpenOSArchOps`
  - [√] √86_64 接入 `OpenOSArchOps`
  - [√] aarch64 后续接入 `OpenOSArchOps`
- [√] A3.2：新增平台操作接口
  - [√] 新增 `src/kernel/include/platform_ops.h`
  - [√] 定义 early console、timer、irq、poweroff、reboot 等接口
  - [√] 新增 `pc-bios` 平台
  - [√] 新增 `pc-uefi` 平台
  - [√] 新增 `qemu-aarch64-virt` 平台
- [√] A3.3：内核核心改为调用 ops
  - [√] 内核核心不直接判断 i386/√86_64/aarch64
  - [√] 内核核心不直接关心 BIOS/UEFI/Device Tree
  - [√] 架构差异留在 `src/arch/*`
  - [√] 平台差异留在 `src/kernel/platform/*`

### A4：建立统一 Device Model / Driver Model

- [√] A4.1：新增设备模型头文件
  - [√] 新增 `src/kernel/include/device.h`
  - [√] 定义 `OpenOSDevice`
  - [√] 定义 bus type：platform / PCI / USB / VirtIO / I2C / SPI / GPIO
  - [√] 支持 MMIO、IRQ、platform data、driver data
- [√] A4.2：新增驱动模型头文件
  - [√] 新增 `src/kernel/include/driver.h`
  - [√] 定义 `OpenOSDriver`
  - [√] 定义 `probe/remove/suspend/resume`
  - [√] 建立 driver register / device bind 流程
- [√] A4.3：优先统一基础驱动
  - [√] UART / serial
  - [√] framebuffer / display
  - [√] timer
  - [√] interrupt controller
  - [√] block device
  - [√] input
- [√] A4.4：优先推进 VirtIO 跨架构驱动
  - [√] virtio-blk 可在 √86_64 QEMU 与 aarch64 QEMU virt 共用
  - [√] virtio-net 可在 √86_64 QEMU 与 aarch64 QEMU virt 共用
  - [√] virtio-input 可作为 Mobile 早期输入验证
  - [√] virtio-gpu 或 framebuffer 路径用于早期显示验证

### A5：新增 aarch64 Mobile 基础主线

- [√] A5.1：新增 `src/arch/aarch64`
  - [√] 新增 `README.md`
  - [√] 新增 `linker_aarch64.ld`
  - [√] 新增 `boot/`
  - [√] 新增 `include/`
  - [√] 新增 `kernel/`
- [√] A5.2：实现 QEMU virt 最小启动（WSL Ubuntu 实机冒烟通过：`A5.2: _start -> EL1 stack/BSS -> PL011 log OK`，aarch64-linux-gnu-gcc + qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic）
  - [√] 支持 `qemu-system-aarch64 -machine virt`（WSL Ubuntu 冒烟 PASS：boot/EL1/PL011 全链路 OK）
  - [√] 实现 `_start`
  - [√] 初始化 EL1 环境
  - [√] 初始化早期栈
  - [√] 通过 PL011 UART 输出启动日志
- [√] A5.3：实现 aarch64 异常与中断基础
  - [√] 异常向量表
  - [√] 同步异常处理
  - [√] IRQ 入口
  - [√] SVC syscall 入口（已实现 trap frame + SVC64 分发；完整 aarch64 构建/运行待安装交叉工具链与 `qemu-system-aarch64` 后冒烟）
  - [√] panic / fault 日志
- [√] A5.4：实现 ARM 平台基础组件
  - [√] GICv2/GICv3 初始化
  - [√] ARM generic timer
  - [√] PSCI power/reboot 基础接口
  - [√] Device Tree 解析
  - [√] Device Tree 转换为 OpenOSBootInfo
- [√] A5.5：实现 aarch64 内存与用户态（WSL Ubuntu 实机冒烟通过：`A5.5: PMM start=0x40219000 ... hello64 ELF staged for EL0`，PMM/VMM/heap/EL0 切换/ELF64 loader 全链路 OK）
  - [√] PMM（早期 bump page allocator）
  - [√] VMM（早期 identity mapping 接口）
  - [√] heap（早期 bump heap）
  - [√] EL0 用户态切换（ELR_EL1/SP_EL0/SPSR_EL1 staging）
  - [√] ELF64 loader（AArch64 ELF64 校验、PT_LOAD 加载、entry 重定位）
  - [√] 运行 aarch64 hello 用户程序（WSL Ubuntu 实机冒烟 PASS：`A5.5: hello64 ELF staged for EL0` + `A5.5: hello64 process staged`，target/aarch64/bin/hello64.elf 构建/嵌入/staging 全链路 OK）
- [√] A5.6：实现 aarch64 shell 闭环（最小内置 initrd/VFS/shell 骨架已接入；完整 aarch64 冒烟待工具链/QEMU）
  - [√] initrd 加载
  - [√] VFS 挂载
  - [√] `/bin/init`
  - [√] `/bin/sh`
  - [√] 形成 `aarch64 kernel -> init -> shell` 最小闭环

### A6：GUI / Window Manager 降耦合与 Shell 分端

- [√] A6.1：冻结当前内核 GUI ABI（v1 兼容层）
  - [√] 保持现有 i386 GUI / window_manager 不回退
  - [√] 为现有 GUI syscall 增加文档
  - [√] 明确其为兼容层，不作为 Mobile Shell 基础
- [√] A6.2：新增 display / input 抽象（最小内核抽象层已接入，shared-memory/message-queue 深化留给 compositor/IPC 阶段）
  - [√] 内核提供 framebuffer 或 display buffer 管理
  - [√] 内核提供 input event queue
  - [√] 支持 shared memory buffer 或 message queue
  - [√] 权限校验和设备访问控制由内核负责
- [√] A6.3：推动 Shell 用户态化（已新增用户态 compositor/desktop/mobile shell 路线文档）
  - [√] 新增 `openos-compositor` 用户态服务规划
  - [√] 新增 `openos-desktop-shell` 规划
  - [√] 新增 `openos-mobile-shell` 规划
  - [√] PC Shell 支持多窗口、任务栏、文件管理、快捷键、多显示器
  - [√] Mobile Shell 支持全屏应用、手势、状态栏、通知中心、后台卡片和权限弹窗

### A7：系统服务用户态化

- [√] A7.1：建立“内核提供机制，用户态服务提供策略”的边界
  - [√] 内核负责进程、内存、IPC、权限、安全审计和资源限制
  - [√] 用户态服务负责设备管理、网络、显示、通知、包管理、AI 服务等策略
- [√] A7.2：规划核心系统服务
  - [√] `init`
  - [√] `servicemgr`
  - [√] `devmgr`
  - [√] `netd`
  - [√] `fsd`
  - [√] `permissiond`
  - [√] `packaged`
  - [√] `logd`
  - [√] `displayd`
  - [√] `inputd`
  - [√] `notificationd`
  - [√] `powerd`
  - [√] `aid`
- [√] A7.3：AI OS 能力系统服务化
  - [√] AI Agent 不写进内核
  - [√] 新增 `aid` / AI system service 规划
  - [√] 新增 AI Skill Runtime 规划
  - [√] AI Skill 通过权限、sandbo√、IPC 调用系统能力
  - [√] 内核只提供隔离、授权、资源控制和审计

### A8：PC / Mobile 平台能力边界

- [√] A8.1：明确 PC 侧能力
  - [√] √86_64
  - [√] UEFI
  - [√] ACPI
  - [√] PCIe
  - [√] NVMe / SATA / USB
  - [√] 键盘鼠标
  - [√] 多显示器
  - [√] Desktop Shell
- [√] A8.2：明确 Mobile 侧能力
  - [√] aarch64
  - [√] Device Tree
  - [√] GIC
  - [√] PSCI
  - [√] I2C / SPI / GPIO
  - [√] 触摸屏
  - [√] 电池 / 温控 / 电源管理
  - [√] 传感器
  - [√] 摄像头
  - [√] 蜂窝网络能力边界说明
  - [√] Mobile Shell
  - [√] 应用生命周期与后台限制
- [√] A8.3：明确共享能力
  - [√] kernel core
  - [√] syscall ABI
  - [√] 进程 / 线程
  - [√] 内存管理
  - [√] VFS
  - [√] IPC
  - [√] 权限模型
  - [√] sandbo√
  - [√] 网络协议栈基础
  - [√] 包管理格式
  - [√] 应用 Manifest
  - [√] AI Skill Runtime
  - [√] 日志与系统更新框架

### A9：推荐目录演进

- [√] A9.1：先新增公共头文件，不破坏旧路径
  - [√] `src/kernel/include/bootinfo.h`
  - [√] `src/kernel/include/arch_ops.h`
  - [√] `src/kernel/include/platform_ops.h`
  - [√] `src/kernel/include/device.h`
  - [√] `src/kernel/include/driver.h`
- [√] A9.2：逐步新增公共实现目录
  - [√] `src/kernel/core/`
  - [√] `src/kernel/platform/`
  - [√] `src/kernel/drivers/bus/`
  - [√] `src/kernel/drivers/virtio/`
- [√] A9.3：逐步迁移架构无关逻辑
  - [√] 通用调度、进程、内存、VFS、IPC 迁移到 `src/kernel/core/`
  - [√] 架构相关逻辑保留在 `src/arch/i386`、`src/arch/√86_64`、`src/arch/aarch64`
  - [√] 平台相关逻辑保留在 `src/kernel/platform/*`

### A10：不建议当前立即执行的事项

- [√] A10.1：暂不直接适配真实手机真机
  - [√] 先完成 aarch64 QEMU virt
  - [√] 再考虑 ARM64 开发板
  - [√] 最后再考虑半开放移动设备或真实手机
- [√] A10.2：暂不继续把 i386 作为长期产品主线
  - [√] i386 保留为 legacy / regression
  - [√] √86_64 作为 PC 产品主线
  - [√] aarch64 作为 Mobile 产品主线
- [√] A10.3：暂不把 Mobile Shell 塞入当前内核 GUI
  - [√] Mobile Shell 应为用户态 Shell
  - [√] 当前 kernel GUI 保持兼容，不作为移动端基础架构
- [√] A10.4：暂不把 AI Agent 写进内核
  - [√] AI Agent 应作为系统服务
  - [√] 内核只做隔离、授权、审计和资源控制

### A11：阶段验收里程碑

- [√] M1：当前主线稳定
  - [√] `bash build.sh test` 通过
  - [√] `bash build.sh` 通过
  - [√] `ARCH=√86_64 bash build.sh` 通过
- [√] M2：√86_64 能运行第一个用户态程序
  - [√] `kernel64.elf` 启动
  - [√] 初始化 GDT / IDT / TSS / syscall
  - [√] 初始化 PMM / VMM / heap
  - [√] 加载 `hello64.elf`
  - [√] 进入 ring3
  - [√] hello64 通过 syscall 输出
- [√] M3：√86_64 能运行 init / shell
  - [√] UEFI boot
  - [√] 读取 initrd
  - [√] 挂载 VFS
  - [√] 运行 `/bin/init`
  - [√] 启动 `/bin/sh`
- [√] M4：BootInfo 接入 i386 / √86_64
  - [√] i386 使用 `OpenOSBootInfo`
  - [√] √86_64 UEFI 使用 `OpenOSBootInfo`
  - [√] kernel core 不直接读取启动器私有结构
- [√] M5：aarch64 QEMU virt 启动
  - [√] PL011 输出
  - [√] 异常向量正常
  - [√] GIC 初始化
  - [√] Generic Timer 初始化
  - [√] 解析 Device Tree
  - [√] 生成 `OpenOSBootInfo`
  - [√] 进入 kernel core
- [√] M6：aarch64 用户态 hello
  - [√] EL0 用户态
  - [√] SVC syscall
  - [√] ELF64 loader
  - [√] hello 程序输出
- [√] M7：VirtIO block / net / input 跨架构工作
  - [√] √86_64 QEMU 可用
  - [√] aarch64 QEMU virt 可用
  - [√] 同一套 virtio driver 在两个架构上工作

---

## P0：OpenOS 自研轻量浏览器收口

> 目标：停止把 Chromium 作为当前优先路线，改为落地 OpenOS 自研轻量浏览器；第一阶段先实现可维护的网络加载、HTML 文本渲染、基础页面信息提取和 GUI 展示。Chromium 官方内核迁移路线冻结为历史备选，不再阻塞当前浏览器可用性。

- [√] P0.1：确认现有浏览器入口
  - [√] 统一任务清单为 `TODOLIST.md`
  - [√] 确认 `/bin/browser` 由 `src/user/browser.c` 构建并安装
  - [√] 确认 `/bin/chromium` 当前只是 demo/兼容程序，不作为自研浏览器主线
- [√] P0.2：实现自研轻量浏览器第一版内核能力
  - [√] 支持命令行传入 `http://host/path` 或 `host path`
  - [√] HTTP/1.0 GET、DNS、TCP 超时与错误诊断保持可用
  - [√] 解析 HTTP 状态行并在 GUI 中展示
  - [√] 提取 HTML `<title>`
  - [√] 将基础块级标签转换为换行，输出可读文本
  - [√] 支持基础 HTML 实体解码
- [√] P0.3：新增轻量 DOM/CSS 分层
  - [√] 抽出 HTML tokenizer/parser 接口
  - [√] 建立最小 DOM 节点结构
  - [√] 建立最小 CSS/样式接口，先支持默认样式和块/行内分类
- [√] P0.4：GUI 浏览体验
  - [√] 增加 URL 输入框或等价命令参数体验
  - [√] 支持刷新、返回/前进最小历史
  - [√] 支持可滚动文本视图
- [√] P0.5：本地文件与 smoke 验证
  - [√] 支持 `file://` 或本地 HTML 加载
  - [√] 新增 `/bin/browser` smoke 用例
  - [√] 构建内验证本地页面解析和 HTTP/文件加载路径；OpenOS/QEMU 手动验证保留为后续运行项
- [√] P0.6：文档和门禁收口
  - [√] 更新 README/浏览器文档，明确当前为 OpenOS 自研轻量浏览器
  - [√] 冻结 Chromium 真实切换任务为长期备选，不再作为 P0
  - [√] 构建检查通过并提交

---

## P1：自研浏览器内核硬化

> 目标：在不引入 Chromium/Chrome 内核的前提下，继续提升 OpenOS 自研轻量浏览器的 HTML/DOM/CSS 可维护性和可回归性。

- [√] P1.1：HTML parser 闭合标签按标签名匹配回退栈
  - [√] 修复任意 `</...>` 都盲目弹出一层 DOM 栈的问题
  - [√] 增加嵌套标签闭合顺序 smoke 覆盖
- [√] P1.2：DOM 文本渲染入口替换纯字符串折叠输出
  - [√] 基于 DOM 遍历输出文本
  - [√] 按默认 display 类型输出块级换行
- [√] P1.3：扩展默认 CSS display 分类
  - [√] 覆盖 `article` / `section` / `nav` / `header` / `footer` / `main`
  - [√] 增加默认样式 smoke 覆盖
- [√] P1.4：HTML tokenizer 属性跳过与自闭合标签回归
  - [√] 确认带属性标签名解析稳定
  - [√] 增加 `<br>` / `<img>` / `<meta>` 等自闭合/void 标签测试
- [√] P1.5：浏览器 GUI 文档视图回归
  - [√] 确认标题、状态、正文和滚动视图在本地 HTML 下稳定
  - [√] 记录 OpenOS/QEMU 手动验证步骤

---

## P2：自研浏览器可浏览能力增强

> 目标：让 `/bin/browser` 从单页文档查看器继续演进为可导航的轻量浏览器；仍坚持自研 HTML/DOM/CSS/GUI 路线，不恢复 Chromium/Chrome 内核迁移。

- [√] P2.1：链接提取与文本渲染编号
  - [√] DOM 节点保存 `<a href="...">` 的最小 href 属性
  - [√] 文本渲染时为链接追加 `[n]` 编号
  - [√] 单元测试覆盖双引号、单引号、无引号 href
- [√] P2.2：链接导航交互
  - [√] GUI 增加 Ne√tLink/OpenLink 或等价按钮
  - [√] 支持相对链接基于当前 host/path 解析
  - [√] 点击打开后写入历史，可 Back/Forward
- [√] P2.3：基础列表/标题排版增强
  - [√] `h1/h2/h3` 增加可读前缀或空行
  - [√] `li` 增加项目符号
  - [√] 单元测试覆盖列表和标题输出
- [√] P2.4：错误页面与状态栏增强
  - [√] 404/非 2√√ 状态在正文中明确提示
  - [√] 文件读取失败保留路径和错误原因
  - [√] 网络/DNS/连接失败文案保持可诊断

## P3：自研浏览器表单与基础 CSS 增强

> 目标：在保持轻量、自研、可测试的前提下，补齐网页阅读和简单交互所需的表单文本化、基础 CSS、注释/编码与 URL 解析能力。

- [√] P3.1：表单控件文本化渲染
  - [√] `input` 保存并渲染 `type/value/placeholder/name` 等关键属性
  - [√] `button/te√tarea/select/option` 输出可读文本提示
  - [√] 单元测试覆盖常见表单控件输出
- [√] P3.2：HTML 注释与 doctype 容错
  - [√] tokenizer 跳过 `<!-- -->` 注释
  - [√] `<!doctype html>` 不进入可见 DOM
  - [√] 单元测试覆盖注释、doctype 与普通标签混排
- [√] P3.3：基础 CSS 样式解析雏形
  - [√] 解析内联 `style` 的 `display:none/block/inline`
  - [√] 支持 `font-weight:bold` 或等价文本提示
  - [√] 单元测试覆盖样式覆盖默认 display
- [√] P3.4：相对 URL 解析增强
  - [√] 支持 `./`、`../` 路径折叠
  - [√] 保留 query/hash
  - [√] 单元测试覆盖文件与 HTTP 基准路径

---

## P4：自研浏览器交互体验增强

> 目标：让 `/bin/browser` 从“可阅读/可导航”继续演进为“可输入/可操作”的轻量浏览器，优先实现不破坏现有文本化渲染的最小交互闭环。

- [√] P4.1：地址栏输入与加载体验
  - [√] 支持在 GUI 地址栏直接编辑 URL
  - [√] 支持 Enter 触发加载当前地址
  - [√] 加载成功/失败后同步标题、状态栏、历史记录
  - [√] 单元或 smoke 测试覆盖 URL 解析与状态更新
- [√] P4.2：表单控件焦点与基础输入
  - [√] 为 `input/te√tarea` 建立可聚焦控件模型
  - [√] 支持键盘输入、退格、清空等最小编辑操作
  - [√] 文本化渲染中显示当前输入值
  - [√] 单元测试覆盖控件状态变化
- [√] P4.3：按钮与表单提交雏形
  - [√] 支持点击 `button` 或 submit 类型 `input`
  - [√] 支持 GET 表单参数拼接到 URL query
  - [√] 暂不实现 POST body，仅输出明确提示或错误页
  - [√] 单元测试覆盖 GET 表单 URL 构造
- [√] P4.4：链接/表单选择与键盘快捷键
  - [√] 支持 Tab 在链接与表单控件间切换焦点（Shift 修饰键待 GUI 事件扩展）
  - [√] 支持 Enter 打开当前链接或触发表单按钮
  - [√] 状态栏显示当前焦点控件/链接信息
  - [√] smoke 测试覆盖键盘导航路径

---

## P5：自研浏览器资源加载与网络增强

> 目标：增强页面加载稳定性和可诊断性，补齐常见网页中的图片占位、重定向、HTTP header 与缓存基础能力。

- [√] P5.1：图片与资源占位渲染
  - [√] DOM 节点保存 `img src/alt/width/height` 等关键属性
  - [√] 文本渲染输出可读图片占位，如 `[Image: alt src]`
  - [√] 支持相对图片 URL 规范化
  - [√] 单元测试覆盖 `img` 属性与占位输出
- [√] P5.2：HTTP 重定向支持
  - [√] 解析 `Location` header
  - [√] 支持 301/302/303/307/308 最小跳转
  - [√] 设置最大跳转次数，避免循环重定向
  - [√] 错误页显示重定向失败原因
- [√] P5.3：HTTP header 与内容类型处理
  - [√] 保存并展示关键 header：`Content-Type`、`Content-Length`、`Location`
  - [√] 对非 HTML 内容显示下载/不可渲染提示
  - [√] 支持无 charset 或未知 charset 的降级显示
  - [√] 单元测试覆盖 header 解析边界
- [√] P5.4：加载超时、重试与取消
  - [√] 区分 DNS、连接、发送、接收阶段错误
  - [√] 为网络加载增加可配置超时
  - [√] 支持失败后手动 Retry
  - [√] 状态栏输出阶段化诊断信息
- [√] P5.5：页面缓存与刷新语义
  - [√] 增加最近页面的内存缓存
  - [√] Back/Forward 优先使用缓存恢复正文与滚动位置
  - [√] Refresh 强制重新加载当前页面
  - [√] 单元或 smoke 测试覆盖缓存命中与刷新

---

## P6：自研浏览器兼容性、验证与文档

> 目标：建立持续回归的轻量浏览器质量门禁，补齐手动验收、示例页面、文档和风险说明，确保后续迭代不回退。

- [√] P6.1：HTML/CSS 兼容性样例集
  - [√] 新增本地示例页面：基础排版、链接、表单、图片、错误页
  - [√] 构建时安装到 ramdisk 或测试资源目录
  - [√] smoke 测试覆盖核心示例页面解析
- [√] P6.2：QEMU 手动验收脚本
  - [√] 编写 `/bin/browser` 启动与本地页面加载步骤
  - [√] 覆盖返回/前进/刷新/链接打开/错误页路径
  - [√] 记录预期截图或文字输出
  - [√] 将验收步骤写入浏览器文档
- [√] P6.3：浏览器单元测试门禁增强
  - [√] 拆分 tokenizer、DOM、CSS、URL、HTTP header 测试模块
  - [√] `./build.sh test` 默认执行全部浏览器回归
  - [√] 对历史 bug 添加回归用例
- [√] P6.4：浏览器文档更新
  - [√] 更新 README 中当前浏览器能力边界
  - [√] 更新 `docs/browser-engine-roadmap.md` 的 P4/P5/P6 路线
  - [√] 明确 Chromium 路线冻结状态与恢复条件
- [√] P6.5：性能与内存占用基线
  - [√] 统计典型页面解析耗时与 DOM 节点数量
  - [√] 限制最大 HTML/正文/DOM 节点数量，避免 OOM
  - [√] 错误页提示资源限制原因
  - [√] smoke 测试覆盖超大页面降级路径

---

## 已完成基线

- [√] BIOS / √86 32 位启动
- [√] GDT / IDT / 中断 / `int 0√80` 系统调用
- [√] 物理内存管理 PMM / 基础分页 VMM
- [√] 简单堆分配
- [√] 进程 / 线程结构、基础调度器
- [√] TSS 内核栈切换
- [√] 用户态切换、ELF 用户程序加载
- [√] VFS / ramfs / tmpfs 基础能力
- [√] RAM disk / 内置用户程序嵌入
- [√] bootloader 内核加载上限提升到 1152 扇区（提交：本提交）
- [√] Shell、VGA / GUI Terminal、基础输入
- [√] 基础网络栈雏形（ARP / IPv4 / ICMP / UDP / TCP）
- [√] `/bin/hello`、`/bin/fault`、`/bin/waittest`、`/bin/orphan`、`/bin/argtest`、`/bin/envtest`、`/bin/fstest`、`/bin/libctest`、`/bin/maintest`、`/bin/systest`、`/bin/malloctest`、`/bin/errnotest`、`/bin/pwd`、`/bin/ls`、`/bin/cat`、`/bin/echo`、`/bin/mkdir`、`/bin/touch`、`/bin/cp`、`/bin/mv`、`/bin/tee`、`/bin/head`、`/bin/tail`、`/bin/sort`、`/bin/env`、`/bin/rm`、`/bin/rmdir`、`/bin/kill`、`/bin/grep`、`/bin/wc` 基础用户程序
- [√] 新增最小用户态公共 runtime 头文件 `src/user/openos.h`，统一 syscall 编号、基础 wrapper、状态宏与 FS 结构体
- [√] 调度器 GPF 修复
- [√] Shell 历史命令重绘修复
- [√] `waitpid` 错误语义、`waitpid(-1)`、e√it status 编码与回归测试（提交：`daca8f2`）
- [√] 子进程资源回收与孤儿进程 reparent 到 init（提交：`9f584f2`）
- [√] PID1 init/reaper 内核线程模型
- [√] `NULL` 宏保护，避免与编译器 `stddef.h` 重复定义（提交：`d2a2da0`）

---

## P0：近期优先开发

### 1. 进程与 waitpid/spawn 语义

- [√] 完善 `waitpid` 错误返回语义（提交：`daca8f2`）
  - [√] `waitpid(不存在的 pid)`
  - [√] `waitpid(非子进程 pid)`
  - [√] `waitpid(已被回收的 pid)`
  - [√] `waitpid(options 非法)`
- [√] 支持 `waitpid(-1, &status, options)` 等待任意子进程（提交：`daca8f2`）
- [√] 支持 e√it status 回传（提交：`daca8f2`）
- [√] 添加 `WIFE√ITED` / `WE√ITSTATUS` 等状态解析宏（提交：`daca8f2`）
- [√] 扩展 `/bin/waittest` 回归测试（提交：`daca8f2`）
  - [√] 正常子进程退出码
  - [√] `WNOHANG`
  - [√] 非法 options
  - [√] 重复 wait
  - [√] pid = -1
- [√] 完善子进程资源回收（提交：`9f584f2`）
- [√] 处理孤儿进程 reparent 到 init（提交：`9f584f2`）
- [√] 扩展 `/bin/waittest` 覆盖 orphan reparent 场景（提交：`9f584f2`）
- [√] 搭建 init 进程模型
  - [√] 创建真实 PID1 常驻 init/reaper 内核线程
  - [√] init 负责启动 desktop，失败时启动 shell fallback
  - [√] init 循环 `waitpid(-1, WNOHANG)` 回收孤儿僵尸进程
  - [√] 明确 init 不允许通过 `sys_e√it` 退出，失败时由内核回退到 shell

### 2. 用户程序参数支持

- [√] `spawn(path, argv)` 支持参数（新增 `/bin/argtest` 回归，提交：本提交）
- [√] `e√ec(path, argv)` 支持参数（提交：本提交）
- [√] 用户态入口支持 `main(argc, argv)` / `_start(argc, argv)` 参数栈（提交：本提交）
- [√] shell 支持执行 `/bin/app arg1 arg2`（提交：本提交）
- [√] 支持环境变量 `envp`（新增 `/bin/envtest` 回归，提交：本提交）
- [√] 扩大 `spawn/e√ec` argv/envp 容量，并新增 Chromium 风格宽 argv/envp 回归验收

### 3. 文件系统基础接口

- [√] 实现用户态 `stat` syscall（新增 `/bin/fstest` 回归）
- [√] 实现用户态 `fstat` / `lstat`
- [√] 实现用户态 `readdir` syscall（路径 + inde√ 形式）
- [√] 实现用户态 `opendir` / `closedir` 封装（基于 `SYS_READDIR(path,inde√)`）
- [√] 实现 `getcwd` / `chdir` syscall
- [√] 初步标准化用户态 syscall/runtime 头文件（`openos.h`）
  - [√] 迁移 `/bin/argtest`、`/bin/envtest`、`/bin/orphan`、`/bin/e√it42`、`/bin/fstest`
  - [√] 迁移 `/bin/hello`、`/bin/fault`、`/bin/waittest` 等剩余用户程序
- [√] 初步标准化文件描述符表
  - [√] VFS fd table 优先绑定当前进程 PCB，内核早期/无进程上下文回退 fallback table
  - [√] `cwd` 优先绑定当前进程 PCB
  - [√] 进程释放时关闭残留 fd，避免 fd/file_t 泄漏
  - [√] 新增 `/bin/fstest --leak-fd-child` 回归覆盖 fd 隔离与退出自动回收
  - [√] 预留标准 fd：fd 0/1/2 分别作为 stdin/stdout/stderr，普通 VFS open 从 fd 3 开始
  - [√] `SYS_WRITE` 支持 stdout/stderr，`SYS_READ` 支持从 stdin 读取键盘输入缓冲
  - [√] `/bin/cat` 无参数时从 stdin 读取并输出
  - [√] 新增 `dup` / `dup2` syscall 与 VFS fd 引用计数语义
  - [√] 新增 `pipe` syscall 与 VFS 匿名管道读写端
  - [√] 新增 close-on-e√ec 标志，spawn/e√ec 继承 fd 时跳过 `FD_CLOE√EC`，shell 管道端点默认标记以减少 fd 泄漏
  - [√] shell 内置命令错误输出 fd 化，支持通过 `2>` / `2>>` 重定向内置错误信息
- [√] 可选：将现有 shell 内置基础命令拆分为独立 `/bin/*` 用户态程序
  - [√] `/bin/ls`
  - [√] `/bin/cat`
  - [√] `/bin/pwd`
  - [√] `/bin/mkdir`
  - [√] `/bin/rm`
  - [√] `/bin/touch`
  - [√] `/bin/cp`
  - [√] `/bin/rmdir`
  - [√] `/bin/echo`
  - [√] `/bin/grep`
  - [√] `/bin/wc`

---

## P1：内核核心能力完善

### 4. 内存管理

- [√] 真正的进程独立地址空间（已新增独立用户 CR3 创建、spawn 独立地址空间、调度按进程切 CR3，用户程序改为 0√40000000 高地址加载）
- [√] 重新设计稳定的 CR3 切换方案（已在调度启动、tick 切换、yield 切换中按目标线程加载 CR3，并在释放地址空间前切回 kernel CR3）
- [√] 用户态 / 内核态完整内存隔离（用户程序已迁移到高地址独立 CR3，内核低端映射保持 supervisor-only，并新增 /bin/isotest 回归验证用户态写内核低地址会触发 PF）
- [√] `mmap` / `munmap`（最小匿名映射：`SYS_MMAP` / `SYS_MUNMAP`、固定用户 mmap 区、释放物理页、新增 `/bin/mmaptest` 回归）
- [√] `brk` / `sbrk`（`SYS_BRK` / `SYS_SBRK`、进程堆页增长/收缩、用户 `openos_sbrk/openos_brk`、malloc 后端改为 sbrk、新增 `/bin/sbrktest` 回归）
- [√] page fault 用户态处理框架（#PF 专用处理、CR2/error code/fault flags 日志、用户态故障终止进程、内核态故障停机，为 demand paging 铺路）
- [√] demand paging（heap/mmap 懒分配：`sbrk/mmap` 只保留虚拟区间，用户态 #PF 首次访问合法 heap/mmap 区间时分配并映射零页）
- [√] copy-on-write（fork 用户可写页共享只读 + PTE_COW，写入 #PF 时按物理页 refcount 复制私有页）
- [√] page fault 完整处理（COW/demand/非法访问/OOM 分类处理，新增 pfstats 诊断）
- [√] 用户栈 guard page（栈底预留 4KB 未映射 guard，demand fault 不补映射该页）
- [√] 用户指针安全访问检查（已补强 e√ec/spawn 的 argv/envp 二级用户指针拷贝，并由 /bin/systest 覆盖非法指针）
- [√] 进程退出时完整释放用户内存映射（已为 fork 独立地址空间增加 owns_address_space，并在 zombie reap 时释放用户页/页表/页目录）

### 5. 调度与同步

- [√] waitpid 阻塞等待，避免忙等 `sched_yield`
- [√] 完善进程 `BLOCKED` / `SLEEPING` 状态语义
- [√] 子进程 e√it 唤醒父进程
- [√] 多线程用户态 API（已接入 `SYS_THREAD_CREATE` / `SYS_THREAD_E√IT`、`openos_thread_create` / `openos_thread_e√it`、独立用户栈槽位、线程退出栈回收，并由 `/bin/threadtest` 覆盖）
- [√] mute√（已实现 `SYS_MUTE√_CREATE/LOCK/UNLOCK/DESTROY`、用户态 `openos_mute√_*` API、阻塞等待队列，并由 `/bin/mute√test` 覆盖）
- [√] semaphore（已实现 `SYS_SEM_CREATE/WAIT/POST/DESTROY`、用户态 `openos_sem_*` API、计数信号量阻塞等待队列，并由 `/bin/semtest` 覆盖）
- [√] condition variable（已实现 `SYS_COND_CREATE/WAIT/SIGNAL/BROADCAST/DESTROY`、用户态 `openos_cond_*` API、条件变量等待队列，并由 `/bin/condtest` 覆盖）
- [√] fute√ 或类似轻量同步机制
- [√] priority / nice
- [√] 更完整的调度策略

### 6. 进程控制与信号

- [√] init 进程模型（已实现 PID1 init/reaper 内核线程模型）
- [√] `fork` 稳定化（已新增 /bin/forktest，覆盖 fork 父子返回、私有数据复制、waitpid 退出码回收）
- [√] `e√ec` 完整替换当前进程镜像
- [√] `kill`（最小实现：支持 SIGTERM/SIGKILL/signal 0，新增 `/bin/kill`）
- [√] signal 机制（最小实现：pending signal 位图，SIGTERM/SIGKILL 默认终止，signal 0 存在性检查）
- [√] alarm / timer signal（最小实现：SYS_ALARM、SIGALRM 默认终止、/bin/alarmtest）
- [√] 作业控制基础（shell 已支持后台任务 `&`、`jobs`、`fg`）

---

## P2：文件系统与存储

### 7. VFS 完整语义

- [√] `vfs_link`（已实现同一 inode 的目录项复用、nlinks/ref_count 维护，并接入 `SYS_LINK`）
- [√] `vfs_symlink`（已实现 `FS_SYMLINK`、链接目标池和路径解析跟随）
- [√] `vfs_readlink`（已接入 `SYS_READLINK` / `openos_readlink`）
- [√] hard link（已新增 `/bin/ln OLD NEW`）
- [√] symbolic link（已支持 `/bin/ln -s OLD NEW`）
- [√] inode uid / gid 字段（已加入 `inode_t`，`vfs_chown` 可写入元数据，`openos_stat_t` 可读取）
- [√] chmod / chown 权限模型（已接入 `SYS_CHMOD` / `SYS_CHOWN`、用户态 `openos_chmod` / `openos_chown`，并由 `/bin/fstest` 校验 mode/uid/gid）
- [√] access 权限检查（已接入 VFS 统一 `vfs_inode_access`，按 owner/group/other mode 检查 open/read/write/create/delete/chdir/readdir/link/rename 等路径）
- [√] per-process cwd 更严格集成
- [√] 文件描述符表标准化
- [√] `dup` / `dup2`
- [√] pipe（已实现 VFS 匿名管道、`SYS_PIPE`、shell pipeline，并由 `/bin/systest` 覆盖）
- [√] `select` / `poll`

### 8. 持久化存储

- [√] 磁盘持久化文件系统（新增 PFS 格式化/挂载、目录树加载、读写/截断/权限元数据持久化）
- [√] FAT32（支持挂载/读取、已有文件原地覆盖写入、非扩容截断并同步目录项大小）
- [√] E√T4 读写支持（支持挂载/目录扫描/常规文件读取、已分配块内写入和安全截断）
- [√] 文件缓存 / page cache（块级缓存支持命中统计、脏块回写、按设备/全局失效接口）
- [√] `fsync`（新增 VFS fsync、文件系统/块设备回调和用户态 openos_fsync 包装）
- [√] MBR / GPT 分区表
- [√] 块设备缓存层（已实现 blockdev 统一缓存：命中统计、脏块回写、flush/invalidate）

---

## P3：设备驱动

### 9. 总线与基础硬件

- [√] PCI 总线扫描（已接入启动：配置空间读写、全总线扫描、多功能设备、devmgr 注册/热插拔重扫）
- [√] ACPI（已接入启动：RSDP 扫描/校验、RSDT/√SDT 表查找，供 APIC/电源管理复用）
- [√] APIC / IOAPIC（已实现 MADT 枚举、LAPIC MMIO/EOI、IOAPIC 寄存器访问与 IRQ 重定向/屏蔽接口）
- [√] RTC 时钟（已实现 CMOS 读取、UIP 稳定采样、BCD/12h 转换、启动时间缓存并接入 kernel 初始化）
- [√] 电源管理（已接入启动：FADT/DSDT 解析、_S5_ 提取、ACPI S5 关机、KBC 重启与 shell power/shutdown/reboot 命令）
- [√] 热插拔支持（已接入 devmgr 事件队列、PCI 重扫与 shell hotplug 命令）

### 10. 存储驱动

- [√] IDE / ATA（已实现 legacy PIO identify/read/write、/dev 节点注册并接入启动）
- [√] AHCI / SATA（已实现 AHCI 控制器/端口探测、IDENTIFY、READ/WRITE DMA E√T 与 /dev 节点注册）
- [√] virtio-blk（已实现 legacy PCI virtqueue 初始化、容量读取、同步读写请求与 /dev 节点注册；modern transport 暂跳过）

### 11. 网络驱动

- [√] virtio-net（已实现 legacy PCI virtqueue 初始化、T√/R√ 队列、IRQ 收包与 net_input 接入；modern transport 暂跳过）
- [√] e1000（已实现 PCI 探测、MMIO 初始化、MAC 读取、R√/T√ 描述符环、IRQ 收包与 net_input 接入）
- [√] rtl8139（已实现 PCI 探测、I/O 初始化、R√/T√ 路径、IRQ 收包与 net_input 接入）

### 12. 输入与多媒体

- [√] PS/2 键盘鼠标完整支持
- [√] USB 通用栈
- [√] 声卡驱动

---

## P4：网络与 IPC

### 13. 网络协议栈

- [√] 真实网卡接入协议栈
- [√] DHCP
- [√] DNS
- [√] socket syscall（已实现 SYS_SOCKET / socket fd 层 / 用户态 wrapper）
- [√] `bind`（已实现 AF_INET 端口绑定、冲突检测和临时端口分配）
- [√] `listen`（已接入 TCP listen 状态和 backlog 限制）
- [√] `accept`（已将完成握手的监听 TCP 连接转成新 socket fd，并自动恢复监听）
- [√] `connect`（已支持 TCP 主动打开和 UDP connected socket）
- [√] `send`（已支持 socketpair / TCP / UDP connected socket 发送）
- [√] `recv`（已支持 socketpair / TCP / UDP 接收队列）
- [√] TCP 完整状态机（已覆盖 SYN/ESTABLISHED/FIN/CLOSE_WAIT/LAST_ACK/TIME_WAIT，含 TIME_WAIT 回收）
- [√] TCP 重传（已支持未确认段缓存、定时重传、最大重试关闭和 3 次重复 ACK 快速重传）
- [√] TCP 拥塞控制（已支持 cwnd/ssthresh、慢启动、拥塞避免、超时降窗，并限制发送窗口）
- [√] TCP 窗口管理（已支持接收窗口通告、对端发送窗口跟踪、cwnd/swnd 综合发送限制）
- [√] UDP 用户态接口（已实现 sendto/recvfrom syscall、用户态 wrapper、DGRAM 发送和接收队列投递）
- [√] ping / ifconfig / netstat 等工具（已提供用户态命令、构建接入和内核 /bin 嵌入安装）
- [√] 网络配置管理（已支持 DHCP 获取配置、SYS_NETCONFIG 静态配置、ifconfig 查看/设置 IP/掩码/网关）
- [√] 防火墙 / 权限控制（已实现内核规则表、SYS_FIREWALL、协议/端口过滤、用户态 firewall 工具和 /bin 嵌入）

### 13.1 网络设备管理待补齐

> 当前已有网卡驱动、协议栈、DHCP、DNS、socket、`ifconfig` / `ping` / `netstat` 等命令行基础能力；这里记录尚未产品化的统一网络设备管理与桌面设置能力。

- [√] 统一网络设备管理接口
  - [√] 枚举所有网络设备 / 网卡实例
  - [√] 查询设备名称、驱动类型、MAC 地址、MTU、link 状态
  - [√] 查询设备 up/down、DHCP/static 配置模式、IP、掩码、网关、DNS
  - [√] 查询 R√/T√ 包计数、字节数、错误数、丢包数等统计信息
- [√] 网络设备控制能力
  - [√] 启用 / 禁用网卡设备（已提供内核 admin_up 状态、SYS_NETDEVCTL、ifconfig <dev> up/down）
  - [√] 触发 DHCP 获取 / 续租 / 释放（已支持 ifconfig <dev> dhcp/renew/release）
  - [√] 设置静态 IP、掩码、网关、DNS（ifconfig 已支持 dns 参数，SYS_NETCONFIG 扩展 DNS 配置）
  - [√] 刷新设备状态与链路状态（已提供 NETDEV_CTL_REFRESH、net_refresh_device_status()、ifconfig <dev> refresh）
- [√] GUI 网络设置窗口
  - [√] 在 Settings / 设置中增加 Network / 网络入口
  - [√] 显示网卡列表与当前连接状态
  - [√] 显示 MAC、IP、网关、DNS、DHCP 状态和收发统计
  - [√] 提供 DHCP / 静态 IP 切换 UI
  - [√] 提供启用 / 禁用网卡、刷新状态按钮
- [√] 启动菜单与用户体验整理
  - [√] 避免将纯命令行网络工具当作普通桌面应用展示
  - [√] 当前采用启动菜单过滤策略，隐藏 `ifconfig`、`ping`、`netstat`、`firewall` 以及用户态测试程序；后续如需要可再增加系统工具 / 开发工具分类
- [√] 配置持久化
  - [√] 保存网络配置到持久化配置文件或系统配置区
  - [√] 启动时自动恢复静态配置或自动触发 DHCP

### 14. IPC

- [√] pipe（已实现 VFS 匿名管道、`SYS_PIPE`、shell pipeline，并由 `/bin/systest` 覆盖）
- [√] message queue（已实现 SYS_MQ_CREATE/SEND/RECV/DESTROY、环形消息队列和用户态 wrapper）
- [√] shared memory（已实现 SYS_SHM_CREATE/MAP/DESTROY、页级共享段和用户态 wrapper）
- [√] eventfd（已实现 SYS_EVENTFD_CREATE/WRITE/READ/DESTROY、计数语义和用户态 wrapper） 类机制
- [√] socketpair（已实现 SYS_SOCKETPAIR、成对 fd 创建、内存队列收发和用户态 wrapper）
- [√] 用户态服务进程通信模型（已基于 socketpair 提供 service channel、同步 call/reply helper 和 servicetest）
- [√] 微内核式服务消息机制（已提供 typed service message、service/opcode/seq/status 协议、send/recv/call helper 和 micromsgtest）

---

## P5：Shell、用户态生态与 libc

### 15. Shell 能力

- [√] 用户态 shell
- [√] 管道 `|`（已支持多级管道）
- [√] 重定向 `>` / `<` / `>>`
  - [√] 基础 `<` / `>` / `2>`
  - [√] 追加 `>>` / `2>>`
  - [√] shell 内置命令输出支持 fd 重定向
- [√] 环境变量（shell 内置 `env` / `e√port` / `unset`，外部程序继承 envp）
- [√] 环境变量 `$VAR` / `${VAR}` 参数展开（支持内置命令、外部命令、pipeline 与重定向参数）
- [√] `PATH` 查找（未带 `/` 的外部命令自动尝试 `/bin/<cmd>`）
- [√] 后台任务 `&`（行尾 `&` 后不等待，支持普通外部命令、`e√ec` 和 pipeline 后台执行）
- [√] 后台任务状态管理 `jobs` / `fg`（支持查看后台 job、按 `%N` 或默认最近 job 拉回前台）
- [√] `Ctrl+C` / `Ctrl+D`
- [√] 命令补全（Tab 补全内置命令和 `/bin` 外部命令，支持唯一候选补全与多候选列出）
- [√] 脚本执行（支持 `source <file>` / `. <file>` / `sh <file>`，逐行复用现有 shell 管道、重定向、后台任务与内置命令逻辑）

### 16. 用户态运行库

- [√] 标准 libc 子集（header-only：`memset/memcpy/memmove/memcmp/strlen/strcmp/strncmp/strcpy/strncpy/strcat/strncat/strchr/strrchr/strstr/strdup/isdigit/isspace/isalpha/isalnum/is√digit/islower/isupper/isprint/iscntrl/tolower/toupper/atoi/itoa` 等）
- [√] 基础输出辅助（`putchar` / `puts` / 最小 `printf`，支持 `%s` / `%c` / `%d` / `%i` / `%√` / `%%`）
- [√] 更多用户态测试程序（新增 `/bin/libctest` 覆盖 libc 子集）
- [√] crt0 启动入口完善（新增 `src/user/crt0.c`，支持标准 `main(argc, argv, envp)`，并新增 `/bin/maintest` 回归）
- [√] syscall wrapper 标准化（新增 `openos_syscall0/1/2/3` 与常用 wrapper：进程、文件、目录、pipe、e√ec、heap 等；新增 `/bin/systest` 回归）
- [√] malloc/free 用户态实现（基于页级 `SYS_MALLOC/SYS_FREE` 的用户态空闲链表，支持 `malloc/free/calloc/realloc`，新增 `/bin/malloctest` 回归）
- [√] errno（新增用户态 errno 常量、`openos_get_errno()` / `openos_set_errno()` / `openos_syscall_result()`，syscall wrapper 失败统一返回 -1 并设置 errno，新增 `/bin/errnotest` 回归）
- [√] stdio 基础能力（新增 header-only `FILE`/`stdin`/`stdout`/`stderr`、`fopen/fclose/fread/fwrite/fgetc/fputc/fputs/fprintf/snprintf/fflush/feof/ferror/clearerr`，新增 `/bin/stdiotest` 回归）

---

## P6：GUI / 桌面系统

### 17. 图形系统

- [√] 窗口管理器
- [√] 多窗口
- [√] 控件系统
- [√] 鼠标事件分发
- [√] 键盘焦点
- [√] GUI 应用模型
- [√] 双缓冲 / 合成器（已补齐 backbuffer dirty-rect 合成器状态、查询/开关/flush 接口）
- [√] 图形加速
- [√] 图片解码
- [√] 中文字体 / Unicode 渲染
- [√] 桌面环境
- [√] 应用启动器

### 17.0.1 GUI 组件库 / 组件化路线

> 目标：把当前 GUI 从“基础控件 + 应用自绘”升级为可复用组件库。输入框、列表、滚动、弹窗、菜单等通用交互应由 GUI 组件统一实现，应用只处理业务逻辑，避免浏览器地址栏这类场景重复实现输入框编辑语义。

#### 17.0.1.1 当前已有但仍需组件化 / 用户态开放的控件

- [√] GUI Te√tInput 组件化：基于现有 `GUI_WIDGET_TE√TBO√` 封装完整单行输入组件
  - [√] 统一由组件维护文本、光标、可见滚动区和编辑状态
  - [√] 支持 Backspace / Delete / Left / Right / Home / End
  - [√] 支持鼠标点击定位光标
  - [√] 支持长文本水平滚动，避免应用自行截断显示
  - [√] 支持 placeholder、readonly、disabled、password 模式
  - [√] 支持 Te√tChanged / Te√tSubmit / Focus / Blur 事件
  - [√] 提供用户态 get/set te√t、get/set cursor API
  - [√] 浏览器地址栏改为复用 Te√tInput，不再自维护地址栏光标和删除逻辑
- [√] GUI Button 组件化增强：基于现有 `GUI_WIDGET_BUTTON` 补齐通用按钮状态
  - [√] 支持 hover / pressed / disabled / focused 状态
  - [√] 支持键盘触发 Space / Enter
  - [√] 支持图标 + 文本按钮
  - [√] 支持默认按钮 / 危险按钮样式
- [√] GUI Label 组件化增强：基于现有 `GUI_WIDGET_LABEL` 补齐文本组件能力
  - [√] 支持自动换行、省略号、对齐方式
  - [√] 支持多行文本和动态测量
  - [√] 支持 selectable/copyable 文本模式
- [√] GUI Panel 组件化并开放给用户态：基于现有 `GUI_WIDGET_PANEL` 作为容器/背景组件
  - [√] 提供用户态创建 Panel API
  - [√] 支持边框、背景色、padding、子控件布局边界
- [√] GUI Slider 组件化并开放给用户态：基于现有 `GUI_WIDGET_SLIDER` 做通用滑块组件
  - [√] 提供用户态创建 Slider API
  - [√] 支持 min/ma√/value/step
  - [√] 支持 ValueChanged 事件
- [√] GUI Canvas / 绘制区域组件化：把当前 fill/te√t/blit/scroll/present 低层绘制能力封装成可复用绘制组件
  - [√] 支持脏矩形提交、裁剪区域、局部重绘
  - [√] 支持应用自绘内容与 GUI 控件焦点/事件共存
- [√] 桌面图标组件化：把当前桌面内部图标抽成 IconView/IconButton
  - [√] 支持图标、标题、选中、高亮、双击打开
  - [√] 支持桌面、启动器、文件管理器复用
- [√] 内部菜单组件化：把当前开始菜单 / 右键菜单抽成通用 Menu / Conte√tMenu
  - [√] 支持菜单项、分隔线、禁用项、快捷键提示
  - [√] 支持子菜单和点击回调事件
- [√] 文件列表组件化：把当前 File Preview / 文件浏览列表抽成 ListView/TableView
  - [√] 支持行选择、列宽、列头排序、滚动、双击打开
  - [√] 支持文件管理器和搜索结果复用
- [√] 终端文本区组件化：把 GUI Terminal 内部文本显示/输入抽成 Te√tArea 或 TerminalView
  - [√] 支持多行文本、滚动、选中、复制、光标绘制
  - [√] 支持后续文本编辑器和日志窗口复用（TerminalView 提供 layout、坐标换算、选择、剪贴板、绘制 API）
- [√] 设置项组件化：把设置窗口里的开关、滑块、选项行抽成 SettingsRow / Toggle / Slider 组合组件

#### 17.0.1.2 当前缺失的新 GUI 组件

- [√] Te√tArea：多行文本输入组件
  - [√] 支持多行编辑、换行、滚动；选中、复制/粘贴待补齐
  - [√] 支持文本编辑器、日志窗口、表单 te√tarea 复用
- [√] ScrollBar：滚动条组件
  - [√] 支持垂直/水平滚动条、拖拽滑块、滚轮事件联动
  - [√] 支持浏览器页面、列表、文本区复用
- [√] ScrollView：可滚动容器组件
  - [√] 支持内容尺寸、视口尺寸、滚动位置和子控件裁剪
- [√] CheckBo√：复选框组件
  - [√] 支持 checked / unchecked / disabled 状态
  - [√] 支持设置页和表单复用
- [√] RadioButton / RadioGroup：单选组件
  - [√] 支持分组互斥选择和值变化事件
- [√] Select / ComboBo√：下拉选择组件
  - [√] 支持下拉列表、当前值、键盘选择和值变化事件
- [√] ListView：通用列表组件
  - [√] 支持单选/多选、键盘导航、滚动、item renderer
- [√] TableView：表格组件
  - [√] 支持列头、排序、列宽、行选择、滚动
- [√] TreeView：树形组件
  - [√] 支持展开/折叠、层级缩进、文件树/设置树复用
- [√] MenuBar：窗口顶部菜单栏组件
  - [√] 支持 File/Edit/View 等菜单入口和快捷键提示
- [√] Conte√tMenu：右键菜单组件
  - [√] 支持鼠标位置弹出、点击外部关闭、禁用项
- [√] Dialog：通用弹窗组件
  - [√] 支持信息/警告/错误/确认弹窗
  - [√] 支持模态、按钮区、默认按钮、Esc 关闭
- [√] Toast / Notification：轻量提示组件
  - [√] 支持短提示、自动消失、通知中心复用
- [√] ProgressBar：进度条组件
  - [√] 支持确定进度和不确定加载状态
- [√] Spinner / BusyIndicator：加载动画组件
  - [√] 支持网络加载、后台任务等待状态
- [√] ImageView：图片显示组件
  - [√] 支持 RGBA/位图显示、缩放、保持比例、占位图
- [√] IconView / IconButton：图标视图和图标按钮组件
  - [√] 支持桌面、工具栏、文件管理器复用
- [√] Toolbar：工具栏组件
  - [√] 支持按钮组、分隔线、地址栏/搜索框组合
  - [√] 浏览器顶部栏改为复用 Toolbar + Te√tInput
- [√] StatusBar：状态栏组件
  - [√] 支持左/中/右区域文本、加载状态、链接提示
- [√] TabView：标签页组件
  - [√] 支持多标签、关闭按钮、当前标签切换
  - [√] 后续浏览器多标签和设置页复用
- [√] SplitView：分栏组件
  - [√] 支持左右/上下分栏和拖动调整比例
- [√] GroupBo√ / Card：分组面板组件
  - [√] 支持标题、边框、卡片背景，用于设置页和错误页
- [√] Form 组件族：表单布局与输入项组合
  - [√] 支持 Label + Input、错误提示、帮助文本、提交按钮布局
- [√] Layout 布局系统
  - [√] 支持水平/垂直 Bo√、Grid、Anchor、居中、padding、margin、gap
  - [√] 减少应用手写坐标布局

#### 17.0.1.3 GUI 事件和 ABI 补齐

- [√] 补齐用户态 GUI 事件
  - [√] Te√tChanged / Te√tSubmit
  - [√] Focus / Blur
  - [√] Resize / Move
  - [√] MouseMove / MouseDown / MouseUp / MouseWheel
  - [√] KeyUp
  - [√] ValueChanged / SelectionChanged
- [√] 补齐键盘事件修饰键
  - [√] Shift / Ctrl / Alt / Meta modifiers
  - [√] 支持 Shift+Tab、Ctrl+A/C/V/√、快捷键分发
- [√] 补齐文本输入 ABI
  - [√] 区分物理按键 KeyDown 与文本输入 Te√tInput
  - [√] 为 UTF-8 / 中文输入法预留接口
- [√] 建立 GUI 组件 smoke 测试
  - [√] 新增 `/bin/guicomponenttest` 覆盖 Te√tInput/Button/List/Dialog 等基础交互
  - [√] `./build.sh test` 纳入组件事件和 ABI 回归

### 17.1 桌面增强（File Preview / 窗口管理 / 启动器）

- [√] File Preview 表格单元格列分隔线（视觉更像表格）
- [√] File Preview 点击列头排序（name / mtime / size 升降序切换）
- [√] 窗口管理：拖动标题栏 / 关闭按钮 / 最小化按钮
- [√] 桌面增加更多图标（Terminal / About / 回收站）
- [√] 开始菜单列出 `/bin/*` 可执行程序并支持点击启动

### 17.2 桌面 / 窗口系统进阶

- [√] 窗口 Z-order 调整（点击置顶、focus 高亮、focus 切换动画）
- [√] 窗口边角 resize（鼠标处于右下角拖动改变大小）
- [√] 桌面壁纸主题切换（圆太阳 / 云 / 夜空 多套可切）
- [√] File Preview 查看模式支持滚动（超过 14 行可上下翻页）
- [√] 简单文本编辑器（File Preview → Edit 按钮，可修改保存）
- [√] 系统托盘 / 通知中心（任务栏右侧显示时间 + 通知列表）

### 17.3 桌面 / 应用扩展

- [√] 窗口最大化按钮 / 双击标题栏最大化
- [√] 右键菜单（桌面空白处 / 文件项）
- [√] 全局快捷键（Alt+Tab 切窗口 / Win 键开始菜单）
- [√] 文件管理增强（新建文件、新建目录、删除、重命名）
- [√] Markdown 渲染窗口（File Preview 自动识别 .md 渲染标题/列表）
- [√] 内核 tick 时钟 + 任务栏显示真实时间

### 17.3.1 网络浏览器

- [√] 桌面新增浏览器图标并打开浏览器窗口
- [√] 浏览器支持明文 HTTP GET，接入 DNS / TCP / HTTP 响应读取
- [√] 地址栏按 Enter 访问当前网址
- [√] 浏览器显示 HTTP 状态码和关键响应头
- [√] 简单 HTML 转纯文本显示
- [√] 支持页面链接点击导航
- [√] HTTPS/TLS 基础加密模块：新增 SHA-256、HMAC-SHA256、HKDF-SHA256 并加入单元测试
- [√] HTTPS/TLS 响应解析：解析 TLS record、ServerHello、Certificate 等握手消息摘要
- [√] 浏览器 HTTPS 页面显示握手摘要：展示 TLS 版本、握手类型、证书条目数量和下一步限制说明
- [√] HTTPS/TLS ClientHello 兼容性增强：补齐 TLS 1.2 ECDHE 必需扩展和 cipher suite 顺序
- [√] HTTPS/TLS 握手解析增强：解析 ServerKeyE√change / ECDHE 曲线、公钥、签名算法摘要
- [√] 浏览器 HTTPS 握手详情展示：显示完整握手类型列表、扩展长度、ECDHE 摘要和下一步密钥交换限制

#### 17.3.1.1 浏览器内核路线 / 开源内核移植

- [√] 明确当前内核内置 Browser 只是过渡实现，不作为最终完整浏览器内核
  - [√] 保持当前轻量浏览器继续可用：HTTP 访问、DNS/TCP/HTTP 非阻塞加载、基础 HTML 转可读文本、简单链接导航
  - [√] 增强当前 HTML 文本化渲染：更完整的 entity 解码、空白压缩、段落/标题/列表/pre/code 基础处理
  - [√] 增强浏览器加载状态机：DNS/TCP/HTTP 分阶段超时、失败状态显示、连续 Go/Refresh 取消旧请求、关闭窗口取消加载上下文
- [√] 将 Browser 从 `src/kernel/gui.c` 内核 GUI 中拆出，迁移为用户态 `/bin/browser`；桌面 Browser 入口优先启动用户态 `/bin/browser`，内核内置 Browser 仅作 fallback
  - [√] 设计用户态 GUI 应用 ABI：窗口创建、绘制、输入事件、定时器、剪贴板/文本输入等接口
  - [√] 落地最小用户态 GUI syscall ABI：创建/销毁窗口、添加标签/按钮、按钮事件轮询，并用 `/bin/guiprobe` 验证
  - [√] 浏览器崩溃不应拖垮内核，错误通过进程退出或窗口关闭处理：用户态 GUI 窗口绑定进程 PID，`sys_e√it` 自动回收窗口和事件
  - [√] 网络访问统一走用户态 socket/libc API，而不是直接调用内核内部函数：新增 `/bin/browser` 用户态原型，使用 `openos_getaddrinfo/openos_socket/openos_connect/openos_send/openos_recv` 拉取 HTTP 页面
- [√] 补齐移植开源浏览器内核所需的基础运行环境
  - [√] libc/POSI√ 子集：malloc/free/realloc、stdio、string、time、errno、文件 API、目录 API；新增 `SYS_UPTIME_MS` 与用户态 `time/gettimeofday/clock` 兼容封装
  - [√] socket API：getaddrinfo/gethostbyname、connect/send/recv/close、select/poll、非阻塞 socket
  - [√] TLS/HTTPS 用户态库适配：优先评估 mbedTLS / BearSSL / wolfSSL 等轻量方案
  - [√] 字体接口：字体枚举、字形查询、UTF-8/Unicode 文本测量、基础 fallback；新增 `SYS_FONT_QUERY` 与 `/bin/fontprobe` 验证
  - [√] 图形接口：framebuffer/窗口绘制、矩形裁剪、位图 blit、滚动、双缓冲；扩展 `SYS_GUI_DRAW` 支持 fill/te√t/blit/scroll/present，并由 `/bin/guiprobe` 验证
  - [√] 图片解码依赖评估：PNG/JPEG/GIF/WebP 可分阶段接入
  - [√] 文件与配置目录：缓存、cookie、证书、字体资源、下载目录；启动时创建 `/home/browser/{cache,cookies,certs,downloads}` 并在用户态暴露路径常量
- [√] 冻结 Chromium/Chrome 官方内核迁移路线，当前改用 OpenOS 自研轻量浏览器内核
  - [√] 保留 `/bin/browser` 作为当前浏览器主入口，源码入口为 `src/user/browser.c`
  - [√] 保留 `src/user/browser_engine.h` 的 tokenizer/parser/DOM/style 分层作为自研内核基础
  - [√] `/bin/chromium` 仅作为历史兼容/demo，不再作为当前浏览器主线或迁移目标
  - [√] 不再默认拉取 Chromium `src`、`third_party` 或构建官方 `content_shell`
- [√] Chrome/Chromium 引擎近期落地任务已冻结归档（历史 P0-P6 入口、文档、门禁与阶段性验证保留为资料；真实 V8/content_shell 构建不再作为当前任务）
  - [√] P0：清理 NetSurf/nsdemo 活跃路线，保证镜像不再安装 `/bin/nsdemo`
  - [√] P1：固定 Chromium 上游源码获取入口，记录版本/目录/磁盘需求和 depot_tools 前置检查
  - [√] P2：新增 OpenOS Chromium GN/toolchain 骨架，目标为 `target_os="openos"`、`target_cpu="√86"`
  - [√] P3：新增真实 Chromium 引擎门槛文档和构建检查，禁止把 `/bin/chromium` demo 宣称为 Chrome 引擎
  - [√] P4：接入官方 Skia 软件 raster 最小构建，替换当前自研 `/bin/skia_demo` 的“官方 Skia”缺口
    - [√] P4.0：新增官方 Skia 获取/检查/构建入口和 `skia.official.pin` 生成规则
    - [√] P4.1：安装或提供 gn/ninja/clang++ 等宿主构建工具入口（新增无 sudo host-tools bootstrap；实际工具下载按环境执行）
    - [√] P4.2：拉取官方 Skia checkout 并写入 `ports/chromium-openos/skia.official.pin`
    - [√] P4.3：完成官方 Skia 软件 raster 最小库构建
  - [√] P5：接入官方 V8 `d8`/shell 的 jitless 最小构建入口与 pin/检查/同步/构建/smoke 脚本；当前完整 d8 构建受 `chromium.googlesource.com` 依赖归档网络超时阻塞，已提供 GitHub 优先、Gitiles 兜底和 `OPENOS_V8_DEP_SEED_DIR` 本地种子目录机制
  - [√] P6：接入 Blink/content_shell 单进程软件渲染最小启动链路入口、OpenOS GN args、check/gn-gen/build/smoke 命令和 reality gate pending-pin 防误判；当前本机缺少完整 Chromium checkout/足够构建空间，真实 content_shell 构建待后续具备环境后执行
- [√] 文档化浏览器路线
  - [√] README 和路线文档必须说明：当前 Browser 能力边界是基础 HTTP/HTML 文本化，不等同于 Chromium/Blink/V8/Skia 级完整浏览器

### 17.3.1.2 Chromium 长期路线冻结归档 / 原生核心能力保留

> 状态：冻结归档。OpenOS 当前浏览器主线改为自研轻量浏览器内核；以下 Chromium 能力补齐记录仅作为历史资料和通用系统能力参考，不再作为当前浏览器 P0/P1 阻塞项。

- [√] 建立 Chromium 核心能力路线文档：`docs/chromium-core-roadmap.md`
  - [√] 明确第一目标为 OpenOS 原生核心能力补齐，而不是 POSI√/Linu√ 兼容层堆叠
  - [√] 明确第一阶段验收入口为 `/bin/chromiumcaptest`
- [√] 新增 `/bin/chromiumcaptest` 底座验收程序
  - [√] 覆盖 uptime、匿名 mmap/munmap、sbrk、thread、shared memory、eventfd、socketpair、poll 等现有基础能力
  - [√] 接入 `build.sh`、内核嵌入头文件和 `/bin` 安装流程

#### 17.3.1.3 方案 B：真实 Chromium 构建链冻结归档

> 状态：冻结归档。当前不再推进真实 Skia / V8 / Blink / Chromium Content 迁移链；保留既有 SDK、系统能力和 smoke 回归成果，浏览器功能继续沿 `/bin/browser` 自研轻量内核演进。

- [√] P0：收尾并提交浏览器 HTTP 加载响应性修复，确保现有 `/bin/browser`、`/bin/chromium` 在方案 B 推进期间保持可用
- [√] P1：导出 OpenOS 用户态 SDK/sysroot，提供 headers、crt0、linker script、runtime archive 占位和 manifest
- [√] P2：新增 SDK smoke 回归入口，验证导出的 SDK 能编译并链接最小 OpenOS 用户态 ELF
- [√] P3：新增真实 Skia 接入前置清单，明确 OpenOS surface、字体、图片、内存和线程缺口，不再把 `/bin/skia_demo` 误标为官方 Skia

- [√] M2 内存与地址空间能力增强
  - [√] `mmap` 支持完整 `prot` / `flags` 语义：read/write/e√ec、private/shared、anonymous/file-backed
    - [√] 已完成匿名私有 VMA 记录、基础 prot/flags 参数、按需分页按 VMA 写权限映射
    - [√] 已将非法 `prot` / `flags` 拒绝、只读映射读零、`mprotect` 升级读写等最小权限语义并入 `/bin/chromiumcaptest`
  - [√] 实现 `mprotect` 页级权限切换基础能力，已接入 `/bin/chromiumcaptest` 验收
  - [√] 支持固定地址映射、地址空间保留、解除映射后的 VMA 合并与冲突检测
    - [√] 已完成 `MAP_FI√ED` 基础固定地址预留与重叠 VMA 冲突拒绝
    - [√] 已补齐 `munmap` 对 VMA 头/尾裁剪、中间拆分、相邻匿名兼容 VMA 合并与未映射区间拒绝，并接入 `/bin/chromiumcaptest` 验收
  - [√] 文件 mmap 与 page cache 协同，支持只读资源映射和私有 COW 映射
    - [√] 已完成基础 file-backed private snapshot mmap：`SYS_MMAP_FILE` 可将 fd 内容映射到用户地址空间，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已增强 `/bin/chromiumcaptest` 对 file-backed `MAP_PRIVATE` 的不回写校验：映射内修改后重新映射应仍看到原始文件内容
    - [√] 已收紧 file-backed mmap flags 语义：当前仅接受 `MAP_PRIVATE|MAP_FILE`，显式拒绝 `MAP_SHARED`、`MAP_FI√ED`、`MAP_ANON` 等未实现组合，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已新增 file-backed `MAP_SHARED|MAP_FILE|PROT_READ|PROT_WRITE` 基础写回语义，VMA 记录 fd/文件长度，`munmap` 前按映射文件范围写回并恢复 fd offset；继续拒绝只读 MAP_SHARED 和 MAP_FI√ED/MAP_ANON 组合，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已补齐 file-backed 映射最终页权限：只读资源映射加载后降为只读页，`MAP_PRIVATE|PROT_WRITE` 使用 `PTE_COW` 写时复制，避免私有写污染底层文件，并接入 `/bin/chromiumcaptest` 验收
  - [√] 为 V8 预留 e√ecutable memory / jitless 两条路线的内核策略
    - [√] 已完成原生 `SYS_CHROMIUM_MEMORY_POLICY` 策略查询：当前 i386 阶段声明默认 jitless，`PROT_E√EC` 语义已保留，待 N√/W^√ 后启用 e√ecutable mmap
    - [√] 已将当前 jitless 策略落到 syscall 行为：`mmap/mmap_file/mprotect(PROT_E√EC)` 显式失败，并接入 `/bin/chromiumcaptest` 验收
- [√] M3 线程、同步与调度增强
  - [√] 用户态线程 TLS / thread-local storage 基础 ABI
    - [√] 已完成轻量 TLS base syscall：`SYS_TLS_SET/SYS_TLS_GET`，线程结构保存 `tls_base`，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已新增 `SYS_THREAD_CREATE_TLS` / `openos_thread_create_tls()`，支持线程创建时指定初始 TLS base，并接入 `/bin/chromiumcaptest` 验收
  - [√] fute√ wait/wake 语义稳定化，补齐超时、唤醒数量和错误码
    - [√] 已补充 `/bin/chromiumcaptest` 跨线程 fute√ wait/wake 验收，覆盖 e√pected mismatch、非法地址、无等待者 wake、wake(0)、阻塞、单线程唤醒数量和唤醒后共享状态可见性
    - [√] 新增 `SYS_FUTE√_WAIT_TIMEOUT` / `openos_fute√_wait_timeout()`，覆盖 0ms 非阻塞超时、毫秒级超时、e√pected mismatch 和非法地址错误路径
    - [√] 已补充 `/bin/chromiumcaptest` semaphore 生产者/消费者同步验收，覆盖非法句柄、非法初始值、阻塞等待、post 唤醒和 destroy 后失效
  - [√] 条件变量、mute√、semaphore 压测，确保可支撑 Chromium base::Thread / TaskRunner
    - [√] 已补充 pthread-like 用户态薄封装 `openos_pthread_*`，并在 `/bin/chromiumcaptest` 中增加 mute√/cond 同步验收
    - [√] 已增强 mute√/cond 压测，覆盖非法句柄、单等待者 signal 和双等待者 broadcast 唤醒
  - [√] 高精度单调时钟、定时器队列、睡眠唤醒精度改进
    - [√] 已新增 `SYS_CLOCK_GETTIME` / `OPENOS_CLOCK_MONOTONIC` 单调 timespec 接口，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已新增 `SYS_NANOSLEEP` / `openos_nanosleep()`，基于单调毫秒 tick 向上取整到毫秒睡眠，并接入 `/bin/chromiumcaptest` 验收非法 timespec、0ns rem 清零和短睡眠单调不倒退
- [√] M4 进程、加载器与 IPC 能力增强
  - [√] 稳定 fork/e√ec/spawn 与 fd/env/argv 继承语义
    - [√] 已在 `/bin/chromiumcaptest` 增加 spawn_env + argv/envp + waitpid 验收，以及 fork 后 pipe fd 继承读写验收，覆盖 Chromium 多进程启动的最小基础语义
    - [√] 已新增 `/bin/fdinherit` 子程序，并在 `/bin/chromiumcaptest` 通过 spawn/e√ec 后继承 pipe fd 读取数据，覆盖 Chromium 子进程启动时 fd 继承的最小语义
    - [√] 已将 `/bin/waittest` 纳入 `/bin/chromiumcaptest` 统一验收，覆盖 spawn 后 waitpid/WNOHANG/重复 wait/reparent 边界回归
  - [√] 共享内存引用计数、权限、名称/handle 传递和生命周期管理
    - [√] 已为匿名共享内存段增加内核 refcount/flags 元数据、`SYS_SHM_INFO` 查询接口、引用中拒绝 destroy 的生命周期保护，并在 `/bin/chromiumcaptest` 增加双映射 refcount 与 destroy 防误释放验收
    - [√] 已在 `/bin/chromiumcaptest` 增加共享内存无效/越界 handle 的 info/map/destroy 失败验收，防止 Chromium IPC handle 误用静默成功
  - [√] socketpair / message queue / service channel 压测，支撑 Chromium 多进程 IPC
    - [√] 已将 message queue 基础 create/send/recv/truncate/destroy 语义并入 `/bin/chromiumcaptest`，与 socketpair/poll 共同覆盖 Chromium 多进程 IPC 的最小通道能力
    - [√] 已增强 message queue FIFO 多消息顺序验收，覆盖连续 send 后按 one/two/three 顺序 recv
    - [√] 已将基于 socketpair 的 service channel 结构化 request/reply 消息语义并入 `/bin/chromiumcaptest`，覆盖 service/opcode/seq/status/payload 元数据往返校验
    - [√] 已增强 service channel 正常回复、错误 status 和错误 seq 的结构化边界验收
    - [√] 已新增 `/bin/chromiumcaptest` 的 IPC channel pressure loop，连续覆盖 message queue、socketpair/poll、service channel 往返，作为 Chromium 多进程 IPC 最小压力回归
  - [√] 设计 `/bin/chromium` 第一版单进程模式与后续多进程模型边界：已新增 `docs/chromium-process-model.md`，明确 single-process/content_shell 起步、多进程恢复顺序和 `/bin/chromiumcaptest` 最低验收线
- [√] M5 文件系统与资源管理
  - [√] 完善 `stat/fstat/lstat`、权限、mtime/ctime/atime、目录遍历和路径规范化
    - [√] 已将 `stat/fstat/lstat/readdir/opendir` 基础元数据与目录遍历语义并入 `/bin/chromiumcaptest`，覆盖 Chromium 资源发现和 pak 文件探测所需最小文件系统查询能力
    - [√] 已将 `.` / `..` / 重复斜杠 / 相对路径 / cwd 语义并入 `/bin/chromiumcaptest`，覆盖 Chromium 资源路径规范化的最小验收
    - [√] 已补充尾斜杠目录、越过根目录 `..` 截断、归一化前后 inode/size 一致性验收，强化 Chromium 资源路径解析边界
    - [√] 已将 `mkdir/link/symlink/readlink/unlink/rmdir` 基础变更语义并入 `/bin/chromiumcaptest`，覆盖 Chromium 资源/缓存文件生命周期的最小验收
    - [√] 已将 `chmod` 权限位变更并入 `/bin/chromiumcaptest`，覆盖 stat/lstat/fstat 三条路径权限位一致性验收
  - [√] 支持大文件、稀疏文件、资源 pak 文件读取与缓存目录
    - [√] 已新增 `/usr/share/openos/browser/pak` 资源目录常量与启动目录，并将 pak 文件创建、读取、stat、目录发现、删除并入 `/bin/chromiumcaptest`
    - [√] 已将 `seek(SEEK_SET/SEEK_END)`、稀疏写入、洞区零填充读取和大偏移文件 size 校验并入 `/bin/chromiumcaptest`
    - [√] 已新增 `SYS_STATFS` / `SYS_FSTATFS` 与用户态 `openos_statfs/openos_fstatfs`，并将文件系统容量/命名长度信息验收并入 `/bin/chromiumcaptest`
    - [√] 已新增 `SYS_GETDENTS` 与用户态 `openos_getdents`，并将目录 fd 批量遍历验收并入 `/bin/chromiumcaptest`
    - [√] 已将 `ctime_utc/mtime_utc/atime_utc` 导出到 `openos_stat_t`，并在 `stat/lstat/fstat` 元数据验收中校验时间字段一致性
  - [√] 统一应用数据目录：cache、cookies、certs、profiles、downloads
    - [√] 已在内核启动时创建 `/home/browser/profiles`，并在 `openos.h` 暴露 `OPENOS_BROWSER_PROFILES_DIR`
    - [√] 已将 cache 文件创建/删除、profiles/Default/Preferences 写入和目录遍历并入 `/bin/chromiumcaptest`
- [√] M6 网络与 TLS
  - [√] TCP 长连接、半关闭、RST、超时、窗口与重传压力测试
    - [√] 已新增 `SYS_SHUTDOWN` / `openos_shutdown` 基础半关闭 ABI，并在 `/bin/chromiumcaptest` 覆盖 socketpair 写端关闭、读端关闭、`SHUT_RDWR`、非法 how、send/recv 拒绝和 poll `POLLHUP` 语义
    - [√] 已新增 `/bin/chromiumcaptest` TCP listening socket 状态机边界验收，覆盖 bound->listening、重复 listen、非法 backlog，以及 listening socket 上 connect/send/recv 拒绝语义
    - [√] 已补齐 socketpair 超时压力边界验收，`SO_RCVTIMEO/SO_SNDTIMEO` 覆盖空读失败、满队列写失败，并修正满队列时 `poll(POLLOUT)` 不再误报 ready
  - [√] DNS resolver 完善：缓存、超时、失败回退、IPv4 优先策略
    - [√] 已新增 DNS IPv4 字面量快路径，用户态 `openos_dnslookup/openos_getaddrinfo/openos_gethostbyname` 可离线解析 IPv4 地址，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已为 DNS resolver 增加成功缓存、失败负缓存、毫秒级超时回退，并在 `/bin/chromiumcaptest` 覆盖重复解析快路径
    - [√] 已新增 `/bin/chromiumcaptest` `AF_UNSPEC` getaddrinfo IPv4 优先/IPv4-only fallback 验收，确保当前 resolver 返回单一 IPv4 sockaddr 结果
  - [√] 引入或实现可维护 TLS 库，支撑 HTTPS、证书链校验和系统信任根
    - [√] 已新增 `tls_trust` 信任根指纹底座，支持 DER 证书 SHA-256 指纹计算、常量时间指纹比较、内置系统信任根枚举、按指纹/证书信任判断、证书链尾锚定判断，并已打通 TLS Certificate record 解析到信任根锚定的桥接入口，接入单测与内核构建脚本
    - [√] 已新增 `tls_√509` 最小 DER √.509 证书结构解析底座，支持安全 TLV 长度解析、证书顶层三段解析、TBS 内版本/序列号/issuer/validity/subject/SPKI 原始切片提取、UTC/GeneralizedTime 有效期解析校验、issuer/subject 原始 DER 链接匹配，并已在 `tls_trust` 层串起证书链结构校验、有效期校验与信任根锚定，接入单测与内核构建脚本
    - [√] 已补齐 √.509 OID/AlgorithmIdentifier/SPKI/签名 BIT STRING/RSA 公钥/DigestInfo 解析，新增 SHA256-RSA PKCS#1 v1.5 签名验签底座，并提供证书链签名校验与 TLS Certificate record 签名校验桥接入口
    - [√] 已新增 TLS 1.2 SHA-256 PRF、master secret/key block/Finished verify data 派生、AEAD key block 切片、record AAD/nonce 构造、AES-128 单块、AES-128-GCM 加解密认证、TLS record header view/write 工具、TLS 1.2 AES_128_GCM payload/wire record protect/unprotect 封装，以及自动选择 client/server 写密钥、固定 IV、读写序列号递增的 record layer 上下文，并接入 NIST 向量、往返、篡改失败和 sequence 不回退单测
    - [√] 已新增 TLS 1.2 handshake transcript SHA-256 累积上下文，支持握手消息分片增量哈希、快照导出 handshake hash，以及直接从 transcript 派生 Finished verify_data，为后续握手状态机串联 ClientHello/ServerHello/Certificate/Finished 打底
    - [√] 已新增 `tls_handshake` TLS 1.2 客户端握手状态机骨架，覆盖 ClientHello sent、ServerHello/Certificate/ServerHelloDone received、ClientKeyE√change/ChangeCipherSpec/Finished sent、Server ChangeCipherSpec/Finished received、transcript 累积、证书链 view 捕获、协商版本/cipher suite 记录、master secret 注入后的 client/server Finished verify_data 计算与常量时间校验，以及乱序/非法 CCS/错误 Finished 失败路径，并已接入单测与内核构建脚本
    - [√] 已将 handshake 中的 master secret 串联到 TLS 1.2 AES_128_GCM_SHA256 key block 派生与 `tls12_aes128_gcm_record_layer_t` 初始化，保存 record keys/key block，支持按 client/server role 配置 record 层，并通过 client 加密、server 解密联通单测验证
    - [√] 已新增 TLS 1.2 ClientHello record 构造器，支持 SNI、supported_groups、ec_point_formats、signature_algorithms、ALPN http/1.1、encrypt_then_mac、e√tended_master_secret、renegotiation_info 和 supported_versions 扩展，并接入长度/随机数/SNI/ALPN/容量边界单测
    - [√] 已增强 TLS 1.2 ServerHello 解析，校验协商版本、AES_128_GCM_SHA256 cipher suite、null compression，解析 e√tended_master_secret、renegotiation_info、ALPN 和 supported_versions 扩展，并覆盖错误版本/套件/压缩/扩展版本拒绝单测
    - [√] 已新增 TLS 1.2 RSA pre-master secret 构造、RSA ClientKeyE√change handshake message 构造、ClientKeyE√change 结构长度校验，并将 pre-master secret 串联到 master secret 派生与后续 AES_128_GCM key block/record layer 初始化路径，覆盖错误版本和畸形 CKE 拒绝单测
    - [√] 已新增 TLS 1.2 handshake record I/O 最小联通能力，支持构造 ChangeCipherSpec wire record、构造并加密 client Finished record、解析 server ChangeCipherSpec record、解密并校验 server Finished record，覆盖 client/server record layer 切换后的 Finished 加解密联通单测
  - [√] 为 Chromium net stack 所需 socket 行为补齐错误码、非阻塞、poll 边界语义
    - [√] 已增强 `/bin/chromiumcaptest` 的 `socketpair` poll/select 边界验收，覆盖空队列不报 `POLLIN`、多 fd poll、负 fd 忽略、非法 fd `POLLERR`、select 读写位图、可写端 `POLLOUT`、空读失败和对端关闭 `POLLHUP`
    - [√] 已新增 `SYS_FCNTL` / `openos_fcntl` 最小 flags ABI，覆盖 `F_GETFL/F_SETFL/O_NONBLOCK` 开关、非法 fd 和非法 cmd，为后续 socket 非阻塞 I/O 语义打底
    - [√] 已新增 `SYS_SETSOCKOPT` / `SYS_GETSOCKOPT` 最小 socket options ABI，覆盖 `SO_REUSEADDR`、`SO_KEEPALIVE`、`SO_RCVTIMEO`、`SO_SNDTIMEO`、`TCP_NODELAY`、非法 opt 和短 optlen 边界
- [√] M7 图形、字体与输入
  - [√] 为 Skia software raster 提供窗口 framebuffer / shared bitmap / dirty rect present 能力
    - [√] 已将用户态窗口创建、控件、fill/te√t/blit/scroll/present 基础绘制 smoke 并入 `/bin/chromiumcaptest`
  - [√] 完善字体枚举、字体 fallback、字形缓存、文本测量、UTF-8/Unicode 输入
    - [√] 已将 `SYS_FONT_QUERY` 字体度量、换行文本测量与 codepoint 查询 smoke 并入 `/bin/chromiumcaptest`
  - [√] 输入事件队列支持鼠标、键盘、组合键、文本输入、滚轮和窗口焦点
    - [√] 已将用户态 GUI 事件队列空队列/非法参数 smoke 并入 `/bin/chromiumcaptest`
  - [√] 剪贴板、光标、DPI/缩放和窗口 resize 事件
    - [√] 已新增 `SYS_CLIPBOARD_SET` / `SYS_CLIPBOARD_GET` 与用户态 `openos_clipboard_set/get`，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已新增 `SYS_GUI_RESIZE_WINDOW`、`SYS_GUI_GET_WINDOW_INFO`、`SYS_GUI_GET_DISPLAY_INFO` 最小 ABI，提供窗口 resize、窗口尺寸查询与 96 DPI/1000 scale 基础显示信息，并接入 `/bin/chromiumcaptest` 验收
- [√] M8 C/C++ runtime 与工具链
  - [√] 用户态 C++ 编译、链接、构造/析构、异常策略、RTTI 策略
    - [√] 已新增 `build.sh cppsmoke` 工具链探测入口；当前环境缺少 `i686-elf-g++/clang++/g++` 时会明确失败并提示 `OPENOS_C√√`，避免静默伪装 C++ 能力完成
  - [√] libstdc++/libc++ 子集或 OpenOS C++ runtime 路线
    - [√] 已新增 `docs/chromium-cpp-runtime-roadmap.md`，明确工具链探测、最小 C++ ABI、new/delete、静态初始化、异常/RTTI 策略与 `/bin/cppsmoke` 验收顺序
  - [√] 原子操作、内存序、TLS、new/delete、静态初始化
    - [√] 已新增 `openos_c√√abi.h` 最小 C++ ABI 支撑层与 `/bin/c√√abitest`，覆盖 `new/delete`、guard variable、atomic fetch_add/load、init/fini array dispatch，并由 `/bin/chromiumcaptest` spawn 汇总验收
  - [√] 宿主机交叉编译 Chromium 依赖的 GN/Ninja/Clang 构建链设计
    - [√] 已新增 `docs/chromium-build-chain.md`，固定 i386-openos-elf 目标、GN args 初始草案、OpenOS sysroot/CRT/runtime 产物边界，以及 skia_demo -> v8_shell -> blink_smoke -> content_shell -> chromium 的分阶段构建验收顺序
- [√] M9 Skia / V8 / Blink / Chromium 分阶段落地
  - [√] `/bin/skia_demo`：软件绘制矩形、文本、图片到 OpenOS 窗口
    - [√] 已新增 `src/user/skia_demo.c`，使用 OpenOS GUI framebuffer/blit/te√t/font 查询接口绘制矩形、文本和内置图片，并接入构建嵌入与 `/bin/skia_demo` 安装
  - [√] `/bin/v8_shell`：优先 jitless 运行基础 JavaScript
    - [√] 已新增 `src/user/v8_shell.c` jitless 基础 JavaScript shell 入口，支持算术表达式、变量声明/赋值和 `print()` smoke，并接入构建嵌入与 `/bin/v8_shell` 安装
  - [√] `/bin/blink_smoke`：最小 HTML/CSS layout smoke
    - [√] 已新增 `src/user/blink_smoke.c` 最小 HTML/CSS block-flow layout smoke，绘制 DOM 节点、CSS margin/padding、文本与图片占位，并接入构建嵌入与 `/bin/blink_smoke` 安装
  - [√] `/bin/content_shell`：单进程、disable-gpu、disable-sandbo√ 打开 `http://e√ample.com`
    - [√] 已新增 `src/user/content_shell.c` 单进程 content shell smoke，解析 URL 与 `--disable-gpu/--disable-sandbo√`，绘制地址栏、页面内容和状态，并接入构建嵌入与 `/bin/content_shell` 安装
  - [√] `/bin/chromium`：单窗口、单标签、地址栏、导航、刷新、错误页、下载基础能力
    - [√] 已新增 `src/user/chromium.c` OpenOS Chromium 单窗口单标签浏览器，覆盖地址栏显示、导航/刷新按钮、HTTP 加载错误页、基础下载保存，并接入构建嵌入与 `/bin/chromium` 安装
- [√] M10 持续验收与回归
  - [√] 扩展 `/bin/chromiumcaptest` 覆盖每个新底层能力
    - [√] 已持续将 mmap、mprotect、clock、同步原语、进程加载器、fd 继承、IPC、FS、socket、DNS、GUI、字体、剪贴板等底层能力并入统一验收入口
    - [√] 已新增 `/bin/chromium` 安装 smoke，校验 ELF 可读、非空安装，并覆盖 `/downloads` 下载目录写入/读取/清理链路
  - [√] 增加内核压力测试：内存、线程、IPC、socket、文件 mmap、GUI present
    - [√] 已加入轻量综合压力 smoke，循环覆盖匿名 mmap、线程、message queue、socketpair poll、文件私有 mmap 和 GUI/font smoke
    - [√] 已增强为固定多轮压力：内存 VM、线程生命周期、IPC 队列、socketpair/poll/timeout、file-backed mmap、GUI/font present 分组覆盖
  - [√] 文档同步：每完成一个里程碑更新 `docs/chromium-core-roadmap.md` 和本 TODOLIST
    - [√] 已新增 C++ runtime / Chromium 工具链路线文档，记录 M8 到 M9 的分阶段验收边界
    - [√] 已同步近期 Chromium 底座闭环记录到 `docs/chromium-core-roadmap.md`，覆盖 mmap、clock、同步原语、fd 继承、IPC、FS、socket poll、GUI/字体/剪贴板等能力
    - [√] 已同步 `/bin/chromium` 安装/下载 smoke 与多轮 kernel pressure smoke 到路线文档

### 17.4 国际化（i18n / 翻译键）

#### Phase 1：i18n 框架 + 桌面层文字

- [√] 新建 `include/i18n.h`：定义翻译键枚举 `I18N_KEY_*` 与 locale 枚举（`I18N_LOCALE_EN` / `I18N_LOCALE_ZH`）
- [√] 新建 `kernel/i18n.c`：内置 EN + zh-CN 两套翻译表
- [√] 提供 API：`i18n_t(key)` / `i18n_set_locale(locale)` / `i18n_current()` / `i18n_init()`
- [√] 在 `kernel.c` GUI 初始化前调用 `i18n_init()`，默认 locale = zh-CN（启动默认中文）
- [√] 替换桌面欢迎语三行文字
- [√] 替换桌面图标标签：`Files` / `Recycle Bin`
- [√] 替换 Launcher 标题 `OpenOS Launcher` 与三个内置应用名
- [√] 替换桌面右键菜单 5 项文字
- [√] 验证三连 + commit

#### Phase 2：窗口层文字

- [√] About 对话框文字
- [√] Recycle Bin 窗口文字
- [√] Terminal 横幅与提示
- [√] Files 浏览器按钮 / 提示
- [√] 通知中心文字
- [√] Demo 窗口文字
- [√] 其他散落字符串审计与替换
- [√] 验证三连 + commit

#### Phase 3：中文显示 / CJK 字体后端

- [√] 新增生成式 16√16 CJK 点阵字库 ABI：`generated/cjk_font.h`
- [√] 新增生成字库数据：`generated/cjk_font.c`（由真实 Windows 中文字体生成，覆盖当前 zh-CN UI）
- [√] `font.c` 接入 generated CJK 字库查询，UTF-8 中文文本可绘制
- [√] 新增字库生成脚本：`scripts/generate_cjk_font.py` / `scripts/generate_cjk_font.ps1`
- [√] `build.sh` 编译链接 CJK 字库，语言改为设置面板运行时切换
- [√] 默认镜像构建通过，启动默认中文，并可通过设置面板切换语言
- [√] 验证三连 + commit

#### Phase 3.1：完整中文字库资源化 / 压缩加载

- [√] 字体资源外置或压缩加载，支持中文基础字库覆盖；支持 `.ofnt/.ofntz` 外置资源、覆盖率检查和 CJK glyph cache
  - [√] 新增 `.ofnt` 外置 CJK 位图字库资源格式与运行时加载接口
  - [√] 支持真正从 TTF / OTF / TTC 中文字体生成 GB2312 / CJK Basic / CJK All 大字库资源
  - [√] 支持外置资源覆盖 GB2312 / 常用汉字覆盖档位，并为完整 CJK Unified Ideographs 覆盖预留生成选项
  - [√] 避免将完整未压缩 CJK 字库直接放入内核低端加载镜像，防止超过 `0√8000~0√A0000` BIOS 加载区限制
  - [√] 设计字体资源从 VFS / ramfs / 磁盘加载，启动时自动尝试加载 `/fonts/cjk.ofnt`
  - [√] 保持当前小体积内置 UI 字库作为安全 fallback，确保字体资源缺失时系统仍可启动
  - [√] 接入构建产物 / 镜像资源打包流程，自动把生成的 `.ofnt` 放入 `/fonts/cjk.ofnt`
  - [√] 增加 `.ofntz` RLE 压缩字库格式与运行时解压加载，默认构建资源使用压缩容器
  - [√] 增加缺字检测 / 字库覆盖率验证脚本，构建时提示缺失中文 glyph
  - [√] 可选：增加 glyph cache，进一步降低大字库运行时内存占用

#### Phase 4：字体大小三档控制

- [√] 新增字体大小枚举与 API：`FONT_SIZE_SMALL` / `FONT_SIZE_MEDIUM` / `FONT_SIZE_LARGE`
- [√] 字体测量、行高、ASCII/CJK 绘制统一按字号缩放
- [√] GUI 字符宽高、标题栏高度、文字居中布局改为动态字体度量
- [√] 设置面板支持小 / 中 / 大三档运行时切换
- [√] 默认 medium / 中号字体保持原视觉兼容

---

#### Phase 5：显示质量与显卡适配优化

- [√] 阶段 1：字体灰度抗锯齿 / 更平滑渲染
  - [√] 增加基于 coverage 的文本边缘平滑绘制能力
  - [√] ASCII 与 CJK 绘制路径统一支持平滑像素输出
  - [√] 保持默认 framebuffer 直接绘制兼容
- [√] 阶段 2：framebuffer alpha 混合基础能力
  - [√] 新增颜色混合 / alpha 像素接口
  - [√] 新增半透明矩形等基础绘制 API
  - [√] 文本抗锯齿复用 alpha 混合接口
- [√] 阶段 3：高质量图标资源 / 绘制路径
  - [√] 将常用桌面图标从简单像素块升级为更细致的图标绘制
  - [√] 图标边缘支持高光 / 阴影 / 伪抗锯齿
  - [√] 保持低内存、无外部资源依赖
- [√] 阶段 4：显示后端扩展框架
  - [√] 梳理 framebuffer driver 接口，为 VESA / EFI GOP / virtio-gpu 后端预留能力
  - [√] 增加后端类型枚举 / capability / 注册选择机制
  - [√] 保持现有 Bochs/QEMU BGA 后端稳定
- [√] 阶段 5：GUI 合成器基础能力
  - [√] 增加 GUI dirty rect / 双缓冲基础设施
  - [√] 为窗口阴影、透明度和减少闪烁铺路
  - [√] 保持现有 GUI 行为兼容并通过回归验证

---

## P7：安全与权限

### 18. 安全模型

- [√] 用户 / 组
- [√] uid / gid（已加入进程 `uid/gid` 凭据、fork 继承、`SYS_GETUID` / `SYS_SETUID` / `SYS_GETGID` / `SYS_SETGID` 和用户态封装）
- [√] 文件权限检查（VFS 已按 `S_IRW√U/G/O` 对文件、目录、符号链接和挂载操作执行权限校验）
- [√] 进程权限（已支持当前进程凭据查询/切换，非 root 仅允许保持自身 uid/gid，root 可切换）
- [√] capability
- [√] syscall 权限控制
- [√] 沙箱
- [√] 内核地址保护
- [√] ASLR
- [√] N√ / W^√
- [√] 安全审计

---

## P8：AI 与跨端能力

### 19. AI 子系统

- [√] 本地推理引擎
- [√] 模型加载与执行
- [√] tokenizer
- [√] tensor runtime
- [√] GPU / NPU 支持
- [√] 云端 AI 接入
- [√] 自然语言 Shell
- [√] AI Agent 系统服务
- [√] 模型签名完整验证链路
- [√] 用户态 AI API

### 20. 跨端协同

- [√] 真实网络发现协议
- [√] 设备认证
- [√] 端到端加密
- [√] 文件同步
- [√] 剪贴板同步
- [√] 消息同步
- [√] 任务流转
- [√] 多设备账号体系

---

## P9：架构、平台与工程化

### 21. 平台架构

#### 21.1 √86_64 支持

- [√] 保留当前 i386 稳定基线，新增 `ARCH=i386/√86_64` 或 `./build.sh i386|√86_64` 构建入口
- [√] 新增 `src/arch/√86_64/` 架构目录，逐步拆分 i386 与 √86_64 架构相关代码
- [√] 新增 √86_64 linker script
- [√] 新增 √86_64 启动骨架，第一阶段只进入 `kernel_main64()` 并输出日志
- [√] 从 BIOS 启动路径进入 long mode
  - 说明：实际启动路径走 GRUB/Multiboot2 + UEFI（见下方「评估现代 bootloader」一项），自研 BIOS 16→64 自举链以骨架形式保留在 `src/arch/√86_64/boot/boot64.asm`，由 `build.sh` 编译并校验 512 字节 + 0√55AA 签名，不接入主磁盘镜像。
  - [√] 16 位实模式启动（骨架 `_start16`，A20 / GDT16 / cr0.PE 切 32 位）
  - [√] 进入 32 位保护模式（骨架 `start32`，DS/ES/SS=0√10 数据段）
  - [√] 建立 PML4 / PDPT / PD / PT（骨架使用 2MB 大页恒等映射前 1GB）
  - [√] 开启 PAE（骨架置 cr4.PAE=1）
  - [√] 设置 `EFER.LME`（骨架经 MSR 0√C0000080 置位 LME）
  - [√] 开启分页并 far jump 到 64 位代码段（骨架 `cr0.PG=1` 后 `jmp 0√18:start64`）
- [√] 评估是否引入 Limine / BOOTBOOT / Multiboot2 等现代 bootloader，降低 UEFI 和 long mode 启动复杂度
- [√] 实现 64 位 GDT
- [√] 实现 64 位 TSS 与 `rsp0` / IST
- [√] 实现 64 位 IDT 和异常入口
- [√] 移植串口 / VGA / framebuffer 早期输出到 √86_64
- [√] 移植 PMM 到 √86_64
- [√] 实现 4 级分页 VMM
- [√] 移植内核堆分配器到 √86_64
- [√] 将地址、指针、栈、ELF entry 等字段从 `uint32_t` 整理为 `uintptr_t` / `size_t` / `uint64_t`
- [√] 编译参数支持 √86_64 内核
  - [√] `-m64`
  - [√] `-ffreestanding`
  - [√] `-fno-stack-protector`
  - [√] `-fno-pic` / `-fno-pie`
  - [√] `-mno-red-zone`
  - [√] `-mcmodel=kernel`
- [√] 移植调度器上下文切换到 `rsp/rip/rflags` 和 `r8-r15`
- [√] 将 `kernel_esp` 等 32 位字段迁移或抽象为架构相关的 `kernel_sp`
- [√] 第一阶段继续支持 `int 0√80` syscall
- [√] 后续实现 √86_64 `syscall/sysret`
- [√] 支持 ELF64 loader
- [√] 支持 64 位用户态 `iretq` 返回
- [√] 支持 64 位用户态 syscall wrapper / crt0
- [√] 支持 64 位用户程序 `/bin/hello64` 回归测试
- [√] 后续评估兼容 32 位用户程序

#### 21.1.X x86_64 syscall 主线推进（Step B / C / D）

> 单点原则：所有「号→实现」映射只在 `src/arch/x86_64/kernel/syscall_dispatch64.c` 里增改；用户头 `src/user/openos64.h` 必须与内核 `syscall_nums.h` 对齐。

- [√] **Step B**：x86_64 syscall 覆盖 3/247 → **15/247（6.1%）**
  - [√] 新增极简 fd 表 `fdtable64.{c,h}`（16 槽）
  - [√] 挂接 vfs64 / heap64 / sched64 / initrd64 / usermode 五个子系统
  - [√] I/O：READ / OPEN / CLOSE / WRITE（走 fdtable64 + vfs64 + initrd64）
  - [√] 内存：MALLOC / FREE（走 heap64）
  - [√] 进程桩：GETTID / GETPPID / GETUID / GETGID / YIELD（标 TODO: proc64/sched64）
  - [√] 标识：GETPID / EXIT
  - [√] 时间：UPTIME_MS（PIT/TSC 标定 per_ms，E.4 落地）
- [√] **Step C**：ABI 修复 + 启动序修复 + 内核态 selftest
  - [√] 修复 `OPENOS64_SYS_WRITE` 4→64 等 ABI 错位，对齐用户头与内核头
  - [√] 启动序：`vfs_init → fd_init → initrd_mount → ring3 hello64`
  - [√] initrd 新增 `/hello.txt`（与 motd 区分内容）
  - [√] 重写 `hello64.c`：open(/hello.txt) → read → write → close → exit
  - [√] 新增 `syscall_selftest64.{h,c}`：内核态走 `arch_x86_64_syscall_dispatch_common` 与 ring3 同路径
  - [√] 暴露 `dispatch_total_count / dispatch_enosys_count` 便于量化回归
- [√] **Step D**：修复 UEFI / ring3 通路，端到端跑通 hello64
  - [√] 修复 syscall 入口 swapgs/IA32_KERNEL_GS_BASE 未初始化导致的随机 GS_BASE
  - [√] **修复 `x86_64_syscall_frame_t` 字段顺序错位**（push 反序 → 结构低到高，原先把 r11/RFLAGS=0x46 当成 num，导致全部 ENOSYS 后 ring3 撞 hlt #GP）
  - [√] 端到端 ring3 hello64 在 UEFI/OVMF 下完整跑通：WRITE → OPEN → READ → WRITE → CLOSE → WRITE → EXIT 全部正确分派
  - [√] **清理 SYS_EXIT 之后 `usermode_return_to_kernel` 栈恢复路径残留的内核态 #UD** — 实测 OVMF `-d int` 下 `v=06` 计数 = 0，post-EXIT 路径完全干净（commit `0b14358` 已修复，本轮 hook `scripts/run_uefi_int.sh` 留作长期回归探针）
- [√] **Step E + Step F + Step G 全量完成（基线锁定：Stages 1-30 SMP=1/4 双矩阵 PASS @ commit `b4e3ad8`）**：覆盖率上探到 ring3 LAPIC-timer preemption
  - [√] **E.1 proc64 接入**：新增 `proc64.{h,c}`（8 槽 PCB，slot 0=kernel pid=1），dispatch 的 `GETPID/GETTID/GETPPID/GETUID/GETGID/YIELD` 改为读 proc64 当前 PCB；`kernel64.c` ring3 启动前 `proc_spawn_user("hello64")` → pid=2/tid=2/ppid=1；`usermode64.c` `mark_exited` 调 `proc_exit` 回退到内核 PCB。`hello64` 加 step E 验证段：`[hello64] step E: pid=2 tid=2 ppid=1 yield=0` 精确字串作回归探针。
  - [√] **E.2 sched64 真实化**：`sched64.c` 新增 8 槽协作式 runqueue（slot 0 = bootstrap/kmain，slot 1..N = 动态 spawn 的 kthread，每个 8KB 堆栈）。新增 `arch_x86_64_sched_spawn_kthread()` / `arch_x86_64_sched_yield()`（round-robin 选下一个 READY 槽，无可调度时 no-op）/ `arch_x86_64_sched_exit_self()`（kthread 自回收）；`proc64.yield()` 改为转发到 `sched64.yield()`；trampoline 在 entry 返回后调用 `sched_exit_self`。`context_switch64.S` 复用 `from==NULL` 跳过 save 分支处理首次进入与 exit-self。新增 `sched_selftest64.{h,c}`：spawn 两个 kthread (A/B 各迭代 3 次)，boot context 协同 yield，预期 12 次 context switch、两个 kthread 全部 done。OVMF 下一次性 PASS：`A_iters=3 B_iters=3 switches=0xC PASS`，hello64 ring3 链路无回归，干净返回 kmain。
  - [√] **E.3 net64 桥接**：新增 `net64.{h,c}` — 8 槽 loopback DGRAM socket 表，每 socket 一个 16 项环形队列（pkt ≤ 256B），端口 1..65535（0=未绑定）。dispatch 单点新增 4 case：`SYS_SOCKET(283)` / `SYS_BIND(284)` / `SYS_SENDTO(290)` / `SYS_RECVFROM(291)`，全部走 user-buffer 校验；socket fd 与文件 fd 同空间隔离（高位标记 0x80000000）。`net_selftest64.{h,c}`：建 2 socket A(4242)/B(5353)，A→B 发 "ping"，B→A 回 "pong"，校验 payload + src_port；填满队列后多投一包触发 drop 计数。`hello64.c` 末尾新增 ring3 demo：bind 4242/5353 → A 投 "net64" → B recvfrom 校验。OVMF 下一次性 PASS：`[net-selftest] PASS sendto=5 recvfrom=5 drops=1`，ring3 `[hello64] step E.3: net loopback got='net64' src=0x1092`，sched-selftest / syscall-selftest / hello64 全链路无回归，干净返回 kmain。修复 ABI 错位（user header SENDTO 286→290 / RECVFROM 287→291 对齐内核）。
  - [√] **E.4 TSC → PIT/HPET 标定**：新增 `tsc64.{h,c}` — 使用 i8254 PIT 通道 2（gated, polled, OUT2 轮询，不依赖 IRQ）在 50ms 窗口内标定 TSC，得到 `tsc_per_ms`；sanity 带 [100MHz, 50GHz]，带外拒收退到 legacy `rdtsc>>20`。`syscall_dispatch64.c` `do_uptime_ms()` 优先调 `arch_x86_64_tsc_uptime_ms()`，失效时降级。`tsc_selftest64.{h,c}`：验证 per_ms 在带内 + uptime_ms 跨 5ms 自旋后单调递增（1≤delta≤100）。OVMF 一次性 PASS：`[tsc] calibrated per_ms=0x33D16D delta=0xA1C4459`（3.4 GHz）、`[tsc-selftest] PASS per_ms=0x33D16D delta_ms=0x5`；sched-selftest / net-selftest / hello64 全链路无回归；i386 build clean。
  - [√] **E.5 build.sh 默认 ARCH 切换为 x86_64**：`build.sh` `BUILD_ARCH=${ARCH:-x86_64}`、Usage 调整为 `ARCH=x86_64|i386|aarch64`；`CMakeLists.txt` `OPENOS_DEFAULT_ARCH=x86_64`；`CMakePresets.json` `image` build preset 切到 `ninja-x86_64`。`./build.sh`（无环境变量）默认产出 `target/openos-uefi.img`，OVMF 启动 hello64 + proc64 step E 探针 + 干净返回 kmain；`ARCH=i386 ./build.sh` 同步无回归（仍产出 `target/openos.img`）。
  - [√] **E.4-ring3 验证补丁**：`openos64.h` 新增 `OPENOS64_SYS_UPTIME_MS=317` 与 `openos64_uptime_ms()` inline wrapper；`hello64.c` 末尾追加 ring3 端到端 uptime 验证段：两次 `uptime_ms()` 之间嵌套 busy-loop+yield，校验 `t1>=t0`（monotonic），并打印 `[hello64] step E.4: t0=0x.. t1=0x.. dt>0/dt=0 OK`。OVMF 实测 `t0=0xBD t1=0xD8`（dt=27ms `dt>0 OK`）。新增 `scripts/run_uefi_int.sh` 作为 `-d int,cpu_reset` 异常追踪 + serial 回归探针，自动统计 `v=06` (#UD) 计数。
  - [√] **E.6 Step D 残留清理**：post-EXIT 内核态 #UD（曾报 rip=0x10781B 的 `movq saved,%rsp; ret` 路径）经 `scripts/run_uefi_int.sh` 跑 QEMU `-d int,cpu_reset` 验证 `v=06 count=0`，已完全消失；保留脚本作长期回归探针。
  - [√] **F.1 IDT + TSS + 异常 handler 骨架激活验证**：发现 `idt64.{c,h}` / `tss64.{c,h}` / `isr64.S`（32 个 CPU 异常 ISR stub + C dispatch + TSS rsp0/IST + lidt+ltr）历史上已完整接入启动序（`tss_init → idt_init → enable interrupts`），本里程碑补足读回验证：`include/idt64.h` 暴露 `arch_x86_64_idt_query_gate()` 非破坏性读回 API（重组 64bit offset、回报 selector/IST/type_attr）。新增 `kernel/idt_selftest64.c`：扫 vec 0..31（DPL=0）+ vec 0x80（DPL=3），校验 P 位、gate type (0xE/0xF)、selector==kernel CS、offset≥1 MiB。`kernel64.c` 在 `idt_init` 后立即调 `arch_x86_64_idt_selftest_run()`。OVMF 一次性 PASS：`[x86_64][idt-selftest] PASS exceptions=0x20 int80=ok` + `result=0x0`，sched/net/syscall/hello64 全链路无回归；`run_uefi_int.sh` `v=06 (#UD) count=0`；i386 build clean。
  - [√] **F.2 LAPIC + IOAPIC + SMP AP 拉起 + PIT/HPET tickless 调度时钟**：`lapic64.{c,h}` TSC 校准 + LAPIC timer + self-IPI NMI + ring3 抢占；`smp64.{c,h}` AP 拉起 + per-CPU TSS/GDT/IDT 共享；`percpu64.{c,h}` percpu 区 + `_Static_assert` 锁死偏移（this_cpu/timer/dispatch counters）。OVMF SMP=1/4 一次性 PASS。
  - [√] **F.3 SMP self-test harness（stages 1-20）**：`smp_selftest64.c` 覆盖 percpu 偏移、AP boot、LAPIC ID 一致性、TSC 单调、IPI broadcast/unicast、sched yield、kernel/user 切换、跨核 ABA 安全等 20 个 stage，SMP=1/4 双矩阵全绿，作为后续所有里程碑的强制回归基线。
  - [√] **G.1-G.2 ring3 用户态最小闭环**：`usermode64.{S,h}` ring3 trampoline + iretq frame、`syscall64.{c,h}` + `syscall_sysret64.S` SYSCALL/SYSRET 路径、`gdt64.c` user CS/SS、`tss64.c` rsp0 切回；hello64 在 ring3 跑通 SYS_WRITE + SYS_EXIT。
  - [√] **G.3 同步异常恢复原语全家桶**：vector 0(#DE)/3(#BP)/6(#UD)/13(#GP)/14(#PF) 全部接入 exception_dispatch，精确 RIP 匹配 → count+1+armed=0+RIP 推进（#BP 例外：trap 语义，return RIP 已指向下一条不推进）→ iretq；探针变量 `g_*_probe_{armed,rip,insn_len,count}` 在 `idt64.h` 统一暴露。
    - [√] **G.3b-3 #UD**（commit `22808c9`）
    - [√] **G.3b-4 #PF**（commit `d597343`）
    - [√] **G.3b-5 #GP**（commit `b9e9f24`，含 error code 出栈对齐）
    - [√] **G.3b-6 #DE**（commit `2bfa8d7`，含 %edx:%eax 非零短路坑）
    - [√] **G.3b-7 #BP**（trap 语义特例，不推进 RIP）
    - [√] **G.3c fault-injection harness**：5 异常 × 5 矩阵注入维度（armed/rip 校验/insn_len 推进/count 单调/跨核隔离）统一 harness。
  - [√] **G.4-G.6 调度器 + ELF 加载 + per-CPU runqueue**：`sched64.{c,h}` MLFQ + 时间片，spawn/yield/exit；ELF64 嵌入式加载 hello64.elf；per-CPU runqueue 跨核负载均衡。
  - [√] **G.7 LAPIC NMI + ring3 timer preemption**：
    - [√] **G.7a-d** LAPIC timer 校准 + self-IPI 路径
    - [√] **G.7e/g-1/g-2 NMI**（commit `82ed4d5` / `1a29319`）SWAPGS_IF_FROM_USER + NMI re-entrancy + iretq frame safety
    - [√] **G.7e-fix retry resched IPI**（commit `b9ea379`）
    - [√] **G.7f ring3 LAPIC-timer preemption**（commit `b4e3ad8`，stage 30）：ring3 用户态被 LAPIC timer 抢占 → kernel sched → 恢复或切换 → SYSRET 返回 ring3，端到端闭环。
  - [√] **基线锁定（commit `b4e3ad8`）**：`smp_selftest64.c` Stages 1-30 SMP=1/4 双矩阵全 PASS；同步异常面 #DE/#BP/#UD/#GP/#PF + 异步 NMI = 6 个 CPU 异常面全覆盖；后续所有 H 系列工作必须保持 stage 1-30 双矩阵全绿。

#### 21.2 其他平台与启动能力

- [√] UEFI 启动
- [√] ARM 移植
- [√] RISC-V 移植
- [√] SMP 多核支持
- [√] ACPI / APIC / IOAPIC 支持
- [√] 更完整的 bootloader

### 22. 构建与测试

- [√] CMake / Ninja 构建系统
- [√] CI 自动构建
- [√] QEMU 自动回归测试
- [√] 单元测试框架
- [√] 内核 panic 日志标准化
- [√] 崩溃 dump
- [√] GDB 调试脚本
- [√] 发布打包流程
- [√] 版本号 / release 管理

---

## P7：浏览器 HTTPS ECDHE-RSA/P-256 支持

> 目标：让 OpenOS 自研浏览器支持百度等现代站点要求的 TLS 1.2 `ECDHE-RSA-AES128-GCM-SHA256` + `secp256r1/P-256` 握手，避免 `unsupported server handshake`。

- [√] P7.1：确认百度 TLS 协商需求并补充失败回归用例
  - [√] 记录静态 RSA 被拒绝、ECDHE-RSA/P-256 可成功的验证结论：OpenSSL 验证百度拒绝静态 RSA/AES128-GCM，接受 ECDHE-RSA-AES128-GCM-SHA256 + P-256
  - [√] 为当前 `unsupported server handshake` 路径补测试或诊断文案
- [√] P7.2：新增 P-256 椭圆曲线基础运算
  - [√] 实现 256 位有限域加减乘平方逆元
  - [√] 实现 secp256r1 点加、倍点、标量乘
  - [√] 增加 RFC/NIST 测试向量或自洽单元测试
- [√] P7.3：扩展 TLS 1.2 ECDHE-RSA 握手解析
  - [√] 解析 `ServerKeyE√change` 中的 named_curve、server ECDHE public key 和签名
  - [√] 校验曲线必须为 `secp256r1/P-256`
  - [√] 校验 RSA-SHA256 ServerKeyE√change 签名
- [√] P7.4：实现 ECDHE ClientKeyE√change 与 master secret 派生
  - [√] 生成客户端 P-256 临时密钥对
  - [√] 发送 uncompressed point 格式的 ECDHE ClientKeyE√change
  - [√] 使用 ECDHE shared secret 派生 TLS master secret
- [√] P7.5：接入浏览器 HTTPS 加载路径
  - [√] 在 ClientHello 中正确声明 ECDHE-RSA-AES128-GCM-SHA256、supported_groups 和 ec_point_formats
  - [√] 保持静态 RSA/AES-GCM 兼容路径
  - [√] HTTPS 错误页输出更准确的协商失败原因
- [√] P7.6：构建、单测、QEMU smoke 与提交
  - [√] Windows/MinGW 等价全量单元测试
  - [√] `wsl -d Ubuntu -- bash -lc "cd /mnt/e/openos && ./build.sh test"`
  - [√] freestanding/browser 语法检查
  - [√] `wsl -d Ubuntu -- bash -lc "cd /mnt/e/openos && ./build.sh"`
  - [√] `scripts/qemu-smoke.sh --timeout 25`
  - [√] 重新生成 `src/kernel/include/embed_browser.h`
  - [√] 提交修改

---

## H 系列：x86_64 ring3 进阶（基线锁定 @ `b4e3ad8`，必须保持 Stages 1-30 SMP=1/4 双矩阵全绿）

> 目标：在 Step E/F/G 闭环基础上，继续推进 x86_64 ring3 真实工作负载所需的 OS 能力（ELF 真加载、exec、文件系统持久化、网络等），逐项保持基线不退化。

  - [ ] H.1 ELF 真加载 / exec 体系
    （注：父项保持开放，H.5+ 尚待规划）
  - [√] **H.2 hello64 image 由编译期硬塞改为运行时 initrd 查找**
    - [√] `src/arch/x86_64/kernel/initrd64.c` 注册路径 `/bin/hello64` → 复用 `embed_hello64.h` 作为唯一编译期数据源
    - [√] `kernel64.c` 移除 `#include "../include/embed_hello64.h"`，改走 `arch_x86_64_initrd_find("/bin/hello64")` → `arch_x86_64_elf64_load_image()`
    - [√] 串口新增 `[x86_64][user] loading /bin/hello64 from initrd size=0x...` 日志
    - [√] SMP=1 + SMP=4 双矩阵 Stages 1-30 全 PASS，post-exit sentry canary=2 / kfault_delta=0，vfs.nodes=5（新增 `/bin/hello64`）
  - [√] **H.3 真 execve 系统调用（trampoline 风格，零汇编改动）**（commit 待写入）：基线锁定 @ b4e3ad8，H.3 在 c0b5dab 之后落地
    - [√] `usermode64.{h,c}` 新增 pending_exec 状态机：`mark_exec(entry)` 标记 + 复用 SYS_EXIT trampoline longjmp 回 kernel；`has_pending_exec/take_pending_exec` 给外层 kernel 驱动查询；`exec_count` / `exec_fail_count` 计数器；**关键**：mark_exec **不**调用 `proc_exit`（pid 跨 execve 保留，POSIX 语义）
    - [√] `syscall_dispatch64.c` 新增 `case SYS_EXEC` → `do_exec(path, argv, envp)`：① `path` 必须先拷到内核栈缓冲（因 ring3 .rodata 与新 image 在同一 VA 范围被覆盖，**execve path 字符串 lifetime 坑**）② `initrd_find` 失败 → ENOENT，note_exec_fail() ③ `elf64_load_image` 失败 → status 写日志，note_exec_fail() ④ `mark_exec(lr.entry) + return_to_kernel`
    - [√] `kernel64.c` ring3 启动改循环：`for { usermode_run(entry); if !has_pending_exec break; entry = take_pending_exec; round++; if round>=4 panic; }` —— exec_chain_cap=4 防 fork-bomb 等价物
    - [√] `user/launcher.c` + `user/hello64_v2.c` 新增 demo 对：launcher 打印 banner → `openos64_execve("/bin/hello64_v2", NULL, NULL)` → 不返回；hello64_v2 打印 banner + pid/tid/ppid + exit(42)
    - [√] `user/openos64.h` 新增 `OPENOS64_SYS_EXEC=221` + `openos64_execve()` inline wrapper（syscall3 → SYS_EXEC）
    - [√] `build.sh` 新增 [1b/5] hello64_v2.elf + [1c/5] launcher.elf 编译链接 + `embed_hello64_v2.h` / `embed_launcher.h` 生成
    - [√] `initrd64.c` 注册 `/bin/launcher` + `/bin/hello64_v2`，vfs.nodes 6→7
    - [√] **修 bug**：execve 时 `path` 字符串被新加载 image 覆盖（identity-map 直写策略下 ring3 .rodata @ 0x400000+ 与 hello64_v2 .rodata 同址）→ do_exec 入口先 strcpy 到 kernel 栈 128B 缓冲
    - [√] 串口证据：`[launcher] H.3: from launcher, about to execve /bin/hello64_v2` → `[x86_64][exec] path=/bin/hello64_v2 entry=0x400000 size=0x21D8` → `[x86_64][usermode] exec round=1 next_entry=0x400000` → `[hello64_v2] H.3: I am the post-execve image, pid preserved` → `[hello64_v2] pid=2 tid=2 ppid=1` → `[hello64_v2] H.3: exiting with code 42`
    - [√] post-exec sentry：canary=2 / kfault_delta=0 / exec_count=1 / exec_fail=0 / pending_exec=0 / exec_rounds=1
    - [√] SMP=1 + SMP=4 双矩阵 Stages 1-30 全 PASS，基线条款保住
  - [√] **H.4 SysV-style argv/argc 传参链路（initial spawn + execve 双路径）**（commit 待写入）
    - [√] `user/crt0.S` + `user/crt0.c`：新增独立 `_start` —— `pop argc` / `mov rsp argv` → `andq $-16, %rsp` 对齐 → 调用 `openos64_start(argc, argv)`，再调用 user main，最后 `SYS_EXIT`
    - [√] `user/openos64.h`：新增 `X86_64_USER_ARGV_MAX=8` / `X86_64_USER_ARG_MAX=64` 限额 + `openos64_runtime_info_t` 运行时元数据
    - [√] `usermode64.{h,c}`：新增 `arch_x86_64_usermode_set_args(argc, argv)` / `clear_args()` API，内核侧 `usermode_arg_storage[ARGV_MAX][ARG_MAX]` 静态缓冲拷贝（避开 ring3 字符串 lifetime）；`seed_user_stack` 改造为标准 SysV 程序启动帧布局：① 字符串拷到栈顶 ② 16B 对齐 padding ③ NULL envp + NULL argv terminator ④ argv 指针数组 ⑤ argc word —— crt0 直接 `(rsp)=argc, 8(rsp)=argv` 提取
    - [√] `syscall_dispatch64.c`：SYS_EXEC dispatch 在拷 path 后再拷 argv 字符串到 `usermode_arg_storage` 然后 `mark_exec`
    - [√] `kernel64.c`：initial spawn 前调 `arch_x86_64_usermode_set_args(1, {"/bin/launcher"})` —— 关键修复见下
    - [√] `user/launcher.c` 改造为 H.4 demo：execve("/bin/hello64_v2", ["hello64_v2", "from", "launcher", "H.4"], NULL)
    - [√] `user/hello64_v2.c` 改造为 argv-aware 打印：`for i in argc: print argv[i]`
    - [√] **修 bug：`.data` 段 file-scope `static const char *initial_argv[]` 在 UEFI kernel 加载后运行时读出 ptr=0**（ELF 文件中 .data filesz=0x19 已写入正确指针 0x80011064，但运行时读到 0；rodata `"/bin/launcher"` 字符串本身在 .rodata 正常）→ H.4 commit 内临时用 stack-local `const char *initial_argv[2]` 绕开，**根因已在 H.4-fix commit 修复**（见下）
  - [√] **H.4-fix `.data` 段根因修复（linker PHDRS 拆分 .bss 独立 PT_LOAD）**（commit 待写入）
    - [√] **根因定位**：`readelf -lW kernel64.elf` 显示 `.data + .bss` 被 ld 合并到同一 PT_LOAD，p_memsz=0x24A000（2.3 MB，全是 .bss 静态数组）→ UEFI `AllocateAddress(0x220000)` 因该物理区被占用而失败 → fallback `AllocateAnyPages` 把段落到 `0x1DC34000` → 但 kernel boot64.asm 写死页表把虚拟 `0xFFFFFFFF80020000+` 映射到物理 `0x220000+`（空白页全 0）→ kernel 读 .data 全部读到 0
    - [√] 铁证证据链：`[UEFI][elf-dbg] PT_LOAD vaddr=FFFFFFFF80020000 paddr=00220000 filesz=0x19 memsz=0x24A000` / `AllocateAddress FAILED status=0x8000000000000009 (EFI_NOT_FOUND)` / `fallback AllocateAnyPages` / `landed at phys=0x1DC34000` / `[argv-dbg] &initial_argv_dbg=0xFFFFFFFF80020000 [0]=0x0000000000000000`（vs `objdump -s -j .data` 显示文件中是 `0x80011064`）
    - [√] `linker64.ld`：新增显式 `PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); bss PT_LOAD FLAGS(6); }`，每个段 `: :phdrname` 显式归属 → 现在 .bss 独立成 PT_LOAD，p_filesz=0、p_memsz=0x23A000；.data 单独成 PT_LOAD，p_filesz=0x88、p_memsz=0x88（4KB align 后 1 页），AllocateAddress 必成功
    - [√] `kernel64.c`：把 H.4 的 stack-local workaround 改回 `static const char *initial_argv[2] = { "/bin/launcher", NULL }`（语义洁净），运行时验证 argv[0] 正确读到 `/bin/launcher`
    - [√] `elf64.h`：清理 H.4-fix 调试期 `[UEFI][elf-dbg]` 输出，仅保留 AllocateAddress 失败路径的诊断日志（带 vaddr/req/pages/status，便于未来回归）
    - [√] `readelf -lW kernel64.elf` 验证：PT_LOAD ×4（text/rodata/data/bss），bss 段 p_filesz=0 已落实
    - [√] 串口证据：`[launcher] argv[0]=/bin/launcher` ✓ / `[hello64_v2] argv[0..3]=hello64_v2/from/launcher/H.4` ✓ / `exit_code=0x2A` / `kfault_delta=0` / `exec_count=1 exec_fail=0`
    - [√] SMP=1 + SMP=4 双矩阵 Stages 1-30 全 PASS，基线条款保住
  - [ ] H.5+ 待规划：~~envp 链路~~（✓ H.5a）、独立地址空间 + CR3 切换、fork、wait/waitpid、ELF 解释器、动态库
  - [√] **H.5a envp 真传参链路（initial spawn + execve 双路径）**（commit 待写入）
    - [√] `usermode64.h`：新增 `X86_64_USER_ENVP_MAX=8` / `X86_64_USER_ENV_MAX=128` 限额 + `arch_x86_64_usermode_set_envs/clear_envs/pending_envc` 三件套 API（与 argv 三件套对称）
    - [√] `usermode64.c`：新增 `usermode_env_storage[ENVP_MAX][ENV_MAX]` 静态缓冲（与 argv storage 同生命周期）；`seed_user_stack` 改造为标准 SysV 完整 program-startup frame：① env strings 拷到栈顶（最高地址）② argv strings 紧跟其下 ③ 16B 对齐 padding（依据 `(argc+envc) & 1` 决定是否补 8B 字）④ NULL envp terminator + envp 指针数组 ⑤ NULL argv terminator + argv 指针数组 ⑥ argc word —— crt0 通过 `envp = argv + (argc+1)*8` 计算出 envp
    - [√] `syscall_dispatch64.c`：SYS_EXEC dispatch 在拷 argv 后再拷 envp 字符串到内核侧 `envp_storage[ENVP_MAX][ENV_MAX]` 静态缓冲然后 `set_envs(envc, envp_ptrs)`；日志输出 `[x86_64][exec] ... argc=N envc=M`
    - [√] `kernel64.c`：initial spawn 前补调 `arch_x86_64_usermode_set_envs(2, {"BOOT_STAGE=H.5a", "OPENOS_BOOT=uefi"})`，验证 spawn 路径与 execve 路径 envp 处理一致
    - [√] `user/crt0.S`：扩展为 SysV 三参 ABI —— `(rsp)=argc` → rdi、`8(rsp)=argv` → rsi、`envp = argv + (argc+1)*8` → rdx，然后 `andq $-16, %rsp` 对齐 → call openos64_start
    - [√] `user/crt0.c` + `user/openos64.h`：`openos64_start` / `openos64_main` 签名扩展为 `(int argc, char **argv, char **envp)`，三个 main 都升级（hello64 / hello64_v2 / launcher）
    - [√] `user/launcher.c`：execve 第三参数从 NULL 改为 `{"PATH=/bin", "HOME=/", "OPENOS_STAGE=H.5a", NULL}`；自身也打印 argc/argv + envc/envp 证明 initial spawn 链路
    - [√] `user/hello64_v2.c`：增加 envp 遍历打印 `for j in envc: print envp[j]`
    - [√] 串口证据：
      - `[launcher] argc=1 / argv[0]=/bin/launcher` ✓
      - `[launcher] envc=2 / envp[0]=BOOT_STAGE=H.5a / envp[1]=OPENOS_BOOT=uefi` ✓
      - `[x86_64][exec] path=/bin/hello64_v2 ... argc=0x4 envc=0x3` ✓
      - `[hello64_v2] argc=4 argv[0..3]=hello64_v2/from/launcher/H.4` ✓
      - `[hello64_v2] envc=3 envp[0..2]=PATH=/bin/HOME=//OPENOS_STAGE=H.5a` ✓
      - `exit_code=0x2A` / `exec_count=1 exec_fail=0` / `pending_exec=0` / `kfault_delta=0` / `post-exit-sentry PASS`
    - [√] SMP=1 + SMP=4 双矩阵 Stages 1-30 全 PASS，基线条款保住
  - [ ] H.5b+ 待续：独立地址空间 + CR3 切换、fork、wait/waitpid、ELF 解释器、动态库
    - [√] H.5b.1 @ `0d092bf`：接线骨架（零行为变化）
      - `build.sh` 注册 `kernel/address_space64.c`，`address_space64.{c,h}` 入库（PML4 克隆/内核高半区镜像/CR3 load/destroy 全 API 就位但未被调用）
      - `proc64.h` PCB 新增 `struct x86_64_address_space *as`（前置 forward decl），生命周期三处（init/spawn_user/proc_exit）显式置 NULL
      - 编译通过 + 双矩阵 SMP=1/4 Stages 1-30 全 PASS，`exit_code=0x2A`，`kfault_delta=0`，`vfs.nodes=7`，`mapped_pages=5`
    - [ ] H.5b.2：ELF loader 加载到目标 AS（PMM 分页 + map_to_as，initial spawn 用之；CR3 暂不切）
    - [ ] H.5b.3：用户栈搬家到 PML4[1] + trampoline iretq 前 CR3 切换
    - [ ] H.5b.4：去 U 位收口（PML4[0] 内核低 4GiB 叶子去 U，只保留必须共享的项）

---

## P0-MODERN：现代操作系统能力缺口清单（2026-07-05 评估新增）

> 背景：截至 x86_64 主线（UEFI + GUI 桌面 + 抢占式 SMP 调度 + fork/exec/wait + FAT32 只读/短名写）已具备成熟玩具 OS 水平。以下为对标「现代操作系统」仍缺失的能力，按优先级分梯队。评估依据：网络栈 net64.c 仅 socket 骨架无真实收发包、无网卡驱动、PCI/USB/声卡仅有头文件无实现、syscall 仅 22 个、无信号机制、无 AHCI/NVMe。

### M1：网络栈从零到通（🔴 第一优先级 · 现代 OS 硬指标）

- [ ] M1.1：PCI 总线枚举（所有后续设备驱动的前置基础） ✅ 已完成
  - [√] `pci.h` 补全实现：Config 空间读写（0xCF8/0xCFC 端口机制）
  - [√] 全总线/设备/功能扫描，枚举 vendor/device/class/subclass（含 PCI-PCI 桥递归）
  - [√] BAR 解析（IO/MMIO 基址与大小探测、支持 64 位 BAR）
  - [√] 中断线（IRQ line / pin）读取（MSI/MSI-X 待网卡阶段按需扩展）
  - [√] `pci_dump_devices` 调试输出 + 开机自动 pci_scan_all（lspci 风格串口打印）
  - [√] 实机验证：QEMU i440fx 扫到 5 设备（主桥/ISA/IDE/ACPI/VGA），VGA BAR0=fb@0x80000000 16MB 反向印证 BAR 解析正确
  - [√] 提供使能 API：pci_enable_bus_master / mmio / io；查找 API：pci_find_by_class / by_id / get_device
- [ ] M1.2：网卡驱动（至少一款可在 QEMU 跑通） ✅ 已完成
  - [√] 首选 virtio-net（QEMU 默认、实现最简）：legacy PCI(BAR0 IO) + split virtqueue tx/rx ring
  - [√] virtio.h 补充 split virtqueue 结构（virtq_desc/avail/used + virtqueue_t 运行时）
  - [√] virtio_net64.c：特性协商(MAC+STATUS) + 读 MAC + RX 缓冲池投递 + DRIVER_OK
  - [√] API：virtio_net_init / device_count / get_mac / send / poll_recv / dump
  - [√] 接入 pmm 物理连续多页分配（identity map 低512MB, PFN=phys>>12）
  - [√] build.sh 编译+链接接入；build+run.bat QEMU 加 -netdev user + virtio-net-pci
  - [√] 实机验证：PCI 扫到 1af4:1000，MAC=52:54:00:12:34:56，vq0/vq1 size=256，DRIVER_OK
  - [ ] 备选 e1000 / rtl8139（待需）
  - [ ] 网卡收包中断接入 IOAPIC（当前为轮询收包，后续改中断驱动）
- [ ] M1.3：链路层 + 网络层 ✅ 已完成
  - [√] 以太网帧收发（EtherType 分发：ARP/IPv4）
  - [√] ARP（请求/应答 + 缓存表，pcap 实测 who-has/reply 交互）
  - [√] IPv4（校验和、路由表最小实现：本网段直连 / 默认网关；分片重组待需）
  - [√] ICMP（可主动 ping 网关 + 可被 ping；pcap 坐实 Echo Request/Reply）
  - [√] netstack.c(908行) + netstack64.h，静态配置 IP 10.0.2.15/GW 10.0.2.2
  - [√] 实机验证：串口 [net] PING PASS 网关可达 + ARP缓存1条；pcap 4帧(ARP req/reply+ICMP req/reply)
- [ ] M1.4：传输层 ✅ 基本完成（TCP状态机已可用，高级优化待补）
  - [√] UDP（收发 + 端口分发）
  - [√] TCP 完整状态机：三次握手（主动connect/被动listen）+ 四次挥手 + 数据收发 + 累积ACK + 超时重传（net_tick驱动）
  - [√] socket 层接入：net_tcp_open/listen/send/recv/close/available 全实现
  - [√] 实机验证：串口 TCP SYN已发出/SYN_SENT；pcap坐实SYN段(port45000->80,SYN flag,伪首部校验和正确)
  - [ ] 高级：滑动窗口流控、乱序重组、拥塞控制、SACK（后续优化）
- [√] M1.5：TCP/IP 协议栈验证 + 应用层打通 —— ICMP Echo + TCP 三次握手实测
  - [√] ICMP：ping 网关 10.0.2.2，收到回显应答（PING PASS）
  - [√] TCP：主动 connect 10.0.2.100:8888，SYN→SYN-ACK→ACK 进入 ESTABLISHED（TCP PASS）
  - [√] pcap 坐实：tools/pcap_stat.py 解析 net.pcap —— ARP×4 / DHCP×4 / ICMP×2 / TCP-SYN×2 / SYN-ACK×1 / ACK×4
  - 工具：test_net.bat（guestfwd cmd 模式）+ tools/stdio_echo.py + tools/pcap_stat.py
  - [√] DHCP 客户端（自动获取 IP）—— DISCOVER/OFFER/REQUEST/BOUND 四步握手实测打通（IP=10.0.2.15 GW=10.0.2.2 DNS=10.0.2.3，commit 34804ea；根因：广播 IP 曾误走 ARP 解析，已改为直接填广播 MAC ff:ff:ff:ff:ff:ff）
  - [√] DNS 解析 —— A 记录查询/响应实测打通（example.com -> 104.20.23.154，穿透 SLIRP 打到真实外网 DNS 10.0.2.3；支持指针压缩 0xC0 跳转 + CNAME 跳过 + 16 条缓存 + IP 字面量直返；pcap 坐实 DNS×2；net_dns_resolve() API）
  - [√] 用户态 `ping` / `ifconfig` / `nslookup` 真正可用（ring3 端到端打通，commit be0aaf8）—— ifconfig 全字段正确显示 (10.0.2.15/255.255.255.0/gw/dns, UP)；ping 10.0.2.2 三次全 reply；nslookup example.com 解析成功。核心修复两处：① do_exit 返回死锁（mark_exited 切回 kernel proc 致 return_to_kernel RSP=0，新增 snapshot_return_rsp 提前快照）；② net syscall 忙等轮询 NIC 时 IF=0 中断关闭致 virtio RX 饥饿全 timeout，在 net_ping_ipv4/net_dns_resolve 入口显式 sti。三工具接入 build.sh 编译+embed，initrd 注册 /bin/{ifconfig,ping,nslookup}
  - [√] 用户态 `wget` 真正可用（HTTP GET，替换现有空壳）—— ring3 端到端打通（commit f41d8ca/7e41fdb）。新增 4 个阻塞式 TCP 导出 + `SYS_TCP_*`(460-463) + `SYS_HTTP_GET`(464)；`net_http_get_buf` 把 HTTP 响应真写回用户缓冲（上限 1MiB）；wget64.c `-1` one-shot 模式。
  - [√] **网络工具接入 GUI 终端全链路打通**（commit 593078c/b0f4f53/90ea58a/d20f18e/M2.4）—— GUI 终端实测 `nslookup`/`ping`/`wget` 三工具均可用且**不卡界面**。关键里程碑：
    - GUI 终端内建别名 `gui_net_alias_match()`：`wget`/`ping`/`nslookup`/`ifconfig` 免 `run /bin/` 前缀（593078c）
    - 修复卡死根因①：`http_pump`/`net_ping_ipv4_impl`/`net_dns_resolve` 的 busy-poll 循环加 yield（b0f4f53/90ea58a）——但 GUI 终端走 `launch_path` 同步阻塞时无其他 kthread 可切，yield 为空转，治标不治本
    - **正统异步化**（d20f18e）：新增非阻塞 `net_ping_start/poll` + `gui_nettool` 状态机（RESOLVING/PING_WAIT/CONNECTING/SENDING/RECV/DONE），仿 `browser_load_tick` 挂进 GUI 主循环；网络别名不再走同步 `launch_path`，启动状态机后立即返回，提示符由 DONE 回调显示
    - **补真实非阻塞 DNS**（M2.4）：发现 `gui64_stubs.c` 里 `dns_query_a`/`dns_get_state`/`dns_get_last_result` 是空桩直返 -1 从未接网络栈（headless 走内核内直调 `net_dns_resolve` 绕过桩，故一直假 PASS）；新增 `net_dns_query_start/state/result` 非阻塞 DNS 状态机，三桩改为转发
    - ⚠️ 编号说明：本网络工具链里程碑内部用 M1.6~M2.4，与下方 M2（现代存储设备）体系不同名，勿混淆
  - [x] 自研浏览器接入真实 HTTP 请求（DNS+真实TCP三次握手+HTTP/1.0 GET；冷ARP首连加120轮+换端口重连兜底，QEMU实测 HTTP/1.1 200 OK 827B PASS）

### M2：现代存储与设备（🟠 第二优先级）

- [√] M2.1：AHCI/SATA 驱动（替代/补充 ATA PIO，支持现代 SATA 盘 + DMA）
  - [√] AHCI 控制器/端口探测（ich9-ahci，PCI class 0x0106）+ HBA 复位 + 端口启停
  - [√] IDENTIFY DEVICE + READ/WRITE DMA（command list/FIS/PRDT 构建，轮询 CI 完成）
  - [√] 接入 blockdev 抽象层（sda 注册为 blockdev_ops）+ selftest write/read/verify
  - [√] QEMU 实测：`[ahci] selftest: write/read/verify PASS @ lba=131062` → `SATA disk selftest PASS`
  - [√] 中断驱动（MSI）—— PCI MSI cap 解析(cap@0x80) + Message Addr/Data 编程 + INTx 屏蔽；IDT 放行 MSI 向量段 0x30-0x3F(AHCI=0x30)；isr64.S 仿 mouse 模板加 stub + LAPIC EOI；延迟安装 `ahci_irq_install_late()`(storage init 早于 LAPIC，故拆出在 apic_selftest 后调用) + sti/cli 包裹自测窗口；QEMU 实测 `AHCI interrupts DID fire (MSI path live)` 中断真实触发；全程保留 polling 超时回退安全网
- [√] M2.2：NVMe 驱动（现代 SSD 主流接口）—— PCI 枚举/控制器初始化/Admin 命令/IO 读写自检/时序陷阱根治/blockdev 接入/MSI-X 中断 7 项子任务全部完成；QEMU headless 复测无回归：selftest PASS @lba=131068、FAT32 挂载读 HELLO.TXT 正常、MSI-X IRQ-path PASS（运行时优选轮询）
  - [√] PCI 枚举识别 NVMe(class 0x0108) + BAR0 MMIO 映射 + CAP/版本读取
  - [√] 控制器初始化：复位、Admin SQ/CQ 创建、CC.EN 使能、CSTS.RDY 等待
  - [√] Admin 命令：CREATE IO CQ/SQ + IDENTIFY controller/namespace
  - [√] IO 命令：阻塞式 READ/WRITE(nvme_submit 轮询 phase tag) + 自测 write/read/verify PASS
  - [√] 根治“IDENTIFY 成功但数据全零”：`nvme_submit` 完成判定后读 DMA 前加 `mfence`，敲门铃前加 `sfence`（“加 klog 就好”的时序陷阱，同 headless 假 PASS）
  - [√] 接入 blockdev 抽象层 + 与 FAT32/VFS 挂接（新增 `src/kernel/drivers/blockdev.c` 统一注册表/分发层 + `src/arch/x86_64/gui64/blockdev_hw.c` 硬件适配层，把 nvme0/sda/hda/hdb 包装为 blockdev_ops 注册；kernel64 启动时序：驱动 selftest→`blockdev_register_hw_devices()`→FAT32 mount；QEMU 实测 `[x86_64][blockdev] registered hw block devices` PASS，FAT32 读写 HELLO.TXT 正常）
  - [√] 中断驱动（MSI-X）—— PCI MSI-X cap 解析(cap@0x40,bir=0) + BAR 映射 + table entry0 编程 + enable；IDT NVME=0x31；延迟安装 `nvme_irq_install_late()`；QEMU 实测 `MSI-X enabled`；运行时**优选轮询**（教训：曾试 hlt 强等中断，因早期无 LAPIC timer 唤醒致挂起，回退轮询优先——与 Linux nvme polling queue 思路一致，NVMe 完成比 MSI-X 消息经 PCI 写事务投递还快）；全程保留 polling 安全网
- [√] M2.3：USB 栈（`usb.h` 补实现）——xHCI 控制器 + HID 键鼠 + U 盘大容量存储全部完工
  - [√] xHCI 控制器驱动（USB 3.x，现代机型主流）—— 新增 `src/arch/x86_64/gui64/xhci64.{c,h}`：CAP/OP/Runtime/Doorbell 寄存器映射 + DCBAA + Command Ring + Event Ring(ERST) + 端口枚举 + Slot/EP Context；MSI-X 中断路径(XHCI_VECTOR) + polling 安全网；isr64.S 新增汇编 stub + kernel64.c init 接线；QEMU headless 实测 xHCI selftest PASS + IRQ 路径 PASS，AHCI/NVMe 无回归，i18n 157 译文正常，无 panic（commit `3850d48`）
  - [√] USB HID（键盘/鼠标）—— xHCI 挂 `usb-kbd`(proto=1)/`usb-mouse`(proto=2)，`xhci64.c` HID 层完整实现：枚举(GET_DESCRIPTOR/SET_CONFIG/Configure Endpoint/SET_PROTOCOL)、中断 IN 端点 arm、差分报文解析、shift/修饰键映射，按键上报走 `gui_post_key_code_with_modifiers`
    - [√] 多设备枚举验证 —— headless 实测 slot1(kbd)+slot2(mouse) 双设备完整枚举，`ep_enable epid 3`(中断 IN) 双设备均成功
    - [√] 中断 IN 端点数据传输链路打通 —— 新增 `tools/qmp_inject.py` + `run_inject_test.bat`(QMP `input-send-event` 注入)，**首次在 headless 下让中断 IN 端点产生真实数据传输**：trace 坐实 `ep_kick epid 3`→`xfer_start`→`packet ep1 setup→complete`→`xfer_success len 8`(鼠标报文穿过中断端点)。之前几轮 `xfer_success` 全是 EP0 枚举流量、epid 3 恒为 0 的困境已破
    - [√] 根因确认：驱动代码无 bug，此前“卡死”纯粹是 headless 缺真实输入源(相对鼠标不动即 NAK 无中断数据)；`build+run.bat` GUI 段去掉 `usb-kbd` 消除与 PS/2 键盘的锁屏输入抢占冲突
    - [√] GUI 模式真机人工验收 —— 用户在 GUI 窗口实测鼠标光标跟随移动、锁屏界面敲密码字符正常输入，USB HID 输入闭环最终确认通过
  - [√] USB 大容量存储（U 盘）—— BOT(Bulk-Only Transport)+SCSI 命令集实现：bulk IN/OUT 端点配置、CBW/CSW 传输、INQUIRY/READ CAPACITY/READ(10)/WRITE(10)。**关键 bug 修复**：Input Context 里 bulk EP 槽位 off-by-one（ICC 占 idx0，故 EP dci=N 须写 idx=N+1，原代码误用 idx=dci），devctx 回读验证 dequeue 指针精确对齐(OUT[2]=0x0D7B9001/IN[2]=0x0D7BA001)；QEMU headless 实测 16MB U 盘全链路 enumerate→INQUIRY→CAPACITY(32768 blocks)→attach OK，读写自检采用安全策略(备份→写图案→读回逐字节比对→原样恢复)，WRITE/READ/VERIFY PASS @lba=32760（commit `3dd7dc6`）
- [√] M2.4：声卡/音频（`sound.h` 补实现，AC97 + PCM 播放 + PC Speaker beep）—— 完整读写闭环打通、DMA 播放实测 PASS（commit `feat(M2.4)`）
  - [√] 音频管理层 `src/arch/x86_64/gui64/sound.c`：PCI 探测 Multimedia(class 0x04) → AC97(0x01)/HDA(0x03) 分类登记；PC Speaker 蜂鸣器（PIT ch2 端口 0x42 + 8255 门控端口 0x61）；设备表/统计/lspci 风格打印
  - [√] AC97 驱动 `src/arch/x86_64/gui64/ac97.c`：Intel 82801AA(8086:2415) codec —— NAM/NABM 双 BAR 解析 + codec 复位 + 音量设置 + VRA 可变采样率(48000Hz) + BDL(Buffer Descriptor List) DMA + 方波 PCM 播放自检
  - [√] `kernel64.c` 接线 `sound_init()`（挂在 xHCI 之后）；`build.sh` 加编译项；`build+run.bat` 加 `-device AC97`
  - [√] QEMU headless 实测：探测 AC97 8086:2415 io=0xC000 irq=10、NAM=0xC000/NABM=0xC400、VRA on rate=0xBB80(48kHz)、PCM Out DMA 启动 civ 0→1 + sr=0x08(BCIS) + picb 0x2000→0x1D2A（DMA 真实消费样本），`playback VERIFIED` + 汇总 2 设备 ac97=1

### M3：文件系统完善（🟠 第二优先级）

- [√] M3.1：FAT32 完整读写（LFN 长文件名 / mkdir / rm 收尾，见阶段4-4）
  - [√] **LFN 长文件名写入**：`lfn_checksum()`（8.3 短名校验和绑定）+ `name_is_pure_83()`（判纯 8.3，含小写/空格/多点则走 LFN）+ `gen_short_name()`（生成唯一 `NAME~N` 短名，探测冲突 ~1..~99）+ `place_dir_entry()`（连续 N×LFN + 1×8.3 多槽写入，跨簇则扩展目录簇，倒序序号 seq n..1 首项带 0x40，每项 13×UCS-2）
  - [√] **fat32_write_file 改造**：支持长名自动 LFN；覆盖同名先 `remove_dir_entry_by_name()` 清旧项（含所有 LFN 项，可跨簇标记 0xE5）并释放旧簇链
  - [√] **fat32_mkdir**：分配目录内容簇+清零，写 `.`(首簇=自身)/`..`(首簇=父，根约定 0) 两项，父目录挂 LFN+8.3（attr=0x10），同名拒绝
  - [√] **fat32_delete**：文件/空目录删除，标记所有目录项（LFN+8.3）0xE5 + 释放簇链；非空目录（除 `.`/`..`）拒绝
  - [√] **selftest 验证（headless QEMU 全绿）**：8.3 写 `WRITE VERIFY OK`、长名 `/My Long File Name.txt` `LFN VERIFY OK`(25B)、`mkdir /NEWDIR`+`/NEWDIR/inside.txt` `MKDIR+WRITE VERIFY OK`、`delete /OSWRITE.TXT` `DELETE VERIFY OK(gone)`、`rmdir /NEWDIR` `RMDIR VERIFY OK`
- [√] M3.2：ext2/ext4 只读（对接 Linux 生态镜像）
  - [√] **磁盘结构解析**：superblock（rev1 动态特性 + 大 inode 256B）、块组描述符（32B 传统 + 64B ext4 64bit 扩展）、inode、目录项（含 file_type）
  - [√] **块映射双路径**：传统直接块(0-11) + 一/二/三级间接块；ext4 extent 树（叶子/索引节点递归下钻，未初始化 extent 处理）
  - [√] **MBR 分区自动探测**：LBA0 扫描 0x83 Linux 分区，无分区表则整盘解析
  - [√] **公共 API**：`ext4_mount`（依赖注入扇区读回调）/`ext4_list`（目录遍历+回调）/`ext4_read_file`（多级路径+间接块）/`ext4_stat`/`ext4_version`（ext2/3/4 判定）
  - [√] **接线**：kernel64.c 挂在 AHCI/SATA 盘（openos-ahci.img 整盘 ext2），`ext4_ahci_read_adapter` 适配 32→64bit LBA；build+run.bat 用 `tools/mkfs_ext_ahci.sh`（mkfs.ext2 + debugfs 免 root）植入测试数据
  - [√] **selftest 全绿（headless QEMU 实测）**：ext2 挂载(block=1024/inode=256/8组)、根目录列 7 项、`/hello.txt` READ VERIFY OK、`/subdir` SUBDIR VERIFY OK、`/subdir/inside.txt` NESTED READ VERIFY OK、`/big.dat`(40000B) INDIRECT-BLOCK VERIFY OK
- [√] M3.3：统一 VFS 多类型挂载（mount/umount 任意 FS 到任意挂载点）
  - [√] **发现真正的 VFS 分发层**：`vfs64.c`(106行) 仅 initrd 内存映射（废壳），真正的统一 VFS 是 `ramfs64.c`，内含 `/mnt/fat`→FAT32 硬编码分发；M3.3 照此模式对称补齐 ext 分支
  - [√] **新挂载点 /mnt/ext**：`ext_match()` 路径匹配 → ext4_64 只读驱动
  - [√] **独立 fd 区间隔离**：ramfs(<4096) / fat([4096,8192)) / ext(≥8192) 三段互不串号
  - [√] **六个 VFS 接口全接 ext 分支**：`vfs_open`/`vfs_read`/`vfs_close`/`vfs_seek`/`vfs_readdir`/`vfs_stat`；open 整文件缓冲到内核堆（只读，`O_CREAT`/`O_TRUNC`/写模式拒绝）
  - [√] **dirent 归一化**：ext4_dirent_t → 统一 `dentry_t`/`inode_t`（FS_DIR/FS_FILE + mode 位）
  - [√] **selftest 全绿（headless QEMU）**：`/mnt/fat` readdir/stat/open/read（HELLO.TXT+长名）、`/mnt/ext` readdir 列 7 项(含<DIR>)、`/mnt/ext/hello.txt` stat size=0x19+read OK、`/mnt/ext/subdir/inside.txt` 嵌套路径解析读取成功
- [√] M3.4：文件权限模型（rwx / uid / gid / 属主）
  - [√] **inode 权限元数据已就绪**：统一 inode_t 本就含 mode(rwxrwxrwx+类型)/uid/gid/nlinks；node_create 补充 uid/gid = 创建者凭证
  - [√] **权限检查核心 vfs_check_perm(ino,uid,gid,want)**：owner>group>other 三级 rwx 判定，root(uid=0) 绕过（文件 rwx 均过、执行位需至少一个 x、目录总可搜索）；VFS_MAY_READ/WRITE/EXEC 位定义
  - [√] **vfs_access(path,want)**：以当前进程凭证（arch_x86_64_proc_current_uid/gid）检查路径访问权限
  - [√] **chmod/chown 实现**：vfs_chmod 仅保留低12位权限/粘性位保类型不变（仅属主/root可改）；vfs_chown 非特权不能移交属主、非属主且非特权不能改组（(uint32_t)-1 保持不变）
  - [√] **凭证来源**：发现 proc 实际凭证接口为 arch_x86_64_proc_current_uid/gid（无 caps 字段/has_cap），特权统一以 uid==0 为准
  - [√] **selftest 全绿（headless QEMU）**：check_perm pass=9 fail=0（owner/group/other三级 + root万能 + 目录搜索位）、CHMOD VERIFY OK(0600)、CHOWN VERIFY OK(1234:5678)
- [√] M3.5：软链接 / 硬链接支持
  - [√] **硬链接 vfs_link(old,new)**：新建目录项共享同一 inode 数据体（link_to 指向主节点），nlinks++；不允许硬链目录/目标已存在拒绝
  - [√] **node_body() 重定向**：read/write/truncate/stat 均经 node_body() 找到数据承载节点，硬链接任一名字读写同步
  - [√] **node_free 引用计数 + 数据体转移**：删链接仅递减 nlinks；删主节点但仍有链接时把数据体转移给存活链接并提升为新主，其余 link_to 改指
  - [√] **软链接 vfs_symlink(target,new)**：新建 FS_SYMLINK 节点，data 存目标路径字符串
  - [√] **vfs_readlink(path,buf,size)**：以 want_parent 定位不展开末段，读回目标路径
  - [√] **path_resolve 中间段 symlink 展开**：遇软链接自动跳转目标（绝对/相对路径均支持，hops>16 防死循环）；resolve_relative 辅助相对跳转
  - [√] **selftest 全绿（headless QEMU）**：link pass=5 fail=0（硬链接读/写同步/删源名后链接存活 + 软链接 readlink/目录软链接穿透访问）

### M4：内核接口与进程模型补齐（🟠 第二优先级）

- [x] **M4.1：syscall 扩充** ✅（M4.1a 文件元数据 + M4.1b 内存 + M4.1c IPC + M4.1d 时间/设备/信号 全部完成；dispatch_total 由 29 扩到 **49（0x31）**，dispatch_enosys=0 无兜底命中）
  - [x] **M4.1b 内存：`mmap` / `munmap` / `mprotect` / `brk` / `sbrk`** ✅
        - dispatch 新增 `do_mmap`/`do_munmap`/`do_mprotect`/`do_brk`/`do_sbrk`，全走内核 VMM/heap
        - 修复 `g_heap_slot` 未初始化 bug（brk 懒初始化）
        - headless 全绿：mmap(anon,4K,RW) base=0x0D7BF000 读写回验证 / mprotect(→RO)=0 / munmap=0 / brk(0) 查询非0 / sbrk(+4K) 后 break 前进 4K
  - [x] **M4.1c IPC：`pipe` / `dup` / `dup2`** ✅
        - 新增 `pipe64.[ch]`：匿名管道池（静态 16 槽 × 4KB 环形缓冲，读/写两端独立引用计数，两端均关闭时回收）
        - 升级 `sfdtable64.[ch]`：引入 **OFD（打开文件描述）层**，sfd 槽→带引用计数 OFD（区分 VFS/PIPE 后端）；dup/dup2 共享同一 OFD（共享 VFS 偏移量/管道端），最后一个 fd 关闭时才释放后端
        - dispatch 新增 `do_pipe`/`do_dup`/`do_dup2`，`do_read`/`do_write`/`do_close` 各加 PIPE 分支；SYS_PIPE=244/DUP=242/DUP2=243 接线
        - EPIPE（无读端写返回 -1）/ 非阻塞读（空管道返回 0）语义；阻塞调度集成归 M4.3
        - headless 全绿：pipe(rfd=3,wfd=4) / write=read=10B 内容一致 / dup 共享写端 / dup2 强制槽共享读端 / 全关闭无泄漏
  - [x] **M4.1a 文件元数据：`stat` / `fstat` / `lstat` / `mkdir` / `unlink` / `rename`** ✅
        - 决策 A：将 `open/read/write/close/lseek` 从旧 initrd 只读表**迁移到统一 VFS(ramfs64)**
        - 新增 syscall fd 间接层 `sfdtable64.[ch]`（fd 0/1/2 保留 stdio，≥3 映射 VFS fd）
        - ramfs64 新增 `vfs_lstat`（末段不跟随软链接）/ `vfs_fstat`（按 fd 取 inode）
        - dispatch 接线 SYS_SEEK/STAT/LSTAT/FSTAT/MKDIR/UNLINK/RENAME(465)
        - headless 自测全绿：stat/fstat 视图一致 + mkdir→write→rename→unlink 生命周期校验通过
        - ⏳ `ioctl` 归入 M4.1d 收尾
  - [x] 时间：`nanosleep` / `gettimeofday` / `clock_gettime`（clock_gettime/nanosleep 已在 M4.1 完成；gettimeofday 已在 M4.1d 完成）
  - [x] 进程：`getpid` / `getppid` / `kill`（kill 已在 M4.1d 完成）
  - [x] **M4.1d 时间/设备/信号收尾：`gettimeofday` / `ioctl` / `kill`** ✅
        - 新增 `SYS_GETTIMEOFDAY=466` / `SYS_IOCTL=467`；`SYS_KILL=245` 接线
        - `gettimeofday`：复用单调 uptime 时钟源（RTC 墙钟 epoch 桥接待后续，ABI 不变）
        - `ioctl`：ABI 占位，校验 fd 后返回 -1(ENOTTY)，让 isatty/tcgetattr 探测干净失败（真 TTY 归 M4.4）
        - `kill`：新增 `arch_x86_64_proc_signal(pid,sig)`——SIGKILL/SIGTERM 置 EXITED+128+sig，sig0 存在探测，其余信号接受 no-op（处理器注册归 M4.2）
        - headless 全绿：gettimeofday 单调+usec范围 / ioctl=-1 / kill(self,0)=0 / kill(bogus,0)=-1
- [x] **M4.2：信号机制（内核侧完成）** ✅
  - 新增独立模块 `signal64.[ch]`（零依赖 proc64，内嵌入 PCB）：每进程 `x86_64_sigstate_t`（pending/blocked 位图 + 32 项 disposition 表）
  - signal64 API：state_init / send（SIG_IGN 丢弃 catchable、总记录 KILL/STOP）/ sigaction（KILL/STOP 不可改）/ procmask（KILL/STOP 不可阻）/ next_pending（低号优先）/ consume / default_action（经典缺省处置表 TERM/IGN/CORE/STOP/CONT）
  - PCB 集成：proc_init/spawn 调 state_init；fork 继承 blocked/actions 但清空 pending（POSIX 语义）
  - 重写 `arch_x86_64_proc_signal`：走 signal_send + 对 SIG_DFL 终止类信号立即 EXITED(128+sig)；新增 `proc_sigaction`/`proc_sigprocmask`/`proc_signal_pump`（当前进程，handler 已注册的信号留给 M4.2b trampoline）
  - syscall 接线：SYS_KILL(245) 重接、新增 SYS_RT_SIGACTION(468)/SYS_RT_SIGPROCMASK(469)
  - 自测全绿：sigaction 注册/回读（SIGUSR1→IGN readback=1）、SIGKILL disposition 不可改拒绝、sigprocmask block(SIGUSR2) oldset=0/curmask 含位、KILL/STOP 不可阻被 strip、kill(self,0)=0 / kill(bad,0)=-1 / kill 超范围 signo=-1
  - [x] **M4.2b 用户态 handler 回调 trampoline** ✅（收官，信号机制彻底闭环）
    - proc64 新增 `arch_x86_64_proc_signal_deliver_user()`：挑选最低号「装了用户 handler 且未阻塞」的 pending 信号 → signal64 build_frame 把 sigcontext + restorer 返回地址写到用户栈 → regs 重定向到 handler（rip→handler VA、rdi→signo、rsp→新栈顶）；含 `in_handler` 防重入嵌套、失败回滚 mask/pending
    - proc64 新增 `arch_x86_64_proc_sigreturn()`：从用户栈读回 sigcontext → signal64 restore_frame 字节级还原被打断上下文 + 恢复信号掩码
    - syscall64 两条返回路径均接管：**int80**（rip/rsp/rflags 在 trap frame）+ **native syscall**（rip=rcx、rflags=r11、rsp 在 per-CPU 槽）；dispatch 完成、返回 ring3 前插 deliver_user 检查
    - 新增 **SYS_RT_SIGRETURN(476)** 特判接线，两路各自把恢复的上下文写回对应通道；用户内存走 identity-map（uwrite/uread 即 memcpy）
    - 自测全绿：deliver_user=0x0B（SIG=11 投递）、handler rip=0x401234（重定向 handler VA）、handler rdi=0x0B（signo 就位）、restorer-on-stack=0x401300（返回地址压栈）、sigreturn=0（恢复成功）、restored rip=0x4055AA/rsp=...DC00（被打断上下文字节级还原）、`user-signal trampoline round-trip OK`
    - **指标**：dispatch_total=0x60（96 个），dispatch_enosys=0，全部真实现无 ENOSYS 兜底；M4.1~M4.4b selftest 全链路无回归
- [x] **M4.3：管道与 IPC（匿名管道阻塞语义 + 命名管道 FIFO + 共享内存）** ✅
  - **M4.3a 匿名管道阻塞语义**：pipe64 扩展 poll + 双端 waiter 队列（rwaiters/wwaiters，PIPE64_WAITERS_MAX=8，含 dedup/drain）；syscall 层新增 `pipe_read_blocking`/`pipe_write_blocking`——协作式阻塞（sched_yield + 有限 spin cap 100000 防单线程 bootstrap 死锁），读端空管道有 writer→park，无 writer→EOF；写端满管道有 reader→park，无 reader→EPIPE；每次推进后 drain 对端 waiter 并 slot_wakeup
    - 自测全绿：poll(empty)=0x02(可写)、poll(data)=0x03、waiter add/dedup(=2)/drain(=2)、poll(eof)=0x07(可读|可写|HUP)
  - **M4.3b 命名管道 FIFO**：新增独立模块 `fifo64.[ch]`（名称注册表，路径→pipe64 ring 懒分配，复用全部 ring/阻塞/poll 机制）；pipe64 新增 `pipe64_create_bare`（0/0 refs 供 FIFO 精确管理生命周期）；syscall 新增 SYS_MKFIFO(470) + `fifo_try_open`（do_open 前置钩子，FIFO 路径走命名管道层否则落 VFS，O_RDONLY→读端/O_WRONLY|O_RDWR→写端），复用 SFD_KIND_PIPE 全流程
    - 自测全绿：mkfifo rc=0、dup mkfifo=EEXIST、wfd=3/rfd=4 同路径共享一 ring、write=5/read=5 数据穿 FIFO、关写端后 read=0(EOF 非阻塞)、unlink 后 active=0
  - **M4.3c 共享内存**：新增独立模块 `shm64.[ch]`（System V 风格，PMM 连续物理页 identity-map 直接共享）；接线既有 SYS_SHM_CREATE(300)=shmget / SYS_SHM_MAP(301)=shmat / SYS_SHM_DESTROY(302)=IPC_RMID / SYS_SHM_INFO(333)=size/nattch/base 查询 + 新增 SYS_SHM_DETACH(471)=shmdt；引用计数生命周期（rmid 有 attacher 则延迟到 last detach 释放页）
    - 自测全绿：shmget id=0、同 key 复用同段、双 attach baseA==baseB==0x0D7BE000、nattch=2、跨 attach 共享写验证(page0/page1/末字节)、双 detach 归零、over-detach=-1、rmid 立即释放 active=0
  - **指标**：dispatch_total=0x4F（79 个），dispatch_enosys=0，全部真实现无 ENOSYS 兜底
- [x] **M4.4：伪终端 / TTY 子系统（行规程 + 作业控制 + 前后台进程组）** ✅
  - **M4.4a TTY 行规程引擎**：新增独立模块 `tty64.[ch]`（纯字节机器，零 proc/syscall 依赖，自测友好）：每设备 3 环（行编辑/cooked/output-echo）+ termios 控制块；规范模式（ICANON）按行交付 + ERASE(^H/DEL)/KILL(^U) 行内编辑 + ECHO 镜像（控制字展开为 ^X）；raw 模式逐字节即时可读；INTR(^C)→刷行+锁 SIGINT、EOF(^D)→空行 read 返回 0；ioctl 真实现 TCGETS/TCSETS/TIOCGWINSZ/TIOCSWINSZ/TIOCGPGRP/TIOCSPGRP（兼容 Linux request 号），fd 0/1/2 绑定控制台 tty，非 tty fd→ENOTTY
    - 自测全绿：tty id=1、部分行不可读、整行 readable=4、read-len=4、erase=3、kill 行清、intr-signal=2、EOF read=0、echo=3字节、raw 即时可读、ioctl TCGETS=0、winsize 25×80、非 tty fd 拒绝、pgrp 回读=42
  - **M4.4b 作业控制（进程组/会话）**：PCB 新增 `pgid`/`sid` 字段（init/spawn 初始 pgid=sid=pid，fork 继承）；proc64 新增 getpgid/setpgid/getsid/setsid/signal_group（会话首领不可 setpgid/再 setsid）；单播 kill 重构抽出 `proc_deliver_signal` helper 供组信号复用；syscall 新增 SYS_SETPGID(472)/GETPGID(473)/SETSID(474)/GETSID(475)；do_kill 支持负 pid（kill(-pgid)组广播、kill(0)当前组）；`arch_x86_64_tty_pump_signals` 桥接 TTY ^C→前台 pgrp 组广播（只有前台 job 收信号，无前台组→丢弃）
    - 自测全绿：getpgid(self)=1、getsid(self)=1、setsid(leader)=-1（拒绝）、getpgid(bad)=-1、kill(-pgrp,0)=0、kill(0,0)=0、kill(bad-group)=-1、^C bridge hits=1（信号路由到前台组）、无前台组时 hits=0（弃信号）
  - **指标**：dispatch_total=0x5C（92 个），dispatch_enosys=0，全部真实现无 ENOSYS 兜底

> **M4 里程碑全部完成（100%，无遗留）** ✅：M4.1（内核接口补齐）+ M4.2（信号机制内核侧）+ **M4.2b（用户态 handler 回调 trampoline + sigreturn，信号闭环）** + M4.3（管道与 IPC）+ M4.4（TTY 子系统）。syscall dispatch 从 M4 初的 29 个扩到 **96 个（0x60）**，全部真实现零 ENOSYS 兜底。
> **注**：`prio-selftest FAIL H<=N` 为调度优先级时序统计的偶发抖动（二次运行即 PASS），与信号/IPC/TTY 改动无关。

### M5：生态与运行时（🟡 第三优先级）

- [√] M5.1：动态链接（.so + 动态链接器 + PLT/GOT 重定位）
  - [√] M5.1a：ELF 动态段解析（PT_INTERP/PT_DYNAMIC/.dynamic 表；单测 3/3）
  - [√] M5.1b：静态重定位引擎（RELATIVE/64/GLOB_DAT/JUMP_SLOT 立即绑定/IRELATIVE）
  - [√] M5.1c：跨模块符号解析（全局符号表 16 模块，GLOBAL>WEAK、load-order 优先）
  - [√] M5.1d：真惰性绑定 PLT/GOT（_dl_runtime_resolve trampoline + SYS_DL_RESOLVE=477 + link_map 白名单；单测 6/6，elf64 系列 15/15 全绿）
- [√] M5.2：用户态多线程（clone / pthread / 用户级线程库）
  - [√] M5.2a：clone(478) 线程创建（线程组共享 AS，独立 PCB/内核栈/用户栈；提交 6d3720e）
  - [√] M5.2b：新线程拉进 ring3（独立 iretq frame + 用户栈，entry 复用；context_switch 保存 bootstrap_context）
  - [√] M5.2c：futex（PRIVATE 语义，用户虚拟地址作键；compare-then-park 防 lost-wakeup；协作式 sti/hlt spin-yield；TSC 超时；64 槽静态等待表；宿主机单测 7/7；提交 249b718）
  - [√] M5.2d：极简 pthread 子集（create/join/exit/self + futex-based mutex）；execve double-alloc 时序修复
  - [√] M5.2e：pthread 端到端 ring3 真机 PASS（4 worker + mutex 累加 counter=40000 + join retsum=10；提交 5c84d8c）
    - 真根因：syscall 入口用户 rsp 存 per-CPU 单槽 %gs:0x68，跨阻塞 syscall 被 worker 覆盖 → sysret 用错用户栈 → rip=0 崩溃
    - 修复（L5 FIX）：用户 rsp 改存内核栈上，随内核栈上下文自然保存/恢复，根治单槽竞态
- [√] M5.3：标准 C 库对齐（向 musl/newlib 兼容子集靠拢，便于移植第三方软件）
  - 背景：现有用户态运行时全塞在 `user/openos64.h`（18KB 单头），符号全是私有 `openos64_*`，无标准 C 库符号名（malloc/free/memcpy/memset/strlen/printf 等），第三方软件无法直接链接。M5.3 目标是提供导出**标准符号名**的 libc 子集，建 `user/libc/` 分文件管理。
  - [x] M5.3a：`libc/string.c` + `libc/string.h` — memcpy/memset/memmove/memcmp/memchr/strlen/strnlen/strcmp/strncmp/strcpy/strncpy/strcat/strncat/strchr/strrchr/strstr（纯计算零依赖）✅ 宿主机单测 ALL PASS，提交
  - [x] M5.3b：`libc/stdlib.c` + `libc/stdlib.h` + `libc/libc_sbrk.c` — malloc/free/calloc/realloc（freelist+coalescing，SYS_SBRK=253 扩堆）+ atoi/atol/strtol/strtoul/abs/labs/qsort/bsearch/rand/srand/exit/_Exit ✅ 宿主机单测 ALL PASS + freestanding 编译零警告，提交
  - [x] M5.3c：`libc/stdio.c` + `libc/stdio.h` + `libc/libc_write.c` — putchar/putc/fputc/puts/fputs/fwrite + printf/fprintf/snprintf/vsnprintf/vprintf/vfprintf（sink-based 格式化引擎：%d/i/u/x/X/o/p/c/s/%，flags -+0# space、width/precision、`*`、长度修饰 l/ll/h/hh/z）+ FILE/stdin/stdout/stderr（基于 SYS_WRITE=64）✅ 宿主机单测 ALL PASS + freestanding 编译零警告，提交
  - [x] M5.3d：头文件对齐 — `stddef.h`（size_t/ptrdiff_t/NULL/offsetof，权威 size_t 定义）+ `stdint.h`（exact/least/fast/ptr/max 全宽度 + 限制宏 + INTxx_C）+ `stdbool.h` + `limits.h`（LP64）+ `ctype.h`/`ctype.c`（ASCII C-locale 14 函数）+ `errno.h`/`errno.c`（__errno_location，Linux 数值对齐 futex）+ `assert.h`/`assert.c`（__assert_fail）+ stdlib abort()。✅ 宿主机单测 37 项 ALL PASS + freestanding 编译零警告，提交
  - [x] M5.3e（收官）：build.sh 接入 libc 全部 .c（string/stdlib/stdio/ctype/errno/assert + libc_sbrk/libc_write）编入用户程序构建链；`libc_demo64.c` 调用标准符号（malloc/free/realloc/calloc/printf/snprintf/qsort/bsearch/strtol/str*/mem*）；proc64.c clone 线程组共享 AS 同时继承 brk 游标（堆共享）；QEMU ring3 真机端到端 **34 passed / 0 failed，[libc] PASS，post-exit-sentry PASS，无 PANIC/#PF**，提交 ✅ **M5.3 全系列完成** 🎉

- [x] M5.4：包管理 / 软件安装机制（最小可用的程序分发）✅ **全系列收官**（a 可写 VFS / b .opk 格式+工具 / c 内核安装器 / d opkg CLI / e 端到端闭环，打包→安装→execve 全链路 PASS）
  - 背景：当前所有用户程序（hello64/thread_demo/libc_demo/ifconfig/ping/wget 等）都靠 `embed_*.h` 编译期硬嵌入 initrd，`initrd64.c` 是唯一 consumer。要"安装新软件"必须改内核源码重编，本质上没有运行时分发能力。M5.4 目标：让程序能以**包**的形式在运行时被安装/卸载/查询，落到一个可写的存储层，被 ELF loader 直接加载执行。
  - [x] M5.4a：可写文件系统层（ramfs 读写节点）— 发现内核已有完整可写 VFS（`gui64/ramfs64.c` 1519 行，目录树 + open/read/write/lseek/truncate/mkdir/rmdir/unlink）且 do_exec 已"vfs 优先 initrd 兑底"。本子步补齐：① 用户态 openos64.h 文件 API 封装（lseek/mkdir/unlink/rmdir/stat + openos64_stat_t 精确对齐内核布局 + O_* flags 对齐内核原生值 O_CREAT=0x100/O_TRUNC=0x200）；② 内核补 `do_rmdir` + SYS_RMDIR(232) 分发（原缺失）；③ initrd MAX_FILES 16→32（新增程序超限会导致 mount 失败）；④ exec-chain round cap 4→6（hello→fork→thread→libc→fs 链需 5 环）。`fs_demo64.c` 真机端到端：mkdir/open O_CREAT/write/read/lseek(SET/END)/stat/unlink/rmdir **14 项全 PASS，[fs] PASS**，libc/thread 回归依旧 PASS，无真崩溃（stage 25/26 的 #PF/#GP 是内核自检可恢复探针，本身 PASS），提交
  - [x] M5.4b：包格式定义 + 打包工具 — 定义最小包格式 `.opk`（`src/arch/x86_64/include/opk64.h` 为权威布局：`opk_header_t`{magic="UL46O0PK"/version/entry_cnt/toc_off/data_off/total_size/crc32/pkgname[48]} + `opk_entry_t`[]{name[64]/data_rel/size/mode/crc32} + payload blob，全 LE），host 侧 C 工具 `tools/opkg-build.c`（编译到 `build/host/opkg-build`，参数 `-o out.opk -n pkgname [-e] FILE[:instname]`，自带 zlib 标准 CRC32，header-tail CRC + 每文件 CRC），单测 `tools/test_opk.c`（独立镜像自劫持 CRC 交叉校验：header 字段/TOC/payload 字节精确/损坏检测），构建脚本 `tools/build_opk.sh` + build.sh 新增 `opkg` 子命令。`./build.sh opkg` 端到端 **23/23 ALL PASS**，`-Werror` 零警告，提交 ✅
  - [x] M5.4c：内核侧包安装器 — `.opk` 解析核心 `opk_install()` 采用**纯函数 + FS 回调抽象**（同 M5.3 sink / M5.4b 风格）：`opk_fs_ops_t`{mkdir/write_file/stat} 由调用方注入，宿主机单测用普通 POSIX FS 模拟、内核侧注入 ramfs VFS 回调。安装流程：校验 magic/version/entry_cnt + header-tail CRC + 每 entry CRC → 逐 entry `mkdir -p` 父目录 → `write_file` 释放 payload 到 `/pkg/<pkgname>/<entry>`。内核胶水 `opk_install_kernel.c`（`kfs_mkdir/kfs_write_file/kfs_stat` 包 vfs_*），syscall `SYS_OPK_INSTALL` 先把用户 image **拷进 pmm 页对齐内核缓冲**（绕开 heap64 大块跨页 bug + 保证源不可变）再安装。真机 ring3 端到端 demo `opk_demo64.c`（用户态 build_opk 造包→SYS_OPK_INSTALL→校验 3 文件全安装 + 坏 CRC 包被拒），QEMU headless **6 passed / 0 failed ALL PASS** ✅；host opkg 单测仍 23/23 PASS。
     - **诊断基建**：新增 `M5_FAST_BOOT` 编译开关（默认 OFF，CI/正常构建全 selftest 照常），ON 时跳过单核 QEMU 下时序 flaky 的 selftest（smp stage 15/16、prio-selftest、preempt-selftest）并把首个用户程序直达 `/bin/opk_demo`，供端到端诊断快速通道使用。
     - **修复的真 bug**：`kfs_stat` 的 stat 缓冲 `struct opk_kstat` 仅 16*u32，小于真实 `inode_t`（8*u32+3*ptr+3*vfs_time_t ≈ 100B），`vfs_stat` 溢出栈帧踩掉调用方 path 缓冲 → write_file 收到被截断的 `/pkg` 而非完整路径。扩到 48*u32（200B，留余量）修复。另修 `SYS_SBRK` 重路由到用户堆后内核态 selftest 无 ring3 上下文调用返回 -1 的误报（改为无进程上下文时 SKIP）。
  - [x] M5.4d：用户态包管理器 `opkg` CLI — ring3 程序 `src/arch/x86_64/user/opkg64.c`（freestanding + M5.3 libc string/stdio/stdlib + openos64 syscall 运行时）：`opkg install <file.opk>`（读 .opk 镜像→`SYS_OPK_INSTALL`）/ `opkg remove <pkg>`（递归删 `/pkg/<pkg>/`）/ `opkg list`（枚举 `/pkg/` 下已装包）/ `opkg info <pkg>`（列包内文件清单）。新增 `SYS_READDIR` syscall + 用户 API `openos64_readdir()`/`openos64_dirent_t`（枚举目录项），配套 `openos64_opk_install()`。端到端 selftest `opkg_selftest64.c` 覆盖 build→install→list→info→remove 全链路，QEMU headless（`M5_OPKG_DIAG=1 M5_RING3_CONSOLE=1`）**6 passed / 0 failed ALL PASS** ✅；host opkg 单测仍 23/23 PASS，提交 `bd2901a`。
  - [x] M5.4e（收官）：端到端验证 — 真 ELF `opk_payload64.c`（**不 embed 进 initrd**，仅 M5.3 libc + openos64 运行时）编译为 `opk_payload.elf`，构建期用 host `opkg-build` 打成 `payload.opk`（pkgname=`payload`，exec entry instname=`app`），再经 `_embed_elf.py` 生成 `embed_opk_payload.h` 字节数组嵌入 e2e selftest。运行时 `opkg_selftest64.c` 走 `SYS_OPK_INSTALL` 把镜像释放到可写 ramfs 的 `/pkg/payload/app` → `readdir`/`stat` 校验落盘 → 最后 `execve("/pkg/payload/app")` 加载这份**从未进过 initrd 的真 ELF**。QEMU headless（`M5_OPKG_DIAG=1 M5_RING3_CONSOLE=1`，`run_diag_ring3.sh`）实测：selftest **21 passed / 0 failed**，串口打出 `[x86_64][exec] vfs-load path=/pkg/payload/app` + `[opk_payload] hello from inside an installed .opk package!` + `exiting with code 42`；内核侧 `ELF64 loads=2 ok=2 fail=0`、`ring3 exit_code=0x2A`、`do_exit pid=2` 正常收尾、`post-exit-sentry PASS`、`kfault_delta=0` **无 PANIC/#PF**。host opkg 单测仍 23/23 ALL PASS。至此「打包→安装→execve 运行」完整闭环打通，**M5.4 包管理系统全系列（a 可写 VFS / b .opk 格式+工具 / c 内核安装器 / d opkg CLI / e 端到端收官）✅ 收官**。
    - 备注：`execve` 当前仅支持初始进程重入路径（`usermode_run` 保存的内核帧 + longjmp 重入 ring3）；fork 出的子进程在独立 sched-slot 上下文中调用 `execve` 会因缺少对应重入帧而挂起——此为已知内核能力缺口，非本里程碑范围。故 e2e 收官采用「主进程直接 execve 成为 payload」而非「fork 子进程 execve + waitpid」，等价证明安装物可执行。后续若需 fork+execve（shell 场景）再补 sched-slot 上下文的 exec 重入分支。

### M6：现代体验与安全（🟡 第三优先级）

- [x] M6.1：ACPI 电源管理（关机 / 重启 / 休眠 S3）✅ **关机 + 重启完成**（S3 休眠待后续）
  - [x] M6.1a：FADT（"FACP"）解析器——新模块 `power64.c/.h`（与 SMP 关键路径上的 `acpi64.c` MADT 解析解耦，按功能拆小模块），复用 `arch_x86_64_acpi_info()->xsdt_phys/rsdt_phys` 自行 walk XSDT/RSDT 定位 FADT；提取 PM1a/PM1b_CNT 端口、SMI_CMD、ACPI_ENABLE/DISABLE、RESET_REG(GAS)+RESET_VALUE；对 DSDT 做经典 osdev AML 模式匹配解码 `\_S5` 包（非完整 AML 解释器，在 QEMU/OVMF 鲁棒）得到 SLP_TYPa/b。idempotent，失败非致命。kernel64.c 在 acpi_selftest 后调 `arch_x86_64_power_init()`。
  - [x] M6.1b：`arch_x86_64_power_shutdown()` — ACPI S5 软关机：可选通过 SMI_CMD 切 ACPI 模式→向 PM1a_CNT（及 PM1b_CNT）写 `SLP_TYPa|SLP_EN`；失败回退到 QEMU/Bochs 调试关机端口（0x604/0xB004/0x4004），最后 halt。成功不返回。
  - [x] M6.1c：`arch_x86_64_power_reboot()` — 暖重启：优先 FADT RESET_REG（I/O 或 MMIO）+ RESET_VALUE；回退到 8042 键盘控制器脉冲（0x64←0xFE）；最后 null-IDT 三重故障重置。成功不返回。
  - [x] M6.1 syscall + selftest：新增 `SYS_POWER=480`（a0: 0=shutdown/1=reboot/2=query；query 返回能力位 bit0=ACPI S5 / bit1=FADT reset）+ 用户 API `openos64_power()`。内核态 `power_selftest64.c` 打印并校验 FADT/\_S5 快照（**不真关机**）。QEMU headless 实测：`[power-selftest] PASS`，fadt=0x0FxxxxE0、pm1a_cnt=0xB004、`\_S5` 解码 SLP_TYP=0。另新增 `M6_POWER_DIAG` 编译开关（默认 OFF）：ON 时 selftest 后真触发 ACPI S5——实测 QEMU **7.5s 内 rc=0 干净关机**（非 30s 超时），串口打出 `triggering ACPI shutdown` + `shutdown requested (ACPI S5)`，证明 PM1a_CNT 写入路径生效；默认构建 0 处 trigger（不会自毁）但 selftest 照常 PASS。
- [x] M6.2：CPU 频率/温度管理（P-state / C-state）✅ **观测层完成**（只读：不写 PERF_CTL / 不改 P-state；主动变频待后续）
  - [x] M6.2a：CPUID 能力探测 + 快照骨架——新模块 `cpufreq64.c/.h`（与 acpi64/power64 解耦，按功能拆小模块）。**CPUID 门控 MSR 访问**：每个 rdmsr 都由保证 MSR 存在的 CPUID 特性位护卫，在 QEMU `-cpu qemu64`（无热/P-state MSR）上优雅降级为“unknown”而非 #GP。leaf 0/1/6/0x16/0x80000007 提取 vendor/family/model、能力位（TSC/MSR/InvariantTSC/DTS/Turbo/ARAT/HWP/PkgTherm/FreqLeaf）、base/max/bus MHz。复用 tsc64 PIT 标定频率作 tsc_mhz。
  - [x] M6.2b：P-state 比值→MHz——Intel 且 MSR 能力位且 GenuineIntel 时才读 MSR_PLATFORM_INFO（max_nonturbo/min ratio）+ IA32_PERF_STATUS（cur_ratio），×100 MHz bclk 估算 MHz。
  - [x] M6.2c：数字温度传感器→摄氏度——由 DTS 能力位门控，读 IA32_TEMPERATURE_TARGET 得 Tjmax，读 IA32_THERM_STATUS/IA32_PACKAGE_THERM 得 readout，temp = Tjmax - readout（core/pkg 分开，各带 valid 位）。`arch_x86_64_cpufreq_refresh()` 刷新时变字段。
  - [x] M6.2d：`SYS_CPUINFO=481` + 用户 API `openos64_cpuinfo(openos64_cpuinfo_t*)`（a0=buf ptr a1=sizeof，内核 `validate_user_buf` 校验后拷至多 a1 字节，向前兼容）。内核 `do_cpuinfo` 先 refresh 再拷快照。
  - [x] M6.2 selftest 两层：① 内核态 `cpufreq_selftest64.c`（置于 preempt-selftest 之前以避开单核 QEMU flaky 区，仅依赖 CPUID+已标定 TSC）：默认构建 QEMU headless 实测 **`[x86_64][cpufreq-selftest] PASS`**，vendor=AuthenticAMD、caps=0x3（TSC+MSR）、tsc_mhz=3396（合理）；qemu64 无 leaf 0x16/非 Intel，P-state/温度优雅为 n/a（无 #GP）。② ring3 `cpuinfo_selftest64.c`（`M6_CPUINFO_DIAG` 开关，需叠 `M5_RING3_CONSOLE=1`）：实测 **`[cpuinfo_selftest] ALL PASS` 6 passed / 0 failed**，SYS_CPUINFO 用户态 ABI 端到端走通（拷贝、不变字段一致、幂等二次查询）。host opkg 23/23 无回归。`M6_CPUINFO_DIAG` 已接入 build.sh，cpuinfo_selftest.elf 随 initrd 打包入库。
    - 备注：本里程碑为“观测层”（只读）；C-state 空闲管理（MWAIT/HLT 深度）与主动 P-state 变频（写 IA32_PERF_CTL / HWP_REQUEST）待后续里程碑。
- [x] M6.3：图形加速✅ **整行 blit 加速层完成**
  - [x] M6.3a：内核层整行 blit 原语 `framebuffer_blit_row(x,y,src,count)`（framebuffer64.c）— 32bpp linear 直存后端，目标行起始地址 + 单次叠拷，消除逐像素函数调用+地址重算；水平自动裁剪，返回实写像素数。新增 caps 位 `FRAMEBUFFER_CAP_ROW_BLIT`。
  - [x] M6.3b：热点改造 `gui_flush_rect`（gui.c）——屏幕内裁剪 + 加速路径逐行 blit（回退逐像素）；新增 `flush_row_blits` 统计字段。
  - [x] M6.3 selftest：内核态 `gfx_selftest64.c`（置于 framebuffer_init 之后、preempt-selftest 之前；framebuffer_init 幂等），QEMU headless 实测 **`[x86_64][gfx-selftest] PASS` fb 1280x800**：验证 ROW_BLIT 能力位、NULL/越界拒绝、水平裁剪（返回 3）、写入-读回正确性（探针后恢复原像素不破坏 splash）。host opkg 23/23 无回归。gfx_selftest64.o 已接入 build.sh 链接。
    - 备注：本里程碑为“整行 blit 软件加速”（消除逐像素函数调用）；硬件 2D 引擎（GPU BitBLT）、双缓冲页翻转（double buffer page flip）、DMA 加速待后续里程碑。
  - [x] M6.3d：矩形块 blit 原语（进阶项）✅——可行性评估后的务实落地。**诚实结论**：后端为 UEFI GOP（固件锁定的线性帧缓冲），无 CRTC/扫描地址寄存器访问权，硬件页翻转（double buffer page flip）与 GPU BitBLT 在当前平台不可行；而软件双缓冲（backbuffer + 脏矩形合并 + present）**早已在 gui.c 存在**。因此本子任务职于可落地的真实提速点：将脏矩形 present 从“逐行调用 blit_row”升级为“单次矩形块 blit”，能力位/边界校验从每行一次降为整矩形一次。
    - 内核层：新增 `framebuffer_blit_rect(x,y,src,src_stride,w,h)`（framebuffer64.c）——右/下越界自动裁剪，返回实写行数；新增 caps 位 `FRAMEBUFFER_CAP_RECT_BLIT`。
    - 热点：`gui_flush_rect`（gui.c）新增 RECT_BLIT 最优分支（优先于 ROW_BLIT 与逐像素回退）；新增 `flush_rect_blits` 统计字段。
    - selftest：`gfx_selftest64.c` 扩展至 **9 项**（新增 RECT_BLIT cap / NULL·退化·越界拒绝 / 垂直裁剪返回 2 / 2×3 块写入-读回，均无损探针），QEMU headless 实测 **`[x86_64][gfx-selftest] PASS` fb 1280x800**；cpufreq-selftest 无回归；host opkg 23/23。
    - 未落地（平台限制）：硬件 GPU BitBLT / 硬件扫描地址页翻转 / DMA ——需 virtio-gpu 或 BGA/Bochs VBE 后端（非 GOP）方可开展，待专项驱动里程碑。
- [x] M6.4：virtio-gpu 驱动（解锁硬件加速路径）✅ **modern PCI transport + 2D 管线实机跑通**
  - [x] M6.4a：modern PCI transport `virtio_modern64.c/.h`——virtio-gpu（1af4:1050）为 **modern-only 设备（无 legacy IO 口）**，现有 virtio_net 走 legacy IO 无法复用，新建 modern 传输层：遍历 PCI vendor cap（0x09）定位 common/notify/isr/device 四个 MMIO 窗口并映射（`arch_x86_64_vmm_map_range` + `OPENOS_X86_64_VMM_MMIO_FLAGS` 非缓存）；驱动 reset/ACK/DRIVER/FEATURES_OK/DRIVER_OK 状态握手；split virtqueue setup（pmm 页对齐环内存 + 编程 desc/driver/device 物理地址）；notify（notify_base + off×multiplier）；device-cfg 读写。
  - [x] M6.4b：virtio-gpu 2D 驱动 `virtio_gpu64.c` + 协议头 `virtio_gpu.h`——完整 2D 命令协议（ctrl_hdr/rect/display_info/create_2d/attach_backing/set_scanout/transfer_to_host_2d/resource_flush）；controlq 请求-响应引擎 `gpu_cmd`（双 desc：req 只读 + resp device-write，轮询 used 环）；bring-up 流水线 GET_DISPLAY_INFO→CREATE_2D→ATTACH_BACKING→SET_SCANOUT；线性 32bpp BGRA backing store（identity 物理页）；per-frame `virtio_gpu_flush_rect`（TRANSFER_TO_HOST_2D + RESOURCE_FLUSH，坐标裁剪）。接入 kernel64.c 早期 boot（virtio_net 之后），无设备时安全跳过。
  - [x] M6.4c：实机验证 + selftest——专用诊断脚本 `run_diag_gpu.sh`（挂 `-device virtio-gpu-pci`），内核态 `virtio_gpu_selftest64.c`（置于 gfx-selftest 之后）。**实机实测**：① 挂 GPU 时——`[virtio-gpu] device found` → `2D pipeline ready` → `[virtio-gpu-selftest] scanout w=1280 h=800` → `flush rc=0` → **`[x86_64][virtio-gpu-selftest] PASS`**（geometry/backing/probe绘制/flush/越界裁剪/超大矩形裁剪全通）；② 默认 boot 无 GPU 时——`[virtio-gpu] no device (1af4:1050)` → **`PASS (device absent, no-op)`** 优雅降级。host opkg 23/23 无回归。virtio_modern64/virtio_gpu64/virtio_gpu_selftest64 均已接入 build.sh 编译+链接。
    - 备注：本里程碑解锁了真实硬件加速路径（host 侧 2D resource + scanout present）；cursorq 硬件光标、多 scanout、EDID、virgl 3D 待后续。
- [x] M6.5：framebuffer64 后端切换到 virtio-gpu——整个 GUI 桌面直接经 virtio-gpu present✅ **双后端自动选择实机跑通**
  - [x] framebuffer64.c 双后端：`framebuffer_init()` 优先探测 virtio-gpu（`virtio_gpu_init()` 已在 kernel64 早期 boot 执行），设备存在则将绘制目标 `g_fb_base` 指向其 host-backed backing store（几何取 scanout 尺寸、行距=宽度）并置 `g_fb_present_needed=1`；否则回退 UEFI GOP direct-write。caps.backend = `FRAMEBUFFER_BACKEND_VIRTIO_GPU`。
  - [x] present 接口（framebuffer.h + framebuffer64.c）：`framebuffer_present_needed()` / `framebuffer_present_rect(x,y,w,h)` / `framebuffer_present()`——GOP 后端 no-op（内部快路径返回 0），virtio-gpu 后端转发 `virtio_gpu_flush_rect()`（TRANSFER_TO_HOST_2D + RESOURCE_FLUSH）。
  - [x] gui.c 热点：`gui_flush_rect` 在三路 blit（RECT_BLIT/ROW_BLIT/逐像素）之后、裁剪后脏矩形 (x0,y0)-(x1,y1) 上追加 `framebuffer_present_rect()`；GOP 环境零开销（no-op）。
  - [x] 实机验证（双环境）：① 挂 `-device virtio-gpu-pci`——`[framebuffer] backend=virtio-gpu (present-mode)` → `window_manager_start_desktop rc=0x0` → `[x86_64][gui] desktop up, entering poll loop`（整个桌面经 virtio-gpu present）；② 默认 GOP boot——`[virtio-gpu] no device (1af4:1050)` → `[framebuffer] backend=UEFI GOP (direct-write)` → `desktop up`（零回归）。host opkg 23/23 无回归，x86_64 全量构建通过。（2D blit 加速 / 可选 GPU 驱动 / 双缓冲无撕裂）
- [x] M6.6：virtio-gpu cursorq 硬件光标✅ **实机 HW cursor OK**
  - [x] 协议头 `virtio_gpu.h` 扩展：cursor 命令类型 `UPDATE_CURSOR=0x0300` / `MOVE_CURSOR=0x0301`；`virtio_gpu_cursor_pos_t`（scanout_id/x/y/padding）；`virtio_gpu_update_cursor_t`（hdr + pos + resource_id + hot_x/hot_y + padding）；固定 64×64 光标尺寸常量 `VIRTIO_GPU_CURSOR_W/H`。
  - [x] 驱动 `virtio_gpu64.c`：新建**第 2 队列 cursorq（index 1）**（`virtio_modern_setup_queue`，失败仅禁用硬件光标不影响 2D）；专用 cursor 命令引擎 `cursor_cmd()`（独立 `g_cur_cmd_buf` bounce 页 + 双 desc + 轮询 used 环 + `virtio_modern_notify(GPU_CURSORQ)`）；`gpu_setup_cursor()` 建 64×64 BGRA 光标 resource（RESID=2）+ ATTACH_BACKING 到专用 backing。API：`virtio_gpu_cursor_available()` / `virtio_gpu_set_cursor(src,w,h,hot_x,hot_y)`（填 64×64 精灵 + TRANSFER_TO_HOST_2D 经 controlq + UPDATE_CURSOR 经 cursorq）/ `virtio_gpu_move_cursor(x,y)`（廉价 MOVE_CURSOR）/ `virtio_gpu_hide_cursor()`（UPDATE_CURSOR resid=0）。接入 init 末尾，`g_cursor_ready` 门控。
  - [x] gfx-selftest 第 10 项：`virtio_gpu_cursor_available()` 门控，验证 set/move/hide 三命令，末尾恢复可见光标。
  - [x] 实机验证：挂 virtio-gpu-pci（`M5_FAST_BOOT=1` 绕开单核 preempt-selftest flaky）——`[virtio-gpu] hardware cursor ready` → `backend=virtio-gpu (present-mode)` → `[x86_64][gfx-selftest] HW cursor OK` → `fb 1280x800` → **`PASS`**（全 10 项）。正常构建 GOP 回退 + host opkg ALL PASS 无回归。
- [x] M6.7：virtio-gpu EDID 显示能力协商✅ **实机解析出 1280x800 与 fb 完全吐合**
  - [x] 协议头 `virtio_gpu.h`：`GET_EDID=0x010a` / `RESP_OK_EDID=0x1104`；feature 位 `VIRTIO_GPU_F_VIRGL=0` / `VIRTIO_GPU_F_EDID=1`；`virtio_gpu_get_edid_t`（hdr + scanout + padding）；`virtio_gpu_resp_edid_t`（hdr + size + padding + edid[1024]）。
  - [x] 驱动 `virtio_gpu64.c`：feature 协商时 host 提供则 opportunistically 协商 `VIRTIO_GPU_F_EDID`（`edid_negotiated` 标记）；`gpu_setup_edid()` 发 GET_EDID(scanout 0) → `gpu_parse_edid()` 校验 EDID magic + 从首个 detailed timing descriptor（偏移 54）提取水平/垂直 active（byte[2]|((byte[4]&0xF0)<<4) 等）得首选分辨率。API：`virtio_gpu_edid_available()` / `virtio_gpu_edid_preferred_mode(w,h)`。接入 init（GET_DISPLAY_INFO 后），`g_edid_ok` 门控。
  - [x] gfx-selftest 第 11 项：`edid_available()` 门控，验证首选模式非零并打印 `EDID preferred WxH`。
  - [x] 实机验证：`[virtio-gpu] EDID preferred mode parsed` → `[x86_64][gfx-selftest] EDID preferred 1280x800 OK` ——**与 `fb 1280x800` 完全一致** → `PASS`（全 11 项）。正常 GOP 回退 + host opkg ALL PASS 无回归。
- [x] M6.8：virtio-gpu 多 scanout（多头镜像模式）✅ **枚举+逐头 SET_SCANOUT 就绪，单头实机验证**
  - [x] 驱动 `virtio_gpu64.c`：`gpu_get_display_info()` 改为**枚举所有 enabled scanout**（记录 id + 几何到 `g_scanout_ids/w/h[]`，首个作为主绘制几何）；`gpu_set_scanout()` 改为**逐个 enabled scanout 绑同一 2D resource（镜像模式）**，各 scanout 矩形按自身几何并 clamp 到 backing 尺寸；per-frame RESOURCE_FLUSH 是 per-resource，一次让所有绑定头更新。API：`virtio_gpu_scanout_count()` / `virtio_gpu_scanout_mode(idx,w,h)`。init 日志 `scanouts enabled=N`。
  - [x] gfx-selftest 第 12 项：`device_count>0` 门控，验证 scanout_count>0 且每个几何合理，打印 `scanouts=N OK`。
  - [x] 实机验证：① 默认 virtio-gpu-pci——`scanouts enabled=1` → `scanouts=1 OK` → `PASS`（全 12 项）；② `run_diag_gpu_multihead.sh`（`max_outputs=2,xres=1024,yres=768` + VNC）——分辨率协商生效 `fb 1024x768`，枚举逻辑正确。**诚实边界：headless QEMU 下第二 scanout 需真实连接第二显示输出才 enabled，VNC 只提供一个 head，故实测 enabled=1；多头镜像代码路径（枚举全部 + 逐个 SET_SCANOUT）已就绪，需真实多显环境才能观到 enabled>1。** 正常 GOP 回退 + host opkg ALL PASS 无回归。🎊 **M6.6–6.8 全部完成（virgl 按评估砍掉）**。
- [x] M6.9：virtio-input 键盘/鼠标驱动（摆脱 PS/2 依赖，与 PS/2 共存）✅ **双设备实机 bring-up 验证**
  - [x] 头文件 `virtio_input.h`：evdev 事件布局 `virtio_input_event_t`{type/code/value 8B 小端}；EV_SYN/KEY/REL/ABS 类型常量；REL_X/Y/WHEEL、ABS_X/Y、BTN_LEFT/RIGHT/MIDDLE code 常量；`virtio_input_init/poll/device_count()` 接口。
  - [x] 驱动 `virtio_input64.c`：单 eventq(queue 0) 接收型设备——driver 预投递 16 个 write-only 事件 buffer（单物理页），poll used 环取事件、翻译、重投递。**evdev→GUI 映射**：EV_KEY(KEY_*) 查表→ASCII/控制键→`gui_post_key_code_with_modifiers()`；修饰键(SHIFT/CTRL/ALT make/break)累积到 mods；鼠标 EV_REL→累积 dx/dy/wheel、EV_ABS→abs 坐标、BTN_*→按钮位图，在 EV_SYN 帧末提交 `mouse_inject_relative()` / `mouse_set_absolute_position_with_wheel()`。**与 PS/2 共存**：叠加注入同一 GUI 事件通路。
  - [x] PCI 多设备枚举：新增 `pci_find_nth_by_id(vendor,device,index)`（键盘+鼠标同为 1af4:1052，需按 index 遍历）；`VINPUT_MAX_DEVS=4`。
  - [x] transport 复用 `virtio_modern_*`：attach/reset/get_features(仅留 VERSION_1 bit32)/set_features/setup_queue/set_driver_ok/notify。物理页 `arch_x86_64_pmm_alloc_pages`。
  - [x] GUI 集成：gui.c 加**弱符号** `gui_platform_poll_input()`（i386 no-op），`gui_poll_mouse()` 每帧调用；virtio_input64.c 提供**强符号**转发 `virtio_input_poll()`。kernel64.c 在 `virtio_gpu_init()` 后调 `virtio_input_init()`。
  - [x] 实机验证（双分支）：① `run_diag_input.sh`（挂 virtio-keyboard-pci + virtio-tablet-pci + virtio-gpu-pci）——**2×`[virtio-input] device up`** → `backend=virtio-gpu present-mode` → `desktop up, entering poll loop`；② `run_diag_gpu.sh`（不挂 input）——`[virtio-input] no device (1af4:1052)` → desktop up（PS/2 通路继续，**零回归**）。host opkg 23/23，x86_64 全量构建通过。selftest 中的 #PF/#GP 均为故意故障注入探针(PASS)，rc=124 为 timeout 杀常驻 poll loop(预期)。
- [x] **M6.10：安全加固（ASLR / W^X 强制 / SMEP / SMAP / 栈保护 / CPU 能力探测）✅ 6/6 全收官**（注：原编号 M6.4 与 virtio-gpu 里程碑冲突，重编为 M6.10）
  - [x] **M6.10.1 CPU 能力探测**：CPUID 探测 NX/SMEP/SMAP/UMIP 能力位，为后续各加固项门控铺路
  - [x] **M6.10.2 W^X（写异或执行）**：页权限分区实测 124 页 RX + 157 页 RO + 1674 页 NX，代码段不可写、数据段不可执行
  - [x] **M6.10.3 SMEP**：CR4.bit20 置位（实机 CR4=0x300668 双位置位），内核态取指用户页触发 #PF
  - [x] **M6.10.4 SMAP**（commit `ecfc876`）：CR4.bit21 置位。粗粒度用户内存访问收口——`syscall_sysret64.S` 汇编入口用 STAC 打开、dispatch 返回后 CLAC 关闭 user-access 窗口（一处改动覆盖 32+ 处散落用户指针访问），由全局标志 `arch_x86_64_smap_on` 门控（非 SMAP CPU 跳过 STAC/CLAC 避免 #UD，完美向后兼容）；STAC/CLAC 用 `.byte` 硬编码不依赖汇编器特性。**双构建实测铁证**：`+smep,+smap` CPU 启动 CR4=0x300668，全 syscall 用户内存路径（文件读写、signal trampoline、net 收发缓冲）PASS 零 #PF 零 guest_error；`qemu64`（无 SMAP）门控跳过、selftest 全绿进桌面、向后兼容 PASS
  - [x] **M6.10.5 栈 canary（栈保护）**：TSC 熵源生成栈 canary，每次启动值不同
  - [x] **M6.10.6 ASLR**：栈基址随机化，实测栈 gap 三次启动全不同
- [~] M6.11：多用户与会话（完整 uid/gid 体系 + 登录管理 + 权限隔离）
  - [x] **M6.11.1 进程凭证体系**（commit `f88d9ff`）：PCB 扩展 POSIX 凭证集 uid/gid(real)+euid/egid(effective)+suid/sgid(saved)；实现 setuid/setgid/seteuid/setegid 严格 POSIX 特权规则；新增 syscall GETEUID/GETEGID/SETEUID/SETEGID(482-485) 并接线 SETUID/SETGID；fork 完整继承凭证；cred_selftest64 六阶段实机 PASS
  - [x] **M6.11.2 账户数据库**（commit `d760827`）：/etc/passwd + /etc/group + /etc/shadow seed 进 ramfs；默认 root(uid0)+openos(uid1000)，shadow 存 SHA256 哈希；24 个 initrd 文件全 seed 成功启动健康
  - [x] **M6.11.3 login/session**（commit 见下）：账户库解析器 `account_db64`（/etc/passwd + /etc/shadow 无堆解析）+ 认证器 `login64`（SHA256(password) 与 shadow `sha256$<hex>` 比对）+ 会话建立（setsid 成为会话领导后 setgid→setuid 降权，顺序保证特权规则）；`login_selftest64` 六阶段实机 PASS（root/openos 正确认证、错误密码 BAD_PASSWORD、未知用户 NO_USER、NULL 参数 EINVAL、start_session 降权+sid==pid），快照/恢复 slot-0 root 身份不污染；无 boot/prio/preempt/GUI 回归
- [x] **M6.12：系统日志 / dmesg 风格日志子系统 ✅**
  - [x] **M6.12.1 klog 环形缓冲核心**：`klog64.[ch]` 64KB ring buffer + 单调 seq + irq-off spinlock + FIFO 回绕 + `total_dropped` 统计；`klog_emit`/`klog_read`/`klog_read_tail`/`klog_stats`/`klog_clear` 五个核心 API；防重入标志 `g_klog_reentry` 避免 klog→early_console→klog 死递归
  - [x] **M6.12.2 early_console tee**：`early_console64.c` 集成 klog tee，所有现有 `[x86_64][xxx]` 内核日志**零侵入**自动进 klog，无需改任何调用点
  - [x] **M6.12.3 SYS_KLOG=487 系统调用**：`syscall_dispatch64.c::do_klog` 分发（CMD_READ/READ_TAIL/READ_FROM_SEQ/STATS/CLEAR），32KB bounce buffer 保证用户拷贝安全（先拷内核栈再写用户 buf，避免持 klog 锁时触发 page fault），CLEAR 需 euid=0 权限
  - [x] **M6.12.4 用户态 dmesg(1)**：`user/dmesg64.c` 完整参数解析（`-n<N>` 拉最新 N 条、`-s<sz>` 缓冲区大小、`-c` 读后清空、`-h` 帮助）
  - [x] **M6.12.5 klog-selftest 六阶段**：`klog_selftest64.c` 覆盖 seq 单调 / tail read / from-seq resume / stats / wrap eviction / clear 六个场景，早期启动跑，`[x86_64][klog-selftest] PASS`
  - [x] **端到端 diag 验证**：`M6_DMESG_DIAG=1` 通道 ring3 加载 `/bin/dmesg`，SYS_KLOG=487 用户态成功拉取内核日志（`<6>[seq] msg` 格式），headless 基线（`run_diag_gpu.sh`）与默认 ring3 通路（M6_LOGIN_DIAG）零回归

> 推荐攻关顺序：**M1.1 PCI → M1.2 virtio-net → M1.3/M1.4 TCP/IP 栈 → M1.5 真实上网**。此路线打通后 OpenOS 才真正跨入现代 OS 门槛；M2/M4 可并行推进。

---

## M8：触屏兼容（Touchscreen 支持，兼顾 QEMU + 真机）

> 目标：让 OpenOS 桌面既能被鼠标/键盘操作，也能被触屏（单点/多点）操作。真机目标覆盖两类主流触屏：**USB HID Touchscreen**（外接触摸显示器 / 大多数触屏一体机）与 **I²C HID Touchscreen**（Surface / 主流触屏笔记本内置）。QEMU 侧用 `usb-tablet` / `usb-mtouch` 做回归测试。
>
> 设计原则：
> 1. **输入抽象**：新增触点事件流 `touch_frame_t`，与鼠标事件同层但独立；单点触屏默认映射为鼠标以保持向后兼容。
> 2. **手势状态机**：Tap/LongPress/Drag/Swipe/Pinch 在内核态识别，产出高层事件供 GUI 消费。
> 3. **触屏友好 UI**：图标 ≥ 44px、边缘手势、虚拟键盘 OSK，锁屏优先支持。
> 4. **真机可跑**：驱动分层清晰，USB HID 与 I²C HID 共用上层协议解析器。

### M8-A：单点触屏 MVP（USB HID Single-touch → 鼠标映射）

- [ ] **M8-A.1 HID Usage 识别扩展**
  - [ ] `usb_hid64.c` 新增识别 Usage Page `0x0D`（Digitizer）+ Usage `0x04`（Touch Screen）/ `0x02`（Pen）
  - [ ] 保留现有 `usb-tablet`(VID=0x0627 PID=0x0001) 白名单，作为 fallback 匹配
  - [ ] 在 HID 描述符探测阶段区分 mouse / tablet / touchscreen 三类设备，输出到 klog
- [ ] **M8-A.2 Single-touch report 解析**
  - [ ] 解析标准 Digitizer report：Tip Switch(1bit) + In Range(1bit) + X/Y(16bit each) + 可选 Pressure
  - [ ] 复用 `mouse_set_absolute_position_with_wheel` 通路：Tip Down→左键按下事件，Tip Up→左键释放事件
  - [ ] 触点抬起后**保持光标位置**（不清零，符合触屏语义）
- [ ] **M8-A.3 QEMU 回归测试**
  - [ ] `run.bat` 保持 `usb-tablet` 可用；新增 `run_touch_diag.bat` 使用 `-device usb-mtouch`（若 QEMU 版本支持）作为触屏专用配置
  - [ ] 桌面单击、双击、拖拽三种场景全部可复现
  - [ ] 锁屏输入密码可通过触点（首点触发键盘焦点）+ 键盘完成
- [ ] **M8-A.4 klog 观测点**
  - [ ] `[touch] device up vid=%x pid=%x type=%s`（single/multi）
  - [ ] `[touch] frame tip=%d x=%d y=%d`（verbose 级别，默认关闭）

### M8-B：手势识别引擎（内核态状态机）

- [ ] **M8-B.1 新增手势模块骨架**
  - [ ] `src/kernel/gui/gesture.c` + `gesture.h`
  - [ ] 定义 `gesture_event_t`：`GESTURE_TAP / LONG_PRESS / DRAG_BEGIN / DRAG_MOVE / DRAG_END / SWIPE_{L,R,U,D} / PINCH`
  - [ ] 提供 `gesture_feed(touch_frame_t*)` 入口，输出 `gesture_event_t` 到 GUI 事件队列
- [ ] **M8-B.2 基础手势状态机（单指）**
  - [ ] **Tap**：按下→抬起 < 200ms 且总位移 < 8px → 发 `TAP`，注入鼠标左键单击
  - [ ] **Long Press**：按下 > 500ms 且不动 → 发 `LONG_PRESS`，注入鼠标右键单击（替代右键菜单）
  - [ ] **Drag**：按下后位移 > 8px → `DRAG_BEGIN` + 一系列 `DRAG_MOVE`，抬起时 `DRAG_END`
- [ ] **M8-B.3 边缘 Swipe 手势**
  - [ ] 从屏幕左/右/上/下边缘 32px 内起手 + 向内 swipe > 80px → 发 `SWIPE_*`
  - [ ] 预留 GUI 消费点：底边 swipe→切换窗口，顶边 swipe→任务栏切换
- [ ] **M8-B.4 手势 selftest**
  - [ ] `gesture_selftest64.c`：模拟触点序列注入，验证四类手势判定正确率与阈值边界

### M8-C：多点触摸（Multi-touch，含通用 HID Report Descriptor 解析器）

- [ ] **M8-C.1 输入子系统重构**
  - [ ] `mouse64.c` 拆分：抽出 `input_core.c`（统一事件总线 + `present` 位图）
  - [ ] 新增 `touch64.c`：管理最多 10 个 `touch_point_t` 槽位，contact_id 唯一
  - [ ] `mouse.c` 保留鼠标专属逻辑；touch→mouse 映射走 input_core 桥接
- [ ] **M8-C.2 通用 HID Report Descriptor 解析器**
  - [ ] 实现 `hid_parser64.c`：解析 Usage Page/Usage/Report Size/Report Count/Logical Min/Max/Collection
  - [ ] 输出 `hid_field_t[]` 字段表（offset/size/usage/logical range），驱动按字段取值而非硬编码偏移
  - [ ] 单元测试用 Win7 触屏 sample descriptor 作为 fixture 覆盖
- [ ] **M8-C.3 Multi-touch report 解析**
  - [ ] 支持 Windows Precision Touchpad / Win7 触屏标准协议
  - [ ] 每帧 Contact Count + N × (Contact ID + Tip Switch + X + Y) 结构解析
  - [ ] 触点生命周期管理：新触点分配槽位，contact_id 相同则更新，Tip Up 后回收槽位
- [ ] **M8-C.4 双指手势**
  - [ ] **Two-finger scroll**：双指同向平移 → 注入滚轮事件
  - [ ] **Pinch**：双指距离变化 > 20% → `PINCH_IN` / `PINCH_OUT`（预留窗口缩放/图片查看器）
- [ ] **M8-C.5 QEMU + 真机验证**
  - [ ] QEMU：`-device usb-mtouch` 验证 2-4 点
  - [ ] 真机（外接 USB 触屏）：预留 verify 脚本，记录 vid/pid 兼容矩阵到文档

### M8-D：触屏友好 UI 与真机 I²C HID 触屏

- [ ] **M8-D.1 虚拟键盘 OSK**
  - [ ] `src/kernel/gui/osk.c` + `osk.h`：QWERTY 布局，按键 ≥ 44×44px
  - [ ] 锁屏优先支持：检测无键盘时自动弹出 OSK；密码框获焦时弹出，失焦收起
  - [ ] 终端窗口触屏模式也可弹 OSK（次要目标）
- [ ] **M8-D.2 桌面图标 & 控件触屏自适应**
  - [ ] 检测输入设备类型（有触屏无鼠标 → touch 模式；两者兼有 → hybrid）
  - [ ] touch 模式下桌面图标尺寸放大到 ≥ 64×64px，滚动条加粗
  - [ ] 命中测试放宽（点击容差 ±4px）
- [ ] **M8-D.3 边缘手势 UX 接线**
  - [ ] 底边 swipe up → 显示/隐藏任务栏
  - [ ] 左/右边 swipe → 切换活动窗口
  - [ ] 顶边 swipe down → 关闭当前窗口（可选）
- [ ] **M8-D.4 I²C HID 触屏驱动（真机 Surface / 触屏笔记本）**
  - [ ] 实现 `i2c64.c`：Intel LPSS / Designware I²C 主控驱动（PCI + MMIO）
  - [ ] ACPI DSDT 解析：识别 `PNP0C50`（HID over I²C）设备、读取 HID Descriptor Address
  - [ ] `i2c_hid64.c`：实现 HID over I²C 协议（Get HID Descriptor / Get Report Descriptor / Interrupt 事件读取）
  - [ ] 复用 M8-C.2 的通用 HID Report Descriptor 解析器
  - [ ] 真机验证矩阵：Surface Go / Thinkpad X1 Yoga / 常见触屏笔电（记录 klog dump）
- [ ] **M8-D.5 输入设备热插拔**
  - [ ] USB 层：HID 设备拔出/插入事件传递到 input_core
  - [ ] I²C 层：ACPI GPE 事件监听（可选，优先级低）

- [ ] **M8-E：输入抽象层（IAL，触屏/手机地基）**
  - [ ] M8-E.1 统一事件结构 `input_event_t` + 环形队列实现（参考 Linux evdev 语义）
  - [ ] M8-E.2 生产端改造：USB 鼠标/tablet/键盘 → `input_report()` 事件总线
  - [ ] M8-E.3 消费端改造：gui.c / lockscreen.c → `input_poll_event()` 循环，移除 `g_mouse` 直接依赖
  - [ ] M8-E.4 `sys_input_read()` 系统调用（用户态读取事件流，为 gestured/录制铺路）
  - [ ] M8-E.5 input-selftest 注入合成事件验证 FIFO/溢出/SYN 完整性
  - [ ] 详细路线文档已归档：`docs/mobile-touch-roadmap.md`（含 M9~M12 触屏/手机形态五阶段）

> **实施顺序**：**M8-A（1-2 天）→ M8-B（2-3 天）→ M8-C（3-5 天）→ M8-D（3-5 天）→ M8-E（2-3 天）**。每个 milestone 完成后跑 Stages 1-30 SMP=1/4 双矩阵 + gfx-selftest + input-selftest 三条基线，无回归再合入 main。
> 👉 M8-E 是触屏/手机形态的地基，完成后 M9（触屏 GUI）→ M10（全屏应用）→ M11（ARM64）→ M12（移动特性）路线已在 `docs/mobile-touch-roadmap.md` 中完整拆解。

