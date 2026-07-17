#!/usr/bin/env bash
# openos 图形界面启动脚本
# 打开 QEMU 窗口，挂 virtio-gpu 硬件图形 + 鼠标键盘，可交互操作桌面/锁屏。
# 串口日志 -> logs/run.ser
set -u
QEMU="/mnt/c/Program Files/qemu/qemu-system-x86_64.exe"
OVMF_CODE='C:\Program Files\qemu\share\edk2-x86_64-code.fd'
OVMF_VARS='E:\openos\target\OVMF_VARS.fd'
IMAGE='E:\openos\target\openos-uefi.img'
SERLOG="/mnt/e/openos/logs/run.ser"
mkdir -p /mnt/e/openos/logs
rm -f "$SERLOG"
echo "[run] 启动 openos 图形界面 (virtio-gpu + 键鼠) ; 串口 -> $SERLOG"
echo "[run] 锁屏默认用户 openos，密码 openos"
"$QEMU" -machine pc -cpu qemu64 -m 256M \
  -drive if=pflash,format=raw,unit=0,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,unit=1,file="$OVMF_VARS" \
  -drive file="$IMAGE",format=raw,media=disk,if=ide,index=0 \
  -device virtio-gpu-pci \
  -device virtio-keyboard-pci \
  -device virtio-tablet-pci \
  -boot c \
  -serial file:"$SERLOG" \
  -no-reboot
echo "[run] QEMU 已退出 rc=$?"
