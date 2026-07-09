#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# 把 xhci_dispatch_hid_event 从 wait_event 之前(依赖未定义处)移到后面,
# 并在 wait_event 之前加前向声明。
f = 'src/arch/x86_64/gui64/xhci64.c'
s = open(f, encoding='utf-8').read()

# 1) 抽出 helper 定义块
start_marker = '/* 将一个已确认为 Transfer Event 的 TRB 分发给对应 HID 设备'
end_marker = '    xhci_hid_arm(d);                           /* 重新武装下一个中断传输 */\n}\n'
i = s.find(start_marker)
assert i >= 0, 'helper start not found'
# helper 定义从注释上一行 /* 开始, 需回退到该注释起点
cstart = s.rfind('/*', 0, i+5)
j = s.find(end_marker, i)
assert j >= 0, 'helper end not found'
jend = j + len(end_marker)
helper_block = s[cstart:jend]

# 从原位置移除
s = s[:cstart] + s[jend:]

# 2) 在 wait_event 之前加前向声明
fwd = 'static void xhci_dispatch_hid_event(volatile xhci_trb_t *evt);\n'
anchor = 'static int xhci_wait_event(uint32_t want_type, uint64_t trb_phys,'
assert anchor in s
s = s.replace(anchor, fwd + anchor, 1)

# 3) 把 helper 定义插到 xhci_hid_arm 定义之后(1110附近)
# 找 xhci_hid_arm 函数体结束: 定位其定义起点后第一个顶格 }
def_anchor = 'static void xhci_hid_arm(xhci_dev_t *d) {'
k = s.find(def_anchor)
assert k >= 0, 'hid_arm def not found'
# 从 k 往后找第一个换行处的顶格 '}\n'
end = s.find('\n}\n', k)
assert end >= 0
insert_at = end + 3
s = s[:insert_at] + '\n' + helper_block + s[insert_at:]

open(f, 'w', encoding='utf-8').write(s)
print('relocated helper OK')
