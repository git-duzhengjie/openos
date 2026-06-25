# openos 待开发功能清单

> 更新时间：2026-06-24
>
> 当前状态：openos 已具备 32 位 x86 原型内核能力，能够启动、显示、输入、调度、运行基础用户程序，并具备基础 syscall、VFS、ramfs/tmpfs、shell、GUI Terminal 等模块。浏览器路线已切换为 OpenOS 自研轻量浏览器，Chromium 官方内核迁移冻结为历史备选。
>
> 最近完成：自研浏览器 P0/P1/P2 已收口；P3 已完成表单控件文本化渲染、HTML 注释与 doctype 容错、基础内联 CSS display/font-weight 解析、相对 URL `./` / `../` 规范化与 query/hash 保留。
>
> 当前推荐下一步：在继续保持自研浏览器回归门禁的同时，优先推进 OPENOS 作为真正操作系统的 PC/Mobile 跨设备架构路线：冻结 i386 稳定基线，将 x86_64 升级为 PC 主线，抽象 BootInfo / HAL / Device Model，并新增 aarch64 作为 Mobile 主线基础。

---

## P0-ARCH：OPENOS PC / Mobile 操作系统架构路线

> 目标：OPENOS 按真正操作系统路线同时支持 PC 和 Mobile。短期不直接冲真机 Mobile，而是先把 x86_64 PC 主线做稳，再抽象启动信息、HAL、平台层和驱动模型，随后新增 aarch64，从 QEMU virt 开始建立 Mobile 基础。

### A0：当前稳定基线与门禁

- [x] A0.1：冻结当前 i386 稳定主线
  - [x] 保持默认 `bash build.sh` 可生成 `target/openos.img`
  - [x] 保持现有 i386 用户态程序、shell、GUI、网络和浏览器 smoke 不回退
  - [x] 明确 i386 后续定位为 legacy / regression / 调试目标
- [x] A0.2：建立跨架构基础构建门禁
  - [x] `bash build.sh test` 必须通过
  - [x] `bash build.sh` 必须通过
  - [x] `ARCH=x86_64 bash build.sh` 必须通过
  - [x] 在 README 或开发文档中记录上述门禁命令
- [x] A0.3：记录当前真实架构状态
  - [x] 记录 i386 为当前最完整主线
  - [x] 记录 x86_64 已有 GDT/TSS/IDT/syscall/PMM/VMM/ELF64/UEFI 骨架
  - [x] 记录 `src/arch/arm` 当前是 ARM32 骨架，不是 Mobile 所需 ARM64 主线
  - [x] 记录 RISC-V 当前为早期 RV64 骨架，不阻塞 PC/Mobile 主线

### A1：x86_64 升级为 PC 主线

- [x] A1.1：完善 x86_64 启动路径
  - [x] 明确 UEFI `BOOTX64.EFI` 启动链
  - [x] 保留 BIOS long mode boot stub 作为兼容/调试路径
  - [x] 统一 x86_64 linker、入口和早期栈初始化
- [x] A1.2：完善 x86_64 早期内核初始化
  - [x] 初始化 GDT / TSS / IDT
  - [x] 初始化异常处理与中断入口
  - [x] 初始化 syscall/sysret 或兼容 syscall 路径
  - [x] 初始化 early console / framebuffer 输出
- [x] A1.3：接入真实内存管理
  - [x] 从启动器传入 memory map
  - [x] 接入 PMM
  - [x] 接入 VMM
  - [x] 接入 heap
  - [x] 保证内核空间和用户空间权限隔离
- [x] A1.4：运行第一个 x86_64 用户态程序
  - [x] 加载 `hello64.elf`
  - [x] 进入 ring3
  - [x] 通过 syscall 输出文本
  - [x] 用户程序 exit 后能返回/回收
- [x] A1.5：x86_64 接入 initrd / VFS / shell
  - [x] 加载 initrd
  - [x] 挂载基础 VFS
  - [x] 启动 `/bin/init`
  - [x] fallback 到 `/bin/sh`
  - [x] 形成 `x86_64 kernel -> init -> shell` 最小闭环

### A2：统一 OpenOSBootInfo

- [x] A2.1：新增架构无关启动信息头文件
  - [x] 新增 `src/kernel/include/bootinfo.h`
  - [x] 定义 `OpenOSBootInfo`
  - [x] 定义 memory region 结构
  - [x] 定义 framebuffer 结构
  - [x] 预留 ACPI RSDP、Device Tree、initrd、cmdline 字段
- [x] A2.2：BIOS / UEFI / aarch64 启动路径统一填充 BootInfo
  - [x] i386 BIOS loader 转换为 BootInfo
  - [x] x86_64 UEFI loader 转换为 BootInfo
  - [x] 后续 aarch64 boot stub 转换为 BootInfo
- [x] A2.3：kernel core 只消费 BootInfo
  - [x] 内核核心不直接读取启动器私有结构
  - [x] memory map、framebuffer、initrd、cmdline 均从 BootInfo 获取
  - [x] 补充 BootInfo 校验和版本检查

### A3：建立 arch_ops / platform_ops 分层

- [x] A3.1：新增架构操作接口
  - [x] 新增 `src/kernel/include/arch_ops.h`
  - [x] 定义 early init、interrupt init、enable/disable interrupt、halt、context switch、cycle counter 等接口
  - [x] i386 接入 `OpenOSArchOps`
  - [x] x86_64 接入 `OpenOSArchOps`
  - [x] aarch64 后续接入 `OpenOSArchOps`
- [x] A3.2：新增平台操作接口
  - [x] 新增 `src/kernel/include/platform_ops.h`
  - [x] 定义 early console、timer、irq、poweroff、reboot 等接口
  - [x] 新增 `pc-bios` 平台
  - [x] 新增 `pc-uefi` 平台
  - [x] 新增 `qemu-aarch64-virt` 平台
- [x] A3.3：内核核心改为调用 ops
  - [x] 内核核心不直接判断 i386/x86_64/aarch64
  - [x] 内核核心不直接关心 BIOS/UEFI/Device Tree
  - [x] 架构差异留在 `src/arch/*`
  - [x] 平台差异留在 `src/kernel/platform/*`

### A4：建立统一 Device Model / Driver Model

- [x] A4.1：新增设备模型头文件
  - [x] 新增 `src/kernel/include/device.h`
  - [x] 定义 `OpenOSDevice`
  - [x] 定义 bus type：platform / PCI / USB / VirtIO / I2C / SPI / GPIO
  - [x] 支持 MMIO、IRQ、platform data、driver data
- [x] A4.2：新增驱动模型头文件
  - [x] 新增 `src/kernel/include/driver.h`
  - [x] 定义 `OpenOSDriver`
  - [x] 定义 `probe/remove/suspend/resume`
  - [x] 建立 driver register / device bind 流程
- [x] A4.3：优先统一基础驱动
  - [x] UART / serial
  - [x] framebuffer / display
  - [x] timer
  - [x] interrupt controller
  - [x] block device
  - [x] input
- [x] A4.4：优先推进 VirtIO 跨架构驱动
  - [x] virtio-blk 可在 x86_64 QEMU 与 aarch64 QEMU virt 共用
  - [x] virtio-net 可在 x86_64 QEMU 与 aarch64 QEMU virt 共用
  - [x] virtio-input 可作为 Mobile 早期输入验证
  - [x] virtio-gpu 或 framebuffer 路径用于早期显示验证

### A5：新增 aarch64 Mobile 基础主线

- [x] A5.1：新增 `src/arch/aarch64`
  - [x] 新增 `README.md`
  - [x] 新增 `linker_aarch64.ld`
  - [x] 新增 `boot/`
  - [x] 新增 `include/`
  - [x] 新增 `kernel/`
- [ ] A5.2：实现 QEMU virt 最小启动
  - [ ] 支持 `qemu-system-aarch64 -machine virt`（待安装 `qemu-system-aarch64` 后运行冒烟）
  - [x] 实现 `_start`
  - [x] 初始化 EL1 环境
  - [x] 初始化早期栈
  - [x] 通过 PL011 UART 输出启动日志
