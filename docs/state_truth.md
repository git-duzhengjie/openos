# OPENOS 真值文档（state truth）

> 目的：本文档是 commit / 阶段进度的**唯一可信来源**。每轮对话开工前
> 先校对 `git log --oneline -20` 与本文档，**严禁凭记忆/期望**编写进度
> 摘要。任何"已完成"声明必须有 commit hash 对应。

## 一、真实 commit 链顶端（截至 2026-06-29 晚）

通过 `git log --oneline -10` 校对得到的真实栈顶：

```
7e66784 A2.P3-B-alpha: vfork-style fork skeleton (kernel-side, deferred user validation)
fcbf598 A2.P0: harden SYS_EXIT-via-syscall contract (kill dead cli;hlt path)
b5afca1 A2.P1: PCB-ize cooperative ring3 longjmp core (3 statics -> PCB)
b3706b6 docs(state_truth)
fa1f986 H.5b.3.A as-clone + selftest
72b9220 H.5b.2.A elf-into-as
eee4c36 H.5b.2.B baseline 高半区切 CR3
ff594ea H.5b.1 docs
```

**真实 HEAD = `7e66784`**。

## 二、虚构 commit 清单（订正）

上一轮对话摘要中宣称的以下 commit **不存在**，是凭记忆/期望捏造：

| 虚构 hash   | 虚构说明                                | 状态     |
| ----------- | --------------------------------------- | -------- |
| `c4ea102`   | "H.5b.2.B 善后：as_activate(NULL) 守卫" | ❌ 不存在 |
| `7a8d215`   | "H.5b.3.B sys_fork wire"                | ❌ 不存在 |
| `e3f209a`   | "H.5b.4 PML4[0] 去 U 收口"              | ❌ 不存在 |

教训：每轮开工前必须 `git log --oneline -20` + `git status --short`
校对真相，不得凭记忆写摘要。

## 三、H.5b 阶段真实进度

| 子步骤   | 名称                            | 状态                       | commit    |
| -------- | ------------------------------- | -------------------------- | --------- |
| H.5b.1   | 接线骨架                        | ✅ 完成                    | ff594ea   |
| H.5b.2.A | elf-into-as                     | ✅ 完成                    | 72b9220   |
| H.5b.2.B | 高半区切 CR3 baseline           | ✅ 完成                    | eee4c36   |
| H.5b.2.B 善后 | as_activate(NULL) 守卫    | ❌ **未做**（留待 A2.P4）  | —         |
| H.5b.3.A | as_clone + selftest             | ✅ 完成                    | fa1f986   |
| H.5b.3.B | sys_fork wire                   | ⚠️ kernel 侧 skeleton 完成，user 验收阻塞于 P3-C | `7e66784` |
| H.5b.3.C | COW                             | ⏸️ deferred 到 H.6          | —         |
| H.5b.4   | PML4[0] 去 U 收口               | ❌ **未做**（留待 A2.P4）  | —         |

## 四、A2 路线图（micro-scheduler + 真 fork + 真 wait）

为何要走 A2：当前 `usermode_run()` 是 inline-asm + 7 个 file-scope
static 单实例全局态，**架构性阻塞真 fork**。必须先把全局态下沉到 PCB
+ 外层 cooperative scheduler 才能让多个 ring3 进程并存。

| 步骤  | 名称                                              | 范围                                                                                                | 状态     |
| ----- | ------------------------------------------------- | --------------------------------------------------------------------------------------------------- | -------- |
| A2.P0 | SYS_EXIT-via-syscall 修复                         | `syscall64.c:130` 的 `cli;hlt` → `mark_exited + return_to_kernel`，统一两条 SYS_EXIT 路径           | ✅ `fcbf598` |
| A2.P1 | PCB 化第一波（longjmp 核心）                      | `prepared_user_frame` / `kernel_return_rsp` / `usermode_canary` 下沉到 PCB                          | ✅ `b5afca1` |
| A2.P2 | PCB 化第二波 + runqueue 主循环                    | 剩余 file-scope static 下沉 + kernel64.c:413 主循环改 runqueue 驱动                                 | 待做     |
| A2.P3-B-α | vfork 语义最小 fork（kernel 侧骨架）          | PCB.fork_pending + syscall wrapper 短路 SYS_FORK + asm helper resume_child + main loop 接线 + SYS_FORK=57 wire | ✅ `7e66784`（**user 验收 deferred**） |
| A2.P3-C | **VMM 高半区 PT 覆盖修复**（P3-B 用户验收阻塞）  | 内核高半区 (`0xffffffff80000000+`) PT 在 user AS 缺页：ELF size > ~0x4000 时 syscall 路径触发 reboot。修法二选一：boot 期 4 KiB-grain map 整个 kernel ELF 区段 / 改 syscall 切 CR3 | ❌ **待做（下一步主线）** |
| A2.P4 | as_activate(NULL) 守卫 + PML4[0] 去 U + selftest  | 关闭 H.5b 收口项                                                                                    | 待做     |

H.5c waitpid 留到 A2.P3 跑通真 fork 之后。

## 五、当前架构地形（必读）

### ring3 入口双路径并存

**路径 A**（主流程，A2 改造对象）：`usermode_run()` 在 `usermode64.c`，
inline-asm + label `1:` longjmp，**7 个 file-scope static**：

- `prepared_user_frame`
- `usermode_kernel_return_rsp`
- `usermode_running`
- `usermode_exited`
- `usermode_canary`
- `usermode_pending_exec` / `usermode_pending_exec_entry`
- `usermode_last_exit_code`

SYS_EXIT/SYS_EXECVE 路径已有 longjmp-back-to-kernel 雏形
（`mark_exited` + `return_to_kernel`），是 cooperative scheduler 雏形。

`kernel64.c:413` 主循环硬编码"跑 1 个到死 + execve 链式"。

**路径 B**（Stage 30 选项）：`sched_spawn_uthread()`，G.7e 引入，
PCB.ctx.rip = `user_thread_trampoline` + scheduler dispatch + swapgs +
iretq，但 SYS_EXIT 不知道怎么回 scheduler。**本轮 A2-coop 不走这条路。**

### 关键代码位置

```
src/arch/x86_64/kernel/
├── usermode64.c           ★ 7个全局态 + usermode_run()（A2.P1/P2 重构目标）
├── kernel64.c:413         ★ 主循环（A2.P2 改 runqueue 驱动）
├── syscall64.c:130        ★ SYS_EXIT-via-syscall 走 `cli;hlt`（A2.P0 修复目标）
├── syscall_dispatch64.c   ★ x86_64 表无 SYS_FORK=57（A2.P3 注册）
├── address_space64.c      ★ as_clone 已完成（H.5b.3.A）
└── proc64.{c,h}           ★ PCB 待加 saved_frame / kernel_return_rsp / canary 字段
```

## 六、铁律

1. **self-talk 必须 client 端过滤**：`[推理: ...]` / `工具调用:` /
   `思路:` / `[内部分析]` 等标签**严禁**进入任何 commit message /
   docs / 对话输出。
2. **每个 commit 独立可回滚**：每步附 selftest 零回归。
3. **开工前先 `git log` 校对真值**，不得凭记忆写状态。
4. **构建用 `wsl -d Ubuntu`**。
