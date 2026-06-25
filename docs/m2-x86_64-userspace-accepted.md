# M2 验收：x86_64 用户态静态能力

## 已验收（构建 / 符号可证明）
- [x] `ARCH=x86_64 ./build.sh` 构建成功
- [x] 生成 `target/x86_64/kernel64.elf`（x86_64 ELF64，高地址 `0xffffffff80000000`）
- [x] 生成 `target/x86_64/bin/hello64.elf`（x86_64 ELF64，用户地址 `0x400000`）
- [x] hello64.elf 已嵌入 `src/arch/x86_64/include/embed_hello64.h`
- [x] kernel64 包含完整用户态框架符号：
  - ELF64 加载：`arch_x86_64_elf64_load_image`
  - 用户态初始化：`arch_x86_64_usermode_init`
  - 用户态切换：`arch_x86_64_usermode_run` / `arch_x86_64_usermode_prepare_iretq`
  - syscall 框架：`arch_x86_64_usermode_syscall_return_trampoline`
- [x] UEFI loader 增加 COM1 串口输出（`src/arch/x86_64/boot/uefi64.c`）

## 待运行时验证（需要 QEMU 日志证明）
- [ ] `target/openos.img` 尚未生成：x86_64 构建目前仅输出独立 ELF 文件，缺少 UEFI ESP 镜像组装步骤
- [ ] kernel64 实际启动并初始化 GDT/IDT/TSS/PMM/VMM
- [ ] hello64.elf 运行时加载成功
- [ ] 实际进入 ring3
- [ ] hello64 通过 syscall 输出可见