- [x] A5.3：实现 aarch64 异常与中断基础
  - [x] 异常向量表
  - [x] 同步异常处理
  - [x] IRQ 入口
  - [x] SVC syscall 入口（已实现 trap frame + SVC64 分发；完整 aarch64 构建/运行待安装交叉工具链与 `qemu-system-aarch64` 后冒烟）
  - [x] panic / fault 日志
- [x] A5.4：实现 ARM 平台基础组件
  - [x] GICv2/GICv3 初始化
  - [x] ARM generic timer
  - [x] PSCI power/reboot 基础接口
  - [x] Device Tree 解析
  - [x] Device Tree 转换为 OpenOSBootInfo
- [ ] A5.5：实现 aarch64 内存与用户态（代码骨架已接线；完整 aarch64 构建/运行待安装交叉工具链与 `qemu-system-aarch64` 后冒烟）
  - [x] PMM（早期 bump page allocator）
  - [x] VMM（早期 identity mapping 接口）
  - [x] heap（早期 bump heap）
  - [x] EL0 用户态切换（ELR_EL1/SP_EL0/SPSR_EL1 staging）
  - [x] ELF64 loader（AArch64 ELF64 校验、PT_LOAD 加载、entry 重定位）
  - [ ] 运行 aarch64 hello 用户程序（`hello64.elf` 构建/嵌入链路已写入；当前环境缺少 aarch64 toolchain/QEMU，待工具链后实机冒烟）
- [x] A5.6：实现 aarch64 shell 闭环（最小内置 initrd/VFS/shell 骨架已接入；完整 aarch64 冒烟待工具链/QEMU）
  - [x] initrd 加载
  - [x] VFS 挂载
  - [x] `/bin/init`
  - [x] `/bin/sh`
  - [x] 形成 `aarch64 kernel -> init -> shell` 最小闭环

### A6：GUI / Window Manager 降耦合与 Shell 分端

- [x] A6.1：冻结当前内核 GUI ABI（v1 兼容层）
  - [x] 保持现有 i386 GUI / window_manager 不回退
  - [x] 为现有 GUI syscall 增加文档
  - [x] 明确其为兼容层，不作为 Mobile Shell 基础
- [x] A6.2：新增 display / input 抽象（最小内核抽象层已接入，shared-memory/message-queue 深化留给 compositor/IPC 阶段）
  - [x] 内核提供 framebuffer 或 display buffer 管理
  - [x] 内核提供 input event queue
  - [x] 支持 shared memory buffer 或 message queue
  - [x] 权限校验和设备访问控制由内核负责
- [x] A6.3：推动 Shell 用户态化（已新增用户态 compositor/desktop/mobile shell 路线文档）
  - [x] 新增 `openos-compositor` 用户态服务规划
  - [x] 新增 `openos-desktop-shell` 规划
  - [x] 新增 `openos-mobile-shell` 规划
  - [x] PC Shell 支持多窗口、任务栏、文件管理、快捷键、多显示器
  - [x] Mobile Shell 支持全屏应用、手势、状态栏、通知中心、后台卡片和权限弹窗

### A7：系统服务用户态化

- [x] A7.1：建立“内核提供机制，用户态服务提供策略”的边界
  - [x] 内核负责进程、内存、IPC、权限、安全审计和资源限制
  - [x] 用户态服务负责设备管理、网络、显示、通知、包管理、AI 服务等策略
- [x] A7.2：规划核心系统服务
  - [x] `init`
  - [x] `servicemgr`
  - [x] `devmgr`
  - [x] `netd`
  - [x] `fsd`
  - [x] `permissiond`
  - [x] `packaged`
  - [x] `logd`
  - [x] `displayd`
  - [x] `inputd`
  - [x] `notificationd`
  - [x] `powerd`
  - [x] `aid`
- [x] A7.3：AI OS 能力系统服务化
  - [x] AI Agent 不写进内核
  - [x] 新增 `aid` / AI system service 规划
  - [x] 新增 AI Skill Runtime 规划
  - [x] AI Skill 通过权限、sandbox、IPC 调用系统能力
  - [x] 内核只提供隔离、授权、资源控制和审计

### A8：PC / Mobile 平台能力边界

- [x] A8.1：明确 PC 侧能力
  - [x] x86_64
  - [x] UEFI
  - [x] ACPI
  - [x] PCIe
  - [x] NVMe / SATA / USB
  - [x] 键盘鼠标
  - [x] 多显示器
  - [x] Desktop Shell
- [x] A8.2：明确 Mobile 侧能力
  - [x] aarch64
  - [x] Device Tree
  - [x] GIC
  - [x] PSCI
  - [x] I2C / SPI / GPIO
  - [x] 触摸屏
  - [x] 电池 / 温控 / 电源管理
  - [x] 传感器
  - [x] 摄像头
  - [x] 蜂窝网络能力边界说明
  - [x] Mobile Shell
  - [x] 应用生命周期与后台限制
- [x] A8.3：明确共享能力
  - [x] kernel core
  - [x] syscall ABI
  - [x] 进程 / 线程
  - [x] 内存管理
  - [x] VFS
  - [x] IPC
  - [x] 权限模型
  - [x] sandbox
  - [x] 网络协议栈基础
  - [x] 包管理格式
  - [x] 应用 Manifest
  - [x] AI Skill Runtime
  - [x] 日志与系统更新框架

### A9：推荐目录演进

- [x] A9.1：先新增公共头文件，不破坏旧路径
  - [x] `src/kernel/include/bootinfo.h`
  - [x] `src/kernel/include/arch_ops.h`
  - [x] `src/kernel/include/platform_ops.h`
  - [x] `src/kernel/include/device.h`
  - [x] `src/kernel/include/driver.h`
- [x] A9.2：逐步新增公共实现目录
  - [x] `src/kernel/core/`
  - [x] `src/kernel/platform/`
  - [x] `src/kernel/drivers/bus/`
  - [x] `src/kernel/drivers/virtio/`
- [x] A9.3：逐步迁移架构无关逻辑
  - [x] 通用调度、进程、内存、VFS、IPC 迁移到 `src/kernel/core/`
  - [x] 架构相关逻辑保留在 `src/arch/i386`、`src/arch/x86_64`、`src/arch/aarch64`
  - [x] 平台相关逻辑保留在 `src/kernel/platform/*`

### A10：不建议当前立即执行的事项

- [x] A10.1：暂不直接适配真实手机真机
  - [x] 先完成 aarch64 QEMU virt
  - [x] 再考虑 ARM64 开发板
  - [x] 最后再考虑半开放移动设备或真实手机
- [x] A10.2：暂不继续把 i386 作为长期产品主线
  - [x] i386 保留为 legacy / regression
  - [x] x86_64 作为 PC 产品主线
  - [x] aarch64 作为 Mobile 产品主线
- [x] A10.3：暂不把 Mobile Shell 塞入当前内核 GUI
  - [x] Mobile Shell 应为用户态 Shell
  - [x] 当前 kernel GUI 保持兼容，不作为移动端基础架构
- [x] A10.4：暂不把 AI Agent 写进内核
  - [x] AI Agent 应作为系统服务
  - [x] 内核只做隔离、授权、审计和资源控制

### A11：阶段验收里程碑

- [x] M1：当前主线稳定
  - [x] `bash build.sh test` 通过
  - [x] `bash build.sh` 通过
  - [x] `ARCH=x86_64 bash build.sh` 通过
- [ ] M2：x86_64 能运行第一个用户态程序
  - [ ] `kernel64.elf` 启动
  - [ ] 初始化 GDT / IDT / TSS / syscall
  - [ ] 初始化 PMM / VMM / heap
  - [ ] 加载 `hello64.elf`
  - [ ] 进入 ring3
  - [ ] hello64 通过 syscall 输出
- [ ] M3：x86_64 能运行 init / shell
  - [ ] UEFI boot
  - [ ] 读取 initrd
  - [ ] 挂载 VFS
  - [ ] 运行 `/bin/init`
  - [ ] 启动 `/bin/sh`
- [ ] M4：BootInfo 接入 i386 / x86_64
  - [ ] i386 使用 `OpenOSBootInfo`
  - [ ] x86_64 UEFI 使用 `OpenOSBootInfo`
  - [ ] kernel core 不直接读取启动器私有结构
