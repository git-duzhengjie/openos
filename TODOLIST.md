# openos 待开发功能清单

> 更新时间：2026-06-15
>
> 当前状态：openos 已具备 32 位 x86 原型内核能力，能够启动、显示、输入、调度、运行基础用户程序，并具备基础 syscall、VFS、ramfs/tmpfs、shell、GUI Terminal 等模块。以下清单记录后续仍需开发或完善的功能。
>
> 最近完成：已补齐 shell 后台任务、`Ctrl+C` / `Ctrl+D`、`jobs` / `fg`、Tab 命令补全、脚本执行；本轮完成用户态运行库 libc 子集，`src/user/openos.h` 新增 `memset/memcpy/memmove/memcmp/strncmp/strchr/strrchr/strstr/isdigit/isspace/atoi/itoa/putchar/puts/printf` 等 header-only 能力，并新增 `/bin/libctest` 回归程序覆盖基础字符串、内存、转换与输出函数。
>
> 当前推荐下一步：继续 P5，标准化 syscall wrapper 命名与错误返回约定，为后续 errno、stdio 和更多用户态工具打基础。

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
- [√] bootloader 内核加载上限提升到 1024 扇区（提交：`d2a2da0`）
- [√] Shell、VGA / GUI Terminal、基础输入
- [√] 基础网络栈雏形（ARP / IPv4 / ICMP / UDP / TCP）
- [√] `/bin/hello`、`/bin/fault`、`/bin/waittest`、`/bin/orphan`、`/bin/argtest`、`/bin/envtest`、`/bin/fstest`、`/bin/libctest`、`/bin/pwd`、`/bin/ls`、`/bin/cat`、`/bin/echo`、`/bin/mkdir`、`/bin/rm`、`/bin/rmdir`、`/bin/grep`、`/bin/wc` 基础用户程序
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
- [ ] 可选：将现有 shell 内置基础命令拆分为独立 `/bin/*` 用户态程序
  - [√] `/bin/ls`
  - [√] `/bin/cat`
  - [√] `/bin/pwd`
  - [√] `/bin/mkdir`
  - [√] `/bin/rm`
  - [√] `/bin/rmdir`
  - [√] `/bin/echo`
  - [√] `/bin/grep`
  - [√] `/bin/wc`

---

## P1：内核核心能力完善

### 4. 内存管理

- [ ] 真正的进程独立地址空间
- [ ] 重新设计稳定的 CR3 切换方案
- [ ] 用户态 / 内核态完整内存隔离
- [ ] `mmap` / `munmap`
- [ ] `brk` / `sbrk`
- [ ] demand paging
- [ ] copy-on-write
- [ ] page fault 完整处理
- [ ] 用户栈 guard page
- [ ] 用户指针安全访问检查
- [ ] 进程退出时完整释放用户内存映射

### 5. 调度与同步

- [ ] waitpid 阻塞等待，避免忙等 `sched_yield`
- [ ] 完善进程 `BLOCKED` / `SLEEPING` 状态语义
- [ ] 子进程 exit 唤醒父进程
- [ ] 多线程用户态 API
- [ ] mutex
- [ ] semaphore
- [ ] condition variable
- [ ] futex 或类似轻量同步机制
- [ ] priority / nice
- [ ] 更完整的调度策略

### 6. 进程控制与信号

- [ ] init 进程模型
- [ ] `fork` 稳定化
- [ ] `exec` 完整替换当前进程镜像
- [ ] `kill`
- [ ] signal 机制
- [ ] alarm / timer signal
- [ ] 作业控制基础

---

## P2：文件系统与存储

### 7. VFS 完整语义

- [ ] `vfs_link`
- [ ] `vfs_symlink`
- [ ] `vfs_readlink`
- [ ] hard link
- [ ] symbolic link
- [ ] inode uid / gid 字段
- [ ] chmod / chown 权限模型
- [ ] access 权限检查
- [ ] per-process cwd 更严格集成
- [√] 文件描述符表标准化
- [√] `dup` / `dup2`
- [ ] pipe
- [ ] `select` / `poll`

### 8. 持久化存储

- [ ] 磁盘持久化文件系统
- [ ] FAT32
- [ ] EXT4 读写支持
- [ ] 文件缓存 / page cache
- [ ] `fsync`
- [ ] MBR / GPT 分区表
- [ ] 块设备缓存层

