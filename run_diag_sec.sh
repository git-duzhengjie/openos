#!/usr/bin/env bash
# M6.4 diag: 用带 SMEP/SMAP 能力的 CPU 启动, 验证 security64 探测 + SMEP 启用
# + 栈保护 canary 初始化。Serial -> logs/diag_sec.ser。
# Single uefi disk ONLY (see run_diag_ring3.sh note re: OVMF disk selection).
set -u
QEMU="/mnt/c/Program Files/qemu/qemu-system-x86_64.exe"
OVMF_CODE='C:\Program Files\qemu\share\edk2-x86_64-code.fd'
OVMF_VARS='E:\openos\target\OVMF_VARS.fd'
IMAGE='E:\openos\target\openos-uefi.img'
SERLOG="/mnt/e/openos/logs/diag_sec.ser"
mkdir -p /mnt/e/openos/logs
rm -f "$SERLOG"
TIMEOUT="${1:-30}"
echo "[diag-sec] headless; -cpu qemu64,+smep,+smap ; serial -> $SERLOG ; timeout ${TIMEOUT}s"
timeout "${TIMEOUT}" "$QEMU" -machine pc -cpu qemu64,+smep,+smap -m 256M \
  -drive if=pflash,format=raw,unit=0,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,unit=1,file="$OVMF_VARS" \
  -drive file="$IMAGE",format=raw,media=disk,if=ide,index=0 \
  -boot c \
  -serial stdio \
  -display none -no-reboot \
  -d guest_errors > "$SERLOG" 2>/dev/null
echo "[diag-sec] QEMU exited rc=$?"
echo "[diag-sec] serial log size: $(wc -c < "$SERLOG" 2>/dev/null) bytes"
echo "[diag-sec] ---- security lines ----"
tr -d '\0\r' < "$SERLOG" | grep -i '\[sec\]' || echo "(none)"
