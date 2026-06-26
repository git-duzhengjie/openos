#!/bin/bash
# UEFI 启动烟测（单文件 OVMF.fd 版）
set +e
cd /mnt/e/openos
SER=/tmp/serial2.txt
LOG=/tmp/qemu2.log
rm -f "$SER" "$LOG"

qemu-system-x86_64 \
  -M q35 -m 512 \
  -bios /usr/share/qemu/OVMF.fd \
  -drive format=raw,file=target/openos-uefi.img,if=ide \
  -serial file:"$SER" -display none \
  -d guest_errors -D "$LOG" &
QPID=$!
sleep 15
kill -9 $QPID 2>/dev/null
wait $QPID 2>/dev/null

echo "=== SERIAL (清洗后) ==="
cat "$SER" | tr -d '\000' | sed 's/\x1b\[[0-9;=]*[a-zA-Z]//g' | tail -30
echo "=== QEMU LOG ==="
tail -20 "$LOG" 2>/dev/null
