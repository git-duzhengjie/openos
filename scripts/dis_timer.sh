#!/usr/bin/env bash
cd /mnt/e/openos
K=target/x86_64/kernel64.elf
echo "===== irq0 / lapic_timer / lapic_resched / isr_common ====="
objdump -d "$K" 2>/dev/null | awk '/<x86_64_irq0>:/{p=1} p{print} /<arch_x86_64_ap_idle_entry>:/{exit}'
