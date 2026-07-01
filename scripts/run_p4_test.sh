#!/bin/bash
# Ad-hoc QEMU smoke for A2.P4 series
set +e
cd /mnt/e/openos
mkdir -p build/log
TAG="${1:-p4a}"
SER="/mnt/e/openos/build/log/serial_${TAG}.txt"
LOG="/mnt/e/openos/build/log/qemu_${TAG}.log"
rm -f "$SER" "$LOG"

timeout 18s qemu-system-x86_64 \
  -M q35 -m 512 \
  -bios /usr/share/qemu/OVMF.fd \
  -drive format=raw,file=target/openos-uefi.img,if=ide \
  -serial file:"$SER" -display none \
  -no-reboot -no-shutdown \
  -d guest_errors,cpu_reset -D "$LOG"

echo "=== SERIAL (${TAG}) ==="
tr -d '\000' <"$SER" | sed 's/\x1b\[[0-9;=]*[a-zA-Z]//g' | tail -100
echo "=== QEMU LOG (${TAG}) ==="
tail -40 "$LOG" 2>/dev/null