- [ ] M5：aarch64 QEMU virt 启动
  - [ ] PL011 输出
  - [ ] 异常向量正常
  - [ ] GIC 初始化
  - [ ] Generic Timer 初始化
  - [ ] 解析 Device Tree
  - [ ] 生成 `OpenOSBootInfo`
  - [ ] 进入 kernel core
- [ ] M6：aarch64 用户态 hello
  - [ ] EL0 用户态
  - [ ] SVC syscall
  - [ ] ELF64 loader
  - [ ] hello 程序输出
- [ ] M7：VirtIO block / net / input 跨架构工作
  - [ ] x86_64 QEMU 可用
  - [ ] aarch64 QEMU virt 可用
  - [ ] 同一套 virtio driver 在两个架构上工作

---

## P0：OpenOS 自研轻量浏览器收口

> 目标：停止把 Chromium 作为当前优先路线，改为落地 OpenOS 自研轻量浏览器；第一阶段先实现可维护的网络加载、HTML 文本渲染、基础页面信息提取和 GUI 展示。Chromium 官方内核迁移路线冻结为历史备选，不再阻塞当前浏览器可用性。

- [x] P0.1：确认现有浏览器入口
  - [x] 统一任务清单为 `TODOLIST.md`
  - [x] 确认 `/bin/browser` 由 `src/user/browser.c` 构建并安装
  - [x] 确认 `/bin/chromium` 当前只是 demo/兼容程序，不作为自研浏览器主线
- [x] P0.2：实现自研轻量浏览器第一版内核能力
  - [x] 支持命令行传入 `http://host/path` 或 `host path`
  - [x] HTTP/1.0 GET、DNS、TCP 超时与错误诊断保持可用
  - [x] 解析 HTTP 状态行并在 GUI 中展示
  - [x] 提取 HTML `<title>`
  - [x] 将基础块级标签转换为换行，输出可读文本
  - [x] 支持基础 HTML 实体解码
- [x] P0.3：新增轻量 DOM/CSS 分层
  - [x] 抽出 HTML tokenizer/parser 接口
  - [x] 建立最小 DOM 节点结构
  - [x] 建立最小 CSS/样式接口，先支持默认样式和块/行内分类
- [x] P0.4：GUI 浏览体验
  - [x] 增加 URL 输入框或等价命令参数体验
  - [x] 支持刷新、返回/前进最小历史
  - [x] 支持可滚动文本视图
- [x] P0.5：本地文件与 smoke 验证
  - [x] 支持 `file://` 或本地 HTML 加载
  - [x] 新增 `/bin/browser` smoke 用例
  - [x] 构建内验证本地页面解析和 HTTP/文件加载路径；OpenOS/QEMU 手动验证保留为后续运行项
- [x] P0.6：文档和门禁收口
  - [x] 更新 README/浏览器文档，明确当前为 OpenOS 自研轻量浏览器
  - [x] 冻结 Chromium 真实切换任务为长期备选，不再作为 P0
  - [x] 构建检查通过并提交

---

## P1：自研浏览器内核硬化

> 目标：在不引入 Chromium/Chrome 内核的前提下，继续提升 OpenOS 自研轻量浏览器的 HTML/DOM/CSS 可维护性和可回归性。

- [x] P1.1：HTML parser 闭合标签按标签名匹配回退栈
  - [x] 修复任意 `</...>` 都盲目弹出一层 DOM 栈的问题
  - [x] 增加嵌套标签闭合顺序 smoke 覆盖
- [x] P1.2：DOM 文本渲染入口替换纯字符串折叠输出
  - [x] 基于 DOM 遍历输出文本
  - [x] 按默认 display 类型输出块级换行
- [x] P1.3：扩展默认 CSS display 分类
  - [x] 覆盖 `article` / `section` / `nav` / `header` / `footer` / `main`
  - [x] 增加默认样式 smoke 覆盖
- [x] P1.4：HTML tokenizer 属性跳过与自闭合标签回归
  - [x] 确认带属性标签名解析稳定
  - [x] 增加 `<br>` / `<img>` / `<meta>` 等自闭合/void 标签测试
- [x] P1.5：浏览器 GUI 文档视图回归
  - [x] 确认标题、状态、正文和滚动视图在本地 HTML 下稳定
  - [x] 记录 OpenOS/QEMU 手动验证步骤

---

## P2：自研浏览器可浏览能力增强

> 目标：让 `/bin/browser` 从单页文档查看器继续演进为可导航的轻量浏览器；仍坚持自研 HTML/DOM/CSS/GUI 路线，不恢复 Chromium/Chrome 内核迁移。

- [x] P2.1：链接提取与文本渲染编号
  - [x] DOM 节点保存 `<a href="...">` 的最小 href 属性
  - [x] 文本渲染时为链接追加 `[n]` 编号
  - [x] 单元测试覆盖双引号、单引号、无引号 href
- [x] P2.2：链接导航交互
  - [x] GUI 增加 NextLink/OpenLink 或等价按钮
  - [x] 支持相对链接基于当前 host/path 解析
  - [x] 点击打开后写入历史，可 Back/Forward
- [x] P2.3：基础列表/标题排版增强
  - [x] `h1/h2/h3` 增加可读前缀或空行
  - [x] `li` 增加项目符号
  - [x] 单元测试覆盖列表和标题输出
- [√] P2.4：错误页面与状态栏增强
  - [√] 404/非 2xx 状态在正文中明确提示
  - [√] 文件读取失败保留路径和错误原因
  - [√] 网络/DNS/连接失败文案保持可诊断

## P3：自研浏览器表单与基础 CSS 增强

> 目标：在保持轻量、自研、可测试的前提下，补齐网页阅读和简单交互所需的表单文本化、基础 CSS、注释/编码与 URL 解析能力。

- [√] P3.1：表单控件文本化渲染
  - [√] `input` 保存并渲染 `type/value/placeholder/name` 等关键属性
  - [√] `button/textarea/select/option` 输出可读文本提示
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
  - [√] 为 `input/textarea` 建立可聚焦控件模型
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

- [√] BIOS / x86 32 位启动
- [√] GDT / IDT / 中断 / `int 0x80` 系统调用
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
- [√] `waitpid` 错误语义、`waitpid(-1)`、exit status 编码与回归测试（提交：`daca8f2`）
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
- [√] 支持 exit status 回传（提交：`daca8f2`）
- [√] 添加 `WIFEXITED` / `WEXITSTATUS` 等状态解析宏（提交：`daca8f2`）
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
  - [√] 明确 init 不允许通过 `sys_exit` 退出，失败时由内核回退到 shell

### 2. 用户程序参数支持

- [√] `spawn(path, argv)` 支持参数（新增 `/bin/argtest` 回归，提交：本提交）
- [√] `exec(path, argv)` 支持参数（提交：本提交）
- [√] 用户态入口支持 `main(argc, argv)` / `_start(argc, argv)` 参数栈（提交：本提交）
- [√] shell 支持执行 `/bin/app arg1 arg2`（提交：本提交）
- [√] 支持环境变量 `envp`（新增 `/bin/envtest` 回归，提交：本提交）
- [√] 扩大 `spawn/exec` argv/envp 容量，并新增 Chromium 风格宽 argv/envp 回归验收

### 3. 文件系统基础接口

- [√] 实现用户态 `stat` syscall（新增 `/bin/fstest` 回归）
- [√] 实现用户态 `fstat` / `lstat`
- [√] 实现用户态 `readdir` syscall（路径 + index 形式）
- [√] 实现用户态 `opendir` / `closedir` 封装（基于 `SYS_READDIR(path,index)`）
- [√] 实现 `getcwd` / `chdir` syscall
- [√] 初步标准化用户态 syscall/runtime 头文件（`openos.h`）
  - [√] 迁移 `/bin/argtest`、`/bin/envtest`、`/bin/orphan`、`/bin/exit42`、`/bin/fstest`
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
  - [√] 新增 close-on-exec 标志，spawn/exec 继承 fd 时跳过 `FD_CLOEXEC`，shell 管道端点默认标记以减少 fd 泄漏
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

