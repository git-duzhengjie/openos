# M2 阶段进展：x86_64 用户态构建链路

## 已完成（静态可验证）
- [x] `ARCH=x86_64 ./build.sh` 构建成功
- [x] 生成 `target/x86_64/kernel64.elf`（ELF64，入口 `0xffffffff80000000`）
- [x] 生成 `target/x86_64/bin/hello64.elf`（ELF64，入口 `0x400000`）
- [x] hello64.elf 已嵌入 `src/arch/x86_64/include/embed_hello64.h`
- [x] kernel64 包含完整符号：
  - ELF64 加载：`arch_x86_64_elf64_load_image`
  - 用户态初始化：`arch_x86_64_usermode_init`
  - 用户态切换：`arch_x86_64_usermode_run` / `arch_x86_64_usermode_prepare_iretq`
  - syscall 框架：`arch_x86_64_usermode_syscall_return_trampoline`

## 待运行时验证（需要 QEMU 日志证明）
- [ ] kernel64 实际启动并初始化 GDT/IDT/TSS/PMM/VMM
- [ ] hello64.elf 运行时加载成功
- [ ] 实际进入 ring3
- [ ] hello64 通过 syscall 输出可见
