#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${OPENOS_GDB_ARCH:-i386}"
BUILD_FIRST="${OPENOS_GDB_BUILD:-1}"
GDB_PORT="${OPENOS_GDB_PORT:-1234}"
QEMU_BIN="${OPENOS_QEMU_BIN:-qemu-system-i386}"
GDB_BIN="${OPENOS_GDB_BIN:-gdb}"
IMAGE_PATH="${OPENOS_GDB_IMAGE:-$ROOT_DIR/target/openos.img}"
KERNEL_ELF="${OPENOS_GDB_KERNEL:-$ROOT_DIR/target/kernel.elf}"
GDB_SCRIPT="${OPENOS_GDB_SCRIPT:-$ROOT_DIR/scripts/openos.gdb}"
EXTRA_QEMU_ARGS=()
OPEN_GDB=1

usage() {
    cat <<USAGE
Usage: $0 [--no-build] [--port PORT] [--image PATH] [--kernel PATH] [--no-gdb] [-- QEMU_ARGS...]

Builds OpenOS if requested, starts QEMU paused with a GDB remote stub, and
optionally opens GDB with scripts/openos.gdb.

Environment overrides:
  OPENOS_GDB_BUILD    Build before debugging, default: 1
  OPENOS_GDB_PORT     GDB TCP port, default: 1234
  OPENOS_GDB_IMAGE    Disk image, default: target/openos.img
  OPENOS_GDB_KERNEL   Kernel ELF symbols, default: target/kernel.elf
  OPENOS_GDB_SCRIPT   GDB command file, default: scripts/openos.gdb
  OPENOS_QEMU_BIN     QEMU binary, default: qemu-system-i386
  OPENOS_GDB_BIN      GDB binary, default: gdb
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)
            BUILD_FIRST=0
            shift
            ;;
        --port)
            GDB_PORT="$2"
            shift 2
            ;;
        --image)
            IMAGE_PATH="$2"
            shift 2
            ;;
        --kernel)
            KERNEL_ELF="$2"
            shift 2
            ;;
        --no-gdb)
            OPEN_GDB=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            EXTRA_QEMU_ARGS+=("$@")
            break
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ "$ARCH" != "i386" ]]; then
    echo "GDB helper currently supports the bootable i386 image only; got OPENOS_GDB_ARCH=$ARCH" >&2
    exit 2
fi

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "QEMU binary not found: $QEMU_BIN" >&2
    exit 127
fi

if [[ "$OPEN_GDB" = "1" ]] && ! command -v "$GDB_BIN" >/dev/null 2>&1; then
    echo "GDB binary not found: $GDB_BIN" >&2
    exit 127
fi

if [[ "$BUILD_FIRST" != "0" ]]; then
    (cd "$ROOT_DIR" && bash build.sh)
fi

if [[ ! -s "$IMAGE_PATH" ]]; then
    echo "OpenOS image not found or empty: $IMAGE_PATH" >&2
    exit 1
fi

if [[ ! -s "$KERNEL_ELF" ]]; then
    echo "Kernel ELF with symbols not found or empty: $KERNEL_ELF" >&2
    exit 1
fi

QEMU_ARGS=(
    -S
    -gdb "tcp::$GDB_PORT"
    -no-reboot
    -no-shutdown
    -serial stdio
    -drive "file=$IMAGE_PATH,format=raw,if=ide"
    -m 128M
)

if [[ ${#EXTRA_QEMU_ARGS[@]} -gt 0 ]]; then
    QEMU_ARGS+=("${EXTRA_QEMU_ARGS[@]}")
fi

echo "[GDB] starting QEMU on tcp::$GDB_PORT"
"$QEMU_BIN" "${QEMU_ARGS[@]}" &
QEMU_PID=$!
cleanup() {
    if kill -0 "$QEMU_PID" >/dev/null 2>&1; then
        kill "$QEMU_PID" >/dev/null 2>&1 || true
        wait "$QEMU_PID" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

sleep 1
if ! kill -0 "$QEMU_PID" >/dev/null 2>&1; then
    echo "QEMU exited before GDB could attach" >&2
    wait "$QEMU_PID" || true
    exit 1
fi

if [[ "$OPEN_GDB" = "1" ]]; then
    "$GDB_BIN" \
        -q \
        -ex "file $KERNEL_ELF" \
        -ex "target remote :$GDB_PORT" \
        -x "$GDB_SCRIPT"
else
    echo "[GDB] QEMU PID: $QEMU_PID"
    echo "[GDB] attach manually: $GDB_BIN -q -ex 'file $KERNEL_ELF' -ex 'target remote :$GDB_PORT'"
    wait "$QEMU_PID"
fi