- [√] 真正的进程独立地址空间（已新增独立用户 CR3 创建、spawn 独立地址空间、调度按进程切 CR3，用户程序改为 0x40000000 高地址加载）
- [√] 重新设计稳定的 CR3 切换方案（已在调度启动、tick 切换、yield 切换中按目标线程加载 CR3，并在释放地址空间前切回 kernel CR3）
- [√] 用户态 / 内核态完整内存隔离（用户程序已迁移到高地址独立 CR3，内核低端映射保持 supervisor-only，并新增 /bin/isotest 回归验证用户态写内核低地址会触发 PF）
- [√] `mmap` / `munmap`（最小匿名映射：`SYS_MMAP` / `SYS_MUNMAP`、固定用户 mmap 区、释放物理页、新增 `/bin/mmaptest` 回归）
- [√] `brk` / `sbrk`（`SYS_BRK` / `SYS_SBRK`、进程堆页增长/收缩、用户 `openos_sbrk/openos_brk`、malloc 后端改为 sbrk、新增 `/bin/sbrktest` 回归）
- [√] page fault 用户态处理框架（#PF 专用处理、CR2/error code/fault flags 日志、用户态故障终止进程、内核态故障停机，为 demand paging 铺路）
- [√] demand paging（heap/mmap 懒分配：`sbrk/mmap` 只保留虚拟区间，用户态 #PF 首次访问合法 heap/mmap 区间时分配并映射零页）
- [√] copy-on-write（fork 用户可写页共享只读 + PTE_COW，写入 #PF 时按物理页 refcount 复制私有页）
- [√] page fault 完整处理（COW/demand/非法访问/OOM 分类处理，新增 pfstats 诊断）
- [√] 用户栈 guard page（栈底预留 4KB 未映射 guard，demand fault 不补映射该页）
- [√] 用户指针安全访问检查（已补强 exec/spawn 的 argv/envp 二级用户指针拷贝，并由 /bin/systest 覆盖非法指针）
- [√] 进程退出时完整释放用户内存映射（已为 fork 独立地址空间增加 owns_address_space，并在 zombie reap 时释放用户页/页表/页目录）

### 5. 调度与同步

- [√] waitpid 阻塞等待，避免忙等 `sched_yield`
- [√] 完善进程 `BLOCKED` / `SLEEPING` 状态语义
- [√] 子进程 exit 唤醒父进程
- [√] 多线程用户态 API（已接入 `SYS_THREAD_CREATE` / `SYS_THREAD_EXIT`、`openos_thread_create` / `openos_thread_exit`、独立用户栈槽位、线程退出栈回收，并由 `/bin/threadtest` 覆盖）
- [√] mutex（已实现 `SYS_MUTEX_CREATE/LOCK/UNLOCK/DESTROY`、用户态 `openos_mutex_*` API、阻塞等待队列，并由 `/bin/mutextest` 覆盖）
- [√] semaphore（已实现 `SYS_SEM_CREATE/WAIT/POST/DESTROY`、用户态 `openos_sem_*` API、计数信号量阻塞等待队列，并由 `/bin/semtest` 覆盖）
- [√] condition variable（已实现 `SYS_COND_CREATE/WAIT/SIGNAL/BROADCAST/DESTROY`、用户态 `openos_cond_*` API、条件变量等待队列，并由 `/bin/condtest` 覆盖）
- [√] futex 或类似轻量同步机制
- [√] priority / nice
- [√] 更完整的调度策略

### 6. 进程控制与信号

- [√] init 进程模型（已实现 PID1 init/reaper 内核线程模型）
- [√] `fork` 稳定化（已新增 /bin/forktest，覆盖 fork 父子返回、私有数据复制、waitpid 退出码回收）
- [√] `exec` 完整替换当前进程镜像
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
- [√] EXT4 读写支持（支持挂载/目录扫描/常规文件读取、已分配块内写入和安全截断）
- [√] 文件缓存 / page cache（块级缓存支持命中统计、脏块回写、按设备/全局失效接口）
- [√] `fsync`（新增 VFS fsync、文件系统/块设备回调和用户态 openos_fsync 包装）
- [√] MBR / GPT 分区表
- [√] 块设备缓存层（已实现 blockdev 统一缓存：命中统计、脏块回写、flush/invalidate）

---

## P3：设备驱动

### 9. 总线与基础硬件

- [√] PCI 总线扫描（已接入启动：配置空间读写、全总线扫描、多功能设备、devmgr 注册/热插拔重扫）
- [√] ACPI（已接入启动：RSDP 扫描/校验、RSDT/XSDT 表查找，供 APIC/电源管理复用）
- [√] APIC / IOAPIC（已实现 MADT 枚举、LAPIC MMIO/EOI、IOAPIC 寄存器访问与 IRQ 重定向/屏蔽接口）
- [√] RTC 时钟（已实现 CMOS 读取、UIP 稳定采样、BCD/12h 转换、启动时间缓存并接入 kernel 初始化）
- [√] 电源管理（已接入启动：FADT/DSDT 解析、_S5_ 提取、ACPI S5 关机、KBC 重启与 shell power/shutdown/reboot 命令）
- [√] 热插拔支持（已接入 devmgr 事件队列、PCI 重扫与 shell hotplug 命令）

### 10. 存储驱动

- [√] IDE / ATA（已实现 legacy PIO identify/read/write、/dev 节点注册并接入启动）
- [√] AHCI / SATA（已实现 AHCI 控制器/端口探测、IDENTIFY、READ/WRITE DMA EXT 与 /dev 节点注册）
- [√] virtio-blk（已实现 legacy PCI virtqueue 初始化、容量读取、同步读写请求与 /dev 节点注册；modern transport 暂跳过）

### 11. 网络驱动

- [√] virtio-net（已实现 legacy PCI virtqueue 初始化、TX/RX 队列、IRQ 收包与 net_input 接入；modern transport 暂跳过）
- [√] e1000（已实现 PCI 探测、MMIO 初始化、MAC 读取、RX/TX 描述符环、IRQ 收包与 net_input 接入）
- [√] rtl8139（已实现 PCI 探测、I/O 初始化、RX/TX 路径、IRQ 收包与 net_input 接入）

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
  - [√] 查询 RX/TX 包计数、字节数、错误数、丢包数等统计信息
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
- [√] 环境变量（shell 内置 `env` / `export` / `unset`，外部程序继承 envp）
- [√] 环境变量 `$VAR` / `${VAR}` 参数展开（支持内置命令、外部命令、pipeline 与重定向参数）
- [√] `PATH` 查找（未带 `/` 的外部命令自动尝试 `/bin/<cmd>`）
- [√] 后台任务 `&`（行尾 `&` 后不等待，支持普通外部命令、`exec` 和 pipeline 后台执行）
- [√] 后台任务状态管理 `jobs` / `fg`（支持查看后台 job、按 `%N` 或默认最近 job 拉回前台）
- [√] `Ctrl+C` / `Ctrl+D`
- [√] 命令补全（Tab 补全内置命令和 `/bin` 外部命令，支持唯一候选补全与多候选列出）
- [√] 脚本执行（支持 `source <file>` / `. <file>` / `sh <file>`，逐行复用现有 shell 管道、重定向、后台任务与内置命令逻辑）

### 16. 用户态运行库

- [√] 标准 libc 子集（header-only：`memset/memcpy/memmove/memcmp/strlen/strcmp/strncmp/strcpy/strncpy/strcat/strncat/strchr/strrchr/strstr/strdup/isdigit/isspace/isalpha/isalnum/isxdigit/islower/isupper/isprint/iscntrl/tolower/toupper/atoi/itoa` 等）
- [√] 基础输出辅助（`putchar` / `puts` / 最小 `printf`，支持 `%s` / `%c` / `%d` / `%i` / `%x` / `%%`）
- [√] 更多用户态测试程序（新增 `/bin/libctest` 覆盖 libc 子集）
- [√] crt0 启动入口完善（新增 `src/user/crt0.c`，支持标准 `main(argc, argv, envp)`，并新增 `/bin/maintest` 回归）
- [√] syscall wrapper 标准化（新增 `openos_syscall0/1/2/3` 与常用 wrapper：进程、文件、目录、pipe、exec、heap 等；新增 `/bin/systest` 回归）
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

