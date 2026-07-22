#!/bin/bash
# openos Phase 2 Build Script (WSL native tools)

set -e
if [ -d /mnt/e/openos ]; then
    cd /mnt/e/openos
elif [ -d /mnt/host/e/openos ]; then
    cd /mnt/host/e/openos
else
    cd /e/openos
fi

BUILD=target
SRC=src/kernel
# Step E.5: default architecture switched to x86_64 (long mode UEFI main line).
# Override with ARCH=i386 or ARCH=aarch64 for legacy/embedded targets.
BUILD_ARCH="${ARCH:-x86_64}"
OPENOS_TCC_SMOKE=${OPENOS_TCC_SMOKE:-0}
KERNEL_EXTRA_CFLAGS="${KERNEL_EXTRA_CFLAGS:-}"
if [ "$OPENOS_TCC_SMOKE" = "1" ]; then
    KERNEL_EXTRA_CFLAGS="$KERNEL_EXTRA_CFLAGS -DOPENOS_TCC_SMOKE_AUTORUN=1"
fi
# M8-A：开启后 QEMU usb-tablet 会被强制当作单点触屏验证 M8-A 代码路径
OPENOS_TOUCH_TEST=${OPENOS_TOUCH_TEST:-0}
if [ "$OPENOS_TOUCH_TEST" = "1" ]; then
    KERNEL_EXTRA_CFLAGS="$KERNEL_EXTRA_CFLAGS -DOPENOS_TOUCH_TEST=1"
fi
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
    echo "Usage: ARCH=x86_64|i386|aarch64 ./build.sh [clean|test|cppsmoke|sdk|sdk-smoke|chromium-source-check|chromium-gn-check|chromium-engine-gate|chromium-real-switch-gate|skia-official-check|v8-official-check|host-tools-check]"
    echo "       (default ARCH is x86_64; pass ARCH=i386 for the legacy 32-bit image)"
    echo "       ./build.sh [x86_64|i386|aarch64] [clean|test|cppsmoke|sdk|sdk-smoke]"
    echo "       ./build.sh cppsmoke    # probe OpenOS userland C++ toolchain"
    echo "       ./build.sh sdk         # export OpenOS userland SDK/sysroot for Chromium ports"
    echo "       ./build.sh sdk-smoke   # verify SDK can build a minimal OpenOS user ELF"
    echo "       ./build.sh chromium-source-check # check Chromium minimal source/dependency closure prerequisites"
    echo "       ./build.sh chromium-source-fetch # fetch minimal Chromium src entrypoint only"
    echo "       ./build.sh chromium-source-fetch-full # explicit full Chromium fetch/gclient sync; not the default OpenOS route"
    echo "       ./build.sh chromium-gn-check # check Chromium OpenOS GN/toolchain overlay"
    echo "       ./build.sh chromium-engine-gate # verify Chromium demo is not mislabeled as real Chrome"
    echo "       ./build.sh chromium-real-switch-gate # final gate for replacing demo /bin/chromium with real Chromium"
    echo "       ./build.sh skia-official-check # check official Skia intake prerequisites"
    echo "       ./build.sh v8-official-check # check official V8 d8 jitless intake prerequisites"
    echo "       ./build.sh v8-official-fetch # fetch official V8 checkout and write v8.official.pin"
    echo "       ./build.sh v8-official-sync-deps # sync official V8 minimal DEPS for d8 build"
    echo "       ./build.sh v8-official-build # build official V8 d8 for host jitless smoke"
    echo "       ./build.sh v8-official-smoke # run official V8 d8 --jitless smoke"
    echo "       ./build.sh chromium-content-shell-check # check Blink/content_shell single-process software path"
    echo "       ./build.sh chromium-content-shell-gn-gen # generate OpenOS content_shell GN output"
    echo "       ./build.sh chromium-content-shell-build # build official Chromium content_shell"
    echo "       ./build.sh chromium-content-shell-smoke # run content_shell single-process software smoke"
    echo "       ./build.sh host-tools-check # check no-sudo host tool bootstrap"
}

check_cpp_toolchain() {
    local cxx="${OPENOS_CXX:-}"
    local candidate

    if [ -z "$cxx" ]; then
        for candidate in i686-elf-g++ clang++ g++; do
            if command -v "$candidate" >/dev/null 2>&1; then
                cxx="$candidate"
                break
            fi
        done
    fi

    if [ -z "$cxx" ] || ! command -v "$cxx" >/dev/null 2>&1; then
        echo "ERROR: no OpenOS C++ toolchain found for cppsmoke." >&2
        echo "       Install i686-elf-g++ or clang++, or set OPENOS_CXX=/path/to/compiler." >&2
        echo "       M8 C++ runtime smoke remains unavailable until this probe passes." >&2
        return 1
    fi

    echo "OpenOS C++ compiler candidate: $cxx"
    "$cxx" --version | head -1
    echo "cppsmoke toolchain probe passed; userland C++ ABI smoke is ready to wire next."
}

case "${1:-}" in
    test)
        exec bash tests/run_unit_tests.sh
        ;;
    cppsmoke|check-cpp|cpp)
        check_cpp_toolchain
        exit $?
        ;;
    sdk|sysroot|export-sdk)
        exec bash scripts/export-openos-sdk.sh
        ;;
    sdk-smoke|smoke-sdk)
        exec bash scripts/sdk-smoke.sh
        ;;
    chromium-source-check|chromium-check|chrome-source-check)
        exec bash scripts/chromium-source.sh --check
        ;;
    chromium-source-fetch|chromium-fetch|chrome-source-fetch)
        exec bash scripts/chromium-source.sh --fetch
        ;;
    chromium-source-fetch-full|chromium-fetch-full|chrome-source-fetch-full)
        exec bash scripts/chromium-source.sh --fetch-full
        ;;
    chromium-gn-check|chrome-gn-check|chromium-toolchain-check)
        exec bash scripts/chromium-openos-gn.sh --check
        ;;
    chromium-engine-gate|chrome-engine-gate|real-chrome-gate)
        exec bash scripts/chromium-engine-gate.sh --check
        ;;
    chromium-real-switch-gate|chrome-real-switch-gate|real-chromium-switch-gate)
        exec bash scripts/chromium-real-switch-gate.sh --check
        ;;
    skia-official-check|skia-check|official-skia-check)
        exec bash scripts/skia-official.sh --check
        ;;
    v8-official-check|v8-check|official-v8-check)
        exec bash scripts/v8-official.sh --check
        ;;
    v8-official-fetch|v8-fetch|official-v8-fetch)
        exec bash scripts/v8-official.sh --fetch
        ;;
    v8-official-sync-deps|v8-sync-deps|official-v8-sync-deps)
        exec bash scripts/v8-official.sh --sync-deps
        ;;
    v8-official-build|v8-build|official-v8-build)
        exec bash scripts/v8-official.sh --build
        ;;
    v8-official-smoke|v8-smoke|official-v8-smoke)
        exec bash scripts/v8-official.sh --smoke
        ;;
    chromium-content-shell-check|content-shell-check|blink-content-shell-check)
        exec bash scripts/chromium-content-shell.sh --check
        ;;
    chromium-content-shell-gn-gen|content-shell-gn-gen|blink-content-shell-gn-gen)
        exec bash scripts/chromium-content-shell.sh --gn-gen
        ;;
    chromium-content-shell-build|content-shell-build|blink-content-shell-build)
        exec bash scripts/chromium-content-shell.sh --build
        ;;
    chromium-content-shell-smoke|content-shell-smoke|blink-content-shell-smoke)
        exec bash scripts/chromium-content-shell.sh --smoke
        ;;
    host-tools-check|bootstrap-host-tools-check)
        exec bash scripts/bootstrap-host-tools.sh --check
        ;;
    host-tools-bootstrap|bootstrap-host-tools)
        exec bash scripts/bootstrap-host-tools.sh --download
        ;;
    opkg|opk|opkg-build)
        exec bash tools/build_opk.sh
        ;;
    i386|x86_64|aarch64)
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
if [ "${1:-}" = "cppsmoke" ] || [ "${1:-}" = "check-cpp" ] || [ "${1:-}" = "cpp" ]; then
    check_cpp_toolchain
    exit $?
fi
if [ "${1:-}" = "sdk" ] || [ "${1:-}" = "sysroot" ] || [ "${1:-}" = "export-sdk" ]; then
    exec bash scripts/export-openos-sdk.sh
fi
if [ "${1:-}" = "sdk-smoke" ] || [ "${1:-}" = "smoke-sdk" ]; then
    exec bash scripts/sdk-smoke.sh
fi

case "$BUILD_ARCH" in
    i386|x86_64|aarch64)
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

