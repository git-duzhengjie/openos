#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_PATH="${OPENOS_QEMU_IMAGE:-$ROOT_DIR/target/openos.img}"
LOG_PATH="${OPENOS_QEMU_LOG:-$ROOT_DIR/target/qemu-smoke.log}"
TIMEOUT_SECONDS="${OPENOS_QEMU_TIMEOUT:-12}"
BUILD_IMAGE="${OPENOS_QEMU_BUILD:-1}"
QEMU_BIN="${OPENOS_QEMU_BIN:-qemu-system-i386}"

usage() {
    cat <<USAGE
Usage: $0 [--no-build] [--timeout SECONDS] [--image PATH] [--log PATH]

Runs an OpenOS QEMU smoke regression test.

Environment overrides:
  OPENOS_QEMU_BIN      QEMU binary, default: qemu-system-i386
  OPENOS_QEMU_IMAGE    Disk image path, default: target/openos.img
  OPENOS_QEMU_LOG      Serial log path, default: target/qemu-smoke.log
  OPENOS_QEMU_TIMEOUT  Run timeout seconds, default: 12
  OPENOS_QEMU_BUILD    Build image first, default: 1
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)
            BUILD_IMAGE=0
            shift
            ;;
        --timeout)
            TIMEOUT_SECONDS="$2"
            shift 2
            ;;
        --image)
            IMAGE_PATH="$2"
            shift 2
            ;;
        --log)
            LOG_PATH="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "QEMU binary not found: $QEMU_BIN" >&2
    exit 127
fi

if [[ "$BUILD_IMAGE" != "0" ]]; then
    (cd "$ROOT_DIR" && bash build.sh)
fi

if [[ ! -s "$IMAGE_PATH" ]]; then
    echo "OpenOS image not found or empty: $IMAGE_PATH" >&2
    exit 1
fi

mkdir -p "$(dirname "$LOG_PATH")"
rm -f "$LOG_PATH"

echo "[QEMU] booting $IMAGE_PATH for ${TIMEOUT_SECONDS}s"
set +e
timeout --preserve-status --kill-after=3s "${TIMEOUT_SECONDS}s" \
    "$QEMU_BIN" \
    -no-reboot \
    -no-shutdown \
    -display none \
    -serial "file:$LOG_PATH" \
    -drive "file=$IMAGE_PATH,format=raw,if=ide" \
    -m 128M
status=$?
set -e

# timeout can return 124/143 when OpenOS keeps running; WSL-hosted Windows QEMU may return 15.
if [[ $status -ne 0 && $status -ne 15 && $status -ne 124 && $status -ne 143 ]]; then
    echo "QEMU exited with unexpected status: $status" >&2
    if [[ -f "$LOG_PATH" ]]; then
        tail -80 "$LOG_PATH" >&2 || true
    fi
    exit "$status"
fi

if [[ ! -s "$LOG_PATH" ]]; then
    echo "QEMU serial log is empty: $LOG_PATH" >&2
    exit 1
fi

if ! grep -Eq "openos|OpenOS|Kernel|\[OK\]|APIC|SMP" "$LOG_PATH"; then
    echo "QEMU serial log does not contain expected OpenOS boot markers" >&2
    tail -120 "$LOG_PATH" >&2 || true
    exit 1
fi

echo "[QEMU] smoke test passed"
tail -40 "$LOG_PATH" || true
