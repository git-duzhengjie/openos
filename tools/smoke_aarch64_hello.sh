#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

./build.sh aarch64

AARCH64_QEMU=${AARCH64_QEMU:-qemu-system-aarch64}
if ! command -v "$AARCH64_QEMU" >/dev/null 2>&1; then
  echo "ERROR: $AARCH64_QEMU not found. Run tools/install_aarch64_toolchain_ubuntu.sh first or set AARCH64_QEMU." >&2
  exit 1
fi

LOG=${OPENOS_AARCH64_SMOKE_LOG:-/tmp/openos-aarch64-hello.log}
rm -f "$LOG"

timeout 12s "$AARCH64_QEMU" \
  -M virt \
  -cpu cortex-a57 \
  -nographic \
  -kernel build/aarch64/openos-aarch64.elf \
  >"$LOG" 2>&1 || true

tail -120 "$LOG"

if grep -q "A5.5: hello64 ELF staged for EL0" "$LOG" || \
   grep -q "hello64 from OpenOS aarch64 EL0" "$LOG"; then
  echo "AARCH64_HELLO_SMOKE_OK"
  exit 0
fi

echo "AARCH64_HELLO_SMOKE_FAILED: expected hello64 staging/output marker not found in $LOG" >&2
exit 1