if [ "$BUILD_ARCH" = "aarch64" ]; then
    echo "===== Building openos aarch64 QEMU virt minimal boot ====="
    AARCH64_SRC=src/arch/aarch64
    AARCH64_BUILD="$BUILD/aarch64"
    AARCH64_USER_BUILD="$AARCH64_BUILD/user"
    AARCH64_USER_BIN="$AARCH64_BUILD/bin"
    AARCH64_QEMU="${AARCH64_QEMU:-qemu-system-aarch64}"

    # Toolchain selection order:
    #   1. Explicit AARCH64_CC/AARCH64_LD/AARCH64_OBJCOPY from the environment.
    #   2. GNU cross tools: aarch64-linux-gnu-gcc/ld/objcopy.
    #   3. LLVM cross mode: clang --target=aarch64-none-elf + ld.lld + llvm-objcopy.
    AARCH64_TOOLCHAIN="custom"
    AARCH64_CC="${AARCH64_CC:-}"
    AARCH64_LD="${AARCH64_LD:-}"
    AARCH64_OBJCOPY="${AARCH64_OBJCOPY:-}"
    AARCH64_CC_TARGET_FLAGS="${AARCH64_CC_TARGET_FLAGS:-}"
    AARCH64_LD_TARGET_FLAGS="${AARCH64_LD_TARGET_FLAGS:-}"

    if [ -z "$AARCH64_CC" ] && [ -z "$AARCH64_LD" ] && [ -z "$AARCH64_OBJCOPY" ]; then
        if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 && \
           command -v aarch64-linux-gnu-ld >/dev/null 2>&1 && \
           command -v aarch64-linux-gnu-objcopy >/dev/null 2>&1; then
            AARCH64_TOOLCHAIN="gnu"
            AARCH64_CC=aarch64-linux-gnu-gcc
            AARCH64_LD=aarch64-linux-gnu-ld
            AARCH64_OBJCOPY=aarch64-linux-gnu-objcopy
        elif command -v clang >/dev/null 2>&1 && \
             command -v ld.lld >/dev/null 2>&1 && \
             command -v llvm-objcopy >/dev/null 2>&1; then
            AARCH64_TOOLCHAIN="llvm"
            AARCH64_CC=clang
            AARCH64_LD=ld.lld
            AARCH64_OBJCOPY=llvm-objcopy
            AARCH64_CC_TARGET_FLAGS="--target=aarch64-none-elf $AARCH64_CC_TARGET_FLAGS"
            AARCH64_LD_TARGET_FLAGS="-m aarch64elf $AARCH64_LD_TARGET_FLAGS"
        else
            echo "ERROR: no usable aarch64 toolchain found." >&2
            echo "Tried GNU:  aarch64-linux-gnu-gcc, aarch64-linux-gnu-ld, aarch64-linux-gnu-objcopy" >&2
            echo "Tried LLVM: clang, ld.lld, llvm-objcopy" >&2
            echo "Install gcc-aarch64-linux-gnu/binutils-aarch64-linux-gnu, install LLVM tools, or set:" >&2
            echo "  AARCH64_CC=/path/to/compiler AARCH64_LD=/path/to/linker AARCH64_OBJCOPY=/path/to/objcopy" >&2
            exit 1
        fi
    fi

    for tool in "$AARCH64_CC" "$AARCH64_LD" "$AARCH64_OBJCOPY"; do
        if [ -z "$tool" ] || ! command -v "$tool" >/dev/null 2>&1; then
            echo "ERROR: missing aarch64 build tool: ${tool:-<unset>}" >&2
            echo "Install gcc-aarch64-linux-gnu/binutils-aarch64-linux-gnu, install LLVM tools, or set AARCH64_CC/AARCH64_LD/AARCH64_OBJCOPY." >&2
            exit 1
        fi
    done

    mkdir -p "$AARCH64_BUILD" "$AARCH64_USER_BUILD" "$AARCH64_USER_BIN"
    rm -f "$AARCH64_BUILD"/*.o "$AARCH64_BUILD"/*.elf "$AARCH64_BUILD"/*.bin \
          "$AARCH64_USER_BUILD"/*.o "$AARCH64_USER_BIN"/*.elf
    AARCH64_CFLAGS="$AARCH64_CC_TARGET_FLAGS -ffreestanding -nostdlib -Wall -Wextra -O2 -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-builtin -fno-pic -fno-pie -I$AARCH64_SRC/include -Isrc/kernel/include"
    AARCH64_USER_CFLAGS="$AARCH64_CC_TARGET_FLAGS -ffreestanding -nostdlib -Wall -Wextra -O2 -fno-stack-protector -fno-builtin -fno-pic -fno-pie -I$AARCH64_SRC/include"
    AARCH64_USER_LDFLAGS="-Ttext=0x400000 -nostdlib"

    echo "Using aarch64 toolchain: $AARCH64_TOOLCHAIN"
    echo "  CC:      $AARCH64_CC $AARCH64_CC_TARGET_FLAGS"
    echo "  LD:      $AARCH64_LD $AARCH64_LD_TARGET_FLAGS"
    echo "  OBJCOPY: $AARCH64_OBJCOPY"

    echo "[1/5] Building aarch64 /bin/hello64 ELF..."
    "$AARCH64_CC" $AARCH64_USER_CFLAGS -c "$AARCH64_SRC/user/hello64.c" -o "$AARCH64_USER_BUILD/hello64.o"
    "$AARCH64_LD" $AARCH64_LD_TARGET_FLAGS $AARCH64_USER_LDFLAGS -o "$AARCH64_USER_BIN/hello64.elf" "$AARCH64_USER_BUILD/hello64.o"
    (
        cd "$AARCH64_USER_BIN"
        "$AARCH64_OBJCOPY" -I binary -O elf64-littleaarch64 -B aarch64 \
            --rename-section .data=.rodata.aarch64_hello64_elf,alloc,load,readonly,data,contents \
            --set-section-alignment .rodata.aarch64_hello64_elf=16 \
            --redefine-sym _binary_hello64_elf_start=__aarch64_hello64_elf_start \
            --redefine-sym _binary_hello64_elf_end=__aarch64_hello64_elf_end \
            --redefine-sym _binary_hello64_elf_size=__aarch64_hello64_elf_size \
            "hello64.elf" "../hello64_elf.o"
    )

    echo "[2/5] Assembling aarch64 boot and exception vectors..."
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/boot/boot.S" -o "$AARCH64_BUILD/boot.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/boot/exception_vectors.S" -o "$AARCH64_BUILD/exception_vectors.o"

    echo "[3/5] Compiling aarch64 C files..."
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_uart.c" -o "$AARCH64_BUILD/aarch64_uart.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_syscall.c" -o "$AARCH64_BUILD/aarch64_syscall.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_exception.c" -o "$AARCH64_BUILD/aarch64_exception.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_memory.c" -o "$AARCH64_BUILD/aarch64_memory.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_elf64.c" -o "$AARCH64_BUILD/aarch64_elf64.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_usermode.c" -o "$AARCH64_BUILD/aarch64_usermode.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_platform.c" -o "$AARCH64_BUILD/aarch64_platform.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_dtb.c" -o "$AARCH64_BUILD/aarch64_dtb.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_gicv3.c" -o "$AARCH64_BUILD/aarch64_gicv3.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_i2c_bus.c" -o "$AARCH64_BUILD/aarch64_i2c_bus.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_gt911.c" -o "$AARCH64_BUILD/aarch64_gt911.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_selftest.c" -o "$AARCH64_BUILD/aarch64_selftest.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -I"src/kernel/include" -c "src/kernel/input/input_core.c" -o "$AARCH64_BUILD/input_core.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_initrd.c" -o "$AARCH64_BUILD/aarch64_initrd.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_vfs.c" -o "$AARCH64_BUILD/aarch64_vfs.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_shell.c" -o "$AARCH64_BUILD/aarch64_shell.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/kernel/aarch64_bootinfo.c" -o "$AARCH64_BUILD/aarch64_bootinfo.o"
    "$AARCH64_CC" $AARCH64_CFLAGS -c "$AARCH64_SRC/src/aarch64_kernel.c" -o "$AARCH64_BUILD/aarch64_kernel.o"

    echo "[4/5] Linking aarch64 kernel..."
    "$AARCH64_LD" $AARCH64_LD_TARGET_FLAGS -T "$AARCH64_SRC/linker_aarch64.ld" -nostdlib \
        "$AARCH64_BUILD/boot.o" \
        "$AARCH64_BUILD/exception_vectors.o" \
        "$AARCH64_BUILD/aarch64_uart.o" \
        "$AARCH64_BUILD/aarch64_syscall.o" \
        "$AARCH64_BUILD/aarch64_exception.o" \
        "$AARCH64_BUILD/aarch64_memory.o" \
        "$AARCH64_BUILD/aarch64_elf64.o" \
        "$AARCH64_BUILD/aarch64_usermode.o" \
        "$AARCH64_BUILD/aarch64_platform.o" \
        "$AARCH64_BUILD/aarch64_dtb.o" \
        "$AARCH64_BUILD/aarch64_gicv3.o" \
        "$AARCH64_BUILD/aarch64_i2c_bus.o" \
        "$AARCH64_BUILD/aarch64_gt911.o" \
        "$AARCH64_BUILD/aarch64_selftest.o" \
        "$AARCH64_BUILD/input_core.o" \
        "$AARCH64_BUILD/aarch64_initrd.o" \
        "$AARCH64_BUILD/aarch64_vfs.o" \
        "$AARCH64_BUILD/aarch64_shell.o" \
        "$AARCH64_BUILD/aarch64_bootinfo.o" \
        "$AARCH64_BUILD/aarch64_kernel.o" \
        "$AARCH64_BUILD/hello64_elf.o" \
        -o "$AARCH64_BUILD/openos-aarch64.elf"
    "$AARCH64_OBJCOPY" -O binary "$AARCH64_BUILD/openos-aarch64.elf" "$AARCH64_BUILD/openos-aarch64.bin"

    echo "[5/5] aarch64 minimal boot image ready."
    echo "  ELF: $AARCH64_BUILD/openos-aarch64.elf"
    echo "  BIN: $AARCH64_BUILD/openos-aarch64.bin"
    echo "  USER: $AARCH64_USER_BIN/hello64.elf"
    if command -v "$AARCH64_QEMU" >/dev/null 2>&1; then
        echo "  Smoke run: $AARCH64_QEMU -M virt,gic-version=3 -cpu cortex-a57 -nographic -kernel $AARCH64_BUILD/openos-aarch64.elf"
        echo "  Smoke DTB: $AARCH64_QEMU -M virt,gic-version=3 -cpu cortex-a57 -nographic -kernel $AARCH64_BUILD/openos-aarch64.bin"
    else
        echo "  QEMU not found; install qemu-system-aarch64 or set AARCH64_QEMU to run smoke boot."
    fi
    exit 0
fi

if [ "$BUILD_ARCH" = "x86_64" ]; then
    echo "===== Building openos Phase 2 (x86_64 kernel + hello64 regression) ====="
    ARCH64_SRC=src/arch/x86_64
    ARCH64_BUILD="$BUILD/x86_64"
    ARCH64_USER_BUILD="$ARCH64_BUILD/user"
    ARCH64_BOOT_BUILD="$ARCH64_BUILD/boot"
    ARCH64_BIN_BUILD="$ARCH64_BUILD/bin"
    # M5.1: M5_RING3_CONSOLE (default OFF) — when ON it skips the early-GUI
    # lockscreen loop so the launcher ring3 closed loop runs and its user output
    # is observable on serial before the desktop comes up. Default is now 0 to
    # restore the early-GUI path WITH the boot lockscreen. Set M5_RING3_CONSOLE=1
    # only for ring3 serial-debug sessions.
    M5_RING3_CONSOLE="${M5_RING3_CONSOLE:-0}"
    M5_RING3_DEF=""
    if [ "$M5_RING3_CONSOLE" != "0" ]; then
        M5_RING3_DEF="-DM5_RING3_CONSOLE"
    fi
    # M5.4c diag: M5_FAST_BOOT (default OFF) skips timing-sensitive SMP selftest
    # stages 15/16 (PIT-tick preempt-gate stress) that are flaky under single-core
    # QEMU. Use ONLY for end-to-end user-program diag runs; full suite stays ON
    # for normal builds/CI.
    M5_FAST_BOOT="${M5_FAST_BOOT:-0}"
    M5_FAST_BOOT_DEF=""
    if [ "$M5_FAST_BOOT" != "0" ]; then
        M5_FAST_BOOT_DEF="-DM5_FAST_BOOT"
    fi
    # M5.4d diag: M5_OPKG_DIAG (default OFF) boots straight into the package-
    # manager end-to-end self-test (/bin/opkg_selftest). Implies fast-boot to
    # skip the flaky SMP selftest. Use ONLY for opkg diag runs.
    M5_OPKG_DIAG="${M5_OPKG_DIAG:-0}"
    M5_OPKG_DIAG_DEF=""
    if [ "$M5_OPKG_DIAG" != "0" ]; then
        M5_OPKG_DIAG_DEF="-DM5_OPKG_DIAG -DM5_FAST_BOOT"
    fi
    # M6.1 diag: M6_POWER_DIAG (default OFF) triggers a real ACPI S5 soft-off
    # right after the power selftest, so QEMU powers off cleanly (rc 0) proving
    # the PM1a_CNT write path works. Implies fast-boot. Use ONLY for power diag.
    M6_POWER_DIAG="${M6_POWER_DIAG:-0}"
    M6_POWER_DIAG_DEF=""
    if [ "$M6_POWER_DIAG" != "0" ]; then
        M6_POWER_DIAG_DEF="-DM6_POWER_DIAG -DM5_FAST_BOOT"
    fi
    # M6.2 diag: M6_CPUINFO_DIAG (default OFF) boots straight into the CPU
    # frequency / thermal self-test (/bin/cpuinfo_selftest), exercising
    # SYS_CPUINFO end-to-end. Implies fast-boot. Use ONLY for cpuinfo diag.
    M6_CPUINFO_DIAG="${M6_CPUINFO_DIAG:-0}"
    M6_CPUINFO_DIAG_DEF=""
    if [ "$M6_CPUINFO_DIAG" != "0" ]; then
        M6_CPUINFO_DIAG_DEF="-DM6_CPUINFO_DIAG -DM5_FAST_BOOT"
    fi
    # M6.11.4 diag: M6_LOGIN_DIAG (default OFF) boots straight into /bin/login
    # with a known-good credential (openos/openos), exercising SYS_LOGIN
    # end-to-end: authenticate against /etc/passwd+/etc/shadow, then
    # setsid+setgid+setuid drop. Implies fast-boot. Use ONLY for login diag.
    M6_LOGIN_DIAG="${M6_LOGIN_DIAG:-0}"
    M6_LOGIN_DIAG_DEF=""
    if [ "$M6_LOGIN_DIAG" != "0" ]; then
        M6_LOGIN_DIAG_DEF="-DM6_LOGIN_DIAG -DM5_FAST_BOOT"
    fi
    # M6.12 diag: M6_DMESG_DIAG (default OFF) boots straight into /bin/dmesg
    # to exercise SYS_KLOG end-to-end. Implies fast-boot.
    M6_DMESG_DIAG="${M6_DMESG_DIAG:-0}"
    M6_DMESG_DIAG_DEF=""
    if [ "$M6_DMESG_DIAG" != "0" ]; then
        M6_DMESG_DIAG_DEF="-DM6_DMESG_DIAG -DM5_FAST_BOOT"
    fi
    ARCH64_CFLAGS="-m64 -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mno-avx -ffreestanding -nostdlib -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE -fno-stack-protector -fno-builtin -DGUI_EARLY_VERIFY $M5_RING3_DEF $M5_FAST_BOOT_DEF $M5_OPKG_DIAG_DEF $M6_POWER_DIAG_DEF $M6_CPUINFO_DIAG_DEF $M6_LOGIN_DIAG_DEF $M6_DMESG_DIAG_DEF -I$ARCH64_SRC/include -Isrc/kernel/include -Isrc/kernel"
    ARCH64_ASFLAGS="-m64 -mcmodel=kernel -mno-red-zone -fno-pic -fno-pie -fno-PIE -I$ARCH64_SRC/include -Isrc/kernel/include"
    ARCH64_USER_CFLAGS="-m64 -mcmodel=large -ffreestanding -nostdlib -Wall -Wextra -O2 -fno-pic -fno-pie -fno-PIE -fno-stack-protector -fno-builtin -I$ARCH64_SRC/user"
    ARCH64_USER_ASFLAGS="-m64 -mcmodel=large -fno-pic -fno-pie -fno-PIE -I$ARCH64_SRC/user"
    ARCH64_LDFLAGS="-m elf_x86_64 -T $ARCH64_SRC/linker64.ld -nostdlib"
    ARCH64_USER_LDFLAGS="-m elf_x86_64 -T $ARCH64_SRC/user/user64.ld -nostdlib"
    # UEFI 编译链：使用 mingw-w64 直接产出标准 PE/COFF 格式 BOOTX64.EFI
    # 这样 EntryPoint/Subsystem/Characteristics 等 PE 头字段会被正确填充，
    # 避免 objcopy 转换 ELF->PE 时关键字段为 0 导致 UEFI 加载器拒绝执行。
    UEFI_CC="x86_64-w64-mingw32-gcc"
    ARCH64_UEFI_CFLAGS="-ffreestanding -fshort-wchar -mno-red-zone -Wall -Wextra -O2 -fno-stack-protector -fno-builtin -I$ARCH64_SRC/include"
    ARCH64_UEFI_ASFLAGS="-ffreestanding -fno-stack-protector"
    # UEFI 应用是 PE32+；--subsystem 10 = EFI Application。
    # 关键：不能同时使用 -shared / --dynamicbase 但又缺 .reloc 表，否则 OVMF LoadImage
    # 会拒绝（表现为 Not Found）。采用静态镀接 + 固定 ImageBase=0x100000，交由 PE
    # loader 按 ImageBase 加载，不需要动态重定位。
    ARCH64_UEFI_LDFLAGS="-nostdlib -nostartfiles -Wl,--subsystem,10 -Wl,-e,_start -Wl,--image-base,0x100000 -Wl,--disable-dynamicbase"

    mkdir -p "$ARCH64_BUILD" "$ARCH64_USER_BUILD" "$ARCH64_BOOT_BUILD" "$ARCH64_BIN_BUILD"
    rm -f "$ARCH64_BUILD"/*.o "$ARCH64_BUILD"/*.elf "$ARCH64_USER_BUILD"/*.o "$ARCH64_BOOT_BUILD"/*.o "$ARCH64_BOOT_BUILD"/*.elf "$ARCH64_BOOT_BUILD"/*.EFI "$ARCH64_BOOT_BUILD"/*.efi "$ARCH64_BIN_BUILD"/*.elf

    echo "[1/5] Building x86_64 /bin/hello64 regression ELF..."
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
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/hello64.elf" "$ARCH64_SRC/include/embed_hello64.h" hello64_elf

    echo "[1b/5] Building x86_64 /bin/hello64_v2 ELF (H.3 execve target)..."
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/hello64_v2.c" -o "$ARCH64_USER_BUILD/hello64_v2.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/hello64_v2.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/hello64_v2.o"
    readelf -h "$ARCH64_BIN_BUILD/hello64_v2.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/hello64_v2.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/hello64_v2.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/hello64_v2.elf" "$ARCH64_SRC/include/embed_hello64_v2.h" hello64_v2_elf

    echo "[1b'/5] Building x86_64 /bin/hello_fork ELF (A2.P5 standalone fork/wait target)..."
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/hello_fork.c" -o "$ARCH64_USER_BUILD/hello_fork.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/hello_fork.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/hello_fork.o"
    readelf -h "$ARCH64_BIN_BUILD/hello_fork.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/hello_fork.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/hello_fork.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/hello_fork.elf" "$ARCH64_SRC/include/embed_hello_fork.h" hello_fork_elf

    echo "[1b''/5] Building x86_64 /bin/thread_demo ELF (M5.2d clone+futex+pthread target)..."
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/pthread64.c" -o "$ARCH64_USER_BUILD/pthread64.o"
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/thread_demo64.c" -o "$ARCH64_USER_BUILD/thread_demo64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/thread_demo.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/pthread64.o" \
        "$ARCH64_USER_BUILD/thread_demo64.o"
    readelf -h "$ARCH64_BIN_BUILD/thread_demo.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/thread_demo.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/thread_demo.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/thread_demo.elf" "$ARCH64_SRC/include/embed_thread_demo.h" thread_demo_elf

    echo "[1b2/5] Building x86_64 /bin/libc_demo ELF (M5.3e standard C library subset end-to-end)..."
    # Compile the libc subset (M5.3a..d). Standard symbol names (memcpy/malloc/printf/...).
    for libc_unit in string stdlib stdio ctype errno assert libc_write libc_sbrk; do
        gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/libc/$libc_unit.c" -o "$ARCH64_USER_BUILD/libc_$libc_unit.o"
    done
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/libc_demo64.c" -o "$ARCH64_USER_BUILD/libc_demo64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/libc_demo.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/libc_string.o" \
        "$ARCH64_USER_BUILD/libc_stdlib.o" \
        "$ARCH64_USER_BUILD/libc_stdio.o" \
        "$ARCH64_USER_BUILD/libc_ctype.o" \
        "$ARCH64_USER_BUILD/libc_errno.o" \
        "$ARCH64_USER_BUILD/libc_assert.o" \
        "$ARCH64_USER_BUILD/libc_libc_write.o" \
        "$ARCH64_USER_BUILD/libc_libc_sbrk.o" \
        "$ARCH64_USER_BUILD/libc_demo64.o"
    readelf -h "$ARCH64_BIN_BUILD/libc_demo.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/libc_demo.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/libc_demo.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/libc_demo.elf" "$ARCH64_SRC/include/embed_libc_demo.h" libc_demo_elf

    echo "[1b3/5] Building x86_64 /bin/fs_demo ELF (M5.4a writable-VFS end-to-end)..."
    # Reuses the libc subset .o already compiled above.
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/fs_demo64.c" -o "$ARCH64_USER_BUILD/fs_demo64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/fs_demo.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/libc_string.o" \
        "$ARCH64_USER_BUILD/libc_stdlib.o" \
        "$ARCH64_USER_BUILD/libc_stdio.o" \
        "$ARCH64_USER_BUILD/libc_ctype.o" \
        "$ARCH64_USER_BUILD/libc_errno.o" \
        "$ARCH64_USER_BUILD/libc_assert.o" \
        "$ARCH64_USER_BUILD/libc_libc_write.o" \
        "$ARCH64_USER_BUILD/libc_libc_sbrk.o" \
        "$ARCH64_USER_BUILD/fs_demo64.o"
    readelf -h "$ARCH64_BIN_BUILD/fs_demo.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/fs_demo.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/fs_demo.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/fs_demo.elf" "$ARCH64_SRC/include/embed_fs_demo.h" fs_demo_elf

    echo "[1b3b/5] Building x86_64 /bin/login ELF (M6.11.4 login/session syscall end-to-end)..."
    # Reuses the libc subset .o already compiled above.
    gcc $ARCH64_USER_CFLAGS -I"$ARCH64_SRC/user" -c "$ARCH64_SRC/user/login64.c" -o "$ARCH64_USER_BUILD/login64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/login.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/libc_string.o" \
        "$ARCH64_USER_BUILD/libc_stdlib.o" \
        "$ARCH64_USER_BUILD/libc_stdio.o" \
        "$ARCH64_USER_BUILD/libc_ctype.o" \
        "$ARCH64_USER_BUILD/libc_errno.o" \
        "$ARCH64_USER_BUILD/libc_assert.o" \
        "$ARCH64_USER_BUILD/libc_libc_write.o" \
        "$ARCH64_USER_BUILD/libc_libc_sbrk.o" \
        "$ARCH64_USER_BUILD/login64.o"
    readelf -h "$ARCH64_BIN_BUILD/login.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/login.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/login.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/login.elf" "$ARCH64_SRC/include/embed_login.h" login_elf

    echo "[1b3c/5] Building x86_64 /bin/dmesg ELF (M6.12 klog viewer)..."
    gcc $ARCH64_USER_CFLAGS -I"$ARCH64_SRC/user" -c "$ARCH64_SRC/user/dmesg64.c" -o "$ARCH64_USER_BUILD/dmesg64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/dmesg.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/libc_string.o" \
        "$ARCH64_USER_BUILD/libc_stdlib.o" \
        "$ARCH64_USER_BUILD/libc_stdio.o" \
        "$ARCH64_USER_BUILD/libc_ctype.o" \
        "$ARCH64_USER_BUILD/libc_errno.o" \
        "$ARCH64_USER_BUILD/libc_assert.o" \
        "$ARCH64_USER_BUILD/libc_libc_write.o" \
        "$ARCH64_USER_BUILD/libc_libc_sbrk.o" \
        "$ARCH64_USER_BUILD/dmesg64.o"
    readelf -h "$ARCH64_BIN_BUILD/dmesg.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/dmesg.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/dmesg.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/dmesg.elf" "$ARCH64_SRC/include/embed_dmesg.h" dmesg_elf

    echo "[1b4/5] Building x86_64 /bin/opk_demo ELF (M5.4c .opk install end-to-end)..."
    # Reuses the libc subset .o already compiled above.
    gcc $ARCH64_USER_CFLAGS -I"$ARCH64_SRC/include" -c "$ARCH64_SRC/user/opk_demo64.c" -o "$ARCH64_USER_BUILD/opk_demo64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/opk_demo.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/libc_string.o" \
        "$ARCH64_USER_BUILD/libc_stdlib.o" \
        "$ARCH64_USER_BUILD/libc_stdio.o" \
        "$ARCH64_USER_BUILD/libc_ctype.o" \
        "$ARCH64_USER_BUILD/libc_errno.o" \
        "$ARCH64_USER_BUILD/libc_assert.o" \
        "$ARCH64_USER_BUILD/libc_libc_write.o" \
        "$ARCH64_USER_BUILD/libc_libc_sbrk.o" \
        "$ARCH64_USER_BUILD/opk_demo64.o"
    readelf -h "$ARCH64_BIN_BUILD/opk_demo.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/opk_demo.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/opk_demo.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/opk_demo.elf" "$ARCH64_SRC/include/embed_opk_demo.h" opk_demo_elf

    echo "[1b5/5] Building x86_64 /bin/opkg ELF (M5.4d user-space package manager CLI)..."
    # Reuses the libc subset .o already compiled above.
    gcc $ARCH64_USER_CFLAGS -I"$ARCH64_SRC/include" -c "$ARCH64_SRC/user/opkg64.c" -o "$ARCH64_USER_BUILD/opkg64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/opkg.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/libc_string.o" \
        "$ARCH64_USER_BUILD/libc_stdlib.o" \
        "$ARCH64_USER_BUILD/libc_stdio.o" \
        "$ARCH64_USER_BUILD/libc_ctype.o" \
        "$ARCH64_USER_BUILD/libc_errno.o" \
        "$ARCH64_USER_BUILD/libc_assert.o" \
        "$ARCH64_USER_BUILD/libc_libc_write.o" \
        "$ARCH64_USER_BUILD/libc_libc_sbrk.o" \
        "$ARCH64_USER_BUILD/opkg64.o"
    readelf -h "$ARCH64_BIN_BUILD/opkg.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/opkg.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/opkg.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/opkg.elf" "$ARCH64_SRC/include/embed_opkg.h" opkg_elf

    echo "[1b6/5] Building x86_64 opk_payload real-ELF package (M5.4e install+execve e2e)..."
    # opk_payload is a genuine ring3 ELF that is NOT embedded in the initrd.
    # Its only route into a running system is: pack -> SYS_OPK_INSTALL -> execve
    # out of the writable ramfs. Build the ELF, pack it into a .opk with the
    # host opkg-build tool, then embed the .opk *image bytes* so the e2e test
    # can install+run it. Package name 'payload', exec entry install-name 'app'
    # -> unpacks to /pkg/payload/app.
    gcc $ARCH64_USER_CFLAGS -I"$ARCH64_SRC/include" -c "$ARCH64_SRC/user/opk_payload64.c" -o "$ARCH64_USER_BUILD/opk_payload64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/opk_payload.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/libc_string.o" \
        "$ARCH64_USER_BUILD/libc_stdlib.o" \
        "$ARCH64_USER_BUILD/libc_stdio.o" \
        "$ARCH64_USER_BUILD/libc_ctype.o" \
        "$ARCH64_USER_BUILD/libc_errno.o" \
        "$ARCH64_USER_BUILD/libc_assert.o" \
        "$ARCH64_USER_BUILD/libc_libc_write.o" \
        "$ARCH64_USER_BUILD/libc_libc_sbrk.o" \
        "$ARCH64_USER_BUILD/opk_payload64.o"
    readelf -h "$ARCH64_BIN_BUILD/opk_payload.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/opk_payload.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/opk_payload.elf" | grep -q ' openos64_main$'
    # host opkg-build packager (self-contained, mirrors opk64.h layout)
    gcc -O2 -Wall -Wextra -Werror -I"$ARCH64_SRC/include" \
        "tools/opkg-build.c" -o "$ARCH64_USER_BUILD/opkg-build"
    "$ARCH64_USER_BUILD/opkg-build" \
        -o "$ARCH64_BIN_BUILD/payload.opk" -n payload \
        -e "$ARCH64_BIN_BUILD/opk_payload.elf:app"
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/payload.opk" "$ARCH64_SRC/include/embed_opk_payload.h" opk_payload_opk

    echo "[1b7/5] Building x86_64 /bin/opkg_selftest ELF (M5.4d/M5.4e package-manager e2e test)..."
    gcc $ARCH64_USER_CFLAGS -I"$ARCH64_SRC/include" -c "$ARCH64_SRC/user/opkg_selftest64.c" -o "$ARCH64_USER_BUILD/opkg_selftest64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/opkg_selftest.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/libc_string.o" \
        "$ARCH64_USER_BUILD/libc_stdlib.o" \
        "$ARCH64_USER_BUILD/libc_stdio.o" \
        "$ARCH64_USER_BUILD/libc_ctype.o" \
        "$ARCH64_USER_BUILD/libc_errno.o" \
        "$ARCH64_USER_BUILD/libc_assert.o" \
        "$ARCH64_USER_BUILD/libc_libc_write.o" \
        "$ARCH64_USER_BUILD/libc_libc_sbrk.o" \
        "$ARCH64_USER_BUILD/opkg_selftest64.o"
    readelf -h "$ARCH64_BIN_BUILD/opkg_selftest.elf" | grep -q 'Class:.*ELF64'
    nm "$ARCH64_BIN_BUILD/opkg_selftest.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/opkg_selftest.elf" "$ARCH64_SRC/include/embed_opkg_selftest.h" opkg_selftest_elf

    echo "[1b8/5] Building x86_64 /bin/cpuinfo_selftest ELF (M6.2d SYS_CPUINFO e2e test)..."
    gcc $ARCH64_USER_CFLAGS -I"$ARCH64_SRC/include" -c "$ARCH64_SRC/user/cpuinfo_selftest64.c" -o "$ARCH64_USER_BUILD/cpuinfo_selftest64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/cpuinfo_selftest.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/libc_string.o" \
        "$ARCH64_USER_BUILD/libc_stdlib.o" \
        "$ARCH64_USER_BUILD/libc_stdio.o" \
        "$ARCH64_USER_BUILD/libc_ctype.o" \
        "$ARCH64_USER_BUILD/libc_errno.o" \
        "$ARCH64_USER_BUILD/libc_assert.o" \
        "$ARCH64_USER_BUILD/libc_libc_write.o" \
        "$ARCH64_USER_BUILD/libc_libc_sbrk.o" \
        "$ARCH64_USER_BUILD/cpuinfo_selftest64.o"
    readelf -h "$ARCH64_BIN_BUILD/cpuinfo_selftest.elf" | grep -q 'Class:.*ELF64'
    nm "$ARCH64_BIN_BUILD/cpuinfo_selftest.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/cpuinfo_selftest.elf" "$ARCH64_SRC/include/embed_cpuinfo_selftest.h" cpuinfo_selftest_elf

    echo "[1c/5] Building x86_64 /bin/launcher ELF (H.3 execve caller)..."
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/launcher.c" -o "$ARCH64_USER_BUILD/launcher.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/launcher.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/launcher.o"
    readelf -h "$ARCH64_BIN_BUILD/launcher.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/launcher.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/launcher.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/launcher.elf" "$ARCH64_SRC/include/embed_launcher.h" launcher_elf

    echo "[1d/5] Building x86_64 /bin/ifconfig ELF (M1.5.3 net tool)..."
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/ifconfig64.c" -o "$ARCH64_USER_BUILD/ifconfig64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/ifconfig64.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/ifconfig64.o"
    readelf -h "$ARCH64_BIN_BUILD/ifconfig64.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/ifconfig64.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/ifconfig64.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/ifconfig64.elf" "$ARCH64_SRC/include/embed_ifconfig64.h" ifconfig64_elf

    echo "[1e/5] Building x86_64 /bin/ping ELF (M1.5.3 net tool)..."
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/ping64.c" -o "$ARCH64_USER_BUILD/ping64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/ping64.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/ping64.o"
    readelf -h "$ARCH64_BIN_BUILD/ping64.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/ping64.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/ping64.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/ping64.elf" "$ARCH64_SRC/include/embed_ping64.h" ping64_elf

    echo "[1f/5] Building x86_64 /bin/nslookup ELF (M1.5.3 net tool)..."
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/nslookup64.c" -o "$ARCH64_USER_BUILD/nslookup64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/nslookup64.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/nslookup64.o"
    readelf -h "$ARCH64_BIN_BUILD/nslookup64.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/nslookup64.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/nslookup64.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/nslookup64.elf" "$ARCH64_SRC/include/embed_nslookup64.h" nslookup64_elf

    echo "[1g/5] Building x86_64 /bin/wget ELF (M1.7 ring3 TCP tool)..."
    gcc $ARCH64_USER_CFLAGS -c "$ARCH64_SRC/user/wget64.c" -o "$ARCH64_USER_BUILD/wget64.o"
    ld $ARCH64_USER_LDFLAGS -o "$ARCH64_BIN_BUILD/wget64.elf" \
        "$ARCH64_USER_BUILD/start.o" \
        "$ARCH64_USER_BUILD/crt0.o" \
        "$ARCH64_USER_BUILD/wget64.o"
    readelf -h "$ARCH64_BIN_BUILD/wget64.elf" | grep -q 'Class:.*ELF64'
    readelf -h "$ARCH64_BIN_BUILD/wget64.elf" | grep -q 'Machine:.*X86-64'
    nm "$ARCH64_BIN_BUILD/wget64.elf" | grep -q ' openos64_main$'
    python3 _embed_elf.py "$ARCH64_BIN_BUILD/wget64.elf" "$ARCH64_SRC/include/embed_wget64.h" wget64_elf

    echo "[1z/5] Embedding i18n JSON translation files into initrd..."
    # i18n 译文以 JSON 为唯一数据源 (res/i18n/*.json)，编译期嵌入 initrd，
    # 运行时由 i18n.c 经 VFS 读取 /etc/i18n/*.json 解析填表，代码不写死任何译文。
    # 先从 i18n.h 枚举自动生成 key 名映射表，保证与枚举永久同步。
    python3 tools/gen_i18n_keys.py
    python3 _embed_elf.py "res/i18n/en.json" "$ARCH64_SRC/include/embed_i18n_en.h" i18n_en_json
    python3 _embed_elf.py "res/i18n/zh.json" "$ARCH64_SRC/include/embed_i18n_zh.h" i18n_zh_json

    echo "[2/5] Compiling x86_64 C files..."
    for cfile in \
        kernel/kernel64.c \
        kernel/gdt64.c \
        kernel/tss64.c \
        kernel/idt64.c \
        kernel/idt_selftest64.c \
        kernel/sched64.c \
        kernel/proc64.c \
        kernel/signal64.c \
        kernel/syscall64.c \
        kernel/syscall_dispatch64.c \
        kernel/opk_install.c \
        kernel/opk_install_kernel.c \
        kernel/syscall_net64.c \
        kernel/syscall_selftest64.c \
        kernel/sched_selftest64.c \
        kernel/net64.c \
        kernel/net_selftest64.c \
        kernel/tsc64.c \
        kernel/tsc_selftest64.c \
        kernel/pic64.c \
        kernel/pit64.c \
        kernel/mouse64.c \
        kernel/keyboard64.c \
        kernel/irq_selftest64.c \
        kernel/sched_preempt_selftest64.c \
        kernel/sched_prio_selftest64.c \
        kernel/lapic64.c \
        kernel/ioapic64.c \
        kernel/apic_selftest64.c \
        kernel/acpi64.c \
        kernel/acpi_dsdt.c \
        kernel/acpi_selftest64.c \
        kernel/power64.c \
        kernel/power_selftest64.c \
        kernel/cpufreq64.c \
        kernel/cpufreq_selftest64.c \
        kernel/cred_selftest64.c \
        kernel/account_db64.c \
        kernel/login64.c \
        kernel/login_selftest64.c \
        kernel/klog64.c \
        kernel/klog_selftest64.c \
        kernel/gesture_selftest64.c \
        kernel/multitouch_selftest64.c \
        kernel/osk_selftest64.c \
        kernel/touch_ui_selftest64.c \
        kernel/input_selftest64.c \
        kernel/notif_center_selftest64.c \
        kernel/app_stack_selftest64.c \
        kernel/app_lifecycle_selftest64.c \
        kernel/app_switcher_selftest64.c \
        kernel/sys_input_read_selftest64.c \
        kernel/nc_fade_selftest64.c \
        kernel/gui_metrics_selftest64.c \
        kernel/gui_input_bridge_selftest64.c \
        kernel/security64.c \
        kernel/gfx_selftest64.c \
        kernel/virtio_gpu_selftest64.c \
        kernel/smp64.c \
        kernel/smp_selftest64.c \
        kernel/percpu64.c \
        kernel/ap_trampoline64.c \
        kernel/delay64.c \
        kernel/fdtable64.c \
        kernel/sfdtable64.c \
        kernel/pipe64.c \
        kernel/fifo64.c \
        kernel/shm64.c \
        kernel/futex64.c \
        kernel/tty64.c \
        kernel/vmem64.c \
        kernel/initrd64.c \
        kernel/vfs64.c \
        kernel/shell64.c \
        kernel/compat32.c \
        kernel/handoff64.c \
        kernel/pmm64.c \
        kernel/vmm64.c \
        kernel/address_space64.c \
        kernel/as_selftest64.c \
        kernel/heap64.c \
        kernel/elf64_loader.c \
        kernel/elf64_dynamic.c \
        kernel/elf64_symtab.c \
        kernel/elf64_lazy.c \
        kernel/usermode64.c \
        kernel/early_console64.c \
        kernel/pci64.c \
        kernel/virtio_net64.c \
        kernel/virtio_modern64.c \
        kernel/virtio_gpu64.c \
        kernel/virtio_input64.c; do
        obj="$ARCH64_BUILD/$(basename "${cfile%.c}").o"
        gcc $ARCH64_CFLAGS -c "$ARCH64_SRC/$cfile" -o "$obj"
    done
    for cfile in \
        src/kernel/arch_ops.c \
        src/kernel/platform_ops.c \
        src/kernel/device.c \
        src/kernel/driver.c \
        src/kernel/basic_devices.c \
        src/arch/x86_64/src/x86_64_arch_ops.c \
        src/kernel/platform/pc_uefi_platform_ops.c; do
        obj="$ARCH64_BUILD/$(basename "${cfile%.c}").o"
        gcc $ARCH64_CFLAGS -c "$cfile" -o "$obj"
    done

    echo "[2b/5] Compiling x86_64 GUI subsystem (ported from i386)..."
    # GUI 移植：i386 版 gui.c/gui_user.c/i18n.c/font.c/window_manager.c/framebuffer.c
    # 直接以 x86_64 目标编译（types.h 已按 __x86_64__ 分支适配）。
    # 需额外 -Isrc/kernel 以解析 gui.c 的 "core/fs/vfs.h"、"net/net.h" 等相对包含。
    # 外部依赖（net/dns/tls/vfs/mouse）由 gui64_stubs.c 提供安全桩，
    # framebuffer 后端由 framebuffer64.c(UEFI GOP) + gui64_shims.c 提供。
    GUI64_CFLAGS="$ARCH64_CFLAGS -Isrc/kernel"
    for cfile in \
        src/kernel/gui/gui.c \
        src/kernel/gui/gesture.c \
        src/kernel/gui/gesture_multi.c \
        src/kernel/gui/gesture3.c \
        src/kernel/gui/hid_parser.c \
        src/kernel/gui/hid_type_infer.c \
        src/kernel/gui/osk.c \
        src/kernel/gui/touch_ui.c \
        src/kernel/gui/notif_center.c \
        src/kernel/gui/app_switcher_ui.c \
        src/kernel/app/app_stack.c \
        src/kernel/app/app_manifest.c \
        src/kernel/app/app_launcher.c \
        src/kernel/app/gui_mode.c \
        src/kernel/gui/gui_metrics.c \
        src/kernel/gui/gui_input_bridge.c \
        src/kernel/input/input_core.c \
        src/kernel/gui/gui_browser.c \
        src/kernel/gui/gui_terminal.c \
        src/kernel/gui/gui_sticky.c \
        src/kernel/gui/gui_file_preview.c \
        src/kernel/gui/gui_settings.c \
        src/kernel/gui/gui_applets.c \
        src/kernel/gui/gui_user.c \
        src/kernel/i18n.c \
        src/kernel/font.c \
        src/kernel/gui/window_manager.c \
        src/kernel/generated/cjk_font.c \
        src/kernel/crypto/sha256.c \
        src/kernel/lockscreen.c \
        src/arch/x86_64/gui64/framebuffer64.c \
        src/arch/x86_64/gui64/gui64_shims.c \
        src/arch/x86_64/gui64/gui64_stubs.c \
        src/arch/x86_64/gui64/ramfs64.c \
        src/arch/x86_64/gui64/ata64.c \
        src/arch/x86_64/gui64/ahci64.c \
        src/arch/x86_64/gui64/nvme64.c \
        src/arch/x86_64/gui64/xhci64.c \
        src/arch/x86_64/gui64/usb_hid64.c \
        src/arch/x86_64/gui64/usb_msc.c \
        src/arch/x86_64/gui64/sound.c \
        src/arch/x86_64/gui64/ac97.c \
        src/kernel/drivers/blockdev.c \
        src/arch/x86_64/gui64/blockdev_hw.c \
        src/kernel/net/netstack.c \
        src/arch/x86_64/gui64/fat32_64.c \
        src/arch/x86_64/gui64/ext4_64.c; do
        obj="$ARCH64_BUILD/$(basename "${cfile%.c}").o"
        gcc $GUI64_CFLAGS -c "$cfile" -o "$obj"
    done

    echo "[3/5] Assembling x86_64 entry files..."
    for sfile in \
        kernel/entry64.S \
        kernel/isr64.S \
        kernel/context_switch64.S \
        kernel/syscall_int80_compat64.S \
        kernel/syscall_sysret64.S \
        kernel/ap_trampoline64.S \
        kernel/usermode64.S; do
        base="$(basename "${sfile%.S}")"
        if [ "$base" = "usermode64" ]; then
            obj="$ARCH64_BUILD/usermode64_asm.o"
        elif [ "$base" = "ap_trampoline64" ]; then
            obj="$ARCH64_BUILD/ap_trampoline64_asm.o"
        else
            obj="$ARCH64_BUILD/$base.o"
        fi
        gcc $ARCH64_ASFLAGS -c "$ARCH64_SRC/$sfile" -o "$obj"
    done

    echo "[4/5] Linking x86_64 kernel..."
    ld $ARCH64_LDFLAGS -o "$ARCH64_BUILD/kernel64.elf" \
        "$ARCH64_BUILD/entry64.o" \
        "$ARCH64_BUILD/kernel64.o" \
        "$ARCH64_BUILD/gdt64.o" \
        "$ARCH64_BUILD/tss64.o" \
        "$ARCH64_BUILD/idt64.o" \
        "$ARCH64_BUILD/idt_selftest64.o" \
        "$ARCH64_BUILD/isr64.o" \
        "$ARCH64_BUILD/sched64.o" \
        "$ARCH64_BUILD/proc64.o" \
        "$ARCH64_BUILD/signal64.o" \
        "$ARCH64_BUILD/context_switch64.o" \
        "$ARCH64_BUILD/syscall64.o" \
        "$ARCH64_BUILD/syscall_dispatch64.o" \
        "$ARCH64_BUILD/opk_install.o" \
        "$ARCH64_BUILD/opk_install_kernel.o" \
        "$ARCH64_BUILD/syscall_net64.o" \
        "$ARCH64_BUILD/syscall_selftest64.o" \
        "$ARCH64_BUILD/sched_selftest64.o" \
        "$ARCH64_BUILD/net64.o" \
        "$ARCH64_BUILD/net_selftest64.o" \
        "$ARCH64_BUILD/tsc64.o" \
        "$ARCH64_BUILD/tsc_selftest64.o" \
        "$ARCH64_BUILD/pic64.o" \
        "$ARCH64_BUILD/pit64.o" \
        "$ARCH64_BUILD/mouse64.o" \
        "$ARCH64_BUILD/keyboard64.o" \
        "$ARCH64_BUILD/irq_selftest64.o" \
        "$ARCH64_BUILD/sched_preempt_selftest64.o" \
        "$ARCH64_BUILD/sched_prio_selftest64.o" \
        "$ARCH64_BUILD/lapic64.o" \
        "$ARCH64_BUILD/ioapic64.o" \
        "$ARCH64_BUILD/apic_selftest64.o" \
        "$ARCH64_BUILD/acpi64.o" \
        "$ARCH64_BUILD/acpi_dsdt.o" \
        "$ARCH64_BUILD/acpi_selftest64.o" \
        "$ARCH64_BUILD/power64.o" \
        "$ARCH64_BUILD/power_selftest64.o" \
        "$ARCH64_BUILD/cpufreq64.o" \
        "$ARCH64_BUILD/cpufreq_selftest64.o" \
        "$ARCH64_BUILD/cred_selftest64.o" \
        "$ARCH64_BUILD/account_db64.o" \
        "$ARCH64_BUILD/login64.o" \
        "$ARCH64_BUILD/login_selftest64.o" \
        "$ARCH64_BUILD/klog64.o" \
        "$ARCH64_BUILD/klog_selftest64.o" \
        "$ARCH64_BUILD/gesture_selftest64.o" \
        "$ARCH64_BUILD/multitouch_selftest64.o" \
        "$ARCH64_BUILD/osk_selftest64.o" \
        "$ARCH64_BUILD/touch_ui_selftest64.o" \
        "$ARCH64_BUILD/input_selftest64.o" \
        "$ARCH64_BUILD/notif_center_selftest64.o" \
        "$ARCH64_BUILD/app_stack_selftest64.o" \
        "$ARCH64_BUILD/app_lifecycle_selftest64.o" \
        "$ARCH64_BUILD/app_switcher_selftest64.o" \
        "$ARCH64_BUILD/sys_input_read_selftest64.o" \
        "$ARCH64_BUILD/nc_fade_selftest64.o" \
        "$ARCH64_BUILD/gui_metrics_selftest64.o" \
        "$ARCH64_BUILD/gui_input_bridge_selftest64.o" \
        "$ARCH64_BUILD/security64.o" \
        "$ARCH64_BUILD/gfx_selftest64.o" \
        "$ARCH64_BUILD/virtio_gpu_selftest64.o" \
        "$ARCH64_BUILD/smp64.o" \
        "$ARCH64_BUILD/smp_selftest64.o" \
        "$ARCH64_BUILD/percpu64.o" \
        "$ARCH64_BUILD/ap_trampoline64.o" \
        "$ARCH64_BUILD/ap_trampoline64_asm.o" \
        "$ARCH64_BUILD/delay64.o" \
        "$ARCH64_BUILD/fdtable64.o" \
        "$ARCH64_BUILD/sfdtable64.o" \
        "$ARCH64_BUILD/pipe64.o" \
        "$ARCH64_BUILD/fifo64.o" \
        "$ARCH64_BUILD/shm64.o" \
        "$ARCH64_BUILD/futex64.o" \
        "$ARCH64_BUILD/tty64.o" \
        "$ARCH64_BUILD/vmem64.o" \
        "$ARCH64_BUILD/initrd64.o" \
        "$ARCH64_BUILD/vfs64.o" \
        "$ARCH64_BUILD/shell64.o" \
        "$ARCH64_BUILD/compat32.o" \
        "$ARCH64_BUILD/handoff64.o" \
        "$ARCH64_BUILD/syscall_int80_compat64.o" \
        "$ARCH64_BUILD/syscall_sysret64.o" \
        "$ARCH64_BUILD/pmm64.o" \
        "$ARCH64_BUILD/vmm64.o" \
        "$ARCH64_BUILD/address_space64.o" \
        "$ARCH64_BUILD/as_selftest64.o" \
        "$ARCH64_BUILD/heap64.o" \
        "$ARCH64_BUILD/elf64_loader.o" \
        "$ARCH64_BUILD/elf64_dynamic.o" \
        "$ARCH64_BUILD/elf64_symtab.o" \
        "$ARCH64_BUILD/elf64_lazy.o" \
        "$ARCH64_BUILD/usermode64.o" \
        "$ARCH64_BUILD/usermode64_asm.o" \
        "$ARCH64_BUILD/early_console64.o" \
        "$ARCH64_BUILD/pci64.o" \
        "$ARCH64_BUILD/virtio_net64.o" \
        "$ARCH64_BUILD/virtio_modern64.o" \
        "$ARCH64_BUILD/virtio_gpu64.o" \
        "$ARCH64_BUILD/virtio_input64.o" \
        "$ARCH64_BUILD/arch_ops.o" \
        "$ARCH64_BUILD/platform_ops.o" \
        "$ARCH64_BUILD/device.o" \
        "$ARCH64_BUILD/driver.o" \
        "$ARCH64_BUILD/basic_devices.o" \
        "$ARCH64_BUILD/x86_64_arch_ops.o" \
        "$ARCH64_BUILD/pc_uefi_platform_ops.o" \
        "$ARCH64_BUILD/gui.o" \
        "$ARCH64_BUILD/gesture.o" \
        "$ARCH64_BUILD/gesture_multi.o" \
        "$ARCH64_BUILD/gesture3.o" \
        "$ARCH64_BUILD/hid_parser.o" \
        "$ARCH64_BUILD/hid_type_infer.o" \
        "$ARCH64_BUILD/osk.o" \
        "$ARCH64_BUILD/touch_ui.o" \
        "$ARCH64_BUILD/notif_center.o" \
        "$ARCH64_BUILD/app_switcher_ui.o" \
        "$ARCH64_BUILD/app_stack.o" \
        "$ARCH64_BUILD/app_manifest.o" \
        "$ARCH64_BUILD/app_launcher.o" \
        "$ARCH64_BUILD/gui_mode.o" \
        "$ARCH64_BUILD/gui_metrics.o" \
        "$ARCH64_BUILD/gui_input_bridge.o" \
        "$ARCH64_BUILD/input_core.o" \
        "$ARCH64_BUILD/gui_browser.o" \
        "$ARCH64_BUILD/gui_terminal.o" \
        "$ARCH64_BUILD/gui_sticky.o" \
        "$ARCH64_BUILD/gui_file_preview.o" \
        "$ARCH64_BUILD/gui_settings.o" \
        "$ARCH64_BUILD/gui_applets.o" \
        "$ARCH64_BUILD/gui_user.o" \
        "$ARCH64_BUILD/i18n.o" \
        "$ARCH64_BUILD/font.o" \
        "$ARCH64_BUILD/window_manager.o" \
        "$ARCH64_BUILD/cjk_font.o" \
        "$ARCH64_BUILD/sha256.o" \
        "$ARCH64_BUILD/lockscreen.o" \
        "$ARCH64_BUILD/framebuffer64.o" \
        "$ARCH64_BUILD/gui64_shims.o" \
        "$ARCH64_BUILD/gui64_stubs.o" \
        "$ARCH64_BUILD/ramfs64.o" \
        "$ARCH64_BUILD/ata64.o" \
        "$ARCH64_BUILD/ahci64.o" \
        "$ARCH64_BUILD/nvme64.o" \
        "$ARCH64_BUILD/xhci64.o" \
        "$ARCH64_BUILD/usb_hid64.o" \
        "$ARCH64_BUILD/usb_msc.o" \
        "$ARCH64_BUILD/sound.o" \
        "$ARCH64_BUILD/ac97.o" \
        "$ARCH64_BUILD/blockdev.o" \
        "$ARCH64_BUILD/blockdev_hw.o" \
        "$ARCH64_BUILD/netstack.o" \
        "$ARCH64_BUILD/fat32_64.o" \
        "$ARCH64_BUILD/ext4_64.o"
    echo "[5/5] x86_64 kernel and hello64 user ELF linked."

    echo "[UEFI] Building x86_64 BOOTX64.EFI via mingw-w64 (native PE)..."
    $UEFI_CC $ARCH64_UEFI_CFLAGS -c "$ARCH64_SRC/boot/uefi64.c" -o "$ARCH64_BOOT_BUILD/uefi64.o"
    $UEFI_CC $ARCH64_UEFI_ASFLAGS -c "$ARCH64_SRC/boot/uefi64_crt0.S" -o "$ARCH64_BOOT_BUILD/uefi64_crt0.o"
    $UEFI_CC $ARCH64_UEFI_LDFLAGS \
        "$ARCH64_BOOT_BUILD/uefi64_crt0.o" \
        "$ARCH64_BOOT_BUILD/uefi64.o" \
        -o "$ARCH64_BOOT_BUILD/BOOTX64.EFI"
    # 校验产物为合法 PE32+ EFI Application
    objdump -f "$ARCH64_BOOT_BUILD/BOOTX64.EFI" | grep -q 'pei-x86-64'
    objdump -p "$ARCH64_BOOT_BUILD/BOOTX64.EFI" | grep -qiE 'Subsystem[[:space:]]+0*a[[:space:]]*\(EFI' || {
        echo "[UEFI] ERROR: PE Subsystem != 10 (EFI Application)" >&2
        objdump -p "$ARCH64_BOOT_BUILD/BOOTX64.EFI" | grep -i 'subsystem' >&2
        exit 1
    }
    # 检查：若 DllCharacteristics 包含 DYNAMIC_BASE（0x40），则必须存在 .reloc。
    # 注：grep -c 零匹配会返回 exit 1，set -e 下需 || true 徽收
    DLLCHAR=$(objdump -p "$ARCH64_BOOT_BUILD/BOOTX64.EFI" | awk '/DllCharacteristics/ {print $2; exit}' || true)
    HAS_RELOC=$(objdump -h "$ARCH64_BOOT_BUILD/BOOTX64.EFI" | grep -cE '\.reloc' || true)
    DYN_BIT=$(( 0x${DLLCHAR:-0} & 0x40 ))
    if [ "$DYN_BIT" -ne 0 ] && [ "${HAS_RELOC:-0}" -eq 0 ]; then
        echo "[UEFI] ERROR: DllCharacteristics=0x$DLLCHAR 含 DYNAMIC_BASE 但缺 .reloc -> OVMF 会拒绝" >&2
        objdump -p "$ARCH64_BOOT_BUILD/BOOTX64.EFI" | grep -i 'characteristics\|imagebase\|subsystem' >&2
        exit 1
    fi
    echo "[UEFI] BOOTX64.EFI OK: DllChar=0x$DLLCHAR, .reloc=$HAS_RELOC, ImageBase=0x100000"

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

    # 创建 UEFI 磁盘镜像
    # 设计：采用裸 FAT32（不加 GPT/MBR）作为 UEFI 启动镜像。OVMF 可直接识别
    # 裸 FAT 卷为 EFI System Volume。之前采用 GPT+ESP 方案时 OVMF 未能
    # 识别到 ESP（LoadImage 返回 Not Found），原因未定，裸 FAT 绕过问题。
    # 后续产品发表可在裸 FAT 能跳后再探索 GPT 路径。
    echo "[UEFI] Creating UEFI disk image (raw FAT32, no GPT)..."
    UEFI_IMG="$BUILD/openos-uefi.img"
    ESP_SIZE_MB=33

    # 使用 mtools 创建裸 FAT32 镜像并复制文件
    echo "  使用 mtools 创建 FAT32 镜像..."
    truncate -s ${ESP_SIZE_MB}M "$UEFI_IMG"
    mkfs.vfat -F 32 -n "ESP" -S 512 -s 1 -R 32 "$UEFI_IMG" >/dev/null
    mmd -i "$UEFI_IMG" ::/EFI
    mmd -i "$UEFI_IMG" ::/EFI/BOOT
    mcopy -i "$UEFI_IMG" "$ARCH64_BOOT_BUILD/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
    mcopy -i "$UEFI_IMG" "$ARCH64_BUILD/kernel64.elf" ::/kernel64.elf

    # 验证镜像内容
    echo "  验证镜像内容:"
    file "$UEFI_IMG"
    mdir -i "$UEFI_IMG" ::/EFI/BOOT/

    echo "  UEFI disk image: $UEFI_IMG"
    echo "  Size: ${ESP_SIZE_MB}MB (raw FAT32)"

    echo "x86_64 Build Successful!"
    echo "Output: $ARCH64_BUILD/kernel64.elf"
    echo "Regression: $ARCH64_BIN_BUILD/hello64.elf"
    echo "UEFI Image: $UEFI_IMG"
    echo "UEFI: $ARCH64_BOOT_BUILD/BOOTX64.EFI"
    exit 0
fi

echo "===== Building openos Phase 2 (i386) ====="

# ---------------------------------------------------------------------------
# [DEPRECATED] i386 (32-bit) branch has been ARCHIVED (2026-07).
# 150 i386-only source files were removed after dependency-diff analysis
# confirmed the x86_64 mainline does not reference them.
# Shared GUI/font sources (gui.c, font.c, i18n.c, window_manager.c,
# cjk_font.c, etc.) remain and are still compiled by the x86_64 segment.
# A backup tarball lives in legacy_backup/i386_files_*.tar.gz.
# Building i386 will now fail with missing-file errors; abort early with a
# clear message instead. To resurrect it, restore from the backup tarball.
# ---------------------------------------------------------------------------
echo "[DEPRECATED] The i386 32-bit branch has been archived and its sources removed." >&2
echo "             x86_64 is the only supported target. Run: ARCH=x86_64 bash build.sh" >&2
echo "             To restore i386, extract legacy_backup/i386_files_*.tar.gz" >&2
exit 1

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
gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
    -fno-stack-protector -fno-builtin \
    -I $SRC/include \
    -c $USR/crt0.c -o $BUILD/crt0.o
verify_user_start() {
    local elf="$1"
    local name="$2"
    local first_text
    first_text=$(nm -n "$elf" | awk '$2 ~ /^[Tt]$/ { print $3; exit }')
    if [ "$first_text" != "_start" ]; then
        echo "ERROR: $name entry text symbol is '$first_text', expected '_start'" >&2
        echo "       Link $name with $BUILD/crt0.o before application objects." >&2
        exit 1
    fi
}
TEST_EMBED_HEADERS="isotest waittest forktest threadtest mutextest semtest condtest futextest nicetest exit42 orphan argtest envtest libctest maintest systest kaddrtest malloctest errnotest stdiotest fstest cxxabitest alarmtest mmaptest sbrktest"
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


if [ -f $USR/guiprobe.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/guiprobe.c -o $BUILD/guiprobe.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/guiprobe.elf $BUILD/crt0.o $BUILD/guiprobe.o
    verify_user_start $BUILD/guiprobe.elf guiprobe.elf
    python3 _embed_elf.py $BUILD/guiprobe.elf $SRC/include/embed_guiprobe.h guiprobe_elf
    echo "  Embedded: guiprobe.elf"
fi

if [ -f $USR/guicomponenttest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/guicomponenttest.c -o $BUILD/guicomponenttest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/guicomponenttest.elf $BUILD/crt0.o $BUILD/guicomponenttest.o
    verify_user_start $BUILD/guicomponenttest.elf guicomponenttest.elf
    python3 _embed_elf.py $BUILD/guicomponenttest.elf $SRC/include/embed_guicomponenttest.h guicomponenttest_elf
    echo "  Embedded: guicomponenttest.elf"
fi

if [ -f $USR/skia_demo.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/skia_demo.c -o $BUILD/skia_demo.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/skia_demo.elf $BUILD/crt0.o $BUILD/skia_demo.o
    verify_user_start $BUILD/skia_demo.elf skia_demo.elf
    python3 _embed_elf.py $BUILD/skia_demo.elf $SRC/include/embed_skia_demo.h skia_demo_elf
    echo "  Embedded: skia_demo.elf"
fi

if [ -f $USR/v8_shell.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/v8_shell.c -o $BUILD/v8_shell.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/v8_shell.elf $BUILD/crt0.o $BUILD/v8_shell.o
    verify_user_start $BUILD/v8_shell.elf v8_shell.elf
    python3 _embed_elf.py $BUILD/v8_shell.elf $SRC/include/embed_v8_shell.h v8_shell_elf
    echo "  Embedded: v8_shell.elf"
fi

if [ -f $USR/blink_smoke.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/blink_smoke.c -o $BUILD/blink_smoke.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/blink_smoke.elf $BUILD/crt0.o $BUILD/blink_smoke.o
    verify_user_start $BUILD/blink_smoke.elf blink_smoke.elf
    python3 _embed_elf.py $BUILD/blink_smoke.elf $SRC/include/embed_blink_smoke.h blink_smoke_elf
    echo "  Embedded: blink_smoke.elf"
fi

if [ -f $USR/content_shell.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/content_shell.c -o $BUILD/content_shell.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/content_shell.elf $BUILD/crt0.o $BUILD/content_shell.o
    verify_user_start $BUILD/content_shell.elf content_shell.elf
    python3 _embed_elf.py $BUILD/content_shell.elf $SRC/include/embed_content_shell.h content_shell_elf
    echo "  Embedded: content_shell.elf"
fi

if [ -f $USR/browser.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include -I src/shared \
        -c $USR/browser.c -o $BUILD/browser.o
    for tls_src in tls_parser tls_crypto tls_x509 tls_p256 tls_handshake; do
        gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
            -fno-stack-protector -fno-builtin \
            -I $SRC/include -I src/shared \
            -c $SRC/${tls_src}.c -o $BUILD/browser_${tls_src}.o
    done
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include -I src/shared \
        -c $USR/tls_user_compat.c -o $BUILD/browser_tls_user_compat.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/browser.elf \
        $BUILD/crt0.o $BUILD/browser.o $BUILD/browser_tls_user_compat.o \
        $BUILD/browser_tls_parser.o $BUILD/browser_tls_crypto.o \
        $BUILD/browser_tls_x509.o $BUILD/browser_tls_p256.o \
        $BUILD/browser_tls_handshake.o
    verify_user_start $BUILD/browser.elf browser.elf
    python3 _embed_elf.py $BUILD/browser.elf $SRC/include/embed_browser.h browser_elf
    echo "  Embedded: browser.elf"
fi

if [ -f $USR/stickynote.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/stickynote.c -o $BUILD/stickynote.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/stickynote.elf $BUILD/crt0.o $BUILD/stickynote.o
    verify_user_start $BUILD/stickynote.elf stickynote.elf
    python3 _embed_elf.py $BUILD/stickynote.elf $SRC/include/embed_stickynote.h stickynote_elf
    echo "  Embedded: stickynote.elf"
fi

if [ -f $USR/chromium.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/chromium.c -o $BUILD/chromium.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/chromium.elf $BUILD/crt0.o $BUILD/chromium.o
    verify_user_start $BUILD/chromium.elf chromium.elf
    python3 _embed_elf.py $BUILD/chromium.elf $SRC/include/embed_chromium.h chromium_elf
    echo "  Embedded: chromium.elf"
fi

if [ -f $USR/fontprobe.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -c $USR/fontprobe.c -o $BUILD/fontprobe.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/fontprobe.elf $BUILD/crt0.o $BUILD/fontprobe.o
    verify_user_start $BUILD/fontprobe.elf fontprobe.elf
    python3 _embed_elf.py $BUILD/fontprobe.elf $SRC/include/embed_fontprobe.h fontprobe_elf
    echo "  Embedded: fontprobe.elf"
fi

if [ -f $USR/chromiumcaptest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/chromiumcaptest.c -o $BUILD/chromiumcaptest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/chromiumcaptest.elf $BUILD/crt0.o $BUILD/chromiumcaptest.o
    verify_user_start $BUILD/chromiumcaptest.elf chromiumcaptest.elf
    python3 _embed_elf.py $BUILD/chromiumcaptest.elf $SRC/include/embed_chromiumcaptest.h chromiumcaptest_elf
    echo "  Embedded: chromiumcaptest.elf"
fi

if [ -f $USR/fdinherit.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/fdinherit.c -o $BUILD/fdinherit.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/fdinherit.elf $BUILD/crt0.o $BUILD/fdinherit.o
    verify_user_start $BUILD/fdinherit.elf fdinherit.elf
    python3 _embed_elf.py $BUILD/fdinherit.elf $SRC/include/embed_fdinherit.h fdinherit_elf
    echo "  Embedded: fdinherit.elf"
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

if [ -f $USR/cxxabitest.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/crt0.c -o $BUILD/crt0.o
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $SRC/include \
        -c $USR/cxxabitest.c -o $BUILD/cxxabitest.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/cxxabitest.elf $BUILD/crt0.o $BUILD/cxxabitest.o
    verify_user_start $BUILD/cxxabitest.elf cxxabitest.elf
    python3 _embed_elf.py $BUILD/cxxabitest.elf $SRC/include/embed_cxxabitest.h cxxabitest_elf
    echo "  Embedded: cxxabitest.elf"
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

if [ -f $USR/tcc.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $USR/tcc_shim -I $USR -I $SRC/include \
        -c $USR/tcc.c -o $BUILD/tcc.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/tcc.elf $BUILD/crt0.o $BUILD/tcc.o
    verify_user_start $BUILD/tcc.elf tcc.elf
    python3 _embed_elf.py $BUILD/tcc.elf $SRC/include/embed_tcc.h tcc_elf

    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $USR -I $SRC/include \
        -c $USR/openos_tcc_runtime.c -o $BUILD/tcc_crt0.o
    python3 tools/gen_embed_resources.py $SRC/include/embed_tcc_resources.h \
        tcc_res_openos_h=$USR/openos.h \
        tcc_res_tccdefs_h=ports/tinycc/include/tccdefs.h \
        tcc_res_user_ld=$USR/user.ld \
        tcc_res_crt0_o=$BUILD/tcc_crt0.o \
        tcc_res_example_hello_c=$USR/tcc_examples/hello.c \
        tcc_res_runtime_c=$USR/openos_tcc_runtime.c
    echo "  Embedded: tcc.elf and OPENOS TinyCC sysroot resources"
fi

if [ "$OPENOS_TCC_SMOKE" = "1" ] && [ -f $USR/tccsmoke.c ]; then
    gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 \
        -fno-stack-protector -fno-builtin \
        -I $USR -I $SRC/include \
        -c $USR/tccsmoke.c -o $BUILD/tccsmoke.o
    ld -m elf_i386 -T $USR/user.ld -o $BUILD/tccsmoke.elf $BUILD/crt0.o $BUILD/tccsmoke.o
    verify_user_start $BUILD/tccsmoke.elf tccsmoke.elf
    python3 _embed_elf.py $BUILD/tccsmoke.elf $SRC/include/embed_tccsmoke.h tccsmoke_elf
    echo "  Embedded: tccsmoke.elf"
else
    rm -f $SRC/include/embed_tccsmoke.h $BUILD/tccsmoke.o $BUILD/tccsmoke.elf
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

for app in ping ifconfig netstat wget curl; do
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
    $KERNEL_EXTRA_CFLAGS \
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
    -c $SRC/arch_ops.c -o $BUILD/arch_ops.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/platform_ops.c -o $BUILD/platform_ops.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/device.c -o $BUILD/device.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/driver.c -o $BUILD/driver.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/basic_devices.c -o $BUILD/basic_devices.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/i386_arch_ops.c -o $BUILD/i386_arch_ops.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/platform/pc_bios_platform_ops.c -o $BUILD/pc_bios_platform_ops.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/core/mm/pmm.c -o $BUILD/pmm.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/core/mm/vmm.c -o $BUILD/vmm.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/core/mm/heap.c -o $BUILD/heap.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/core/sched/scheduler.c -o $BUILD/scheduler.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/smp.c -o $BUILD/smp.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/core/ipc/syscall.c -o $BUILD/syscall.o

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
    -c $SRC/drivers/virtio.c -o $BUILD/virtio.o

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
    -c $SRC/drivers/virtio_input.c -o $BUILD/virtio_input.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/drivers/virtio_gpu.c -o $BUILD/virtio_gpu.o

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
    -c $SRC/display.c -o $BUILD/display.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/input.c -o $BUILD/input.o

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

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/tls_p256.c -o $BUILD/tls_p256.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/tls_handshake.c -o $BUILD/tls_handshake.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/tls_trust.c -o $BUILD/tls_trust.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/tls_x509.c -o $BUILD/tls_x509.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -Os \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/gui.c -o $BUILD/gui.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/gui_user.c -o $BUILD/gui_user.o

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
    -I $SRC/include -I $SRC/core/fs -I $SRC/core/proc \
    -c $SRC/core/proc/process.c -o $BUILD/process.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/core/proc -I $SRC/core/fs \
    -c $SRC/core/proc/elf_loader.c -o $BUILD/elf_loader.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/core/fs \
    -c $SRC/core/fs/vfs.c -o $BUILD/vfs.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include \
    -c $SRC/core/fs/ramfs.c -o $BUILD/ramfs.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/core/fs \
    -c $SRC/core/fs/tmpfs.c -o $BUILD/tmpfs.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/core/fs \
    -c $SRC/core/fs/ext4.c -o $BUILD/ext4.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/core/fs \
    -c $SRC/core/fs/pfs.c -o $BUILD/pfs.o

gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -O2 \
    -fno-pie -fno-stack-protector -fno-builtin -fno-pic -fno-jump-tables \
    -I $SRC/include -I $SRC/core/fs \
    -c $SRC/core/fs/fat32.c -o $BUILD/fat32.o

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
    $BUILD/arch_ops.o \
    $BUILD/platform_ops.o \
    $BUILD/device.o \
    $BUILD/driver.o \
    $BUILD/basic_devices.o \
    $BUILD/i386_arch_ops.o \
    $BUILD/pc_bios_platform_ops.o \
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
    $BUILD/display.o \
    $BUILD/input.o \
    $BUILD/i18n.o \
    $BUILD/tls_crypto.o \
    $BUILD/tls_parser.o \
    $BUILD/tls_handshake.o \
    $BUILD/tls_trust.o \
    $BUILD/tls_x509.o \
    $BUILD/tls_p256.o \
    $BUILD/gui.o \
    $BUILD/gui_user.o \
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
    $BUILD/virtio.o \
    $BUILD/virtio_blk.o \
    $BUILD/virtio_net.o \
    $BUILD/virtio_input.o \
    $BUILD/virtio_gpu.o \
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
BOOT_LOAD_SECTORS=4096
if [ "$KERNEL_SECTORS" -gt "$BOOT_LOAD_SECTORS" ]; then
    echo "ERROR: kernel.bin is ${KERNEL_SECTORS} sectors, but bootloader loads only ${BOOT_LOAD_SECTORS} sectors."
    echo "Increase bootloader high-memory load chunks or move to a larger disk image before building the image."
    exit 1
fi
echo "  kernel.bin: ${KERNEL_BYTES} bytes (${KERNEL_SECTORS}/${BOOT_LOAD_SECTORS} sectors loaded by bootloader)"

echo "[5/5] Generating disk image..."
dd if=/dev/zero of=$BUILD/openos.img bs=512 count=8192 2>/dev/null
dd if=$BUILD/boot.bin of=$BUILD/openos.img bs=512 count=1 conv=notrunc 2>/dev/null
dd if=$BUILD/kernel.bin of=$BUILD/openos.img bs=512 seek=1 conv=notrunc 2>/dev/null

echo ""
echo "========================================="
echo "  openos Phase 3 Build Successful!"
echo "  Output: target/openos.img"
echo "========================================="
======="
