#!/usr/bin/env bash
# Network verification diag: single uefi disk + virtio-net-pci (user/NAT) + pcap.
# Exercises kernel64 M1.3~M1.6 netstack self-tests (ARP/ICMP/DHCP/DNS/HTTP).
# Serial -> logs/diag_net.ser ; pcap -> logs/diag_net.pcap
set -u
QEMU="/mnt/c/Program Files/qemu/qemu-system-x86_64.exe"
OVMF_CODE='C:\Program Files\qemu\share\edk2-x86_64-code.fd'
OVMF_VARS='E:\openos\target\OVMF_VARS.fd'
IMAGE='E:\openos\target\openos-uefi.img'
SERLOG="/mnt/e/openos/logs/diag_net.ser"
PCAP='E:\openos\logs\diag_net.pcap'
mkdir -p /mnt/e/openos/logs
rm -f "$SERLOG" /mnt/e/openos/logs/diag_net.pcap
TIMEOUT="${1:-40}"
echo "[diag-net] headless; virtio-net-pci (NAT) attached; serial -> $SERLOG ; timeout ${TIMEOUT}s"
timeout "${TIMEOUT}" "$QEMU" -machine pc -cpu qemu64 -m 256M \
  -drive if=pflash,format=raw,unit=0,file="$OVMF_CODE",readonly=on \
  -drive if=pflash,format=raw,unit=1,file="$OVMF_VARS" \
  -drive file="$IMAGE",format=raw,media=disk,if=ide,index=0 \
  -netdev user,id=n0 \
  -device virtio-net-pci,netdev=n0 \
  -object filter-dump,id=f0,netdev=n0,file="$PCAP" \
  -boot c \
  -serial stdio \
  -display none -no-reboot \
  -d guest_errors > "$SERLOG" 2>/dev/null
echo "[diag-net] QEMU exited rc=$?"
echo "[diag-net] serial log size: $(wc -c < "$SERLOG" 2>/dev/null) bytes"
echo "[diag-net] ---- net lines ----"
tr -d '\0\r' < "$SERLOG" | grep -iE 'net|arp|icmp|ping|dhcp|dns|http|tcp|eth0' || echo "(none)"
