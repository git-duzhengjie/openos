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

## UEFI 镜像构建完成（已验证）
- [x] `target/openos-uefi.img` UEFI GPT 磁盘镜像已生成（32MB ESP 分区）
- [x] BOOTX64.EFI 已正确放置于 ESP:/EFI/BOOT/ 目录
- [x] kernel64.elf 已嵌入 ESP 分区根目录
- [x] UEFI loader 完整实现：串口日志、ELF64 解析、帧缓冲设置、ExitBootServices、内核跳转

## 待运行时验证（需要 QEMU 日志证明）
- [ ] kernel64 实际启动并初始化 GDT/IDT/TSS/PMM/VMM
- [ ] hello64.elf 运行时加载成功
- [ ] 实际进入 ring3
- [ ] hello64 通过 syscall 输出可见