- [√] GUI TextInput 组件化：基于现有 `GUI_WIDGET_TEXTBOX` 封装完整单行输入组件
  - [√] 统一由组件维护文本、光标、可见滚动区和编辑状态
  - [√] 支持 Backspace / Delete / Left / Right / Home / End
  - [√] 支持鼠标点击定位光标
  - [√] 支持长文本水平滚动，避免应用自行截断显示
  - [√] 支持 placeholder、readonly、disabled、password 模式
  - [√] 支持 TextChanged / TextSubmit / Focus / Blur 事件
  - [√] 提供用户态 get/set text、get/set cursor API
  - [√] 浏览器地址栏改为复用 TextInput，不再自维护地址栏光标和删除逻辑
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
  - [√] 支持 min/max/value/step
  - [√] 支持 ValueChanged 事件
- [√] GUI Canvas / 绘制区域组件化：把当前 fill/text/blit/scroll/present 低层绘制能力封装成可复用绘制组件
  - [√] 支持脏矩形提交、裁剪区域、局部重绘
  - [√] 支持应用自绘内容与 GUI 控件焦点/事件共存
- [√] 桌面图标组件化：把当前桌面内部图标抽成 IconView/IconButton
  - [√] 支持图标、标题、选中、高亮、双击打开
  - [√] 支持桌面、启动器、文件管理器复用
- [√] 内部菜单组件化：把当前开始菜单 / 右键菜单抽成通用 Menu / ContextMenu
  - [√] 支持菜单项、分隔线、禁用项、快捷键提示
  - [√] 支持子菜单和点击回调事件
- [√] 文件列表组件化：把当前 File Preview / 文件浏览列表抽成 ListView/TableView
  - [√] 支持行选择、列宽、列头排序、滚动、双击打开
  - [√] 支持文件管理器和搜索结果复用
- [√] 终端文本区组件化：把 GUI Terminal 内部文本显示/输入抽成 TextArea 或 TerminalView
  - [√] 支持多行文本、滚动、选中、复制、光标绘制
  - [√] 支持后续文本编辑器和日志窗口复用（TerminalView 提供 layout、坐标换算、选择、剪贴板、绘制 API）
- [√] 设置项组件化：把设置窗口里的开关、滑块、选项行抽成 SettingsRow / Toggle / Slider 组合组件

#### 17.0.1.2 当前缺失的新 GUI 组件

- [x] TextArea：多行文本输入组件
  - [x] 支持多行编辑、换行、滚动；选中、复制/粘贴待补齐
  - [√] 支持文本编辑器、日志窗口、表单 textarea 复用
- [x] ScrollBar：滚动条组件
  - [x] 支持垂直/水平滚动条、拖拽滑块、滚轮事件联动
  - [x] 支持浏览器页面、列表、文本区复用
- [x] ScrollView：可滚动容器组件
  - [x] 支持内容尺寸、视口尺寸、滚动位置和子控件裁剪
- [x] CheckBox：复选框组件
  - [x] 支持 checked / unchecked / disabled 状态
  - [x] 支持设置页和表单复用
- [x] RadioButton / RadioGroup：单选组件
  - [x] 支持分组互斥选择和值变化事件
- [x] Select / ComboBox：下拉选择组件
  - [x] 支持下拉列表、当前值、键盘选择和值变化事件
- [x] ListView：通用列表组件
  - [x] 支持单选/多选、键盘导航、滚动、item renderer
- [x] TableView：表格组件
  - [x] 支持列头、排序、列宽、行选择、滚动
- [x] TreeView：树形组件
  - [x] 支持展开/折叠、层级缩进、文件树/设置树复用
- [x] MenuBar：窗口顶部菜单栏组件
  - [x] 支持 File/Edit/View 等菜单入口和快捷键提示
- [x] ContextMenu：右键菜单组件
  - [x] 支持鼠标位置弹出、点击外部关闭、禁用项
- [x] Dialog：通用弹窗组件
  - [x] 支持信息/警告/错误/确认弹窗
  - [x] 支持模态、按钮区、默认按钮、Esc 关闭
- [x] Toast / Notification：轻量提示组件
  - [x] 支持短提示、自动消失、通知中心复用
- [x] ProgressBar：进度条组件
  - [x] 支持确定进度和不确定加载状态
- [x] Spinner / BusyIndicator：加载动画组件
  - [x] 支持网络加载、后台任务等待状态
- [x] ImageView：图片显示组件
  - [x] 支持 RGBA/位图显示、缩放、保持比例、占位图
- [x] IconView / IconButton：图标视图和图标按钮组件
  - [x] 支持桌面、工具栏、文件管理器复用
- [x] Toolbar：工具栏组件
  - [x] 支持按钮组、分隔线、地址栏/搜索框组合
  - [x] 浏览器顶部栏改为复用 Toolbar + TextInput
- [x] StatusBar：状态栏组件
  - [x] 支持左/中/右区域文本、加载状态、链接提示
- [x] TabView：标签页组件
  - [x] 支持多标签、关闭按钮、当前标签切换
  - [x] 后续浏览器多标签和设置页复用
- [x] SplitView：分栏组件
  - [x] 支持左右/上下分栏和拖动调整比例
- [x] GroupBox / Card：分组面板组件
  - [x] 支持标题、边框、卡片背景，用于设置页和错误页
- [x] Form 组件族：表单布局与输入项组合
  - [x] 支持 Label + Input、错误提示、帮助文本、提交按钮布局
- [x] Layout 布局系统
  - [x] 支持水平/垂直 Box、Grid、Anchor、居中、padding、margin、gap
  - [x] 减少应用手写坐标布局

#### 17.0.1.3 GUI 事件和 ABI 补齐

- [x] 补齐用户态 GUI 事件
  - [x] TextChanged / TextSubmit
  - [x] Focus / Blur
  - [x] Resize / Move
  - [x] MouseMove / MouseDown / MouseUp / MouseWheel
  - [x] KeyUp
  - [x] ValueChanged / SelectionChanged
- [x] 补齐键盘事件修饰键
  - [x] Shift / Ctrl / Alt / Meta modifiers
  - [x] 支持 Shift+Tab、Ctrl+A/C/V/X、快捷键分发
- [x] 补齐文本输入 ABI
  - [x] 区分物理按键 KeyDown 与文本输入 TextInput
  - [x] 为 UTF-8 / 中文输入法预留接口
- [x] 建立 GUI 组件 smoke 测试
  - [x] 新增 `/bin/guicomponenttest` 覆盖 TextInput/Button/List/Dialog 等基础交互
  - [x] `./build.sh test` 纳入组件事件和 ABI 回归

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
- [√] HTTPS/TLS 握手解析增强：解析 ServerKeyExchange / ECDHE 曲线、公钥、签名算法摘要
- [√] 浏览器 HTTPS 握手详情展示：显示完整握手类型列表、扩展长度、ECDHE 摘要和下一步密钥交换限制

#### 17.3.1.1 浏览器内核路线 / 开源内核移植

- [√] 明确当前内核内置 Browser 只是过渡实现，不作为最终完整浏览器内核
  - [√] 保持当前轻量浏览器继续可用：HTTP 访问、DNS/TCP/HTTP 非阻塞加载、基础 HTML 转可读文本、简单链接导航
  - [√] 增强当前 HTML 文本化渲染：更完整的 entity 解码、空白压缩、段落/标题/列表/pre/code 基础处理
  - [√] 增强浏览器加载状态机：DNS/TCP/HTTP 分阶段超时、失败状态显示、连续 Go/Refresh 取消旧请求、关闭窗口取消加载上下文
