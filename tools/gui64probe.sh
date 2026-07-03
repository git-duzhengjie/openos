#!/bin/bash
# gui64 移植探测脚本：编译全部 gui 源 + fb64 + shims，输出缺失符号
set -e
cd /mnt/e/openos
OUT=build/gui64probe
mkdir -p "$OUT"
CF="-m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nostdlib -Wall -O2 -fno-pic -fno-pie -fno-stack-protector -fno-builtin -I src/kernel/include -I src/arch/x86_64/include"

echo "=== 编译 i386 GUI 源 (x86_64 目标) ==="
for f in gui gui_user i18n font window_manager; do
  gcc $CF -c src/kernel/$f.c -o "$OUT/$f.o" 2>"$OUT/$f.err" && echo "  OK  $f" || { echo "  FAIL $f"; head -20 "$OUT/$f.err"; }
done

echo "=== 编译 x86_64 后端/桩 ==="
gcc $CF -c src/arch/x86_64/gui64/framebuffer64.c -o "$OUT/fb64.o" 2>"$OUT/fb64.err" && echo "  OK  fb64" || { echo "  FAIL fb64"; head -20 "$OUT/fb64.err"; }
gcc $CF -c src/arch/x86_64/gui64/gui64_shims.c -o "$OUT/shims.o" 2>"$OUT/shims.err" && echo "  OK  shims" || { echo "  FAIL shims"; head -20 "$OUT/shims.err"; }

echo "=== 符号分析 ==="
cd "$OUT"
for f in gui gui_user i18n font window_manager fb64 shims; do nm $f.o 2>/dev/null; done | grep ' U ' | awk '{print $2}' | sort -u > allundef.txt
for f in gui gui_user i18n font window_manager fb64 shims; do nm $f.o 2>/dev/null; done | grep -E ' [TDBRtdbr] ' | awk '{print $3}' | sort -u > allprov.txt
echo "--- undef 总数 ---"; wc -l < allundef.txt
echo "--- 仍缺符号（需 x86_64 内核提供或补桩）---"
comm -23 allundef.txt allprov.txt
