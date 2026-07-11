#!/bin/bash
cd /mnt/e/openos
F=target/x86_64/kernel64.elf
for a in ffffffff800077ee ffffffff800046a0 ffffffff800044b5; do
  echo -n "0x$a -> "
  addr2line -f -e "$F" "0x$a" 2>/dev/null | tr '\n' ' '
  echo
done
echo "--- poison 0xAF grep ---"
grep -rn '0xAF\|0xafafafaf\|poison' src/arch/x86_64/kernel/sched64.c | head
