#!/bin/bash
set -u
cd /mnt/e/openos
cp -n /usr/share/OVMF/OVMF_VARS_4M.fd target/OVMF_VARS_wsl.fd 2>/dev/null || true
timeout 15 qemu-system-x86_64 -machine q35 -cpu qemu64 -m 512 \
  -drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=target/OVMF_VARS_wsl.fd \
  -drive format=raw,file=target/openos-uefi.img,if=ide \
  -nographic -serial mon:stdio -display none 2>&1 | tee /tmp/openos-boot.log | grep -E 'selftest|gesture|mt-|PASS|FAIL' | head -40
echo "--- tail ---"
tail -20 /tmp/openos-boot.log
