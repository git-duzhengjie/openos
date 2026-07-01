#!/usr/bin/env bash
# A2.P2 wait/waitpid smoke: run QEMU + capture serial, then grep interesting lines.
set +e
cd "$(dirname "$0")/.."
mkdir -p build/log
SER="build/log/serial_a2p2_wait_cr3fix.txt"
rm -f "$SER"
timeout --preserve-status --kill-after=3s 25s \
  qemu-system-x86_64 \
    -M q35 -m 512 \
    -bios /usr/share/qemu/OVMF.fd \
    -drive format=raw,file=target/openos-uefi.img,if=ide \
    -serial "file:$SER" \
    -display none \
    -no-reboot -no-shutdown \
    >/tmp/qemu.stderr 2>&1
echo "EXIT=$?"
echo "--- stderr ---"
tail -20 /tmp/qemu.stderr
echo "--- serial tail (filtered) ---"
tr -d '\000' <"$SER" | sed 's/\x1b\[[0-9;=]*[a-zA-Z]//g' \
  | grep -aE 'hello64|\[fork|\[wait|\[do_exit|\[exit|BUG|#PF|#GP|panic|reboot|PASS|FAIL' \
  | tail -80
echo "--- serial last 40 lines (raw) ---"
tr -d '\000' <"$SER" | sed 's/\x1b\[[0-9;=]*[a-zA-Z]//g' | tail -40