- [√] 将 Browser 从 `src/kernel/gui.c` 内核 GUI 中拆出，迁移为用户态 `/bin/browser`；桌面 Browser 入口优先启动用户态 `/bin/browser`，内核内置 Browser 仅作 fallback
  - [√] 设计用户态 GUI 应用 ABI：窗口创建、绘制、输入事件、定时器、剪贴板/文本输入等接口
  - [√] 落地最小用户态 GUI syscall ABI：创建/销毁窗口、添加标签/按钮、按钮事件轮询，并用 `/bin/guiprobe` 验证
  - [√] 浏览器崩溃不应拖垮内核，错误通过进程退出或窗口关闭处理：用户态 GUI 窗口绑定进程 PID，`sys_exit` 自动回收窗口和事件
  - [√] 网络访问统一走用户态 socket/libc API，而不是直接调用内核内部函数：新增 `/bin/browser` 用户态原型，使用 `openos_getaddrinfo/openos_socket/openos_connect/openos_send/openos_recv` 拉取 HTTP 页面
- [√] 补齐移植开源浏览器内核所需的基础运行环境
  - [√] libc/POSIX 子集：malloc/free/realloc、stdio、string、time、errno、文件 API、目录 API；新增 `SYS_UPTIME_MS` 与用户态 `time/gettimeofday/clock` 兼容封装
  - [√] socket API：getaddrinfo/gethostbyname、connect/send/recv/close、select/poll、非阻塞 socket
  - [√] TLS/HTTPS 用户态库适配：优先评估 mbedTLS / BearSSL / wolfSSL 等轻量方案
  - [√] 字体接口：字体枚举、字形查询、UTF-8/Unicode 文本测量、基础 fallback；新增 `SYS_FONT_QUERY` 与 `/bin/fontprobe` 验证
  - [√] 图形接口：framebuffer/窗口绘制、矩形裁剪、位图 blit、滚动、双缓冲；扩展 `SYS_GUI_DRAW` 支持 fill/text/blit/scroll/present，并由 `/bin/guiprobe` 验证
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
  - [√] P2：新增 OpenOS Chromium GN/toolchain 骨架，目标为 `target_os="openos"`、`target_cpu="x86"`
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
  - [√] 明确第一目标为 OpenOS 原生核心能力补齐，而不是 POSIX/Linux 兼容层堆叠
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
  - [√] `mmap` 支持完整 `prot` / `flags` 语义：read/write/exec、private/shared、anonymous/file-backed
    - [√] 已完成匿名私有 VMA 记录、基础 prot/flags 参数、按需分页按 VMA 写权限映射
    - [√] 已将非法 `prot` / `flags` 拒绝、只读映射读零、`mprotect` 升级读写等最小权限语义并入 `/bin/chromiumcaptest`
  - [√] 实现 `mprotect` 页级权限切换基础能力，已接入 `/bin/chromiumcaptest` 验收
  - [√] 支持固定地址映射、地址空间保留、解除映射后的 VMA 合并与冲突检测
    - [√] 已完成 `MAP_FIXED` 基础固定地址预留与重叠 VMA 冲突拒绝
    - [√] 已补齐 `munmap` 对 VMA 头/尾裁剪、中间拆分、相邻匿名兼容 VMA 合并与未映射区间拒绝，并接入 `/bin/chromiumcaptest` 验收
  - [√] 文件 mmap 与 page cache 协同，支持只读资源映射和私有 COW 映射
    - [√] 已完成基础 file-backed private snapshot mmap：`SYS_MMAP_FILE` 可将 fd 内容映射到用户地址空间，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已增强 `/bin/chromiumcaptest` 对 file-backed `MAP_PRIVATE` 的不回写校验：映射内修改后重新映射应仍看到原始文件内容
    - [√] 已收紧 file-backed mmap flags 语义：当前仅接受 `MAP_PRIVATE|MAP_FILE`，显式拒绝 `MAP_SHARED`、`MAP_FIXED`、`MAP_ANON` 等未实现组合，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已新增 file-backed `MAP_SHARED|MAP_FILE|PROT_READ|PROT_WRITE` 基础写回语义，VMA 记录 fd/文件长度，`munmap` 前按映射文件范围写回并恢复 fd offset；继续拒绝只读 MAP_SHARED 和 MAP_FIXED/MAP_ANON 组合，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已补齐 file-backed 映射最终页权限：只读资源映射加载后降为只读页，`MAP_PRIVATE|PROT_WRITE` 使用 `PTE_COW` 写时复制，避免私有写污染底层文件，并接入 `/bin/chromiumcaptest` 验收
  - [√] 为 V8 预留 executable memory / jitless 两条路线的内核策略
    - [√] 已完成原生 `SYS_CHROMIUM_MEMORY_POLICY` 策略查询：当前 i386 阶段声明默认 jitless，`PROT_EXEC` 语义已保留，待 NX/W^X 后启用 executable mmap
    - [√] 已将当前 jitless 策略落到 syscall 行为：`mmap/mmap_file/mprotect(PROT_EXEC)` 显式失败，并接入 `/bin/chromiumcaptest` 验收
- [√] M3 线程、同步与调度增强
  - [√] 用户态线程 TLS / thread-local storage 基础 ABI
    - [√] 已完成轻量 TLS base syscall：`SYS_TLS_SET/SYS_TLS_GET`，线程结构保存 `tls_base`，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已新增 `SYS_THREAD_CREATE_TLS` / `openos_thread_create_tls()`，支持线程创建时指定初始 TLS base，并接入 `/bin/chromiumcaptest` 验收
  - [√] futex wait/wake 语义稳定化，补齐超时、唤醒数量和错误码
    - [√] 已补充 `/bin/chromiumcaptest` 跨线程 futex wait/wake 验收，覆盖 expected mismatch、非法地址、无等待者 wake、wake(0)、阻塞、单线程唤醒数量和唤醒后共享状态可见性
    - [√] 新增 `SYS_FUTEX_WAIT_TIMEOUT` / `openos_futex_wait_timeout()`，覆盖 0ms 非阻塞超时、毫秒级超时、expected mismatch 和非法地址错误路径
    - [√] 已补充 `/bin/chromiumcaptest` semaphore 生产者/消费者同步验收，覆盖非法句柄、非法初始值、阻塞等待、post 唤醒和 destroy 后失效
  - [√] 条件变量、mutex、semaphore 压测，确保可支撑 Chromium base::Thread / TaskRunner
    - [√] 已补充 pthread-like 用户态薄封装 `openos_pthread_*`，并在 `/bin/chromiumcaptest` 中增加 mutex/cond 同步验收
    - [√] 已增强 mutex/cond 压测，覆盖非法句柄、单等待者 signal 和双等待者 broadcast 唤醒
  - [√] 高精度单调时钟、定时器队列、睡眠唤醒精度改进
    - [√] 已新增 `SYS_CLOCK_GETTIME` / `OPENOS_CLOCK_MONOTONIC` 单调 timespec 接口，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已新增 `SYS_NANOSLEEP` / `openos_nanosleep()`，基于单调毫秒 tick 向上取整到毫秒睡眠，并接入 `/bin/chromiumcaptest` 验收非法 timespec、0ns rem 清零和短睡眠单调不倒退
