#!/usr/bin/env bash
cd "$(dirname "$0")/.."
for r in "$@"; do
  f="build/log/ser_f1_r${r}.clean.txt"
  [ -f "$f" ] || { echo "MISS $f"; continue; }
  echo "===== run $r ====="
  grep -a 'exception\|pre_iret\|irq0_iret\|vector=\|err_code' "$f" | head -20
done
