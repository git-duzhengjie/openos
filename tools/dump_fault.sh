#!/bin/bash
cd /mnt/e/openos
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/ovmf_vars.fd 2>/dev/null
rm -f /tmp/thread_demo.ser /tmp/qmon.sock
timeout 50 qemu-system-x86_64 \
  -machine q35 -cpu qemu64 -m 512 \
  -drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/ovmf_vars.fd \
  -drive format=raw,file=target/openos-uefi.img \
  -serial file:/tmp/thread_demo.ser \
  -monitor unix:/tmp/qmon.sock,server,nowait \
  -display none -no-reboot 2>/tmp/qemu.err &
QPID=$!
sleep 32
python3 tools/qmon_dump.py > /tmp/qmon_out.txt 2>&1 || echo "dump failed: $?"
sleep 2
kill $QPID 2>/dev/null
wait 2>/dev/null
echo "=== MONITOR OUT ==="
cat /tmp/qmon_out.txt
