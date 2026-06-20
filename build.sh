#!/bin/bash
# openos Phase 2 Build Script (WSL native tools)

set -e
if [ -d /mnt/e/openos ]; then
    cd /mnt/e/openos
else
    cd /e/openos
fi

BUILD=target
SRC=src/kernel
BUILD_ARCH="${ARCH:-i386}"
OPENOS_CJK_RESOURCE=${OPENOS_CJK_RESOURCE:-1}
OPENOS_CJK_RESOURCE_PATH=${OPENOS_CJK_RESOURCE_PATH:-$BUILD/cjk.ofnt}
# Default to a real Chinese coverage resource instead of the tiny UI subset.
# If the host lacks Pillow or a Chinese font, the build falls back to the
# checked-in UI subset unless OPENOS_CJK_STRICT=1 is set.
OPENOS_CJK_COVERAGE=${OPENOS_CJK_COVERAGE:-gb2312}
OPENOS_CJK_COMPRESS=${OPENOS_CJK_COMPRESS:-1}
OPENOS_CJK_FONT=${OPENOS_CJK_FONT:-}
OPENOS_CJK_STRICT=${OPENOS_CJK_STRICT:-0}
if [ -z "${OPENOS_CJK_EMBED+x}" ]; then
    OPENOS_CJK_EMBED=1
fi
if [ "$OPENOS_CJK_COVERAGE" != "ui" ] && [ "$OPENOS_CJK_EMBED" != "1" ] && [ "$OPENOS_CJK_RESOURCE_PATH" = "$BUILD/cjk.ofnt" ]; then
    OPENOS_CJK_RESOURCE_PATH=$BUILD/cjk-large.ofntz
fi

usage() {
    echo "Usage: ARCH=i386|x86_64 ./build.sh [clean|test]"
    echo "       ./build.sh [i386|x86_64] [clean|test]"
}

case "${1:-}" in
    test)
        exec bash tests/run_unit_tests.sh
        ;;
    i386|x86_64)
        BUILD_ARCH="$1"
        shift
        ;;
    ""|clean)
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        echo "Unsupported architecture or command: $1" >&2
        usage >&2
        exit 1
        ;;
esac

if [ "${1:-}" = "test" ]; then
    exec bash tests/run_unit_tests.sh
fi

case "$BUILD_ARCH" in
    i386|x86_64)
        ;;
    *)
        echo "Unsupported ARCH=$BUILD_ARCH" >&2
        usage >&2
        exit 1
        ;;
esac

if [ "${1:-}" = "clean" ]; then
    echo "Cleaning build artifacts for $BUILD_ARCH..."
    rm -rf "$BUILD"
    exit 0
fi

if [ -n "${1:-}" ]; then
    echo "Unsupported command: $1" >&2
    usage >&2
    exit 1
fi

if [ -x scripts/gen-version-header.sh ]; then
    OPENOS_VERSION_HEADER=$(bash scripts/gen-version-header.sh)
    echo "Version header: $OPENOS_VERSION_HEADER"
fi

