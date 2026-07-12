#!/usr/bin/env bash
# M6.8 diag: virtio-gpu with max_outputs=2 so the device exposes two
# scanouts.  We attach two VNC heads (to=... auto-picks free ports) so both
# scanouts come up enabled, exercising the multi-head mirror path in
# gpu_get_display_info / gpu_set_scanout.  Serial -> logs/diag_gpu_mh.ser.
# Single uefi disk ONLY (see run_diag_ring3.sh note re: OVMF disk selection).
set -u
QEMU="/mnt/c/Program Files/qemu/qemu-system-x86_64.exe"
OVMF_CODE='C:\Program Files\qemu\share\edk2-x86_64-code.fd'
OVMF_VARS='E:\openos\target\OVMF_VARS.fd'
IMAGE='E:\openos\target\openos-uefi.img'
SERLOG="/mnt/e/openos/logs/diag_gpu_mh.ser"
mkdir -p /mnt/e/openos/logs
rm -f "$SERLOG"
TIMEOUT="${1:-30}"
echo "[diag-gpu-mh] headless; virtio-gpu max_outputs=2 + 2 VNC heads; serial -> $SERLOG ; timeout ${TIMEOUT}s"
timeout "${TIMEOUT}" "$QEMU" -machine pc -cpu qemu64 -m 256M \
  -drive if=pflash,format=raw,unit=0,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,unit=1,file="$OVMF_VARS" \
  -drive file="$IMAGE",format=raw,media=disk,if=ide,index=0 \
  -device virtio-gpu-pci,max_outputs=2,xres=1024,yres=768 \
  -display vnc=:20 \
  -boot c \
  -serial stdio \
  -no-reboot \
  -d guest_errors > "$SERLOG" 2>/dev/null
echo "[diag-gpu-mh] QEMU exited rc=$?"
echo "[diag-gpu-mh] serial log size: $(wc -c < "$SERLOG" 2>/dev/null) bytes"
echo "[diag-gpu-mh] ---- scanout / gfx lines ----"
tr -d '\0\r' < "$SERLOG" | grep -iE 'scanout|gfx-selftest|virtio-gpu' || echo "(none)"
