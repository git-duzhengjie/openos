# OpenOS x86_64 架构目录

该目录用于承载 OpenOS 的 x86_64 架构相关代码，保持当前 i386 稳定基线不受影响。

第一阶段拆分边界：

- `include/`：x86_64 架构公共声明、地址宽度、入口原型。
- `kernel/`：x86_64 早期内核入口、GDT、TSS、IDT、异常入口、早期输出、调度上下文、PMM、VMM、内核堆、ELF64 loader 与用户态 `iretq` 返回骨架。
- `user/`：x86_64 用户态最小 `crt0`、`syscall` 指令 wrapper、链接脚本与 hello64 验证程序。
- `compat32`：评估长期模式下运行 32 位用户程序所需的 compat GDT、`int 0x80` 入口、ELF32 loader、用户栈和指针 thunk 条件。
- `boot/`：x86_64 BIOS long mode 启动骨架与 UEFI `BOOTX64.EFI` 骨架，包含实模式、保护模式、PAE/PML4、EFER.LME、分页和 64 位串口日志。
- 后续将继续增加完整 stage2 loader、通用 64 位类型适配等模块。

当前状态：目录骨架已建立，已支持 64 位 GDT、TSS、`rsp0`、IST 栈、IDT、32 个异常入口、`int 0x80` syscall 兼容入口、`syscall/sysret` MSR 配置与 LSTAR 入口、串口/VGA 早期控制台、framebuffer 描述/像素输出接口、64 位 `rsp/rip/rflags/r8-r15` 调度上下文、64 位 PMM、4 级分页 VMM、内核堆分配器、ELF64 loader、用户态 `iretq` 返回 frame、x86_64 用户态 `crt0/syscall` wrapper、`/bin/hello64` 回归 ELF 构建、32 位用户程序兼容性评估骨架与 UEFI `BOOTX64.EFI` 启动骨架，默认 i386 构建仍保持稳定。

推荐验证方式：

```bash
ARCH=x86_64 bash build.sh
# 输出 target/x86_64/kernel64.elf、target/x86_64/bin/hello64.elf 与 target/x86_64/boot/BOOTX64.EFI
```

手动验证 64 位骨架时应使用内核代码模型和禁用 red-zone，例如：

```bash
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/kernel64.c -o /tmp/openos_kernel64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/gdt64.c -o /tmp/openos_gdt64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/tss64.c -o /tmp/openos_tss64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/idt64.c -o /tmp/openos_idt64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/early_console64.c -o /tmp/openos_early_console64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/pmm64.c -o /tmp/openos_pmm64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/vmm64.c -o /tmp/openos_vmm64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/heap64.c -o /tmp/openos_heap64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/elf64_loader.c -o /tmp/openos_elf64_loader.o
gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib \
    -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE \
    -fno-stack-protector -fno-builtin \
    -c src/arch/x86_64/kernel/usermode64.c -o /tmp/openos_usermode64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -fno-pic -fno-pie -fno-PIE \
    -c src/arch/x86_64/kernel/isr64.S -o /tmp/openos_isr64.o
gcc -m64 -mcmodel=kernel -mno-red-zone -fno-pic -fno-pie -fno-PIE \
    -c src/arch/x86_64/kernel/entry64.S -o /tmp/openos_entry64.o
ld -m elf_x86_64 -T src/arch/x86_64/linker64.ld -nostdlib \
    -o /tmp/openos_kernel64.elf /tmp/openos_entry64.o \
    /tmp/openos_kernel64.o /tmp/openos_gdt64.o /tmp/openos_tss64.o \
    /tmp/openos_idt64.o /tmp/openos_early_console64.o /tmp/openos_pmm64.o \
    /tmp/openos_vmm64.o /tmp/openos_heap64.o /tmp/openos_elf64_loader.o \
    /tmp/openos_usermode64.o /tmp/openos_usermode64_asm.o /tmp/openos_isr64.o
```
