#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# 在门铃之前 dump 刚写入的 TRB 内存内容, 确认 QEMU 能读到有效 Normal TRB
f = 'src/arch/x86_64/gui64/xhci64.c'
s = open(f, encoding='utf-8').read()

old = '''      early_console64_write("\\n"); }
    xhci_ring_dev_doorbell(d->slot_id, ring_phys_dci);'''
new = '''      early_console64_write("\\n");
      volatile uint32_t *tp = (volatile uint32_t *)trb;
      early_console64_write("[trb-mem] w0="); early_console64_write_hex64(tp[0]);
      early_console64_write(" w1="); early_console64_write_hex64(tp[1]);
      early_console64_write(" w2="); early_console64_write_hex64(tp[2]);
      early_console64_write(" w3="); early_console64_write_hex64(tp[3]);
      early_console64_write("\\n"); }
    xhci_ring_dev_doorbell(d->slot_id, ring_phys_dci);'''

cnt = s.count(old)
print('matches:', cnt)
assert cnt == 1, 'anchor count != 1'
s = s.replace(old, new, 1)
open(f, 'w', encoding='utf-8').write(s)
print('patched trb-mem dump OK')