---

## P3：设备驱动

### 9. 总线与基础硬件

- [ ] PCI 总线扫描
- [ ] ACPI
- [ ] APIC / IOAPIC
- [ ] RTC 时钟
- [ ] 电源管理
- [ ] 热插拔支持

### 10. 存储驱动

- [ ] IDE / ATA
- [ ] AHCI / SATA
- [ ] virtio-blk

### 11. 网络驱动

- [ ] virtio-net
- [ ] e1000
- [ ] rtl8139

### 12. 输入与多媒体

- [ ] PS/2 键盘鼠标完整支持
- [ ] USB 通用栈
- [ ] 声卡驱动

---

## P4：网络与 IPC

### 13. 网络协议栈

- [ ] 真实网卡接入协议栈
- [ ] DHCP
- [ ] DNS
- [ ] socket syscall
- [ ] `bind`
- [ ] `listen`
- [ ] `accept`
- [ ] `connect`
- [ ] `send`
- [ ] `recv`
- [ ] TCP 完整状态机
- [ ] TCP 重传
- [ ] TCP 拥塞控制
- [ ] TCP 窗口管理
- [ ] UDP 用户态接口
- [ ] ping / ifconfig / netstat 等工具
- [ ] 网络配置管理
- [ ] 防火墙 / 权限控制

### 14. IPC

- [ ] pipe
- [ ] message queue
- [ ] shared memory
- [ ] eventfd 类机制
- [ ] socketpair
- [ ] 用户态服务进程通信模型
- [ ] 微内核式服务消息机制

---

## P5：Shell、用户态生态与 libc

### 15. Shell 能力

- [ ] 用户态 shell
- [√] 管道 `|`（已支持多级管道）
- [√] 重定向 `>` / `<` / `>>`
  - [√] 基础 `<` / `>` / `2>`
  - [√] 追加 `>>` / `2>>`
  - [√] shell 内置命令输出支持 fd 重定向
- [√] 环境变量（shell 内置 `env` / `export` / `unset`，外部程序继承 envp）
- [√] `PATH` 查找（未带 `/` 的外部命令自动尝试 `/bin/<cmd>`）
- [√] 后台任务 `&`（行尾 `&` 后不等待，支持普通外部命令、`exec` 和 pipeline 后台执行）
- [√] 后台任务状态管理 `jobs` / `fg`（支持查看后台 job、按 `%N` 或默认最近 job 拉回前台）
- [√] `Ctrl+C` / `Ctrl+D`
- [√] 命令补全（Tab 补全内置命令和 `/bin` 外部命令，支持唯一候选补全与多候选列出）
- [√] 脚本执行（支持 `source <file>` / `. <file>` / `sh <file>`，逐行复用现有 shell 管道、重定向、后台任务与内置命令逻辑）

### 16. 用户态运行库

- [√] 标准 libc 子集（header-only：`memset/memcpy/memmove/memcmp/strlen/strcmp/strncmp/strchr/strrchr/strstr/isdigit/isspace/atoi/itoa` 等）
- [√] 基础输出辅助（`putchar` / `puts` / 最小 `printf`，支持 `%s` / `%c` / `%d` / `%i` / `%x` / `%%`）
- [√] 更多用户态测试程序（新增 `/bin/libctest` 覆盖 libc 子集）
- [√] crt0 启动入口完善（新增 `src/user/crt0.c`，支持标准 `main(argc, argv, envp)`，并新增 `/bin/maintest` 回归）
- [ ] syscall wrapper 标准化
- [ ] malloc/free 用户态实现
- [ ] errno
- [ ] stdio 基础能力

---

## P6：GUI / 桌面系统

### 17. 图形系统

- [ ] 窗口管理器
- [ ] 多窗口
- [ ] 控件系统
- [ ] 鼠标事件分发
- [ ] 键盘焦点
- [ ] GUI 应用模型
- [ ] 双缓冲 / 合成器
- [ ] 图形加速
- [ ] 图片解码
- [ ] 中文字体 / Unicode 渲染
- [ ] 桌面环境
- [ ] 应用启动器

---

## P7：安全与权限

### 18. 安全模型

