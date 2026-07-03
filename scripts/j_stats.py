#!/usr/bin/env python3
import os
os.chdir('/mnt/e/openos')
for i in range(1, 16):
    p = 'build/log/ser_f1_r%d.clean.txt' % i
    s = open(p, errors='ignore').read()
    gp = s.count('vector=0x000000000000000D')
    jp = s.count('J-probe')
    wm = s.count('wait-multi] got pid=')
    st29 = s.count('stage 29')
    af = s.count('AF-PATTERN')
    print(f'r{i:02d} gp={gp} jprobe={jp} af={af} wm={wm} stg29={st29}')
