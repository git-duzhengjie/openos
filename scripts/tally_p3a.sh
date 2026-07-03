#!/usr/bin/env bash
# γ.5-P3-α 对照表 v2：正确处理 \r + 字段交织
set -u
cd "$(dirname "$0")/.."
OUT=build/log/p3a_tally.txt
: > "$OUT"
printf "%-4s %-4s %-6s %-6s %-14s %-8s %-8s\n" RUN GP uC1 kC1 EXC_RIP preSS preCS >> "$OUT"
n=0; gpn=0; un=0; ss30=0
for i in $(seq 1 25); do
  f="build/log/ser_f1_r${i}.clean.txt"
  [ -f "$f" ] || continue
  T=$(tr -d '\r' < "$f")
  gp="no"
  echo "$T" | grep -qE 'vector=0x0*D .*err=0x0*30' && gp="YES"
  # exception rip 片段（可能被交织截断）
  exc=$(echo "$T" | grep -oE 'vector=0x0*D err=0x0*30 rip=0x[0-9A-Fa-f]+' | head -1 | grep -oE 'rip=0x[0-9A-Fa-f]+' | sed 's/rip=//')
  [ -z "$exc" ] && exc="-"
  # pre_iret frame
  pre=$(echo "$T" | grep -oE 'pre_iret RIP=0x[0-9A-Fa-f]+ CS=0x[0-9A-Fa-f]+ RFLAGS=0x[0-9A-Fa-f]+ RSP=0x[0-9A-Fa-f]+ SS=0x[0-9A-Fa-f]+' | head -1)
  pss=$(echo "$pre" | grep -oE 'SS=0x[0-9A-Fa-f]+' | sed 's/SS=0x0*//'); [ -z "$pss" ] && pss="-"
  pcs=$(echo "$pre" | grep -oE 'CS=0x[0-9A-Fa-f]+' | sed 's/CS=0x0*//'); [ -z "$pcs" ] && pcs="-"
  # 最后一次 cpu1 tick
  c1=$(echo "$T" | grep -oE 'cpu0x0*1=u:0x[0-9A-Fa-f]+,k:0x[0-9A-Fa-f]+' | tail -1)
  u1=$(echo "$c1" | grep -oE 'u:0x[0-9A-Fa-f]+' | sed 's/u:0x0*//'); [ -z "$u1" ] && u1=0
  k1=$(echo "$c1" | grep -oE 'k:0x[0-9A-Fa-f]+' | sed 's/k:0x0*//'); [ -z "$k1" ] && k1=0
  [ "$gp" = "YES" ] && gpn=$((gpn+1))
  ud=$(printf "%d" "0x${u1}" 2>/dev/null || echo 0)
  [ "$ud" -ge 1 ] && un=$((un+1))
  [ "$pss" = "30" ] && ss30=$((ss30+1))
  printf "%-4s %-4s %-6s %-6s %-14s %-8s %-8s\n" "$i" "$gp" "0x$u1" "0x$k1" "$exc" "$pss" "$pcs" >> "$OUT"
  n=$((n+1))
done
{ echo "---- SUMMARY ----"; echo "runs=$n gp=$gpn u1>=1=$un preSS==0x30=$ss30"; } >> "$OUT"
cat "$OUT"
