#!/bin/bash
# openos Phase 2 Build Script (WSL native tools)

set -e
if [ -d /mnt/e/openos ]; then
    cd /mnt/e/openos
else
    cd /e/openos
fi

echo "===== Building openos Phase 2 ====="

BUILD=target
SRC=src/kernel

if [ "${1:-}" = "clean" ]; then
    echo "Cleaning build artifacts..."
    rm -rf "$BUILD"
    exit 0
fi

mkdir -p $BUILD

echo "[1/5] Assembling boot.asm..."
nasm -f bin $SRC/../boot/boot.asm -o $BUILD/boot.bin

echo "[2/5] Assembling kernel asm files..."
nasm -f elf32 $SRC/entry.asm -o $BUILD/entry.o
nasm -f elf32 $SRC/isr.asm -o $BUILD/isr.o
nasm -f elf32 $SRC/gdt_flush.asm -o $BUILD/gdt_flush.o
nasm -f elf32 $SRC/sched/context_switch.asm -o $BUILD/context_switch.o
nasm -f elf32 $SRC/timer_isr.asm -o $BUILD/timer_isr.o
nasm -f elf32 $SRC/switch_to_user.asm -o $BUILD/switch_to_user.o

# 编译用户程序并嵌入内�?
echo "[2.5] Building user program..."
USR=src/user
if [ -f $USR/hello.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/hello.c -o $BUILD/hello.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/hello.elf $BUILD/hello.o
    python3 _embed_elf.py $BUILD/hello.elf $SRC/include/embed_hello.h hello_elf
    echo "  Embedded: hello.elf"
fi

if [ -f $USR/fault.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/fault.c -o $BUILD/fault.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/fault.elf $BUILD/fault.o
    python3 _embed_elf.py $BUILD/fault.elf $SRC/include/embed_fault.h fault_elf
    echo "  Embedded: fault.elf"
fi

if [ -f $USR/waittest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/waittest.c -o $BUILD/waittest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/waittest.elf $BUILD/waittest.o
    python3 _embed_elf.py $BUILD/waittest.elf $SRC/include/embed_waittest.h waittest_elf
    echo "  Embedded: waittest.elf"
fi

if [ -f $USR/exit42.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/exit42.c -o $BUILD/exit42.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/exit42.elf $BUILD/exit42.o
    python3 _embed_elf.py $BUILD/exit42.elf $SRC/include/embed_exit42.h exit42_elf
    echo "  Embedded: exit42.elf"
fi

if [ -f $USR/orphan.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/orphan.c -o $BUILD/orphan.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/orphan.elf $BUILD/orphan.o
    python3 _embed_elf.py $BUILD/orphan.elf $SRC/include/embed_orphan.h orphan_elf
    echo "  Embedded: orphan.elf"
fi

if [ -f $USR/envtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/envtest.c -o $BUILD/envtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/envtest.elf $BUILD/envtest.o
    python3 _embed_elf.py $BUILD/envtest.elf $SRC/include/embed_envtest.h envtest_elf
    echo "  Embedded: envtest.elf"
fi

if [ -f $USR/argtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/argtest.c -o $BUILD/argtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/argtest.elf $BUILD/argtest.o
    python3 _embed_elf.py $BUILD/argtest.elf $SRC/include/embed_argtest.h argtest_elf
    echo "  Embedded: argtest.elf"
fi

if [ -f $USR/libctest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/libctest.c -o $BUILD/libctest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/libctest.elf $BUILD/libctest.o
    python3 _embed_elf.py $BUILD/libctest.elf $SRC/include/embed_libctest.h libctest_elf
    echo "  Embedded: libctest.elf"
fi

if [ -f $USR/maintest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/crt0.c -o $BUILD/crt0.o
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/maintest.c -o $BUILD/maintest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/maintest.elf $BUILD/crt0.o $BUILD/maintest.o
    python3 _embed_elf.py $BUILD/maintest.elf $SRC/include/embed_maintest.h maintest_elf
    echo "  Embedded: maintest.elf"
fi

if [ -f $USR/systest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/crt0.c -o $BUILD/crt0.o
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/systest.c -o $BUILD/systest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/systest.elf $BUILD/crt0.o $BUILD/systest.o
    python3 _embed_elf.py $BUILD/systest.elf $SRC/include/embed_systest.h systest_elf
    echo "  Embedded: systest.elf"
fi

if [ -f $USR/malloctest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/crt0.c -o $BUILD/crt0.o
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/malloctest.c -o $BUILD/malloctest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/malloctest.elf $BUILD/crt0.o $BUILD/malloctest.o
    python3 _embed_elf.py $BUILD/malloctest.elf $SRC/include/embed_malloctest.h malloctest_elf
    echo "  Embedded: malloctest.elf"
fi

if [ -f $USR/errnotest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/crt0.c -o $BUILD/crt0.o
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/errnotest.c -o $BUILD/errnotest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/errnotest.elf $BUILD/crt0.o $BUILD/errnotest.o
    python3 _embed_elf.py $BUILD/errnotest.elf $SRC/include/embed_errnotest.h errnotest_elf
    echo "  Embedded: errnotest.elf"
fi

if [ -f $USR/stdiotest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/crt0.c -o $BUILD/crt0.o
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/stdiotest.c -o $BUILD/stdiotest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/stdiotest.elf $BUILD/crt0.o $BUILD/stdiotest.o
    python3 _embed_elf.py $BUILD/stdiotest.elf $SRC/include/embed_stdiotest.h stdiotest_elf
    echo "  Embedded: stdiotest.elf"
fi

if [ -f $USR/fstest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/fstest.c -o $BUILD/fstest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/fstest.elf $BUILD/fstest.o
    python3 _embed_elf.py $BUILD/fstest.elf $SRC/include/embed_fstest.h fstest_elf
    echo "  Embedded: fstest.elf"
fi

if [ -f $USR/pwd.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/pwd.c -o $BUILD/pwd.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/pwd.elf $BUILD/pwd.o
    python3 _embed_elf.py $BUILD/pwd.elf $SRC/include/embed_pwd.h pwd_elf
    echo "  Embedded: pwd.elf"
fi


if [ -f $USR/ls.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/ls.c -o $BUILD/ls.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/ls.elf $BUILD/ls.o
    python3 _embed_elf.py $BUILD/ls.elf $SRC/include/embed_ls.h ls_elf
    echo "  Embedded: ls.elf"
fi

if [ -f $USR/cat.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/cat.c -o $BUILD/cat.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/cat.elf $BUILD/cat.o
    python3 _embed_elf.py $BUILD/cat.elf $SRC/include/embed_cat.h cat_elf
    echo "  Embedded: cat.elf"
fi

if [ -f $USR/echo.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/echo.c -o $BUILD/echo.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/echo.elf $BUILD/echo.o
    python3 _embed_elf.py $BUILD/echo.elf $SRC/include/embed_echo.h echo_elf
    echo "  Embedded: echo.elf"
fi

if [ -f $USR/grep.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/grep.c -o $BUILD/grep.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/grep.elf $BUILD/grep.o
    python3 _embed_elf.py $BUILD/grep.elf $SRC/include/embed_grep.h grep_elf
    echo "  Embedded: grep.elf"
fi

if [ -f $USR/wc.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/wc.c -o $BUILD/wc.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/wc.elf $BUILD/wc.o
    python3 _embed_elf.py $BUILD/wc.elf $SRC/include/embed_wc.h wc_elf
    echo "  Embedded: wc.elf"
fi

if [ -f $USR/mkdir.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/mkdir.c -o $BUILD/mkdir.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/mkdir.elf $BUILD/mkdir.o
    python3 _embed_elf.py $BUILD/mkdir.elf $SRC/include/embed_mkdir.h mkdir_elf
    echo "  Embedded: mkdir.elf"
fi

if [ -f $USR/rm.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/rm.c -o $BUILD/rm.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/rm.elf $BUILD/rm.o
    python3 _embed_elf.py $BUILD/rm.elf $SRC/include/embed_rm.h rm_elf
    echo "  Embedded: rm.elf"
fi

if [ -f $USR/touch.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/touch.c -o $BUILD/touch.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/touch.elf $BUILD/touch.o
    python3 _embed_elf.py $BUILD/touch.elf $SRC/include/embed_touch.h touch_elf
    echo "  Embedded: touch.elf"
fi

if [ -f $USR/rmdir.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/rmdir.c -o $BUILD/rmdir.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/rmdir.elf $BUILD/rmdir.o
    python3 _embed_elf.py $BUILD/rmdir.elf $SRC/include/embed_rmdir.h rmdir_elf
    echo "  Embedded: rmdir.elf"
fi

echo "[3/5] Compiling kernel C files..."
gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/kernel.c -o $BUILD/kernel.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/idt.c -o $BUILD/idt.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/gdt.c -o $BUILD/gdt.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/mm/pmm.c -o $BUILD/pmm.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/mm/vmm.c -o $BUILD/vmm.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/mm/heap.c -o $BUILD/heap.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/sched/scheduler.c -o $BUILD/scheduler.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/ipc/syscall.c -o $BUILD/syscall.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/serial.c -o $BUILD/serial.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/keyboard.c -o $BUILD/keyboard.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/mouse.c -o $BUILD/mouse.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/usb_tablet.c -o $BUILD/usb_tablet.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/chardev.c -o $BUILD/chardev.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/blockdev.c -o $BUILD/blockdev.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/input_buffer.c -o $BUILD/input_buffer.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/vga.c -o $BUILD/vga.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/framebuffer.c -o $BUILD/framebuffer.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/gui.c -o $BUILD/gui.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/font.c -o $BUILD/font.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/string.c -o $BUILD/string.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/usermode.c -o $BUILD/usermode.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/usermem.c -o $BUILD/usermem.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/fs -I $SRC/proc \
    -c $SRC/proc/process.c -o $BUILD/process.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/proc -I $SRC/fs \
    -c $SRC/proc/elf_loader.c -o $BUILD/elf_loader.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/fs \
    -c $SRC/fs/vfs.c -o $BUILD/vfs.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/fs/ramfs.c -o $BUILD/ramfs.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/fs \
    -c $SRC/fs/tmpfs.c -o $BUILD/tmpfs.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/fs \
    -c $SRC/fs/ext4.c -o $BUILD/ext4.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/net \
    -c $SRC/net/net.c -o $BUILD/net.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/net \
    -c $SRC/net/discovery.c -o $BUILD/discovery.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/net \
    -c $SRC/net/sync.c -o $BUILD/sync.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/net \
    -c $SRC/net/bus.c -o $BUILD/bus.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/devmgr.c -o $BUILD/devmgr.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/ai/ai.c -o $BUILD/ai.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/shell.c -o $BUILD/shell.o

echo "[4/5] Linking kernel.elf..."
ld -m elf_i386 -T $SRC/linker.ld \
    -o $BUILD/kernel.elf \
    $BUILD/entry.o \
    $BUILD/isr.o \
    $BUILD/gdt_flush.o \
    $BUILD/context_switch.o \
    $BUILD/timer_isr.o \
    $BUILD/switch_to_user.o \
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
    $BUILD/framebuffer.o \
    $BUILD/gui.o \
    $BUILD/font.o \
    $BUILD/string.o \
    $BUILD/keyboard.o \
    $BUILD/mouse.o \
    $BUILD/usb_tablet.o \
    $BUILD/chardev.o \
    $BUILD/blockdev.o \
    $BUILD/input_buffer.o \
    $BUILD/usermode.o \
    $BUILD/usermem.o \
    $BUILD/process.o \
    $BUILD/elf_loader.o \
    $BUILD/vfs.o \
    $BUILD/ramfs.o \
    $BUILD/tmpfs.o \
    $BUILD/ext4.o \
    $BUILD/net.o \
    $BUILD/discovery.o \
    $BUILD/sync.o \
    $BUILD/bus.o \
    $BUILD/devmgr.o \
    $BUILD/ai.o \
    $BUILD/shell.o

objcopy -O binary $BUILD/kernel.elf $BUILD/kernel.bin

KERNEL_BYTES=$(stat -c%s "$BUILD/kernel.bin")
KERNEL_SECTORS=$(( (KERNEL_BYTES + 511) / 512 ))
BOOT_LOAD_SECTORS=1024
if [ "$KERNEL_SECTORS" -gt "$BOOT_LOAD_SECTORS" ]; then
    echo "ERROR: kernel.bin is ${KERNEL_SECTORS} sectors, but bootloader loads only ${BOOT_LOAD_SECTORS} sectors."
    echo "Increase bootloader kernel load chunks before building the image."
    exit 1
fi
echo "  kernel.bin: ${KERNEL_BYTES} bytes (${KERNEL_SECTORS}/${BOOT_LOAD_SECTORS} sectors loaded by bootloader)"

echo "[5/5] Generating disk image..."
dd if=/dev/zero of=$BUILD/openos.img bs=512 count=2880 2>/dev/null
dd if=$BUILD/boot.bin of=$BUILD/openos.img bs=512 count=1 conv=notrunc 2>/dev/null
dd if=$BUILD/kernel.bin of=$BUILD/openos.img bs=512 seek=1 conv=notrunc 2>/dev/null

echo ""
echo "========================================="
echo "  openos Phase 3 Build Successful!"
echo "  Output: target/openos.img"
echo "========================================="