if [ "$BUILD_ARCH" = "x86_64" ]; then
    echo "===== Building openos Phase 2 (x86_64 kernel + hello64 regression) ====="
    ARCH64_SRC=src/arch/x86_64
    ARCH64_BUILD="$BUILD/x86_64"
    ARCH64_USER_BUILD="$ARCH64_BUILD/user"
    ARCH64_BOOT_BUILD="$ARCH64_BUILD/boot"
    ARCH64_BIN_BUILD="$ARCH64_BUILD/bin"
    ARCH64_CFLAGS="-m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE -fno-stack-protector -fno-builtin -I$ARCH64_SRC/include"
    ARCH64_ASFLAGS="-m64 -mcmodel=kernel -mno-red-zone -fno-pic -fno-pie -fno-PIE -I$ARCH64_SRC/include"
    ARCH64_USER_CFLAGS="-m64 -ffreestanding -nostdlib -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE -fno-stack-protector -fno-builtin -I$ARCH64_SRC/user"
    ARCH64_USER_ASFLAGS="-m64 -fno-pic -fno-pie -fno-PIE -I$ARCH64_SRC/user"
    ARCH64_LDFLAGS="-m elf_x86_64 -T $ARCH64_SRC/linker64.ld -nostdlib"
    ARCH64_USER_LDFLAGS="-m elf_x86_64 -T $ARCH64_SRC/user/user64.ld -nostdlib"
    ARCH64_UEFI_CFLAGS="-m64 -ffreestanding -fshort-wchar -mno-red-zone -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE -fno-stack-protector -fno-builtin -I$ARCH64_SRC/include"
    ARCH64_UEFI_ASFLAGS="-m64 -fno-pic -fno-pie -fno-PIE"
    ARCH64_UEFI_LDFLAGS="-m elf_x86_64 -T $ARCH64_SRC/boot/uefi64.ld -nostdlib"

    mkdir -p "$ARCH64_BUILD" "$ARCH64_USER_BUILD" "$ARCH64_BOOT_BUILD" "$ARCH64_BIN_BUILD"
    rm -f "$ARCH64_BUILD"/*.o "$ARCH64_BUILD"/*.elf "$ARCH64_USER_BUILD"/*.o "$ARCH64_BOOT_BUILD"/*.o "$ARCH64_BOOT_BUILD"/*.elf "$ARCH64_BOOT_BUILD"/*.EFI "$ARCH64_BOOT_BUILD"/*.efi "$ARCH64_BIN_BUILD"/*.elf

    echo "[1/4] Compiling x86_64 C files..."
    for cfile in \
        kernel/kernel64.c \
        kernel/gdt64.c \
        kernel/tss64.c \
        kernel/idt64.c \
        kernel/sched64.c \
        kernel/syscall64.c \
        kernel/compat32.c \
        kernel/pmm64.c \
        kernel/vmm64.c \
        kernel/heap64.c \
        kernel/elf64_loader.c \
        kernel/usermode64.c \
        kernel/early_console64.c; do
        obj="$ARCH64_BUILD/$(basename "${cfile%.c}").o"
        gcc $ARCH64_CFLAGS -c "$ARCH64_SRC/$cfile" -o "$obj"
    done

    echo "[2/4] Assembling x86_64 entry files..."
    for sfile in \
        kernel/entry64.S \
        kernel/isr64.S \
        kernel/context_switch64.S \
        kernel/syscall_int80_compat64.S \
        kernel/syscall_sysret64.S \
        kernel/usermode64.S; do
        base="$(basename "${sfile%.S}")"
        if [ "$base" = "usermode64" ]; then
            obj="$ARCH64_BUILD/usermode64_asm.o"
        else
            obj="$ARCH64_BUILD/$base.o"
        fi
        gcc $ARCH64_ASFLAGS -c "$ARCH64_SRC/$sfile" -o "$obj"
    done

    echo "[3/4] Linking x86_64 kernel..."
    ld $ARCH64_LDFLAGS -o "$ARCH64_BUILD/kernel64.elf" \
        "$ARCH64_BUILD/entry64.o" \
        "$ARCH64_BUILD/kernel64.o" \
        "$ARCH64_BUILD/gdt64.o" \
        "$ARCH64_BUILD/tss64.o" \
        "$ARCH64_BUILD/idt64.o" \
        "$ARCH64_BUILD/isr64.o" \
        "$ARCH64_BUILD/sched64.o" \
        "$ARCH64_BUILD/context_switch64.o" \
        "$ARCH64_BUILD/syscall64.o" \
        "$ARCH64_BUILD/compat32.o" \
        "$ARCH64_BUILD/syscall_int80_compat64.o" \
        "$ARCH64_BUILD/syscall_sysret64.o" \
        "$ARCH64_BUILD/pmm64.o" \
        "$ARCH64_BUILD/vmm64.o" \
        "$ARCH64_BUILD/heap64.o" \
        "$ARCH64_BUILD/elf64_loader.o" \
        "$ARCH64_BUILD/usermode64.o" \
        "$ARCH64_BUILD/usermode64_asm.o" \
        "$ARCH64_BUILD/early_console64.o"

    echo "[4/4] Building x86_64 /bin/hello64 regression ELF..."
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/crt0.c" -o "$ARCH64_USER_BUILD/crt0.o"
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/hello64.c" -o "$ARCH64_USER_BUILD/hello64.o"
    gcc $ARCH64_USER_ASFLAGS -c "$ARCH64_SRC/user/crt0.S" -o "$ARCH64_USER_BUILD/start.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/hello64.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/hello64.o"
    readelf -h "$ARCH64_BIN_BUILD/hello64.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/hello64.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/hello64.elf" | grep -q ' openos64_main$'

    echo "[UEFI] Building x86_64 BOOTX64.EFI skeleton..."
    gcc $ARCH64_UEFI_CFLAGS -c "$ARCH64_SRC/boot/uefi64.c" -o "$ARCH64_BOOT_BUILD/uefi64.o"
    gcc $ARCH64_UEFI_ASFLAGS -c "$ARCH64_SRC/boot/uefi64_crt0.S" -o "$ARCH64_BOOT_BUILD/uefi64_crt0.o"
    ld $ARCH64_UEFI_LDFLAGS -o "$ARCH64_BOOT_BUILD/uefi64_loader.elf" \
        "$ARCH64_BOOT_BUILD/uefi64_crt0.o" \
        "$ARCH64_BOOT_BUILD/uefi64.o"
    objcopy -O pei-x86-64 --subsystem=10 "$ARCH64_BOOT_BUILD/uefi64_loader.elf" "$ARCH64_BOOT_BUILD/BOOTX64.EFI"
    objdump -f "$ARCH64_BOOT_BUILD/BOOTX64.EFI" | grep -q 'pei-x86-64'

    echo "[BIOS] Assembling x86_64 BIOS long-mode boot stub (boot64.asm)..."
    # 21.1 BIOS long-mode 自举骨架：16->32->64 切换链路。
    # 当前主启动路径走 GRUB/Multiboot2 + UEFI，本骨架仅做语法 / 字节布局回归，不接入磁盘镜像。
    nasm -f bin "$ARCH64_SRC/boot/boot64.asm" -o "$ARCH64_BOOT_BUILD/boot64.bin"
    BOOT64_BYTES=$(stat -c%s "$ARCH64_BOOT_BUILD/boot64.bin")
    if [ "$BOOT64_BYTES" -ne 512 ]; then
        echo "ERROR: boot64.bin must be 512 bytes (MBR), got $BOOT64_BYTES" >&2
        exit 1
    fi
    # 校验 0x55AA 启动签名
    BOOT64_SIG=$(xxd -s 510 -l 2 -p "$ARCH64_BOOT_BUILD/boot64.bin")
    if [ "$BOOT64_SIG" != "55aa" ]; then
        echo "ERROR: boot64.bin missing 0x55AA boot signature, got 0x$BOOT64_SIG" >&2
        exit 1
    fi
    echo "  boot64.bin: $BOOT64_BYTES bytes, signature 0x$BOOT64_SIG OK"

    echo "x86_64 Build Successful!"
    echo "Output: $ARCH64_BUILD/kernel64.elf"
    echo "Regression: $ARCH64_BIN_BUILD/hello64.elf"
    echo "UEFI: $ARCH64_BOOT_BUILD/BOOTX64.EFI"
    exit 0
fi

echo "===== Building openos Phase 2 (i386) ====="

mkdir -p $BUILD
rm -f $BUILD/*.elf

if [ "$OPENOS_CJK_RESOURCE" = "1" ]; then
    echo "[0.5/5] Exporting CJK resource..."
    CJK_GENERATED=0
    CJK_REQUESTED_COVERAGE="$OPENOS_CJK_COVERAGE"

    if [ "$OPENOS_CJK_COVERAGE" = "ui" ]; then
        CJK_GENERATOR_ARGS=(--from-c $SRC/generated/cjk_font.c --resource-out "$OPENOS_CJK_RESOURCE_PATH")
        if [ "$OPENOS_CJK_COMPRESS" = "1" ]; then
            CJK_GENERATOR_ARGS+=(--compress)
        fi
        PYTHONDONTWRITEBYTECODE=1 python3 scripts/generate_cjk_font.py "${CJK_GENERATOR_ARGS[@]}"
        CJK_GENERATED=1
    else
        CJK_GENERATOR_ARGS=(--coverage "$OPENOS_CJK_COVERAGE" --resource-out "$OPENOS_CJK_RESOURCE_PATH")
        if [ "$OPENOS_CJK_COMPRESS" = "1" ]; then
            CJK_GENERATOR_ARGS+=(--compress)
        fi
        if [ -n "$OPENOS_CJK_FONT" ]; then
            CJK_GENERATOR_ARGS+=(--font "$OPENOS_CJK_FONT")
        fi
        if PYTHONDONTWRITEBYTECODE=1 python3 scripts/generate_cjk_font.py "${CJK_GENERATOR_ARGS[@]}"; then
            CJK_GENERATED=1
        else
            if command -v powershell.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
                echo "  Python generator failed; trying Windows GDI+ CJK generator..."
                PS_RESOURCE_PATH=$(wslpath -w "$OPENOS_CJK_RESOURCE_PATH")
                PS_SCRIPT_PATH=$(wslpath -w "scripts/generate_cjk_font.ps1")
                PS_ARGS=(-NoProfile -ExecutionPolicy Bypass -File "$PS_SCRIPT_PATH" -Out "" -ResourceOut "$PS_RESOURCE_PATH" -Coverage "$OPENOS_CJK_COVERAGE")
                if [ "$OPENOS_CJK_COMPRESS" = "1" ]; then
                    PS_ARGS+=(-Compress)
                fi
                if powershell.exe "${PS_ARGS[@]}"; then
                    CJK_GENERATED=1
                fi
            fi
            if [ "$CJK_GENERATED" != "1" ]; then
                if [ "$OPENOS_CJK_STRICT" = "1" ]; then
                    echo "ERROR: failed to generate OPENOS_CJK_COVERAGE=$CJK_REQUESTED_COVERAGE resource" >&2
                    echo "       Install Pillow and a Chinese TTF/TTC font, or use Windows PowerShell/GDI+ generator." >&2
                    exit 1
                fi
                echo "WARNING: failed to generate OPENOS_CJK_COVERAGE=$CJK_REQUESTED_COVERAGE; falling back to checked-in UI CJK subset." >&2
                echo "         For full Chinese coverage install Pillow and a Chinese font, or set OPENOS_CJK_STRICT=1 in CI." >&2
                OPENOS_CJK_COVERAGE=ui
                OPENOS_CJK_RESOURCE_PATH=$BUILD/cjk.ofnt
                CJK_GENERATOR_ARGS=(--from-c $SRC/generated/cjk_font.c --resource-out "$OPENOS_CJK_RESOURCE_PATH")
                if [ "$OPENOS_CJK_COMPRESS" = "1" ]; then
                    CJK_GENERATOR_ARGS+=(--compress)
                fi
                PYTHONDONTWRITEBYTECODE=1 python3 scripts/generate_cjk_font.py "${CJK_GENERATOR_ARGS[@]}"
                CJK_GENERATED=1
            fi
        fi
    fi

    if [ "$CJK_GENERATED" != "1" ]; then
        echo "ERROR: no CJK resource was generated" >&2
        exit 1
    fi
    if [ "$OPENOS_CJK_EMBED" = "1" ]; then
        if [ "$OPENOS_CJK_RESOURCE_PATH" != "$BUILD/cjk.ofnt" ]; then
            cp "$OPENOS_CJK_RESOURCE_PATH" "$BUILD/cjk.ofnt"
        fi
        CJK_COVERAGE_CHECK_ARGS=("$BUILD/cjk.ofnt")
        if [ "${OPENOS_CJK_COVERAGE_STRICT:-0}" = "1" ]; then
            CJK_COVERAGE_CHECK_ARGS+=(--fail-on-missing)
        fi
        PYTHONDONTWRITEBYTECODE=1 python3 scripts/check_cjk_coverage.py "${CJK_COVERAGE_CHECK_ARGS[@]}"
    else
        echo "  CJK resource kept external at $OPENOS_CJK_RESOURCE_PATH (OPENOS_CJK_EMBED=0)"
        printf '' > "$BUILD/cjk.ofnt"
    fi
    (cd $BUILD && objcopy -I binary -O elf32-i386 -B i386 cjk.ofnt cjk_ofnt.o)
else
    printf '' > "$BUILD/cjk.ofnt"
    (cd $BUILD && objcopy -I binary -O elf32-i386 -B i386 cjk.ofnt cjk_ofnt.o)
fi

echo "[1/5] Assembling boot.asm..."
nasm -f bin $SRC/../boot/boot.asm -o $BUILD/boot.bin

echo "[2/5] Assembling kernel asm files..."
nasm -f elf32 $SRC/entry.asm -o $BUILD/entry.o
nasm -f elf32 $SRC/isr.asm -o $BUILD/isr.o
nasm -f elf32 $SRC/gdt_flush.asm -o $BUILD/gdt_flush.o
nasm -f elf32 $SRC/sched/context_switch.asm -o $BUILD/context_switch.o
nasm -f elf32 $SRC/timer_isr.asm -o $BUILD/timer_isr.o
nasm -f elf32 $SRC/switch_to_user.asm -o $BUILD/switch_to_user.o
nasm -f elf32 $SRC/kernel_thread_trampoline.asm -o $BUILD/kernel_thread_trampoline.o

# 编译用户程序并嵌入内�?
echo "[2.5] Building user program..."
USR=src/user
OPENOS_EMBED_TESTS=${OPENOS_EMBED_TESTS:-0}
TEST_EMBED_HEADERS="isotest waittest forktest threadtest mutextest semtest condtest futextest nicetest exit42 orphan argtest envtest libctest maintest systest kaddrtest malloctest errnotest stdiotest fstest alarmtest mmaptest sbrktest"
if [ "$OPENOS_EMBED_TESTS" != "1" ]; then
    for app in $TEST_EMBED_HEADERS; do
        rm -f "$SRC/include/embed_${app}.h"
    done
fi
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

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/waittest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/waittest.c -o $BUILD/waittest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/waittest.elf $BUILD/waittest.o
    python3 _embed_elf.py $BUILD/waittest.elf $SRC/include/embed_waittest.h waittest_elf
    echo "  Embedded: waittest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/forktest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/forktest.c -o $BUILD/forktest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/forktest.elf $BUILD/forktest.o
    python3 _embed_elf.py $BUILD/forktest.elf $SRC/include/embed_forktest.h forktest_elf
    echo "  Embedded: forktest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/threadtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/threadtest.c -o $BUILD/threadtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/threadtest.elf $BUILD/threadtest.o
    python3 _embed_elf.py $BUILD/threadtest.elf $SRC/include/embed_threadtest.h threadtest_elf
    echo "  Embedded: threadtest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/mutextest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/mutextest.c -o $BUILD/mutextest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/mutextest.elf $BUILD/mutextest.o
    python3 _embed_elf.py $BUILD/mutextest.elf $SRC/include/embed_mutextest.h mutextest_elf
    echo "  Embedded: mutextest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/semtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/semtest.c -o $BUILD/semtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/semtest.elf $BUILD/semtest.o
    python3 _embed_elf.py $BUILD/semtest.elf $SRC/include/embed_semtest.h semtest_elf
    echo "  Embedded: semtest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/condtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/condtest.c -o $BUILD/condtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/condtest.elf $BUILD/condtest.o
    python3 _embed_elf.py $BUILD/condtest.elf $SRC/include/embed_condtest.h condtest_elf
    echo "  Embedded: condtest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/futextest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/futextest.c -o $BUILD/futextest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/futextest.elf $BUILD/futextest.o
    python3 _embed_elf.py $BUILD/futextest.elf $SRC/include/embed_futextest.h futextest_elf
    echo "  Embedded: futextest.elf"
fi

if [ -f $USR/mqtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/mqtest.c -o $BUILD/mqtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/mqtest.elf $BUILD/mqtest.o
    echo "  Built: mqtest.elf"
fi

if [ -f $USR/shmtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/shmtest.c -o $BUILD/shmtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/shmtest.elf $BUILD/shmtest.o
    echo "  Built: shmtest.elf"
fi

if [ -f $USR/eventfdtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/eventfdtest.c -o $BUILD/eventfdtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/eventfdtest.elf $BUILD/eventfdtest.o
    echo "  Built: eventfdtest.elf"
fi

if [ -f $USR/socketpairtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/socketpairtest.c -o $BUILD/socketpairtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/socketpairtest.elf $BUILD/socketpairtest.o
    echo "  Built: socketpairtest.elf"
fi

if [ -f $USR/servicetest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/servicetest.c -o $BUILD/servicetest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/servicetest.elf $BUILD/servicetest.o
    echo "  Built: servicetest.elf"
fi

if [ -f $USR/micromsgtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/micromsgtest.c -o $BUILD/micromsgtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/micromsgtest.elf $BUILD/micromsgtest.o
    echo "  Built: micromsgtest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/nicetest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/nicetest.c -o $BUILD/nicetest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/nicetest.elf $BUILD/nicetest.o
    python3 _embed_elf.py $BUILD/nicetest.elf $SRC/include/embed_nicetest.h nicetest_elf
    echo "  Embedded: nicetest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/isotest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/isotest.c -o $BUILD/isotest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/isotest.elf $BUILD/isotest.o
    python3 _embed_elf.py $BUILD/isotest.elf $SRC/include/embed_isotest.h isotest_elf
    echo "  Embedded: isotest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/exit42.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/exit42.c -o $BUILD/exit42.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/exit42.elf $BUILD/exit42.o
    python3 _embed_elf.py $BUILD/exit42.elf $SRC/include/embed_exit42.h exit42_elf
    echo "  Embedded: exit42.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/orphan.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/orphan.c -o $BUILD/orphan.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/orphan.elf $BUILD/orphan.o
    python3 _embed_elf.py $BUILD/orphan.elf $SRC/include/embed_orphan.h orphan_elf
    echo "  Embedded: orphan.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/envtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/envtest.c -o $BUILD/envtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/envtest.elf $BUILD/envtest.o
    python3 _embed_elf.py $BUILD/envtest.elf $SRC/include/embed_envtest.h envtest_elf
    echo "  Embedded: envtest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/argtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/argtest.c -o $BUILD/argtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/argtest.elf $BUILD/argtest.o
    python3 _embed_elf.py $BUILD/argtest.elf $SRC/include/embed_argtest.h argtest_elf
    echo "  Embedded: argtest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/libctest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/libctest.c -o $BUILD/libctest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/libctest.elf $BUILD/libctest.o
    python3 _embed_elf.py $BUILD/libctest.elf $SRC/include/embed_libctest.h libctest_elf
    echo "  Embedded: libctest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/maintest.c ]; then
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

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/systest.c ]; then
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

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/kaddrtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/crt0.c -o $BUILD/crt0.o
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/kaddrtest.c -o $BUILD/kaddrtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/kaddrtest.elf $BUILD/crt0.o $BUILD/kaddrtest.o
    python3 _embed_elf.py $BUILD/kaddrtest.elf $SRC/include/embed_kaddrtest.h kaddrtest_elf
    echo "  Embedded: kaddrtest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/malloctest.c ]; then
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

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/errnotest.c ]; then
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

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/stdiotest.c ]; then
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

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/fstest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/fstest.c -o $BUILD/fstest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/fstest.elf $BUILD/fstest.o
    python3 _embed_elf.py $BUILD/fstest.elf $SRC/include/embed_fstest.h fstest_elf
    echo "  Embedded: fstest.elf"
fi

if [ -f $USR/sh.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/sh.c -o $BUILD/sh.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/sh.elf $BUILD/sh.o
    python3 _embed_elf.py $BUILD/sh.elf $SRC/include/embed_sh.h sh_elf
    echo "  Embedded: sh.elf"
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

if [ -f $USR/ai.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/ai.c -o $BUILD/ai.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/ai.elf $BUILD/ai.o
    python3 _embed_elf.py $BUILD/ai.elf $SRC/include/embed_ai.h ai_elf
    echo "  Embedded: ai.elf"
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

if [ -f $USR/cp.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/cp.c -o $BUILD/cp.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/cp.elf $BUILD/cp.o
    python3 _embed_elf.py $BUILD/cp.elf $SRC/include/embed_cp.h cp_elf
    echo "  Embedded: cp.elf"
fi

if [ -f $USR/mv.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/mv.c -o $BUILD/mv.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/mv.elf $BUILD/mv.o
    python3 _embed_elf.py $BUILD/mv.elf $SRC/include/embed_mv.h mv_elf
    echo "  Embedded: mv.elf"
fi

if [ -f $USR/tee.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/tee.c -o $BUILD/tee.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/tee.elf $BUILD/tee.o
    python3 _embed_elf.py $BUILD/tee.elf $SRC/include/embed_tee.h tee_elf
    echo "  Embedded: tee.elf"
fi

if [ -f $USR/head.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/head.c -o $BUILD/head.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/head.elf $BUILD/head.o
    python3 _embed_elf.py $BUILD/head.elf $SRC/include/embed_head.h head_elf
    echo "  Embedded: head.elf"
fi

if [ -f $USR/tail.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/tail.c -o $BUILD/tail.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/tail.elf $BUILD/tail.o
    python3 _embed_elf.py $BUILD/tail.elf $SRC/include/embed_tail.h tail_elf
    echo "  Embedded: tail.elf"
fi

if [ -f $USR/sort.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/sort.c -o $BUILD/sort.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/sort.elf $BUILD/sort.o
    python3 _embed_elf.py $BUILD/sort.elf $SRC/include/embed_sort.h sort_elf
    echo "  Embedded: sort.elf"
fi

if [ -f $USR/env.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/env.c -o $BUILD/env.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/env.elf $BUILD/env.o
    python3 _embed_elf.py $BUILD/env.elf $SRC/include/embed_env.h env_elf
    echo "  Embedded: env.elf"
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

if [ -f $USR/ln.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/ln.c -o $BUILD/ln.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/ln.elf $BUILD/ln.o
    python3 _embed_elf.py $BUILD/ln.elf $SRC/include/embed_ln.h ln_elf
    echo "  Embedded: ln.elf"
fi

if [ -f $USR/kill.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/kill.c -o $BUILD/kill.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/kill.elf $BUILD/kill.o
    python3 _embed_elf.py $BUILD/kill.elf $SRC/include/embed_kill.h kill_elf
    echo "  Embedded: kill.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/alarmtest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/alarmtest.c -o $BUILD/alarmtest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/alarmtest.elf $BUILD/alarmtest.o
    python3 _embed_elf.py $BUILD/alarmtest.elf $SRC/include/embed_alarmtest.h alarmtest_elf
    echo "  Embedded: alarmtest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/mmaptest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/mmaptest.c -o $BUILD/mmaptest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/mmaptest.elf $BUILD/mmaptest.o
    python3 _embed_elf.py $BUILD/mmaptest.elf $SRC/include/embed_mmaptest.h mmaptest_elf
    echo "  Embedded: mmaptest.elf"
fi

if [ "$OPENOS_EMBED_TESTS" = "1" ] && [ -f $USR/sbrktest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/sbrktest.c -o $BUILD/sbrktest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/sbrktest.elf $BUILD/sbrktest.o
    python3 _embed_elf.py $BUILD/sbrktest.elf $SRC/include/embed_sbrktest.h sbrktest_elf
    echo "  Embedded: sbrktest.elf"
fi

gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
    -fno-stack-protector -fno-builtin \
    -I $SRC/include \
    -c $USR/crt0.c -o $BUILD/crt0.o

for app in ping ifconfig netstat; do
    if [ -f $USR/$app.c ]; then
        gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
            -fno-stack-protector -fno-builtin \
            -I $SRC/include \
            -c $USR/$app.c -o $BUILD/$app.o
        ld -m elf_i386 -T $USR/user.ld -o $BUILD/$app.elf $BUILD/crt0.o $BUILD/$app.o
        python3 _embed_elf.py $BUILD/$app.elf $SRC/include/embed_$app.h ${app}_elf
        echo "  Embedded: $app.elf"
    fi
done

for app in id groups cap sandbox; do
    if [ -f $USR/$app.c ]; then
        gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
            -fno-stack-protector -fno-builtin \
            -I $SRC/include \
            -c $USR/$app.c -o $BUILD/$app.o
        ld -m elf_i386 -T $USR/user.ld -o $BUILD/$app.elf $BUILD/$app.o
        python3 _embed_elf.py $BUILD/$app.elf $SRC/include/embed_$app.h ${app}_elf
        echo "  Embedded: $app.elf"
    fi
done

if [ -f $USR/firewall.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/firewall.c -o $BUILD/firewall.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/firewall.elf $BUILD/firewall.o
    python3 _embed_elf.py $BUILD/firewall.elf $SRC/include/embed_firewall.h firewall_elf
    echo "  Embedded: firewall.elf"
fi

check_user_elf_wx() {
    local elf="$1"
    readelf -l "$elf" | awk '
        /LOAD/ && $0 ~ /RWE/ { bad=1 }
        END { exit bad ? 1 : 0 }
    '
}

for elf in $BUILD/*.elf; do
    [ -f "$elf" ] || continue
    if ! check_user_elf_wx "$elf"; then
        echo "[ERROR] W^X violation: RWX LOAD segment in $elf" >&2
        exit 1
    fi
done

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
    -c $SRC/smp.c -o $BUILD/smp.o

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
    -c $SRC/panic.c -o $BUILD/panic.o

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
    -c $SRC/drivers/usb.c -o $BUILD/usb.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/sound.c -o $BUILD/sound.o

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
    -c $SRC/drivers/ata.c -o $BUILD/ata.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/ahci.c -o $BUILD/ahci.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/virtio_blk.c -o $BUILD/virtio_blk.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/virtio_net.c -o $BUILD/virtio_net.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/e1000.c -o $BUILD/e1000.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/rtl8139.c -o $BUILD/rtl8139.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/pci.c -o $BUILD/pci.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/acpi.c -o $BUILD/acpi.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/apic.c -o $BUILD/apic.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/rtc.c -o $BUILD/rtc.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/power.c -o $BUILD/power.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/input_buffer.c -o $BUILD/input_buffer.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/vga.c -o $BUILD/vga.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -Os \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/framebuffer.c -o $BUILD/framebuffer.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/i18n.c -o $BUILD/i18n.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/tls_crypto.c -o $BUILD/tls_crypto.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/tls_parser.c -o $BUILD/tls_parser.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -Os \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/gui.c -o $BUILD/gui.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/image.c -o $BUILD/image.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/window_manager.c -o $BUILD/window_manager.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/font.c -o $BUILD/font.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/generated/cjk_font.c -o $BUILD/cjk_font.o

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
    -c $SRC/aslr.c -o $BUILD/aslr.o

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
    -I $SRC/include -I $SRC/fs \
    -c $SRC/fs/pfs.c -o $BUILD/pfs.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/fs \
    -c $SRC/fs/fat32.c -o $BUILD/fat32.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/net \
    -c $SRC/net/net.c -o $BUILD/net.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/net \
    -c $SRC/net/socket.c -o $BUILD/socket.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/net \
    -c $SRC/net/dhcp.c -o $BUILD/dhcp.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/net \
    -c $SRC/net/dns.c -o $BUILD/dns.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/net \
    -c $SRC/net/net_config.c -o $BUILD/net_config.o

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
    -c $SRC/net/account.c -o $BUILD/account.o

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
    $BUILD/kernel_thread_trampoline.o \
    $BUILD/kernel.o \
    $BUILD/idt.o \
    $BUILD/gdt.o \
    $BUILD/pmm.o \
    $BUILD/vmm.o \
    $BUILD/heap.o \
    $BUILD/scheduler.o \
    $BUILD/smp.o \
    $BUILD/syscall.o \
    $BUILD/serial.o \
    $BUILD/panic.o \
    $BUILD/vga.o \
    $BUILD/framebuffer.o \
    $BUILD/i18n.o \
    $BUILD/tls_crypto.o \
    $BUILD/tls_parser.o \
    $BUILD/gui.o \
    $BUILD/image.o \
    $BUILD/window_manager.o \
    $BUILD/font.o \
    $BUILD/cjk_font.o \
    $BUILD/cjk_ofnt.o \
    $BUILD/string.o \
    $BUILD/keyboard.o \
    $BUILD/mouse.o \
    $BUILD/usb_tablet.o \
    $BUILD/usb.o \
    $BUILD/sound.o \
    $BUILD/chardev.o \
    $BUILD/blockdev.o \
    $BUILD/ata.o \
    $BUILD/ahci.o \
    $BUILD/virtio_blk.o \
    $BUILD/virtio_net.o \
    $BUILD/e1000.o \
    $BUILD/rtl8139.o \
    $BUILD/pci.o \
    $BUILD/acpi.o \
    $BUILD/apic.o \
    $BUILD/rtc.o \
    $BUILD/power.o \
    $BUILD/input_buffer.o \
    $BUILD/usermode.o \
    $BUILD/aslr.o \
    $BUILD/usermem.o \
    $BUILD/process.o \
    $BUILD/elf_loader.o \
    $BUILD/vfs.o \
    $BUILD/ramfs.o \
    $BUILD/tmpfs.o \
    $BUILD/ext4.o \
    $BUILD/pfs.o \
    $BUILD/fat32.o \
    $BUILD/net.o \
    $BUILD/socket.o \
    $BUILD/dhcp.o \
    $BUILD/dns.o \
    $BUILD/net_config.o \
    $BUILD/discovery.o \
    $BUILD/sync.o \
    $BUILD/account.o \
    $BUILD/bus.o \
    $BUILD/devmgr.o \
    $BUILD/ai.o \
    $BUILD/shell.o

objcopy -O binary $BUILD/kernel.elf $BUILD/kernel.bin

KERNEL_BYTES=$(stat -c%s "$BUILD/kernel.bin")
KERNEL_SECTORS=$(( (KERNEL_BYTES + 511) / 512 ))
BOOT_LOAD_SECTORS=2816
if [ "$KERNEL_SECTORS" -gt "$BOOT_LOAD_SECTORS" ]; then
    echo "ERROR: kernel.bin is ${KERNEL_SECTORS} sectors, but bootloader loads only ${BOOT_LOAD_SECTORS} sectors."
    echo "Increase bootloader high-memory load chunks or move to a larger disk image before building the image."
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
