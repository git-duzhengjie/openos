#!/bin/bash
# openos Phase 2 Build Script (WSL native tools)

set -e
cd /mnt/e/openos

echo "===== Building openos Phase 2 ====="

BUILD=target
SRC=src/kernel

mkdir -p $BUILD

echo "[1/5] Assembling boot.asm..."
nasm -f bin $SRC/../boot/boot.asm -o $BUILD/boot.bin

echo "[2/5] Assembling kernel asm files..."
nasm -f elf32 $SRC/entry.asm -o $BUILD/entry.o
nasm -f elf32 $SRC/isr.asm -o $BUILD/isr.o
nasm -f elf32 $SRC/gdt_flush.asm -o $BUILD/gdt_flush.o
nasm -f elf32 $SRC/sched/context_switch.asm -o $BUILD/context_switch.o
nasm -f elf32 $SRC/timer_isr.asm -o $BUILD/timer_isr.o

echo "[3/5] Compiling kernel C files..."
gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/kernel.c -o $BUILD/kernel.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/idt.c -o $BUILD/idt.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/gdt.c -o $BUILD/gdt.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/mm/pmm.c -o $BUILD/pmm.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/mm/vmm.c -o $BUILD/vmm.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/mm/heap.c -o $BUILD/heap.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/sched/scheduler.c -o $BUILD/scheduler.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/ipc/syscall.c -o $BUILD/syscall.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/serial.c -o $BUILD/serial.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/vga.c -o $BUILD/vga.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic \
    -I $SRC/include \
    -c $SRC/string.c -o $BUILD/string.o

echo "[4/5] Linking kernel.elf..."
ld -m elf_i386 -T $SRC/linker.ld \
    -o $BUILD/kernel.elf \
    $BUILD/entry.o \
    $BUILD/isr.o \
    $BUILD/gdt_flush.o \
    $BUILD/context_switch.o \
    $BUILD/timer_isr.o \
    $BUILD/kernel.o \
    $BUILD/idt.o \
    $BUILD/gdt.o \
    $BUILD/pmm.o \
    $BUILD/vmm.o \
    $BUILD/heap.o \
    $BUILD/scheduler.o \
    $BUILD/syscall.o \
    $BUILD/serial.o \
    $BUILD/vga.o \
    $BUILD/string.o

objcopy -O binary $BUILD/kernel.elf $BUILD/kernel.bin

echo "[5/5] Generating disk image..."
dd if=/dev/zero of=$BUILD/openos.img bs=512 count=2880 2>/dev/null
dd if=$BUILD/boot.bin of=$BUILD/openos.img bs=512 count=1 conv=notrunc 2>/dev/null
dd if=$BUILD/kernel.bin of=$BUILD/openos.img bs=512 seek=1 conv=notrunc 2>/dev/null

echo ""
echo "========================================="
echo "  openos Phase 2 Build Successful!"
echo "  Output: target/openos.img"
echo "========================================="