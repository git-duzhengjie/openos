#!/usr/bin/env bash
cd /mnt/e/openos
for i in 2 20; do
  f=build/log/ser_f1_r${i}.clean.txt
  [ -f "$f" ] || continue
  echo "===== run $i pre_iret lines ====="
  tr -d '\r' < "$f" | grep -aoE 'pre_iret[^]]*SS=0x[0-9A-Fa-f]+' | tail -5
  echo "----- exception line -----"
  tr -d '\r' < "$f" | grep -aoE 'vector=0x0*D err=0x0*30 rip=0x[0-9A-Fa-f]+ cs=0x[0-9A-Fa-f]+' | head -2
done
