# OPENOS 触屏/手机形态兼容路线（M8-E ~ M12）

> 更新时间：2026-07-17
> 状态：**规划中**（M8-A 已完成，M8-B/C/D 已在 TODOLIST.md 主档立项）
> 关联主档：TODOLIST.md 中 "M8 触屏输入栈" 章节
>
> 目标：让 OPENOS 从"PC 桌面 + 单点触屏 tablet"进一步演进为"能在触屏为主的手机形态设备上完整可用"的操作系统。
> 本文档承接 TODOLIST.md 的 M8-A~D，覆盖 输入抽象层 → 触屏 GUI → 应用运行时 → ARM64 移植 → 移动特性 五阶段。
>
> **底线约束**：每完成一个子里程碑，Stages 1-30 SMP=1/4 双矩阵 + gfx-selftest + input-selftest 三条基线不得回退。

---

## M8-E 输入抽象层（Input Abstraction Layer, IAL）

**目标**：所有输入设备（USB 鼠标 / xHCI tablet / 未来的 I2C 多点触屏 / PS2 键盘 / 未来的传感器）统一走一条事件总线，GUI/锁屏/终端只消费抽象事件，不再直接读 g_mouse。

**背景**：当前 gui.c 与 lockscreen.c 直接调用 mouse_snapshot，usb_hid64.c 直接写 g_mouse。设备扩展性差、无法多点、无法录制回放、无法在用户态注入。

### 交付物
1. `src/kernel/include/input_event.h`
   - 统一事件结构 `input_event_t { type, code, value, timestamp, device_id }`
   - 事件类型：EV_KEY / EV_REL / EV_ABS / EV_MT_SLOT / EV_MT_TRACKING_ID / EV_MT_POSITION_X/Y / EV_MT_PRESSURE / EV_SYN
   - 参考 Linux evdev 语义，便于未来对接 evdev-like 用户态工具
2. `src/kernel/input/input_core.c`
   - 环形队列 input_queue（容量 256 事件）
   - `input_report(dev_id, type, code, value)` 生产端 API
   - `input_poll_event(&ev)` 消费端 API
   - `input_sync(dev_id)` 提交一帧多字段事件
3. `src/kernel/input/input_devices.c`
   - 设备注册表 `input_device_register(name, caps)` → 返回 device_id
   - capability 位图（KEY / REL / ABS / MT）
4. **生产端改造**：usb_hid64.c 鼠标/tablet 路径 → `input_report`；键盘同理
5. **消费端改造**：gui.c / lockscreen.c 主循环改为 `while(input_poll_event(&ev)) gui_dispatch_input(&ev)`；mouse_snapshot 保留为兼容 shim
6. `sys_input_read()` 系统调用（新增），用户态可读取事件流
7. 自检 input-selftest：注入合成事件序列，验证 FIFO / 溢出 / SYN 完整性

### 验证
- [ ] 鼠标 / tablet / 键盘功能与 M8-A 完全一致
- [ ] input-selftest PASS
- [ ] Stages 1-30 SMP=1/4 无回退

### 影响面
- 新增：3 文件（约 400 行）
- 修改：4 文件（约 150 行）


---

## M9 GUI 触屏化改造

**目标**：移除鼠标指针依赖，将桌面 GUI 从"鼠标第一"转变为"触摸第一"，保留窗口模式作为 PC 兼容项（类似 Samsung DeX）。

**背景**：当前 GUI 依赖 cursor 指针、32px 小图标、点击命中测试。触屏需要 44pt（约 64px）触摸目标、事件驱动命中、手势映射、无指针显示。

### M9-A 触摸目标改造
1. Desktop 图标尺寸：32px → 64px（44pt 基线，留边距）
2. 按钮/控件最小尺寸：终端 / 窗口标题栏 / 按钮 → 最小 48x48px
3. 命中测试扩张：`gui_hit_test(x,y, padding=8)`，触摸点周围 8px 也算命中（Fat Fingers 补偿）

### M9-B 模式切换与指针控制
1. `gui_set_mode(GUI_MODE_DESKTOP | GUI_MODE_TOUCH)` 公开 API
2. GUI_MODE_TOUCH 时 `cursor_visible = 0` 且不渲染任何指针
3. 保留 `gui_hw_cursor_tick` 仅用于 PC 兼容模式
4. 新增 `touch_mode` 内核参数，默认 auto（检测到触屏设备自动切换）

### M9-C 基础手势映射
1. tap（点按）→ 对应鼠标左键 click
2. long-press（长按 ≥ 500ms）→ 对应右键/上下文菜单
3. swipe（滑动）→ 桌面翻页 / 列表滚动
4. 手势阈值：移动 ≥ 10px 视为滑动，否则视为点按

### M9-D 事件驱动命中测试
1. 命中测试不再依赖 mouse_snapshot，改为 `gui_hit_test_at(touch_x, touch_y)`
2. 支持多指同时命中（返回窗口列表 + 控件 ID）
3. 新增 `input_dispatch_touch(points[])` 批量处理触点

### 验证
- [ ] 触摸模式无指针，桌面图标可点击（触屏 tablet 验证）
- [ ] 长按触发右键菜单
- [ ] 滑动可滚动终端窗口
- [ ] 桌面图标点击准确率 ≥ 95%
- [ ] PC 兼容模式（鼠标）功能无回退

