#!/usr/bin/env bash
cd /mnt/e/openos || exit 1
for f in build/log/ser_f1_r*.clean.txt; do
    echo "=== $f ==="
    grep -aE "tick_hits per-CPU" "$f" | tail -3
done
