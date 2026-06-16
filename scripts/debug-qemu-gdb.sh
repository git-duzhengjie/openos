#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU_BIN="${OPENOS_QEMU_BIN:-qemu-system-i386}"
MEMORY="${OPENOS_QEMU_MEM:-512M}"
IMAGE="${OPENOS_QEMU_IMAGE:-$ROOT_DIR/target/openos.img}"
GDB_PORT="${OPENOS_GDB_PORT:-1234}"
DISPLAY_MODE="${OPENOS_QEMU_DISPLAY:-none}"

cd "$ROOT_DIR"

if [[ ! -f "$IMAGE" ]]; then
    echo "Image not found: $IMAGE" >&2
    echo "Run: bash build.sh" >&2
    exit 1
fi

cat <<MSG
[OpenOS] Starting QEMU paused for GDB.
[OpenOS] In another terminal run:
  gdb -q -x scripts/gdb-openos.gdb
  (gdb) openos-connect
  (gdb) openos-break-boot
  (gdb) continue
MSG

exec "$QEMU_BIN" \
    -drive "format=raw,file=$IMAGE" \
    -m "$MEMORY" \
    -serial stdio \
    -display "$DISPLAY_MODE" \
    -S \
    -gdb "tcp::$GDB_PORT"
