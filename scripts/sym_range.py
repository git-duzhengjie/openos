#!/usr/bin/env python3
import re
lo, hi = 0xffffffff80003800, 0xffffffff80004400
with open('logs/nm.txt') as f:
    for ln in f:
        m = re.match(r'([0-9a-f]+)\s+([tT])\s+(\S+)', ln)
        if m:
            a = int(m.group(1),16)
            if lo <= a <= hi:
                print(ln.strip())
