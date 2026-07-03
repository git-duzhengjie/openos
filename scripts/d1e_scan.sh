#!/bin/bash
cd "$(dirname "$0")/.." || exit 1
for i in $(seq 1 20); do
    f=build/log/ser_f1_r${i}.clean.txt
    [ -f "$f" ] || continue
    n=$(grep -ac 'vector=' "$f")
    if [ "$n" -gt 0 ]; then
        echo "=================== run$i GP ==================="
        grep -aE 'vector=|err_code=|RIP=|CS=|RSP=|SS=|GDTR|GDT\[|TSS\.|IST[1-7]=' "$f" | head -20
        echo
    fi
done
