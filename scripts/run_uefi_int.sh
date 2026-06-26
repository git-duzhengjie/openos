#!/bin/bash
# UEFI 启动 + 异常路径追踪
set +e
cd /mnt/e/openos
SER=/tmp/serial_int.txt
LOG=/tmp/qemu_int.log
rm -f "$SER" "$LOG"

qemu-system-x86_64 \
  -M q35 -m 512 \
  -bios /usr/share/qemu/OVMF.fd \
  -drive format=raw,file=target/openos-uefi.img,if=ide \
  -serial file:"$SER" -display none \
  -d int,cpu_reset -D "$LOG" -no-reboot &
QPID=$!
sleep 12
kill -9 $QPID 2>/dev/null
wait $QPID 2>/dev/null

echo "=== SERIAL ==="
cat "$SER" | tr -d '\000' | sed 's/\x1b\[[0-9;=]*[a-zA-Z]//g' | tail -40
echo "=== INT v=06 (#UD) count ==="
grep -c 'v=06' "$LOG" 2>/dev/null
echo "=== Last 5 exception entries ==="
grep -B1 -A8 'check_exception' "$LOG" | tail -60
