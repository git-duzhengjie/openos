#!/usr/bin/env python3
import re, sys, subprocess
addrs = [int(a,16) for a in sys.argv[1:]]
syms = []
p = subprocess.run(['nm', 'target/x86_64/kernel64.elf'], capture_output=True, text=True)
for ln in p.stdout.split('\n'):
    m = re.match(r'([0-9a-f]+)\s+[tT]\s+(\S+)', ln)
    if m:
        syms.append((int(m.group(1),16), m.group(2)))
syms.sort()
for a in addrs:
    best = None
    for s in syms:
        if s[0] <= a: best = s
        else: break
    if best:
        print(f'{a:016x} -> {best[1]} +0x{a-best[0]:x}')
    else:
        print(f'{a:016x} -> ???')
