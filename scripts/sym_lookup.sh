#!/usr/bin/env bash
set -e
ELF=target/x86_64/kernel64.elf
for a in "$@"; do
  printf "0x%s -> " "$a"
  nm "$ELF" | sort | awk -v t="0x$a" '
    BEGIN{addr=strtonum(t)}
    {a=strtonum("0x"$1); if (a>0 && a<=addr && $2 ~ /[tT]/) {prev=$3; prevaddr=$1}}
    END{printf "%s (+0x%x)\n", prev, addr-strtonum("0x"prevaddr)}'
done
