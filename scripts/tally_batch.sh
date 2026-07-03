#!/usr/bin/env bash
cd /mnt/e/openos || exit 1
for f in build/log/ser_f1_r*.clean.txt; do
  n=$(basename "$f" .clean.txt | sed 's/ser_f1_r//')
  gp=$(grep -c 'exception' "$f")
  jp=$(grep -c 'j_probe\|jprobe\|J-probe\|ring\[' "$f")
  printf 'r%-3s excpt=%s jprobe=%s\n' "$n" "$gp" "$jp"
done | sort -V
