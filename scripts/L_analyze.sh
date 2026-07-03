#!/usr/bin/env bash
cd /mnt/e/openos
for i in "$@"; do
  echo "=== run$i ==="
  grep -B1 -A3 'vector=0x000000000000000D' build/log/ser_f1_r${i}.clean.txt | head -10
  echo "--- J-probe dump (if any) ---"
  grep -A9 'J-probe last' build/log/ser_f1_r${i}.clean.txt | head -12
done
