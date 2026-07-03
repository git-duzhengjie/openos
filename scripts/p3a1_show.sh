#!/usr/bin/env bash
# γ.5-P3-α1: dump exception + pre_iret + irq0_iret snapshots for GP runs
set -u
cd "$(dirname "$0")/.."
for r in "$@"; do
  f="build/log/ser_f1_r${r}.clean.txt"
  [ -f "$f" ] || { echo "MISS $f"; continue; }
  echo "===== run $r ====="
  awk 'BEGIN{RS="\r?\n"}
       /exception|pre_iret|irq0_iret|jprobe|J-probe.*header|ring\[0\]/ {print}' "$f" \
    | head -30
done
