#!/usr/bin/env bash
cd /mnt/e/openos
K=target/x86_64/kernel64.elf
echo "=== file ==="; file "$K"
echo "=== which func: 0x...80001E4A and 0x...80001F15 ==="
nm -n "$K" 2>/dev/null | awk '{a=strtonum("0x"$1)} a>=0xffffffff80001c00 && a<=0xffffffff80002100 {print $0}'
echo "=== disasm 0x80001E00 .. 0x80001F60 ==="
objdump -d "$K" 2>/dev/null | awk '/^ffffffff80001e00:/{p=1} p{print} /^ffffffff80001f60:/{exit}'