- [√] M4 进程、加载器与 IPC 能力增强
  - [√] 稳定 fork/exec/spawn 与 fd/env/argv 继承语义
    - [√] 已在 `/bin/chromiumcaptest` 增加 spawn_env + argv/envp + waitpid 验收，以及 fork 后 pipe fd 继承读写验收，覆盖 Chromium 多进程启动的最小基础语义
    - [√] 已新增 `/bin/fdinherit` 子程序，并在 `/bin/chromiumcaptest` 通过 spawn/exec 后继承 pipe fd 读取数据，覆盖 Chromium 子进程启动时 fd 继承的最小语义
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
    - [√] 已新增 `tls_x509` 最小 DER X.509 证书结构解析底座，支持安全 TLV 长度解析、证书顶层三段解析、TBS 内版本/序列号/issuer/validity/subject/SPKI 原始切片提取、UTC/GeneralizedTime 有效期解析校验、issuer/subject 原始 DER 链接匹配，并已在 `tls_trust` 层串起证书链结构校验、有效期校验与信任根锚定，接入单测与内核构建脚本
    - [√] 已补齐 X.509 OID/AlgorithmIdentifier/SPKI/签名 BIT STRING/RSA 公钥/DigestInfo 解析，新增 SHA256-RSA PKCS#1 v1.5 签名验签底座，并提供证书链签名校验与 TLS Certificate record 签名校验桥接入口
    - [√] 已新增 TLS 1.2 SHA-256 PRF、master secret/key block/Finished verify data 派生、AEAD key block 切片、record AAD/nonce 构造、AES-128 单块、AES-128-GCM 加解密认证、TLS record header view/write 工具、TLS 1.2 AES_128_GCM payload/wire record protect/unprotect 封装，以及自动选择 client/server 写密钥、固定 IV、读写序列号递增的 record layer 上下文，并接入 NIST 向量、往返、篡改失败和 sequence 不回退单测
    - [√] 已新增 TLS 1.2 handshake transcript SHA-256 累积上下文，支持握手消息分片增量哈希、快照导出 handshake hash，以及直接从 transcript 派生 Finished verify_data，为后续握手状态机串联 ClientHello/ServerHello/Certificate/Finished 打底
    - [√] 已新增 `tls_handshake` TLS 1.2 客户端握手状态机骨架，覆盖 ClientHello sent、ServerHello/Certificate/ServerHelloDone received、ClientKeyExchange/ChangeCipherSpec/Finished sent、Server ChangeCipherSpec/Finished received、transcript 累积、证书链 view 捕获、协商版本/cipher suite 记录、master secret 注入后的 client/server Finished verify_data 计算与常量时间校验，以及乱序/非法 CCS/错误 Finished 失败路径，并已接入单测与内核构建脚本
    - [√] 已将 handshake 中的 master secret 串联到 TLS 1.2 AES_128_GCM_SHA256 key block 派生与 `tls12_aes128_gcm_record_layer_t` 初始化，保存 record keys/key block，支持按 client/server role 配置 record 层，并通过 client 加密、server 解密联通单测验证
    - [√] 已新增 TLS 1.2 ClientHello record 构造器，支持 SNI、supported_groups、ec_point_formats、signature_algorithms、ALPN http/1.1、encrypt_then_mac、extended_master_secret、renegotiation_info 和 supported_versions 扩展，并接入长度/随机数/SNI/ALPN/容量边界单测
    - [√] 已增强 TLS 1.2 ServerHello 解析，校验协商版本、AES_128_GCM_SHA256 cipher suite、null compression，解析 extended_master_secret、renegotiation_info、ALPN 和 supported_versions 扩展，并覆盖错误版本/套件/压缩/扩展版本拒绝单测
    - [√] 已新增 TLS 1.2 RSA pre-master secret 构造、RSA ClientKeyExchange handshake message 构造、ClientKeyExchange 结构长度校验，并将 pre-master secret 串联到 master secret 派生与后续 AES_128_GCM key block/record layer 初始化路径，覆盖错误版本和畸形 CKE 拒绝单测
    - [√] 已新增 TLS 1.2 handshake record I/O 最小联通能力，支持构造 ChangeCipherSpec wire record、构造并加密 client Finished record、解析 server ChangeCipherSpec record、解密并校验 server Finished record，覆盖 client/server record layer 切换后的 Finished 加解密联通单测
  - [√] 为 Chromium net stack 所需 socket 行为补齐错误码、非阻塞、poll 边界语义
    - [√] 已增强 `/bin/chromiumcaptest` 的 `socketpair` poll/select 边界验收，覆盖空队列不报 `POLLIN`、多 fd poll、负 fd 忽略、非法 fd `POLLERR`、select 读写位图、可写端 `POLLOUT`、空读失败和对端关闭 `POLLHUP`
    - [√] 已新增 `SYS_FCNTL` / `openos_fcntl` 最小 flags ABI，覆盖 `F_GETFL/F_SETFL/O_NONBLOCK` 开关、非法 fd 和非法 cmd，为后续 socket 非阻塞 I/O 语义打底
    - [√] 已新增 `SYS_SETSOCKOPT` / `SYS_GETSOCKOPT` 最小 socket options ABI，覆盖 `SO_REUSEADDR`、`SO_KEEPALIVE`、`SO_RCVTIMEO`、`SO_SNDTIMEO`、`TCP_NODELAY`、非法 opt 和短 optlen 边界
- [√] M7 图形、字体与输入
  - [√] 为 Skia software raster 提供窗口 framebuffer / shared bitmap / dirty rect present 能力
    - [√] 已将用户态窗口创建、控件、fill/text/blit/scroll/present 基础绘制 smoke 并入 `/bin/chromiumcaptest`
  - [√] 完善字体枚举、字体 fallback、字形缓存、文本测量、UTF-8/Unicode 输入
    - [√] 已将 `SYS_FONT_QUERY` 字体度量、换行文本测量与 codepoint 查询 smoke 并入 `/bin/chromiumcaptest`
  - [√] 输入事件队列支持鼠标、键盘、组合键、文本输入、滚轮和窗口焦点
    - [√] 已将用户态 GUI 事件队列空队列/非法参数 smoke 并入 `/bin/chromiumcaptest`
  - [√] 剪贴板、光标、DPI/缩放和窗口 resize 事件
    - [√] 已新增 `SYS_CLIPBOARD_SET` / `SYS_CLIPBOARD_GET` 与用户态 `openos_clipboard_set/get`，并接入 `/bin/chromiumcaptest` 验收
    - [√] 已新增 `SYS_GUI_RESIZE_WINDOW`、`SYS_GUI_GET_WINDOW_INFO`、`SYS_GUI_GET_DISPLAY_INFO` 最小 ABI，提供窗口 resize、窗口尺寸查询与 96 DPI/1000 scale 基础显示信息，并接入 `/bin/chromiumcaptest` 验收
- [√] M8 C/C++ runtime 与工具链
  - [√] 用户态 C++ 编译、链接、构造/析构、异常策略、RTTI 策略
    - [√] 已新增 `build.sh cppsmoke` 工具链探测入口；当前环境缺少 `i686-elf-g++/clang++/g++` 时会明确失败并提示 `OPENOS_CXX`，避免静默伪装 C++ 能力完成
  - [√] libstdc++/libc++ 子集或 OpenOS C++ runtime 路线
    - [√] 已新增 `docs/chromium-cpp-runtime-roadmap.md`，明确工具链探测、最小 C++ ABI、new/delete、静态初始化、异常/RTTI 策略与 `/bin/cppsmoke` 验收顺序
  - [√] 原子操作、内存序、TLS、new/delete、静态初始化
    - [√] 已新增 `openos_cxxabi.h` 最小 C++ ABI 支撑层与 `/bin/cxxabitest`，覆盖 `new/delete`、guard variable、atomic fetch_add/load、init/fini array dispatch，并由 `/bin/chromiumcaptest` spawn 汇总验收
  - [√] 宿主机交叉编译 Chromium 依赖的 GN/Ninja/Clang 构建链设计
    - [√] 已新增 `docs/chromium-build-chain.md`，固定 i386-openos-elf 目标、GN args 初始草案、OpenOS sysroot/CRT/runtime 产物边界，以及 skia_demo -> v8_shell -> blink_smoke -> content_shell -> chromium 的分阶段构建验收顺序
- [√] M9 Skia / V8 / Blink / Chromium 分阶段落地
  - [√] `/bin/skia_demo`：软件绘制矩形、文本、图片到 OpenOS 窗口
    - [√] 已新增 `src/user/skia_demo.c`，使用 OpenOS GUI framebuffer/blit/text/font 查询接口绘制矩形、文本和内置图片，并接入构建嵌入与 `/bin/skia_demo` 安装
  - [√] `/bin/v8_shell`：优先 jitless 运行基础 JavaScript
    - [√] 已新增 `src/user/v8_shell.c` jitless 基础 JavaScript shell 入口，支持算术表达式、变量声明/赋值和 `print()` smoke，并接入构建嵌入与 `/bin/v8_shell` 安装
  - [√] `/bin/blink_smoke`：最小 HTML/CSS layout smoke
    - [√] 已新增 `src/user/blink_smoke.c` 最小 HTML/CSS block-flow layout smoke，绘制 DOM 节点、CSS margin/padding、文本与图片占位，并接入构建嵌入与 `/bin/blink_smoke` 安装
  - [√] `/bin/content_shell`：单进程、disable-gpu、disable-sandbox 打开 `http://example.com`
    - [√] 已新增 `src/user/content_shell.c` 单进程 content shell smoke，解析 URL 与 `--disable-gpu/--disable-sandbox`，绘制地址栏、页面内容和状态，并接入构建嵌入与 `/bin/content_shell` 安装
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