- [ ] 用户 / 组
- [ ] uid / gid
- [ ] 文件权限检查
- [ ] 进程权限
- [ ] capability
- [ ] syscall 权限控制
- [ ] 沙箱
- [ ] 内核地址保护
- [ ] ASLR
- [ ] NX / W^X
- [ ] 安全审计

---

## P8：AI 与跨端能力

### 19. AI 子系统

- [ ] 本地推理引擎
- [ ] 模型加载与执行
- [ ] tokenizer
- [ ] tensor runtime
- [ ] GPU / NPU 支持
- [ ] 云端 AI 接入
- [ ] 自然语言 Shell
- [ ] AI Agent 系统服务
- [ ] 模型签名完整验证链路
- [ ] 用户态 AI API

### 20. 跨端协同

- [ ] 真实网络发现协议
- [ ] 设备认证
- [ ] 端到端加密
- [ ] 文件同步
- [ ] 剪贴板同步
- [ ] 消息同步
- [ ] 任务流转
- [ ] 多设备账号体系

---

## P9：架构、平台与工程化

### 21. 平台架构

#### 21.1 x86_64 支持

- [ ] 保留当前 i386 稳定基线，新增 `ARCH=i386/x86_64` 或 `./build.sh i386|x86_64` 构建入口
- [ ] 新增 `src/arch/x86_64/` 架构目录，逐步拆分 i386 与 x86_64 架构相关代码
- [ ] 新增 x86_64 linker script
- [ ] 新增 x86_64 启动骨架，第一阶段只进入 `kernel_main64()` 并输出日志
- [ ] 从 BIOS 启动路径进入 long mode
  - [ ] 16 位实模式启动
  - [ ] 进入 32 位保护模式
  - [ ] 建立 PML4 / PDPT / PD / PT
  - [ ] 开启 PAE
  - [ ] 设置 `EFER.LME`
  - [ ] 开启分页并 far jump 到 64 位代码段
- [ ] 评估是否引入 Limine / BOOTBOOT / Multiboot2 等现代 bootloader，降低 UEFI 和 long mode 启动复杂度
- [ ] 实现 64 位 GDT
- [ ] 实现 64 位 TSS 与 `rsp0` / IST
- [ ] 实现 64 位 IDT 和异常入口
- [ ] 移植串口 / VGA / framebuffer 早期输出到 x86_64
- [ ] 移植 PMM 到 x86_64
- [ ] 实现 4 级分页 VMM
- [ ] 移植内核堆分配器到 x86_64
- [ ] 将地址、指针、栈、ELF entry 等字段从 `uint32_t` 整理为 `uintptr_t` / `size_t` / `uint64_t`
- [ ] 编译参数支持 x86_64 内核
  - [ ] `-m64`
  - [ ] `-ffreestanding`
  - [ ] `-fno-stack-protector`
  - [ ] `-fno-pic` / `-fno-pie`
  - [ ] `-mno-red-zone`
  - [ ] `-mcmodel=kernel`
- [ ] 移植调度器上下文切换到 `rsp/rip/rflags` 和 `r8-r15`
- [ ] 将 `kernel_esp` 等 32 位字段迁移或抽象为架构相关的 `kernel_sp`
- [ ] 第一阶段继续支持 `int 0x80` syscall
- [ ] 后续实现 x86_64 `syscall/sysret`
- [ ] 支持 ELF64 loader
- [ ] 支持 64 位用户态 `iretq` 返回
- [ ] 支持 64 位用户态 syscall wrapper / crt0
- [ ] 支持 64 位用户程序 `/bin/hello64` 回归测试
- [ ] 后续评估兼容 32 位用户程序

#### 21.2 其他平台与启动能力

- [ ] UEFI 启动
- [ ] ARM 移植
- [ ] RISC-V 移植
- [ ] SMP 多核支持
- [ ] ACPI / APIC / IOAPIC 支持
- [ ] 更完整的 bootloader

### 22. 构建与测试

- [ ] CMake / Ninja 构建系统
- [ ] CI 自动构建
- [ ] QEMU 自动回归测试
- [ ] 单元测试框架
- [ ] 内核 panic 日志标准化
- [ ] 崩溃 dump
- [ ] GDB 调试脚本
- [ ] 发布打包流程
- [ ] 版本号 / release 管理
