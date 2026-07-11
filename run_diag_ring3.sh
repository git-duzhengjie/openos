#!/usr/bin/env bash
# M5.2e diag: headless single-core ring3 run, serial -> stdout -> logs/diag.ser
# NOTE: qemu.exe is a Windows binary -> all file paths must be Windows-style.
set -u
QEMU="/mnt/c/Program Files/qemu/qemu-system-x86_64.exe"
OVMF_CODE='C:\Program Files\qemu\share\edk2-x86_64-code.fd'
OVMF_VARS='E:\openos\target\OVMF_VARS.fd'
IMAGE='E:\openos\target\openos-uefi.img'
# NOTE(M5.2e): DO NOT attach data/fat disks here. Attaching multiple IDE disks
# made OVMF boot the WRONG disk (a stale firmware/image), silently running the
# OLD hello_fork path instead of the new thread_demo. Single uefi disk ONLY.
SERLOG="/mnt/e/openos/logs/diag.ser"
mkdir -p /mnt/e/openos/logs
rm -f "$SERLOG"
TIMEOUT="${1:-25}"
echo "[diag] headless single-core; serial -> $SERLOG ; timeout ${TIMEOUT}s"
timeout "${TIMEOUT}" "$QEMU" -machine pc -cpu qemu64 -m 256M \
  -drive if=pflash,format=raw,unit=0,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,unit=1,file="$OVMF_VARS" \
  -drive file="$IMAGE",format=raw,media=disk,if=ide,index=0 \
  -boot c \
  -serial stdio \
  -display none -no-reboot \
  -d guest_errors > "$SERLOG" 2>/dev/null
echo "[diag] QEMU exited rc=$?"
echo "[diag] serial log size: $(wc -c < "$SERLOG" 2>/dev/null) bytes"
