#!/bin/bash
# UEFI 启动烟测 — SMP 多核版 (Step G.4)
set +e
cd /mnt/e/openos
SER=/tmp/serial_smp.txt
LOG=/tmp/qemu_smp.log
rm -f "$SER" "$LOG"

SMP_N="${SMP_N:-2}"

qemu-system-x86_64 \
  -M q35 -m 512 -smp "$SMP_N" \
  -bios /usr/share/qemu/OVMF.fd \
  -drive format=raw,file=target/openos-uefi.img,if=ide \
  -serial file:"$SER" -display none \
  -d guest_errors -D "$LOG" &
QPID=$!
sleep 15
kill -9 $QPID 2>/dev/null
wait $QPID 2>/dev/null

echo "=== SERIAL (清洗后) ==="
cat "$SER" | tr -d '\000' | sed 's/\x1b\[[0-9;=]*[a-zA-Z]//g' | tail -60
echo "=== QEMU LOG ==="
tail -20 "$LOG" 2>/dev/null
