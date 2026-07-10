#!/bin/bash
# M5.2d 端到端验证：抓 thread_demo 完整输出（等待 join 完成或超时）
cd /mnt/e/openos
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/ovmf_vars.fd 2>/dev/null
rm -f /tmp/td_run.ser
qemu-system-x86_64 \
  -machine q35 -cpu qemu64 -m 512 \
  -drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/ovmf_vars.fd \
  -drive format=raw,file=target/openos-uefi.img \
  -serial file:/tmp/td_run.ser \
  -display none -no-reboot 2>/tmp/qemu.err &
QPID=$!
for i in $(seq 1 45); do
  sleep 1
  if tr -d '\0' < /tmp/td_run.ser 2>/dev/null | grep -qE 'thread_demo. (all workers joined|DONE|PASS|FAIL|counter)'; then
    sleep 1; break
  fi
done
kill -9 $QPID 2>/dev/null
cp /tmp/td_run.ser /mnt/e/openos/logs/td_run.txt
echo "=== thread_demo related output ==="
tr -d '\0\r' < /tmp/td_run.ser | grep -niE 'thread_demo|worker|counter|join|PANIC|vector=0x0000000000000' | tail -40
