# OpenOS aarch64 bootstrap skeleton

This directory contains the minimal aarch64 bootstrap-side adapter used by the
PC/Mobile architecture roadmap.

Current scope:

- `boot/boot.S`: QEMU virt raw-kernel entry. It preserves the DTB base from `x0`,
  switches into an EL1 execution context when necessary, clears `.bss`, installs
  an early stack and jumps into `aarch64_kernel_main`;
- `boot/exception_vectors.S`: EL1 exception vector table with 16 architectural
  slots and a common C dispatch path;
- `src/aarch64_kernel.c`: early kernel entry, PL011 boot log and exception-vector
  installation;
- `src/aarch64_uart.c`: QEMU virt PL011 UART early console at `0x09000000`;
- `src/aarch64_exception.c`: synchronous exception / IRQ / panic diagnostic
  scaffold;
- `linker_aarch64.ld`: early physical layout for the QEMU virt bring-up image,
  starting at `0x40200000`. QEMU `virt` RAM begins at `0x40000000`, and this
  base leaves room for firmware/DTB scratch space while keeping the kernel
  directly executable with `-kernel`;
- boot arguments are kept ready for conversion into the architecture-neutral
  `openos_bootinfo_t` structure;
- Device Tree, initrd and cmdline handoff fields are reserved for the future
  mobile boot path;
- the code remains freestanding and independent from the current i386/x86_64
  kernel entry paths.

Build:

```sh
./build.sh aarch64
```

`build.sh aarch64` selects a toolchain in this order:

1. explicit `AARCH64_CC` / `AARCH64_LD` / `AARCH64_OBJCOPY` environment values;
2. GNU cross tools: `aarch64-linux-gnu-gcc`, `aarch64-linux-gnu-ld`,
   `aarch64-linux-gnu-objcopy`;
3. LLVM cross tools: `clang --target=aarch64-none-elf`, `ld.lld`,
   `llvm-objcopy`.

If none are installed it fails with a clear diagnostic. During this checkpoint,
real cross-build validation passed by using a locally unpacked GNU aarch64
toolchain and produced:

- `target/aarch64/openos-aarch64.elf`
- `target/aarch64/openos-aarch64.bin`

QEMU smoke boot is supported when `qemu-system-aarch64` is installed. The image
is linked at `0x40200000`, inside QEMU `virt` RAM, so `-kernel` can jump into the
raw aarch64 entry point directly.

Smoke run after installing QEMU:

```sh
qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -kernel target/aarch64/openos-aarch64.elf
```

The full aarch64 MMU setup, scheduler port and userspace path are tracked by the
later aarch64 Mobile mainline tasks.
