#!/usr/bin/env bash
# M6.4 diag: headless run with a virtio-gpu-pci device attached, so the
# virtio_gpu_init() bring-up path (modern transport + 2D pipeline) is
# actually exercised. Serial -> logs/diag_gpu.ser.
# Single uefi disk ONLY (see run_diag_ring3.sh note re: OVMF disk selection).
set -u
QEMU="/mnt/c/Program Files/qemu/qemu-system-x86_64.exe"
OVMF_CODE='C:\Program Files\qemu\share\edk2-x86_64-code.fd'
OVMF_VARS='E:\openos\target\OVMF_VARS.fd'
IMAGE='E:\openos\target\openos-uefi.img'
SERLOG="/mnt/e/openos/logs/diag_gpu.ser"
mkdir -p /mnt/e/openos/logs
rm -f "$SERLOG"
TIMEOUT="${1:-30}"
echo "[diag-gpu] headless; virtio-gpu-pci attached; serial -> $SERLOG ; timeout ${TIMEOUT}s"
timeout "${TIMEOUT}" "$QEMU" -machine pc -cpu qemu64 -m 256M \
  -drive if=pflash,format=raw,unit=0,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,unit=1,file="$OVMF_VARS" \
  -drive file="$IMAGE",format=raw,media=disk,if=ide,index=0 \
  -device virtio-gpu-pci \
  -boot c \
  -serial stdio \
  -display none -no-reboot \
  -d guest_errors > "$SERLOG" 2>/dev/null
echo "[diag-gpu] QEMU exited rc=$?"
echo "[diag-gpu] serial log size: $(wc -c < "$SERLOG" 2>/dev/null) bytes"
echo "[diag-gpu] ---- virtio-gpu lines ----"
tr -d '\0\r' < "$SERLOG" | grep -i 'virtio-gpu\|virtio-modern' || echo "(none)"