- [√] 新增生成式 16x16 CJK 点阵字库 ABI：`generated/cjk_font.h`
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
  - [√] 避免将完整未压缩 CJK 字库直接放入内核低端加载镜像，防止超过 `0x8000~0xA0000` BIOS 加载区限制
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

- [x] 阶段 1：字体灰度抗锯齿 / 更平滑渲染
  - [x] 增加基于 coverage 的文本边缘平滑绘制能力
  - [x] ASCII 与 CJK 绘制路径统一支持平滑像素输出
  - [x] 保持默认 framebuffer 直接绘制兼容
- [x] 阶段 2：framebuffer alpha 混合基础能力
  - [x] 新增颜色混合 / alpha 像素接口
  - [x] 新增半透明矩形等基础绘制 API
  - [x] 文本抗锯齿复用 alpha 混合接口
- [x] 阶段 3：高质量图标资源 / 绘制路径
  - [x] 将常用桌面图标从简单像素块升级为更细致的图标绘制
  - [x] 图标边缘支持高光 / 阴影 / 伪抗锯齿
  - [x] 保持低内存、无外部资源依赖
- [x] 阶段 4：显示后端扩展框架
  - [x] 梳理 framebuffer driver 接口，为 VESA / EFI GOP / virtio-gpu 后端预留能力
  - [x] 增加后端类型枚举 / capability / 注册选择机制
  - [x] 保持现有 Bochs/QEMU BGA 后端稳定
- [x] 阶段 5：GUI 合成器基础能力
  - [x] 增加 GUI dirty rect / 双缓冲基础设施
  - [x] 为窗口阴影、透明度和减少闪烁铺路
  - [x] 保持现有 GUI 行为兼容并通过回归验证

---

## P7：安全与权限

### 18. 安全模型

- [√] 用户 / 组
- [√] uid / gid（已加入进程 `uid/gid` 凭据、fork 继承、`SYS_GETUID` / `SYS_SETUID` / `SYS_GETGID` / `SYS_SETGID` 和用户态封装）
- [√] 文件权限检查（VFS 已按 `S_IRWXU/G/O` 对文件、目录、符号链接和挂载操作执行权限校验）
- [√] 进程权限（已支持当前进程凭据查询/切换，非 root 仅允许保持自身 uid/gid，root 可切换）
- [√] capability
- [√] syscall 权限控制
- [√] 沙箱
- [√] 内核地址保护
- [√] ASLR
- [√] NX / W^X
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

#### 21.1 x86_64 支持

- [√] 保留当前 i386 稳定基线，新增 `ARCH=i386/x86_64` 或 `./build.sh i386|x86_64` 构建入口
- [√] 新增 `src/arch/x86_64/` 架构目录，逐步拆分 i386 与 x86_64 架构相关代码
- [√] 新增 x86_64 linker script
- [√] 新增 x86_64 启动骨架，第一阶段只进入 `kernel_main64()` 并输出日志
- [√] 从 BIOS 启动路径进入 long mode
  - 说明：实际启动路径走 GRUB/Multiboot2 + UEFI（见下方「评估现代 bootloader」一项），自研 BIOS 16→64 自举链以骨架形式保留在 `src/arch/x86_64/boot/boot64.asm`，由 `build.sh` 编译并校验 512 字节 + 0x55AA 签名，不接入主磁盘镜像。
  - [√] 16 位实模式启动（骨架 `_start16`，A20 / GDT16 / cr0.PE 切 32 位）
  - [√] 进入 32 位保护模式（骨架 `start32`，DS/ES/SS=0x10 数据段）
  - [√] 建立 PML4 / PDPT / PD / PT（骨架使用 2MB 大页恒等映射前 1GB）
  - [√] 开启 PAE（骨架置 cr4.PAE=1）
  - [√] 设置 `EFER.LME`（骨架经 MSR 0xC0000080 置位 LME）
  - [√] 开启分页并 far jump 到 64 位代码段（骨架 `cr0.PG=1` 后 `jmp 0x18:start64`）
- [√] 评估是否引入 Limine / BOOTBOOT / Multiboot2 等现代 bootloader，降低 UEFI 和 long mode 启动复杂度
- [√] 实现 64 位 GDT
- [√] 实现 64 位 TSS 与 `rsp0` / IST
- [√] 实现 64 位 IDT 和异常入口
- [√] 移植串口 / VGA / framebuffer 早期输出到 x86_64
- [√] 移植 PMM 到 x86_64
- [√] 实现 4 级分页 VMM
- [√] 移植内核堆分配器到 x86_64
- [√] 将地址、指针、栈、ELF entry 等字段从 `uint32_t` 整理为 `uintptr_t` / `size_t` / `uint64_t`
- [√] 编译参数支持 x86_64 内核
  - [√] `-m64`
  - [√] `-ffreestanding`
  - [√] `-fno-stack-protector`
  - [√] `-fno-pic` / `-fno-pie`
  - [√] `-mno-red-zone`
  - [√] `-mcmodel=kernel`
- [√] 移植调度器上下文切换到 `rsp/rip/rflags` 和 `r8-r15`
- [√] 将 `kernel_esp` 等 32 位字段迁移或抽象为架构相关的 `kernel_sp`
- [√] 第一阶段继续支持 `int 0x80` syscall
- [√] 后续实现 x86_64 `syscall/sysret`
- [√] 支持 ELF64 loader
- [√] 支持 64 位用户态 `iretq` 返回
- [√] 支持 64 位用户态 syscall wrapper / crt0
- [√] 支持 64 位用户程序 `/bin/hello64` 回归测试
- [√] 后续评估兼容 32 位用户程序

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

- [x] P7.1：确认百度 TLS 协商需求并补充失败回归用例
  - [x] 记录静态 RSA 被拒绝、ECDHE-RSA/P-256 可成功的验证结论：OpenSSL 验证百度拒绝静态 RSA/AES128-GCM，接受 ECDHE-RSA-AES128-GCM-SHA256 + P-256
  - [x] 为当前 `unsupported server handshake` 路径补测试或诊断文案
- [x] P7.2：新增 P-256 椭圆曲线基础运算
  - [x] 实现 256 位有限域加减乘平方逆元
  - [x] 实现 secp256r1 点加、倍点、标量乘
  - [x] 增加 RFC/NIST 测试向量或自洽单元测试
- [x] P7.3：扩展 TLS 1.2 ECDHE-RSA 握手解析
  - [x] 解析 `ServerKeyExchange` 中的 named_curve、server ECDHE public key 和签名
  - [x] 校验曲线必须为 `secp256r1/P-256`
  - [x] 校验 RSA-SHA256 ServerKeyExchange 签名
- [x] P7.4：实现 ECDHE ClientKeyExchange 与 master secret 派生
  - [x] 生成客户端 P-256 临时密钥对
  - [x] 发送 uncompressed point 格式的 ECDHE ClientKeyExchange
  - [x] 使用 ECDHE shared secret 派生 TLS master secret
- [x] P7.5：接入浏览器 HTTPS 加载路径
  - [x] 在 ClientHello 中正确声明 ECDHE-RSA-AES128-GCM-SHA256、supported_groups 和 ec_point_formats
  - [x] 保持静态 RSA/AES-GCM 兼容路径
  - [x] HTTPS 错误页输出更准确的协商失败原因
- [x] P7.6：构建、单测、QEMU smoke 与提交
  - [x] Windows/MinGW 等价全量单元测试
  - [x] `wsl -d Ubuntu -- bash -lc "cd /mnt/e/openos && ./build.sh test"`
  - [x] freestanding/browser 语法检查
  - [x] `wsl -d Ubuntu -- bash -lc "cd /mnt/e/openos && ./build.sh"`
  - [x] `scripts/qemu-smoke.sh --timeout 25`
  - [x] 重新生成 `src/kernel/include/embed_browser.h`
  - [x] 提交修改