### 影响面
- 修改：3 文件（gui.c / gui.h / lockscreen.c）约 300 行
- 新增：手势阈值配置头文件 1 个

---

## M10 应用运行时（全屏手机模型）

**目标**：在窗口模型之上叠加"全屏应用栈"模型，适配手机"一次一个前台应用"的交互范式。

### M10-A 全屏应用 API
1. `app_launch(path)` → 启动 ELF 程序并进入全屏模式
2. `app_exit()` → 返回上一个应用或桌面（栈式返回）
3. `app_switcher()` → 显示最近应用列表（三指上滑或实体键触发）

### M10-B 应用清单（Manifest）
1. `manifest.toml` 规范：图标、名称、权限、生命周期钩子
2. 钩子：`on_pause / on_resume / on_destroy`
3. 权限声明：`access_input / access_network / access_storage`

### M10-C 生命周期管理
1. 前台应用：独占输入焦点 + 全屏渲染
2. 后台应用：暂停事件循环 + 内存可回收标记
3. OOM 策略：LRU 回收后台应用（类似 Android Low Memory Killer）

### M10-D PC 兼容模式保留
1. 桌面窗口模式作为可选（类似 Samsung DeX）
2. `gui_set_mode(GUI_MODE_DESKTOP)` 可随时切回
3. 窗口可拖拽/缩放（鼠标模式）

### 验证
- [ ] 启动终端 → 全屏显示，无窗口边框
- [ ] 返回手势 → 回到桌面
- [ ] 应用切换器 → 列出已启动应用
- [ ] 后台应用在内存压力下被回收

### 影响面
- 新增：`src/kernel/app/` 目录（约 500 行）
- 修改：gui.c / syscall_dispatch64.c 约 200 行

---

## M11 ARM64 移植（手机硬件基座）

**目标**：将 OPENOS 从 x86_64 扩展到 aarch64，为手机真机铺路。先在 QEMU virt 验证，再对接真实开发板。

### M11-A 构建系统扩展
1. `build.sh` 新增 `TARGET=aarch64` 参数
2. 交叉编译链：`aarch64-none-elf-gcc`（WSL/Ubuntu 可通过 apt 安装）
3. 链接脚本新增 `linker_aarch64.ld`

### M11-B UEFI on ARM
1. 复用现有 UEFI 引导逻辑 80%（EFI 协议层跨平台）
2. QEMU 验证：`qemu-system-aarch64 -M virt -bios AAVMF_CODE.fd`
3. 启动日志输出到 UART 与 UEFI GOP 双路径

### M11-C 驱动抽象层（HAL）
1. PCI 枚举 → Device Tree 解析（ARM 标准）
2. 中断控制器：APIC → GICv3
3. 定时器：APIC Timer → Generic Timer

### M11-D ARM 触屏驱动骨架
1. I2C 控制器驱动（GPIO 位模拟或硬件 I2C）
2. Goodix GT911 驱动骨架（常见手机触屏主控）
3. 触点上报走 M8-E 输入事件总线（无缝接入）

### 验证
- [ ] QEMU aarch64 virt 机型可启动到桌面
- [ ] Stages 1-30 基础 selftest PASS
- [ ] UART 与 GOP 显示正常

### 影响面
- 新增：`src/arch/aarch64/` 目录（约 1000 行）
- 修改：build.sh / 平台抽象头文件 约 200 行

---

## M12 移动特性（手机可用）

**目标**：补齐手机形态的核心特性，达到"可日常使用"级别。

### M12-A 电源管理
1. CPU Idle / Suspend-to-RAM
2. 唤醒源：触屏双击 / 电源键
3. 屏保与自动熄屏

### M12-B 传感器栈
1. 加速度计（I2C 读取）→ 自动旋转
2. 陀螺仪 → 手势唤醒
3. 光线传感器 → 背光调节

### M12-C 显示旋转
1. 横屏/竖屏自动切换（基于加速度计）
2. GUI 渲染管线旋转（90/180/270 度）
3. 触摸坐标同步旋转映射

### M12-D 系统 UI
1. 状态栏（时间、电量、信号占位）
2. 通知中心（下拉手势）
3. 快速设置（亮度、WiFi、飞行模式）

### 验证
- [ ] 横屏握持 → GUI 自动旋转，触摸坐标正确
- [ ] 无操作 30s → 自动熄屏，双击唤醒
- [ ] 下拉通知中心可展开

### 影响面
- 新增：`src/kernel/sensors/` + `src/kernel/power/` 目录（约 800 行）
- 修改：gui.c / 显示驱动 约 200 行

---

## 关键架构决策（已确认）

1. **输入抽象层优先**：不做手势引擎前先做 M8-E 事件总线，这是所有后续工作的地基。
2. **手势识别放用户态**：核心逻辑在 gestured 服务，内核只上报 raw points，便于迭代。
3. **双模 GUI 而非重写**：共享渲染管线，仅切换命中策略与交互模式。
4. **ARM64 先 QEMU 后真机**：不用等硬件，virt 机型 + EDK2 就能验证 80% 移植工作。
5. **保持 POSIX-like**：应用运行时基于现有 SYS_LOGIN 体系扩展，不引入重量级框架。

---

## 下一步建议

当前最优开局是 **M8-E 输入抽象层**——改动小、收益大、不依赖真机，且能立刻用现有 QEMU tablet 环境验证。
