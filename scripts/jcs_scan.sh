#!/usr/bin/env bash
# γ.5-P3-α: 从现有日志榨取 GP run 的 J-probe 入口 CS 序列
# 决定性判据：GP 前最近 timer 的入口 cs=0x23(正常) 还是 0x33(已污染)
set -u
cd "$(dirname "$0")/.."
OUT=build/log/jcs_scan.txt
: > "$OUT"
for i in $(seq 1 25); do
  f="build/log/ser_f1_r${i}.clean.txt"
  [ -f "$f" ] || continue
  T=$(tr -d '\r' < "$f")
  echo "$T" | grep -qE 'vector=0x0*D .*err=0x0*30' || continue
  echo "==== RUN $i (GP) ====" >> "$OUT"
  # J-probe dump 行：t=.. cpu=.. slot=.. st=.. ktop=.. cs=.. rip=.. rsp=.. ss=.. rfl=..
  echo "$T" | grep -oE 't=0x[0-9A-Fa-f]+ cpu=0x[0-9A-Fa-f]+ slot=0x[0-9A-Fa-f]+ st=0x[0-9A-Fa-f]+ ktop=0x[0-9A-Fa-f]+ cs=0x[0-9A-Fa-f]+ rip=0x[0-9A-Fa-f]+ rsp=0x[0-9A-Fa-f]+ ss=0x[0-9A-Fa-f]+ rfl=0x[0-9A-Fa-f]+' >> "$OUT"
  echo "" >> "$OUT"
done
cat "$OUT"
echo "======== 入口CS 分布（GP run 内所有 J-probe 记录） ========"
grep -oE 'cs=0x[0-9A-Fa-f]+' "$OUT" | sort | uniq -c
